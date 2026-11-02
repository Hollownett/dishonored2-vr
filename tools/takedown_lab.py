"""Takedown camera lab: where should the VR eye sit during a takedown?  (offline: no game, no DLL, no headset)

    python tools/takedown_lab.py <tag> [--scenes corvo_back,emily_back,...] [--no-video] [--capsules] [--workers N] [--tau S --vmax M --leash M]

Poses the game's own player clips (body + both arm clips, tools/decode_md6anim.py) on the extracted player meshes (artifacts/fullbody-assets),
puts the paired victim clip next to them by the shared anchor (artifacts/takedown-camera-census/victim_pairing.py reads the game index, read only),
and renders first-person tiles with the numpy rasteriser of tools/render_body_ik.py for each candidate eye placement:
  A flat game camera; B0/B02 today's raw animated eye with a level / 25 deg down headset gaze; B/B2 the same through the D2 eye smoother;
  C up 12 cm; D back 15 cm; E over the shoulder (12 cm sideways + 8 up); F automatic clearance (head >= 0.40 m): C-F with both gazes.
Output, artifacts/takedown-lab/<tag>/:
  <scene>_sheet_1_level.png, <scene>_sheet_2_down25.png   rows = candidates, columns = entry / contact / strike / end
  <scene>_paths.png (side + top view of every eye path), <scene>_timeline.png, <scene>.mp4 (rows A, B2 and the best staging row)
  metrics.csv (scene x candidate), sanity.txt, third_person.png, index.html
Frame: world +X forward, +Y left, +Z up, metres, 30 fps; a pair shares one frame with the clip anchors at the origin (the rotation of the victim
anchor is applied too: a front kill's victim faces back).  The victim is the base tall NPC body and head mesh (unclothed, grey, skinned to the same
base_tall skeleton as the clips) with a flat cap; --capsules, or a failed mesh load, draws a capsule figure instead.
Approximations: no blade mesh (the strike point is the right hand attachment joint), no physical head offset or lean in the eye, the head metrics use a sphere of 0.11 m at the census head centre,
intrusion visibility is a 96 px region-coded render every 2nd frame, the smoother time constant is a free choice (the spec fixes cap and leash only).
"""
import argparse
import csv
import html
import json
import math
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CENSUS = ROOT / 'artifacts/takedown-camera-census'
sys.path[:0] = [str(ROOT / 'tools'), str(CENSUS)]
import census as C            # noqa: E402  clip decoding, quaternion helpers, body skeleton indices
import render_body_ik as R    # noqa: E402  numpy rasteriser, skinning, views, ground, labels

OUT_ROOT = ROOT / 'artifacts/takedown-lab'
RAWP = C.RAW
FPS = 30.0

# ------------------------------------------------------------------ the candidates
VR_HFOV, FLAT_HFOV, TILE = 104.0, 90.0, 560           # headset-like square FOV, the flat game's 16:9 90 degrees, tile edge (px)
HEAD_R = 0.11                                          # victim head sphere (m); its centre = mid(head joint, head_end joint) as in the census
CLEAR_M, HEAD_COVER_DEG, STRIKE_CONE_DEG = 0.40, 25.0, 35.0
DOWN_DEG = 25.0                                        # the "looking down" gaze
VIS_TH = 0.01                                          # the neck stump (the coat's empty hood and collar) / a shoulder cap counts as visible from this share of the view
SMOOTH_TAU, SMOOTH_VMAX, SMOOTH_LEASH = 0.05, 4.0, 0.30   # D2 scene smoother: critically damped (time constant, s: not in the spec), speed cap (m/s), leash (m)
ACTION_M = 0.60                                        # the takedown action window: frames where the animated eye is within this of the victim head
RAMP_S = 0.25                                          # staging offsets C/D/E ease in over this long (smoothstep)
AUTO_UP_CAP, AUTO_CAP, AUTO_STEP = 0.12, 0.20, 0.01    # F: up budget, total cap, max change per frame (m)
# row key -> (label, staging, gaze pitch down in degrees; None = the game's own camera).  B0/B02 are today's raw animated eye, B/B2 add the D2 smoother
ROWS = {
    'A': ('A Game (flat 90 deg): what the game frames', 'anim', None),
    'B0': ('B0 VR today: raw animated eye, level', 'anim', 0.0), 'B': ('B VR today + D2 smoother, level', 'none', 0.0),
    'B02': ('B02 VR today: raw animated eye, down 25', 'anim', DOWN_DEG), 'B2': ('B2 VR today + D2 smoother, down 25', 'none', DOWN_DEG),
    'C': ('C Up 12 cm, level', 'up', 0.0), 'C2': ('C2 Up 12 cm, down 25', 'up', DOWN_DEG),
    'D': ('D Back 15 cm, level', 'back', 0.0), 'D2': ('D2 Back 15 cm, down 25', 'back', DOWN_DEG),
    'E': ('E Over shoulder 12 side+8 up, level', 'shoulder', 0.0), 'E2': ('E2 Over shoulder, down 25', 'shoulder', DOWN_DEG),
    'F': ('F Auto clearance, level', 'auto', 0.0), 'F2': ('F2 Auto clearance, down 25', 'auto', DOWN_DEG),
}
METRIC_ONLY = {'F3': ('F3 Auto clearance at 3 cm/frame, level', 'auto_fast', 0.0)}   # F with a 3x faster ease: what its 1 cm/frame costs (metrics only)
SHEETS = (('1_level', ['A', 'B0', 'B', 'C', 'D', 'E', 'F']), ('2_down25', ['A', 'B02', 'B2', 'C2', 'D2', 'E2', 'F2']))
STAGING_SET = ['C', 'C2', 'D', 'D2', 'E', 'E2', 'F', 'F2']

# scene key -> dict(title, char, parts=[(player folder, clip stem, victim clip, first frame used)], strike rule)
SCENES = {
    'corvo_back': dict(title='1. Corvo, assassination from behind (tall victim)', char='corvo', strike='pitch',
                       parts=[('corvo', 'generic_corvo_assassination_back', 'generic_corvo_assassination_back', 0)]),
    'emily_back': dict(title='2. Emily, assassination from behind (pitches down to 41 deg)', char='emily', strike='pitch',
                       parts=[('emily', 'generic_emily_assassination_back', 'generic_emily_assassination_back', 0)]),
    'corvo_choke': dict(title='3. Corvo, choke from behind: chokeback_in, then choke_loop', char='corvo', strike='loop_mid',
                        parts=[('corvo', 'generic_corvo_chokeback_in', 'generic_corvo_chokeback_in', 4),
                               ('corvo', 'generic_corvo_choke_loop', 'generic_corvo_choke_loop', 0)]),
    'corvo_front': dict(title='4. Corvo, assassination from the front', char='corvo', strike='pitch',
                        parts=[('corvo', 'generic_corvo_assassination_front', 'generic_corvo_assassination_front', 0)]),
    'corvo_fatality': dict(title='5. Corvo, sword fatality (fatality_front_a)', char='corvo', strike='pitch',
                           parts=[('corvo', 'generic_corvo_fatality_front_a', 'generic_corvo_fatality_front_a', 0)]),
}
MOMENTS = ('entry', 'contact', 'strike', 'end')


# ------------------------------------------------------------------ small math
def fk(Q, T, par):
    """Local joint quaternions (nf,nj,4 xyzw) and translations -> global rotation (nf,nj,3,3) and position (nf,nj,3)."""
    Rm, P = np.zeros(Q.shape[:2] + (3, 3)), np.zeros(Q.shape[:2] + (3,))
    for j, p in enumerate(par):
        r = C.quat_to_mat(Q[:, j])
        if p < 0:
            Rm[:, j], P[:, j] = r, T[:, j]
        else:
            Rm[:, j] = Rm[:, p] @ r
            P[:, j] = P[:, p] + np.einsum('fab,fb->fa', Rm[:, p], T[:, j])
    return Rm, P


def mat4s(Rm, P):
    M = np.zeros(Rm.shape[:-2] + (4, 4))
    M[..., :3, :3], M[..., :3, 3], M[..., 3, 3] = Rm, P, 1.0
    return M


def flat(M):
    """(..., 4, 4) row-major -> (..., 16) in the renderer's column-major order."""
    return np.swapaxes(M, -1, -2).reshape(M.shape[:-2] + (16,)).astype(np.float32)


def unit(v):
    v = np.asarray(v, float)
    return v / max(float(np.linalg.norm(v)), 1e-12)


def gaze_basis(yaw, down_deg):
    """Headset rotation: heading yaw (rad, + left), pitch down_deg below level, no roll -> (fwd, left, up)."""
    p = math.radians(-down_deg)
    fwd = np.array([math.cos(p) * math.cos(yaw), math.cos(p) * math.sin(yaw), math.sin(p)])
    left = np.array([-math.sin(yaw), math.cos(yaw), 0.0])
    return fwd, left, np.cross(fwd, left)


def smoothstep(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


# ------------------------------------------------------------------ player and victim tracks
_ARM_SK = {s: json.loads((R.PACK / f'skeleton_{s}_arm.json').read_text(encoding='utf-8')) for s in ('left', 'right')}
_ARM_PAR = {s: sk['parents'] for s, sk in _ARM_SK.items()}
_ARM_JOINTS = {s: sk['joint_names'] for s, sk in _ARM_SK.items()}


def decode_player(folder, stem, skip):
    """One player move -> body mats (n,20,4,4), arm mats (n,69,4,4) x2 (arm-model space), spine03 frame (n,4,4), all in the clip's frame
    (anchor-relative: the clip's own anchor at the origin) from frame `skip` on."""
    body = C.m.decode(str(RAWP / folder / 'body' / f'ply_body_{stem}.md6anim'))
    Rb, Pb = fk(np.asarray(body['Q'], float), np.asarray(body['T'], float), C.PAR)
    anchor = Pb[skip, C.ANCHOR].copy()
    Pb = Pb - anchor
    out = dict(body=mat4s(Rb, Pb)[skip:], frame=mat4s(Rb, Pb)[skip:, C.J['center_spine03_jnt']], fps=float(body['frame_rate']), anchor=anchor)
    for s in ('left', 'right'):
        a = C.m.decode(str(RAWP / folder / f'{s}arm' / f'ply_{s}arm_{stem}.md6anim'))
        assert len(a['Q']) == len(Rb), f'{stem}: {s} arm has {len(a["Q"])} frames, body {len(Rb)}'
        out[s + '_arm'] = mat4s(*fk(np.asarray(a['Q'], float), np.asarray(a['T'], float), _ARM_PAR[s]))[skip:]
    return out


def decode_victim(npc, vclip, skip, body_type='tall'):
    """The victim's clip -> joint rotation/position in the same anchor-relative frame."""
    path = _victim_path(body_type, vclip)
    saved = C.m.skeleton_for
    C.m.skeleton_for = lambda p, nj: ('npc', {'local_bind': npc.bind})
    try:
        a = C.m.decode(str(path))
    finally:
        C.m.skeleton_for = saved
    Rv, Pv = fk(np.asarray(a['Q'], float), np.asarray(a['T'], float), npc.par)
    ia = npc.J['anchor_jnt']
    Ra = Rv[skip, ia].T                                   # the anchors coincide in the world: a front kill's victim anchor is turned 180 deg
    return np.einsum('ab,fjbc->fjac', Ra, Rv[skip:]), np.einsum('ab,fjb->fja', Ra, Pv[skip:] - Pv[skip, ia])


def _victim_path(body, vclip):
    import victim_pairing as VP
    return VP.V / f'models/characters/{body}/_animations_/generic/{vclip}.md6anim'


def ensure_victim_clips(scenes):
    """Extract the victim side of the scenes (the census helper: read-only on the game, into the ignored census/victim folder)."""
    import victim_pairing as VP
    VP.BODIES = ('tall',)
    VP.PAIRS = [(v, f'{f}/body/ply_body_{stem}.md6anim') for sc in scenes for f, stem, v, _ in sc['parts']]
    if not hasattr(VP, '_lab_wrapped'):                                   # the base tall NPC body and head meshes come out of the same index
        VP._lab_wrapped, wanted = True, VP.wanted
        VP.wanted = lambda: wanted() + list(VICTIM_MESH)
    VP.ensure_extracted()
    return VP


def load_scene(key):
    sc = SCENES[key]
    VP = ensure_victim_clips([sc])
    npc = VP.Npc('tall')
    J = npc.J
    chunks, vchunks, joins = [], [], []
    notes = []
    for folder, stem, vclip, skip in sc['parts']:
        chunks.append(decode_player(folder, stem, skip))
        vr, vp = decode_victim(npc, vclip, skip)
        short = len(chunks[-1]['body']) - len(vp)
        if short:                                                                   # the NPC clip can end early (fatalities): hold its last pose
            assert 0 < short <= 4, f'{stem}: victim has {len(vp)} frames, player {len(chunks[-1]["body"])}'
            vr, vp = (np.concatenate([x, np.repeat(x[-1:], short, 0)]) for x in (vr, vp))
            notes.append(f'{vclip}: the victim clip is {short} frames shorter than the player clip; its last pose is held')
        vchunks.append((vr, vp))
    for a, b, va, vb in zip(chunks[:-1], chunks[1:], vchunks[:-1], vchunks[1:]):   # chained clips: does the next start where the last ended?
        joins.append((float(np.linalg.norm(a['body'][-1, C.CAM, :3, 3] - b['body'][0, C.CAM, :3, 3])),
                      float(np.linalg.norm(va[1][-1, J['center_head_jnt']] - vb[1][0, J['center_head_jnt']]))))
    drop = [0] + [1] * (len(chunks) - 1)                                           # a chained clip's first frame repeats the last one of the previous
    cat = lambda xs: np.concatenate([x[d:] for x, d in zip(xs, drop)])
    S = dict(key=key, spec=sc, char=sc['char'], fps=chunks[0]['fps'], joins=joins, notes=notes, npc=npc,
             body=cat([c['body'] for c in chunks]), frame=cat([c['frame'] for c in chunks]),
             left_arm=cat([c['left_arm'] for c in chunks]), right_arm=cat([c['right_arm'] for c in chunks]),
             vR=cat([v[0] for v in vchunks]), vP=cat([v[1] for v in vchunks]))
    S['n'] = len(S['body'])
    S['split'] = [len(x[d:]) for x, d in zip([c['body'] for c in chunks], drop)]      # frames of each chained clip
    S['cam_p'] = S['body'][:, C.CAM, :3, 3]
    S['cam_R'] = S['body'][:, C.CAM, :3, :3]
    S['head'] = 0.5 * (S['vP'][:, J['center_head_jnt']] + S['vP'][:, J['center_head_end_jnt']])
    # the strike point: the right hand attachment joint (arm-model space -> shared frame through the spine03 frame). No weapon tip: the
    # attachment frame has no consistent blade axis across the clips (sanity.txt), so the hand stands for the blade.
    ia = _ARM_JOINTS['right'].index('right_hand_attachment_jnt')
    S['strike_pt'] = np.einsum('fab,fb->fa', S['frame'][:, :3, :3], S['right_arm'][:, ia, :3, 3]) + S['frame'][:, :3, 3]
    return S


# ------------------------------------------------------------------ the victim figure (the base NPC mesh, or capsules between its skeleton joints)
VIC_COLORS = dict(skin=(.86, .66, .52), cloth=(.62, .26, .22), pants=(.28, .30, .38), boot=(.14, .12, .11), cap=(.11, .12, .18), nose=(.74, .50, .40))
# (joint a, joint b, radius at a, radius at b, colour): tapered capsules between skeleton joints
_LIMBS = [('center_pelvis_jnt', 'center_spine03_jnt', .150, .165, 'cloth'), ('center_spine03_jnt', 'center_neck_jnt', .165, .070, 'cloth'),
          ('left_arm_jnt', 'right_arm_jnt', .075, .075, 'cloth'), ('center_neck_jnt', 'center_head_jnt', .055, .055, 'skin'),
          ('center_pelvis_jnt', 'left_femur_jnt', .09, .09, 'pants'), ('center_pelvis_jnt', 'right_femur_jnt', .09, .09, 'pants')]
for _s in ('left', 'right'):
    _LIMBS += [(f'{_s}_arm_jnt', f'{_s}_elbow_jnt', .060, .050, 'cloth'), (f'{_s}_elbow_jnt', f'{_s}_wrist_jnt', .050, .040, 'cloth'),
               (f'{_s}_wrist_jnt', f'{_s}_hand_attachment_jnt', .045, .040, 'skin'),
               (f'{_s}_femur_jnt', f'{_s}_tibia_jnt', .090, .070, 'pants'), (f'{_s}_tibia_jnt', f'{_s}_ankle_jnt', .065, .050, 'pants'),
               (f'{_s}_ankle_jnt', f'{_s}_toes_jnt', .050, .045, 'boot'), (f'{_s}_toes_jnt', f'{_s}_toes_end_jnt', .045, .040, 'boot')]


def capsule_mesh(a, b, ra, rb, seg=14, ring=5):
    """Tapered capsule from a (radius ra) to b (radius rb): verts, normals, triangles (a sphere when a == b)."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    ax = unit(b - a) if np.linalg.norm(b - a) > 1e-6 else np.array([0.0, 0.0, 1.0])
    u = unit(np.cross(ax, [1.0, 0.0, 0.0] if abs(ax[0]) < 0.9 else [0.0, 1.0, 0.0]))
    w = np.cross(ax, u)
    t = np.linspace(0.0, math.pi / 2, ring)
    rings = [(b + rb * math.cos(x) * ax, rb * math.sin(x), math.cos(x)) for x in t] + [(a - ra * math.cos(x) * ax, ra * math.sin(x), -math.cos(x)) for x in t[::-1]]
    ph = np.linspace(0.0, 2 * math.pi, seg, endpoint=False)
    radial = np.cos(ph)[:, None] * u + np.sin(ph)[:, None] * w
    verts, norms = [], []
    for c, r, k in rings:
        verts.append(c + r * radial)
        n = k * ax + math.sqrt(max(0.0, 1 - k * k)) * radial
        norms.append(n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9))
    tris = []
    for r_ in range(len(rings) - 1):
        for p in range(seg):
            q = (p + 1) % seg
            tris += [(r_ * seg + p, (r_ + 1) * seg + p, r_ * seg + q), (r_ * seg + q, (r_ + 1) * seg + p, (r_ + 1) * seg + q)]
    return np.concatenate(verts).astype(np.float32), np.concatenate(norms).astype(np.float32), np.array(tris, np.intp)


VICTIM_MESH = ('models/characters/tall/base/base_tall_body.md6mesh', 'models/characters/tall/base/base_tall_head.md6mesh')
_VM = {}


def victim_mesh_assets(vdir):
    """The base tall NPC's body and head meshes (the game's unclothed grey base character, skinned to base_tall.md6skl like the victim clips),
    read with the player-pack parsers; triangles split into colour regions by their bind-pose position (hands and neck skin, tunic, trousers, boots)."""
    if 'a' in _VM:
        return _VM['a']
    from build_player_asset_pack import parse_mesh, merge_surfaces, parse_skeleton
    sk = parse_skeleton(Path(vdir) / 'models/characters/tall/base/base_tall.md6skl')
    jp = lambda n: sk['model'][sk['names'].index(n), :3, 3]
    hands = [(jp(f'{s_}_wrist_jnt'), unit(jp(f'{s_}_wrist_jnt') - jp(f'{s_}_elbow_jnt'))) for s_ in ('left', 'right')]   # wrist and forearm direction
    out = []
    for k, rel in enumerate(VICTIM_MESH):
        m = merge_surfaces(parse_mesh(Path(vdir) / rel))
        pos, idx = m['positions'].astype(np.float32), m['index'].astype(np.intp)
        if k == 1:
            idx = idx[:int(m['index_start'][1])]                           # the head surface only: the eyeball surface is skinned through a palette we do not know
        c = pos[idx].mean(1)
        reg = np.full(len(idx), 'cloth', object)
        if k == 0:
            reg[c[:, 2] < 1.0], reg[c[:, 2] < 0.16] = 'pants', 'boot'
            hand = np.zeros(len(c), bool)
            for w, d_ in hands:                                            # everything past a wrist along the forearm
                hand |= ((c - w) @ d_ > -0.01) & (np.linalg.norm(c - w, axis=1) < 0.30)
            reg[(c[:, 2] > 1.56) | hand] = 'skin'
        else:
            reg[:] = 'skin'
        order = np.concatenate([np.nonzero(reg == r_)[0] for r_ in VIC_COLORS])
        surf, start = [], 0
        for r_, col in VIC_COLORS.items():
            n = int((reg == r_).sum())
            surf.append((start, n, np.array([[col]], np.float32)))
            start += n
        out.append(dict(positions=pos, normals=m['normals'][:, :3].astype(np.float32), uv0=np.zeros((len(pos), 2), np.float32), weights=m['weights'], joints=m['joints'].astype(np.intp),
                        inverse_bind=sk['inverse'].astype(np.float32), index=idx[order], surfaces=surf))
    _VM['a'] = out
    return out


def victim_parts(D, i):
    """The victim of frame i as renderer parts: the skinned base NPC body and head plus the cap, or the capsule figure."""
    J = D['J']
    P, Rh = D['vP'][i], D['vR'][i, J['center_head_jnt']]
    if not D.get('vdir'):
        return [victim_part(P, Rh, J)]
    mats = mat4s(D['vR'][i], P).astype(np.float32)
    parts = []
    for a in victim_mesh_assets(D['vdir']):
        v, n = R.skin(a, mats)
        parts.append(dict(verts=v, normals=n, uv0=a['uv0'], index=a['index'], surfaces=a['surfaces']))
    head_c = 0.5 * (P[J['center_head_jnt']] + P[J['center_head_end_jnt']])
    v, n, t = cap_mesh(head_c, unit(P[J['center_head_end_jnt']] - P[J['center_head_jnt']]))
    parts.append(dict(verts=v, normals=n, uv0=np.zeros((len(v), 2), np.float32), index=t, surfaces=[(0, len(t), np.array([[VIC_COLORS['cap']]], np.float32))]))
    return parts


def cap_mesh(head_c, up):
    """A flat cap with a brim on the head (two short cylinders)."""
    parts = [R.shape_mesh(dict(kind='cylinder', center=c, axis=up, half=h, radius=r_)) for c, h, r_ in ((head_c + up * 0.085, 0.035, 0.118), (head_c + up * 0.052, 0.006, 0.150))]
    base = np.cumsum([0] + [len(v) for v, _, _ in parts[:-1]])
    return np.concatenate([v for v, _, _ in parts]), np.concatenate([n for _, n, _ in parts]), np.concatenate([t + b for (_, _, t), b in zip(parts, base)])


def victim_part(P, Rh, J):
    """One frame of the victim as a renderer part: joint positions P (nj,3), head rotation Rh (head local +X forward, +Y up).
    Capsules between its skeleton joints, a head sphere at the census head centre (radius 0.11), a nose, and a flat cap with a brim."""
    g = lambda n: P[J[n]]
    head_c = 0.5 * (g('center_head_jnt') + g('center_head_end_jnt'))
    up, fwd = unit(g('center_head_end_jnt') - g('center_head_jnt')), Rh[:, 0]
    groups = {k: [] for k in VIC_COLORS}
    for ja, jb, ra, rb, col in _LIMBS:
        groups[col].append(capsule_mesh(g(ja), g(jb), ra, rb))
    groups['skin'].append(capsule_mesh(head_c, head_c, HEAD_R, HEAD_R, seg=26, ring=10))
    groups['nose'].append(capsule_mesh(head_c + fwd * 0.105 - up * 0.015, head_c + fwd * 0.105 - up * 0.015, .024, .024, seg=10, ring=4))
    groups['cap'].append(cap_mesh(head_c, up))
    verts, norms, tris, surf, base = [], [], [], [], 0
    for col, items in groups.items():
        start = sum(len(t) for t in tris)
        for v, n, t in items:
            verts.append(v); norms.append(n); tris.append(t + base); base += len(v)
        surf.append((start, sum(len(t) for _, _, t in items), np.array([[VIC_COLORS[col]]], np.float32)))
    verts = np.concatenate(verts)
    return dict(verts=verts, normals=np.concatenate(norms), uv0=np.zeros((len(verts), 2), np.float32), index=np.concatenate(tris), surfaces=surf)


# ------------------------------------------------------------------ rendering one frame
_CHAR = {}


def assets(char):
    if char not in _CHAR:
        _CHAR[char] = R.load_character(char)
    return _CHAR[char]


def pose_player(S, i):
    """Skin the player's body and both arms for frame i (the renderer's own frame format)."""
    fr = dict(body=flat(S['body'][i]), left_arm=flat(S['left_arm'][i]), right_arm=flat(S['right_arm'][i]),
              left_arm_frame=flat(S['frame'][i]), right_arm_frame=flat(S['frame'][i]))
    return R.pose_frame(fr, assets(S['char']))


def make_view(eye, fwd, left, up, hfov, w, h):
    return R.View('fp', w, h, eye, fwd, -np.asarray(left), up, True, hfov=math.radians(hfov))


def render_view(view, posed, victim):
    color = np.full((view.h, view.w, 3), R.BACKGROUND, np.float32)
    color[..., 2] += np.linspace(0.0, 0.06, view.h, dtype=np.float32)[:, None]
    depth = np.full((view.h, view.w), np.inf, np.float32)
    for part in list(posed.values()) + (victim or []):
        R.draw_mesh(color, depth, view, part)
    R.draw_ground(color, depth, view)
    return np.clip(color * 255.0, 0, 255).astype(np.uint8)


# ------------------------------------------------------------------ eye paths: the D2 smoother and the staging candidates
def d2_smooth(p, fps=FPS, tau=None, vmax=None, leash=None):
    """D2 scene smoother: a critically damped 3D follow of the animated eye (time constant tau), a speed cap vmax, and a leash:
    the eye never lags the animated eye by more than `leash`, past it it is dragged along at the animation's own speed."""
    tau, vmax, leash = tau or SMOOTH_TAU, vmax or SMOOTH_VMAX, leash or SMOOTH_LEASH
    dt, om = 1.0 / fps, 2.0 / tau
    x_ = om * dt
    ex = 1.0 / (1 + x_ + 0.48 * x_ ** 2 + 0.235 * x_ ** 3)
    out, x, v = np.zeros_like(p), p[0].copy(), np.zeros(3)
    out[0] = x
    for i in range(1, len(p)):
        change = x - p[i]
        tmp = (v + om * change) * dt
        v = (v - om * tmp) * ex
        nx = p[i] + (change + tmp) * ex
        step = nx - x
        sp = np.linalg.norm(step) / dt
        if sp > vmax:
            nx, v = x + step * (vmax / sp), v * min(1.0, vmax / max(np.linalg.norm(v), 1e-9))
        off = nx - p[i]
        if np.linalg.norm(off) > leash:
            nx = p[i] + unit(off) * leash
        x = out[i] = nx
    return out


def _reach(e, h, base_off, axis, cap):
    """Smallest t in [0, cap] moving the eye along `axis` that clears the head by CLEAR_M, else the t that gets farthest."""
    t = np.linspace(0.0, max(cap, 0.0), 41)
    d = np.linalg.norm(h - (e + base_off + t[:, None] * axis), axis=1)
    ok = np.nonzero(d >= CLEAR_M)[0]
    return (t[ok[0]] if len(ok) else t[int(np.argmax(d))]), len(ok) > 0


def clearance_target(e, h):
    """F: the offset that puts the head CLEAR_M away: up first (to AUTO_UP_CAP), then back (horizontally away from the head), then sideways,
    all within the AUTO_CAP total."""
    z = np.array([0.0, 0.0, 1.0])
    u, ok = _reach(e, h, 0 * z, z, AUTO_UP_CAP)
    o = u * z
    if ok:
        return o
    hv = (e - h) * [1, 1, 0]
    back = unit(hv) if np.linalg.norm(hv) > 1e-6 else np.array([-1.0, 0.0, 0.0])
    b, ok = _reach(e, h, o, back, math.sqrt(max(AUTO_CAP ** 2 - u * u, 0.0)))
    o = o + b * back
    if ok:
        return o
    side, rem = np.cross(z, back), math.sqrt(max(AUTO_CAP ** 2 - float(o @ o), 0.0))
    (s1, _), (s2, _) = _reach(e, h, o, side, rem), _reach(e, h, o, -side, rem)
    return o + max((s1 * side, s2 * -side), key=lambda c: np.linalg.norm(h - (e + o + c)))


def staging_paths(S, sm, yaw):
    """Eye positions of the staging variants (smoothed animated eye + offset), plus the offsets."""
    n, t = S['n'], np.arange(S['n']) / S['fps']
    ramp = smoothstep(t / RAMP_S)[:, None]                       # C, D, E ease in over RAMP_S (smoothstep)
    head, strike = S['head'], S['strike_pt']
    left0 = np.array([-math.sin(yaw), math.cos(yaw), 0.0])
    hz = (sm - head) * [1, 1, 0]
    away = hz / np.maximum(np.linalg.norm(hz, axis=1, keepdims=True), 1e-6)
    d = np.linalg.norm(head - sm, axis=1)
    k = int(np.argmin(d))                                         # E's side is the one that clears the head at its closest approach
    s_dir, hv = unit(strike[k] - sm[k]), head[k] - sm[k]
    lateral = hv - (hv @ s_dir) * s_dir                           # the head's offset from the eye-to-strike line
    side = 1.0 if lateral @ left0 < 0 else -1.0                   # move to the side that clears it (right shoulder when it is on the line)
    off = dict(none=np.zeros((n, 3)), up=ramp * [0, 0, 0.12], back=ramp * 0.15 * away,
               shoulder=ramp * (side * 0.12 * left0 + [0, 0, 0.08]))
    tgt = np.array([clearance_target(sm[i], head[i]) if d[i] < CLEAR_M else np.zeros(3) for i in range(n)])
    for name, rate in (('auto', AUTO_STEP), ('auto_fast', 3 * AUTO_STEP)):   # F: the offset eases toward its target by at most `rate` per frame
        cur, off[name] = np.zeros(3), np.zeros((n, 3))
        for i in range(n):
            step = tgt[i] - cur
            cur = cur + step * min(1.0, rate / max(np.linalg.norm(step), 1e-9))
            off[name][i] = cur
    return off, side


def heading(Rc):
    f = Rc[0][:, 0]
    return math.atan2(f[1], f[0])


def build_rows(S):
    """Every row: label, eye path (n,3), gaze basis per frame (n,3) fwd/left/up, hfov."""
    n, sm = S['n'], d2_smooth(S['cam_p'], S['fps'])
    yaw = heading(S['cam_R'])
    off, side = staging_paths(S, sm, yaw)
    pos = dict(anim=S['cam_p'], **{k: sm + v for k, v in off.items()})
    rows = {}
    for key, (label, st, down) in {**ROWS, **METRIC_ONLY}.items():
        if down is None:                                           # the game's own camera, roll and all
            fwd, left, up = (S['cam_R'][:, :, c] for c in range(3))
        else:
            fwd, left, up = (np.tile(b, (n, 1)) for b in gaze_basis(yaw, down))
        rows[key] = dict(label=label, eye=pos[st], fwd=fwd, left=left, up=up, hfov=FLAT_HFOV if key == 'A' else VR_HFOV, down=down, staging=st,
                         offset=np.linalg.norm(pos[st] - sm, axis=1) if st in off else np.zeros(n))
    S['smoothed'], S['yaw'], S['side'] = sm, yaw, side
    return rows


def find_moments(S):
    d = np.linalg.norm(S['head'] - S['cam_p'], axis=1)
    pitch = np.degrees(np.arcsin(S['cam_R'][:, 2, 0]))
    if S['spec']['strike'] == 'loop_mid':
        strike = S['split'][0] + S['split'][1] // 2
    else:
        strike = int(np.argmin(pitch))
        if -pitch[strike] < 15 or strike < 8:                      # the game's pitch peaks low and early (Corvo's back stab: 7 deg at 0.1 s):
            neck = S['vP'][:, S['npc'].J['center_neck_jnt']]       # take the frame where the right hand is nearest the victim's neck
            strike = int(np.argmin(np.linalg.norm(S['strike_pt'] - neck, axis=1)))
    return dict(entry=0, contact=int(np.argmin(d)), strike=int(strike), end=S['n'] - 1)


# ------------------------------------------------------------------ per-frame metrics
def row_metrics(S, r):
    """Per-frame numbers of one row (head distance and size, strike angle, speed ...); vis numbers are merged in later."""
    eye, n = r['eye'], S['n']
    hv, sv = S['head'] - eye, S['strike_pt'] - eye
    dist = np.linalg.norm(hv, axis=1)
    cover = 2 * np.degrees(np.arcsin(np.minimum(1.0, HEAD_R / np.maximum(dist, 1e-6))))
    sx, sy, sz = (np.einsum('fa,fa->f', sv, r[k]) for k in ('fwd', 'left', 'up'))
    off_gaze = np.degrees(np.arctan2(np.hypot(sy, sz), sx))                    # angle between the gaze and the strike point
    in_view = frustum(r, sv)
    step = np.r_[0.0, np.linalg.norm(np.diff(eye, axis=0), axis=1)]
    return dict(dist=dist, cover=cover, strike_off=off_gaze, in_view=in_view, step=step, speed=step * S['fps'], action=np.linalg.norm(S['head'] - S['cam_p'], axis=1) < ACTION_M,
                anim_dist=np.linalg.norm(eye - S['cam_p'], axis=1), offset=r['offset'])


# ------------------------------------------------------------------ own-body intrusion: nearest vertex, neck stump, shoulder caps
def rim_loop(index, positions, near, min_n=15):
    """Vertex indices of the open boundary loop (edges used by one triangle, positions welded) whose centre is closest to `near`."""
    key = np.round(positions * 1e5).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    tri = inv[index]
    e = np.sort(np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]]), axis=1)
    k = e[:, 0].astype(np.int64) * (inv.max() + 1) + e[:, 1]
    u, cnt = np.unique(k, return_counts=True)
    a, b = (u[cnt == 1] // (inv.max() + 1)), (u[cnt == 1] % (inv.max() + 1))
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for x, y in zip(a, b):
        parent[find(x)] = find(y)
    comps = {}
    for x in set(a) | set(b):
        comps.setdefault(find(x), []).append(x)
    rep = np.zeros((inv.max() + 1, 3))
    rep[inv] = positions
    cands = [(np.linalg.norm(rep[c].mean(0) - near), c) for c in comps.values() if len(c) >= min_n]
    return np.nonzero(np.isin(inv, min(cands, key=lambda t: t[0])[1]))[0]


_REGION = {}
_GREY, _RED, _BLUE = (np.array([[c]], np.float32) for c in ((.6, .6, .6), (1, 0, 0), (0, 0, 1)))


def regions(char):
    """Static triangle sets per character: the body's neck (triangles skinned to the neck joint: the headless stump), and per arm the
    shoulder end (triangles within 7 cm of the open proximal rim: the shell's cap, which has an inside)."""
    if char in _REGION:
        return _REGION[char]
    A = assets(char)
    body = A['body']
    wn = (body['weights'].astype(np.float32) / 255 * (body['joints'] == body['joint_names'].index('center_neck_jnt'))).sum(1)
    neck = (wn > 0.5)[body['index']].all(1)
    reg = dict(body=np.concatenate([body['index'][~neck], body['index'][neck]]), n_rest=int((~neck).sum()))
    for side in ('left', 'right'):
        arm = A[f'{side}_arm']
        sh = arm['model_bind'][arm['joint_names'].index(f'{side}_shoulder_jnt')][:, 3]
        rim = arm['positions'][rim_loop(arm['index'], arm['positions'], sh)]
        near = np.array([np.linalg.norm(rim - v, axis=1).min() for v in arm['positions']]) < 0.07
        band = near[arm['index']].all(1)
        reg[side] = (arm['index'][~band], arm['index'][band])
    _REGION[char] = reg
    return reg


def vis_row(char, posed, eye, fwd, left, up, hfov, w, h):
    """Own-body intrusion seen from one eye: nearest own vertex (m) and the share of the view showing the neck stump, the inside of a
    shoulder cap, and any own body (low-res region-coded render of the player's body and arms only, depth tested)."""
    reg = regions(char)
    view = make_view(eye, fwd, left, up, hfov, w, h)
    color, depth = np.zeros((h, w, 3), np.float32), np.full((h, w), np.inf, np.float32)
    body = dict(posed['body'], index=reg['body'], surfaces=[(0, reg['n_rest'], _GREY), (reg['n_rest'], len(reg['body']) - reg['n_rest'], _RED)])
    R.draw_mesh(color, depth, view, body)
    for side in ('left', 'right'):
        part = posed[f'{side}_arm']
        rest, band = reg[side]
        c = part['verts'][band].mean(1)
        back = np.einsum('tj,tj->t', part['normals'][band].mean(1), c - eye) > 0      # the normals point out: this is a face seen from inside
        idx = np.concatenate([rest, band[~back], band[back]])
        R.draw_mesh(color, depth, view, dict(part, index=idx, surfaces=[(0, len(rest) + int((~back).sum()), _GREY), (len(idx) - int(back.sum()), int(back.sum()), _BLUE)]))
    r_, g_, b_ = color[..., 0], color[..., 1], color[..., 2]
    px = float(w * h)
    near = min(float(np.linalg.norm(p['verts'] - eye, axis=1).min()) for p in posed.values())
    return (near, float(((r_ > .15) & (g_ < .05) & (b_ < .05)).sum() / px), float(((b_ > .15) & (r_ < .05) & (g_ < .05)).sum() / px),
            float((color.sum(-1) > .05).sum() / px))


# ------------------------------------------------------------------ the worker: one frame -> tiles and intrusion numbers
_W = {}


def lower_priority():
    """Offline work must not disturb a game running on the same machine: below-normal priority for this process and its workers."""
    try:
        if os.name == 'nt':
            import ctypes
            k = ctypes.windll.kernel32
            k.GetCurrentProcess.restype = ctypes.c_void_p
            k.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            k.SetPriorityClass(k.GetCurrentProcess(), 0x4000)       # BELOW_NORMAL_PRIORITY_CLASS
        else:
            os.nice(5)
    except Exception:                                               # noqa: BLE001 - a nicety, never fatal
        pass


def _worker_init(data):
    lower_priority()
    _W.update(data)


def render_task(task):
    """task = (scene key, frame, tile specs, vis specs); a spec = (name, eye, fwd, left, up, hfov, w, h).  Skins the frame once."""
    key, i, tiles, vis = task
    D = _W[key]
    posed = pose_player(D, i)
    out = dict(tiles={}, vis={})
    if tiles:
        victim = victim_parts(D, i)
        for name, eye, fwd, left, up, hfov, w, h in tiles:
            out['tiles'][name] = render_view(make_view(eye, fwd, left, up, hfov, w, h), posed, victim)
    for name, eye, fwd, left, up, hfov, w, h in vis:
        out['vis'][name] = vis_row(D['char'], posed, eye, fwd, left, up, hfov, w, h)
    return key, i, out


def light(S, vdir=None):
    return dict(char=S['char'], body=S['body'], left_arm=S['left_arm'], right_arm=S['right_arm'], frame=S['frame'], vP=S['vP'], vR=S['vR'], J=S['npc'].J, vdir=vdir)


def spec(name, r, i, w, h):
    return (name, r['eye'][i], r['fwd'][i], r['left'][i], r['up'][i], r['hfov'], w, h)


TILE_PX = {'A': (TILE, int(TILE * 9 / 16))}                    # the flat game is 16:9; the headset views are square
VIS_PX = {'A': (128, 72)}


# ------------------------------------------------------------------ tiles and sheets
HEAD_H, FOOT_H = 56, 30
GOOD, BAD, WARN, TXT = (140, 235, 140), (255, 110, 110), (255, 190, 90), (235, 225, 120)


def text(d, xy, s, size, fill):
    d.text(xy, s, fill=fill, font=R._font(size), stroke_width=1, stroke_fill=(0, 0, 0))


def cell(img, key, r, S, i, moment):
    """One labelled tile: header (row, moment), the picture with the gaze cross, the 35 degree cone and the strike marker, and a footer."""
    h_, w_ = img.shape[:2]
    view = make_view(r['eye'][i], r['fwd'][i], r['left'][i], r['up'][i], r['hfov'], w_, h_)
    im = Image.fromarray(img)
    d = ImageDraw.Draw(im)
    cx, cy, rr = view.cx, view.cy, view.f * math.tan(math.radians(STRIKE_CONE_DEG))
    for a0 in range(0, 360, 12):
        d.arc((cx - rr, cy - rr, cx + rr, cy + rr), a0, a0 + 6, fill=(255, 255, 255))
    d.line((cx - 9, cy, cx + 9, cy), fill=(255, 255, 255), width=2)
    d.line((cx, cy - 9, cx, cy + 9), fill=(255, 255, 255), width=2)
    sv = S['strike_pt'][i] - r['eye'][i]
    sx, sy, sz = sv @ r['fwd'][i], sv @ r['left'][i], sv @ r['up'][i]
    px, py, pz = view.project(S['strike_pt'][i][None])
    if sx > 0.03 and 0 <= px[0] < w_ and 0 <= py[0] < h_:
        d.ellipse((px[0] - 11, py[0] - 11, px[0] + 11, py[0] + 11), outline=(255, 60, 220), width=3)
        d.ellipse((px[0] - 2, py[0] - 2, px[0] + 2, py[0] + 2), fill=(255, 60, 220))
    else:                                                   # off screen: an arrow on the border, pointing at it
        u = unit([-sy, -sz])
        c = np.array([cx, cy]) + u * (min(w_, h_) / 2 - 16)
        n = np.array([-u[1], u[0]])
        d.polygon([tuple(c + u * 12), tuple(c - u * 6 + n * 9), tuple(c - u * 6 - n * 9)], fill=(255, 60, 220))
    pf = r['pf']
    out = Image.new('RGB', (TILE, HEAD_H + h_ + FOOT_H), (26, 26, 30))
    out.paste(im, (0, HEAD_H))
    d = ImageDraw.Draw(out)
    text(d, (8, 5), r['label'], 21, (255, 255, 255))
    text(d, (8, 31), f'{moment}  frame {i}  {i / S["fps"]:.2f} s' + (f'  eye offset {pf["offset"][i] * 100:.0f} cm' if r['staging'] not in ('anim', 'none') else ''), 16, TXT)
    x, y = 8, HEAD_H + h_ + 6
    dist = pf['dist'][i]
    for s, col in ((f'head {dist:.2f} m ({pf["cover"][i]:.0f} deg)', GOOD if dist >= CLEAR_M else BAD),
                   (f'strike {pf["strike_off"][i]:.0f} deg' + ('' if pf['in_view'][i] else ' (out)'), GOOD if pf['strike_off'][i] <= STRIKE_CONE_DEG else BAD),
                   *([('IN HEAD', BAD)] if dist < HEAD_R else []), *([('NECK', WARN)] if pf['neck'][i] > VIS_TH else []), *([('CAP', WARN)] if pf['cap'][i] > VIS_TH else [])):
        text(d, (x, y), s, 17, col)
        x += int(d.textlength(s, font=R._font(17))) + 14
    return np.asarray(out)


def make_sheet(S, rows, mom, row_keys, shots, path, gaze_name):
    parts = []
    banner = Image.new('RGB', (4 * TILE + 3 * R.GAP, 44), (40, 40, 48))
    d = ImageDraw.Draw(banner)
    text(d, (8, 4), f'{S["spec"]["title"]}  -  {gaze_name}', 22, (255, 255, 255))
    text(d, (8, 27), 'tile = headset view 104x104 deg (A: flat 90 deg). white dashed circle = 35 deg from the gaze, magenta = strike point (right hand). '
         'head red < 0.40 m. NECK / CAP = the headless neck stump (empty hood) / the inside of a shoulder cap covers >= 1% of the view', 14, TXT)
    parts.append(np.asarray(banner))
    for k in row_keys:
        cells = [cell(shots[(k, m)], k, rows[k], S, mom[m], m) for m in MOMENTS]
        gap = np.full((cells[0].shape[0], R.GAP, 3), 90, np.uint8)
        parts.append(np.hstack([c for pair in zip(cells, [gap] * 3 + [None]) for c in pair if c is not None]))
    R.save_sheet(parts, path)


# ------------------------------------------------------------------ aggregate metrics
CSV_COLS = ['scene', 'candidate', 'label', 'gaze_down_deg', 'frames', 'head_min_m', 'head_median_m', 'head_ge_0p40_share', 'head_ge_0p40_share_action', 'eye_inside_head_share',
            'head_cover_gt25deg_share', 'strike_within35deg_share', 'strike_within35deg_share_action', 'strike_in_view_share', 'strike_off_gaze_median_deg',
            'own_nearest_min_m', 'own_nearest_median_m', 'neck_stump_visible_share', 'neck_stump_view_max_share', 'shoulder_cap_visible_share', 'shoulder_cap_view_max_share',
            'own_body_in_view_median_share',
            'eye_speed_peak_mps', 'eye_speed_p95_mps', 'eye_step_max_m', 'anim_dist_max_m', 'anim_dist_p95_m', 'offset_max_m']
NAN = float('nan')


def aggregate(S, key, r):
    """One metrics row: the whole clip, plus the *_action shares over the action window (animated eye within ACTION_M of the victim head)."""
    pf, n = r['pf'], S['n']
    sp, act = pf['speed'][1:], pf['action']
    have = 'nearest' in pf
    share = lambda m, a=None: float(np.mean(m if a is None else m[a])) if (a is None or a.any()) else NAN
    vis = lambda k, f: float(f(pf[k])) if have else NAN
    return dict(scene=S['key'], candidate=key, label=r['label'], gaze_down_deg='game' if r['down'] is None else r['down'], frames=n,
                head_min_m=pf['dist'].min(), head_median_m=np.median(pf['dist']), head_ge_0p40_share=share(pf['dist'] >= CLEAR_M),
                head_ge_0p40_share_action=share(pf['dist'] >= CLEAR_M, act), eye_inside_head_share=share(pf['dist'] < HEAD_R),
                head_cover_gt25deg_share=share(pf['cover'] > HEAD_COVER_DEG), strike_within35deg_share=share(pf['strike_off'] <= STRIKE_CONE_DEG),
                strike_within35deg_share_action=share(pf['strike_off'] <= STRIKE_CONE_DEG, act), strike_in_view_share=share(pf['in_view']),
                strike_off_gaze_median_deg=np.median(pf['strike_off']), own_nearest_min_m=vis('nearest', np.min), own_nearest_median_m=vis('nearest', np.median),
                neck_stump_visible_share=vis('neck', lambda v: np.mean(v > VIS_TH)), neck_stump_view_max_share=vis('neck', np.max),
                shoulder_cap_visible_share=vis('cap', lambda v: np.mean(v > VIS_TH)), shoulder_cap_view_max_share=vis('cap', np.max),
                own_body_in_view_median_share=vis('body', np.median), eye_speed_peak_mps=sp.max(), eye_speed_p95_mps=np.percentile(sp, 95),
                eye_step_max_m=pf['step'][1:].max(), anim_dist_max_m=pf['anim_dist'].max(), anim_dist_p95_m=np.percentile(pf['anim_dist'], 95), offset_max_m=pf['offset'].max())


def write_csv(path, recs):
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(CSV_COLS)
        for r in recs:
            w.writerow([('' if math.isnan(r[c]) else f'{r[c]:.4f}') if isinstance(r[c], (float, np.floating)) else r[c] for c in CSV_COLS])


# ------------------------------------------------------------------ diagrams
PATH_COLORS = dict(anim='#111111', B='#8c8c8c', C='#1f77b4', D='#ff7f0e', E='#2ca02c', F='#d62728')
MOMENT_MARK = dict(entry='o', contact='s', strike='*', end='X')


def make_paths(S, rows, mom, path):
    """Side view (x forward, z up) and top view (forward up): the animated camera, each candidate's eye, the victim head, the strike point."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axs = plt.subplots(1, 2, figsize=(14, 7.2), dpi=100, gridspec_kw=dict(width_ratios=[1.0, 1]))
    views = ((lambda p: (p[..., 0], p[..., 2]), 'side view: x forward (m), z up (m)'), (lambda p: (-p[..., 1], p[..., 0]), 'top view: forward up, left = left (m)'))
    for ax, (proj, title) in zip(axs, views):
        ax.set_title(title, loc='left', fontsize=11)
        hx, hy = proj(S['head'][mom['contact']])                   # the 0.40 m clearance circle around the head at the contact moment
        ax.add_patch(plt.Circle((hx, hy), CLEAR_M, fill=False, ls=':', color='#9467bd', lw=1))
        ax.plot(*proj(S['head']), color='#9467bd', lw=2, label='victim head (r 0.11 m)')
        ax.plot(*proj(S['strike_pt']), color='#e377c2', lw=1.5, ls='--', label='strike point (right hand)')
        ax.plot(*proj(S['cam_p']), color=PATH_COLORS['anim'], lw=2.2, label='animated camera (what the game uses)')
        for k, lab in (('B', 'B smoothed (D2)'), ('C', 'C up 12'), ('D', 'D back 15'), ('E', 'E shoulder'), ('F', 'F auto')):
            ax.plot(*proj(rows[k]['eye']), color=PATH_COLORS[k], lw=1.4, label=f'eye {lab}')
        for m, i in mom.items():
            for p_, col in ((S['cam_p'], 'black'), (S['head'], '#9467bd')):
                x, y = proj(p_[i])
                ax.plot([x], [y], MOMENT_MARK[m], color=col, ms=9 if m == 'strike' else 7, mfc='white' if m == 'entry' else col)
        for i in sorted(set(mom.values())):                           # one label per frame (contact and strike can coincide)
            ax.annotate('/'.join(m for m, j in mom.items() if j == i), proj(S['cam_p'][i]), xytext=(5, 6), textcoords='offset points', fontsize=8)
        ax.set_aspect('equal')
        ax.grid(True, color='#dddddd')
    axs[0].legend(fontsize=8, loc='upper center', bbox_to_anchor=(0.5, -0.06), ncol=3)
    fig.suptitle(S['spec']['title'] + '   (markers: o entry, square contact, star strike, X end; dotted circle = 0.40 m around the head at contact)', fontsize=10, x=0.01, ha='left')
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def make_timeline(S, rows, mom, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    t = np.arange(S['n']) / S['fps']
    panels = (('victim head distance (m)', 'dist', ['B0', 'B', 'C', 'D', 'E', 'F'], CLEAR_M, '0.40 m'),
              (f'head angular size (deg, sphere r {HEAD_R} m)', 'cover', ['B0', 'B', 'C', 'D', 'E', 'F'], HEAD_COVER_DEG, '25 deg'),
              ('strike point off a LEVEL gaze (deg)', 'strike_off', ['B', 'C', 'D', 'E', 'F'], STRIKE_CONE_DEG, '35 deg'),
              ('strike point off a gaze 25 deg DOWN (deg)', 'strike_off', ['B2', 'C2', 'D2', 'E2', 'F2'], STRIKE_CONE_DEG, '35 deg'),
              ('eye speed (m/s)', 'speed', ['B0', 'B', 'F'], SMOOTH_VMAX, '4 m/s'))
    fig, axs = plt.subplots(len(panels), 1, figsize=(12, 2.5 * len(panels)), dpi=100, sharex=True)
    for ax, (title, field, keys, ref, rl) in zip(axs, panels):
        for k in keys:
            ax.plot(t, rows[k]['pf'][field], color=PATH_COLORS[k[0]] if k[0] in PATH_COLORS else '#444444', lw=1.6, ls='--' if k == 'B0' else '-', label=k)
        ax.axhline(ref, color='#999999', ls=':', lw=1)
        ax.annotate(rl, (t[-1], ref), xytext=(-4, 3), textcoords='offset points', ha='right', fontsize=8, color='#666666')
        for m, i in mom.items():
            ax.axvline(t[i], color='#cccccc', lw=1)
        ax.set_title(title, loc='left', fontsize=10)
        ax.legend(fontsize=8, ncol=len(keys), loc='upper right')
        ax.grid(True, color='#eeeeee')
    for m, i in mom.items():
        axs[-1].annotate(m, (t[i], 0), xytext=(2, 4), textcoords='offset points', fontsize=8, color='#555555')
    axs[-1].set_xlabel('time (s)')
    fig.suptitle(S['spec']['title'], fontsize=11, x=0.01, ha='left')
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


# ------------------------------------------------------------------ sanity checks
def armhole_dist(char, posed):
    """Distance from the body's armhole vertices (the open loop nearest the upper-arm joint) to the nearest vertex of each posed arm shell:
    small = the arm comes out of the sleeve hole; large = the shell has drifted away from the torso."""
    A = assets(char)
    spine = A['body']['joint_names'].index('center_spine03_jnt')
    F = A['body']['model_bind'][spine]
    out = {}
    for side in ('left', 'right'):
        arm = A[f'{side}_arm']
        aj = arm['model_bind'][arm['joint_names'].index(f'{side}_arm_jnt')][:, 3]
        hole = rim_loop(A['body']['index'], A['body']['positions'], F[:, :3] @ aj + F[:, 3], min_n=20)
        d = np.linalg.norm(posed['body']['verts'][hole][:, None] - posed[f'{side}_arm']['verts'][None], axis=2).min(1)
        out[side] = (float(np.median(d)), float(np.percentile(d, 90)), float(d.max()))
    return out


def blade_probe(S):
    """Which axis of the right hand attachment frame points into the victim's torso when the hand is nearest it (to place a weapon tip)?"""
    J, ia = S['npc'].J, _ARM_JOINTS['right'].index('right_hand_attachment_jnt')
    Rh = np.einsum('fab,fbc->fac', S['frame'][:, :3, :3], S['right_arm'][:, ia, :3, :3])
    pts = np.stack([S['vP'][:, J[n]] for n in ('center_neck_jnt', 'center_spine03_jnt', 'center_spine02_jnt', 'center_spine01_jnt')], 1)
    ls, best = np.linspace(0, 0.9, 46), []
    for ax in range(3):
        for sg in (1, -1):
            seg = S['strike_pt'][:, None] + ls[None, :, None] * (sg * Rh[:, :, ax])[:, None]
            d = np.linalg.norm(seg[:, :, None] - pts[:, None], axis=3)
            f = int(d.min(axis=(1, 2)).argmin())
            best.append((float(d[f].min()), '+' if sg > 0 else '-', 'xyz'[ax], f, float(ls[np.unravel_index(d[f].argmin(), d[f].shape)[0]])))
    return min(best)


def pose_task(args):
    """Third-person ortho views (three quarter, side, top) of one frame: the player, the victim capsules, the ground."""
    key, i = args
    D = _W[key]
    posed = pose_player(D, i)
    vic = victim_parts(D, i)
    pts = np.vstack([p['verts'] for p in list(posed.values()) + vic])
    center = (pts.min(0) + pts.max(0)) / 2
    center[2] = 0.95
    tiles = []
    for name, az, el in (('three-quarter', 35, 12), ('side', 90, 0), ('top', 0, 89)):
        a, e = math.radians(az), math.radians(el)
        fwd = -np.array([math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)], np.float32)
        tiles.append(render_view(R.ortho_view(name, fwd, center + np.array([0.3, 0, 0]), 2.2), posed, vic))
    return key, i, np.hstack(tiles)


def frustum(r, rel):
    """rel (n,3) points relative to the eye -> in_view (n,) for this row's FOV (the flat game's frame is 16:9)."""
    sx, sy, sz = (np.einsum('fa,fa->f', rel, r[k]) for k in ('fwd', 'left', 'up'))
    half = math.tan(math.radians(r['hfov'] / 2))
    return (sx > 0.03) & (np.abs(sy) <= half * sx) & (np.abs(sz) <= half * (9 / 16 if r['hfov'] == FLAT_HFOV else 1.0) * sx)


def sanity(scenes, out):
    L, TJ = [], C.J
    ok = lambda b: 'PASS' if b else 'CHECK'
    L.append('1. Row A camera sits at the head: |camera_jnt - center_head_jnt| over every frame of every scene')
    dh = np.concatenate([np.linalg.norm(sc['S']['cam_p'] - sc['S']['body'][:, TJ['center_head_jnt'], :3, 3], axis=1) for sc in scenes.values()])
    L.append(f'   min {dh.min() * 100:.2f} cm, max {dh.max() * 100:.2f} cm (constant: the camera is rigid on the head pivot, 3.3 cm ahead + 0.8 cm up, as in the census) -> {ok(dh.max() - dh.min() < 1e-3)}')
    L.append('   row A framing (flat game 90 x 58.7 deg): share of frames with the right hand / the victim head centre inside the frame; Y/- at entry, contact, strike, end')
    for k, sc in scenes.items():
        S, r = sc['S'], sc['rows']['A']
        hand, head = frustum(r, S['strike_pt'] - r['eye']), frustum(r, S['head'] - r['eye'])
        at = lambda a: ' '.join('Y' if a[sc['mom'][m]] else '-' for m in MOMENTS)
        L.append(f'   {k:15s} hand {hand.mean() * 100:3.0f}% [{at(hand)}]   head {head.mean() * 100:3.0f}% [{at(head)}]')
    L.append('2. The victim stands where the census says (victim_pairing.py: shared anchor frame, 1.00 m ahead at the start of the back stab)')
    for k, lo, hi, name, ref in (('corvo_back', 0, None, 'back stab, whole clip', (0.24, 0.37, 0.99)), ('corvo_back', 8, 45, 'back stab, frames 8-44 (the stab)', (0.24, 0.31, 0.40)),
                                 ('corvo_choke', 0, 15, 'chokeback_in (clip frames 4-18)', (0.23, 0.28, 0.67)), ('corvo_choke', 15, None, 'choke_loop', (0.23, 0.23, 0.27))):
        if k not in scenes:
            continue
        S = scenes[k]['S']
        d = np.linalg.norm(S['head'] - S['cam_p'], axis=1)[lo:hi]
        got = (d.min(), np.median(d), d.max())
        L.append(f'   {name:36s} head distance min/median/max {got[0]:.2f} / {got[1]:.2f} / {got[2]:.2f} m   census {ref[0]:.2f} / {ref[1]:.2f} / {ref[2]:.2f} '
                 f'-> {ok(all(abs(a - b) <= 0.015 for a, b in zip(got[:2], ref[:2])) and abs(got[2] - ref[2]) <= 0.04)}')
    if 'corvo_back' in scenes:
        S = scenes['corvo_back']['S']
        L.append(f'   corvo_back frame 0: victim pelvis {S["vP"][0, S["npc"].J["center_root_jnt"], 0]:.3f} m and head centre {S["head"][0, 0]:.3f} m ahead of the player anchor '
                 f'-> {ok(abs(S["head"][0, 0] - 1.0) < 0.02)}  (the census used the per-frame anchor: its max 0.99 vs 0.96 here, because the victim anchor jumps 7 cm in z on the last frame; we hold it fixed)')
    for k, sc in scenes.items():
        for n in sc['S']['joins']:
            L.append(f'   {k}: chained clips join with a gap of {n[0] * 1000:.1f} mm (camera) and {n[1] * 1000:.1f} mm (victim head) -> {ok(max(n) < 0.005)}')
        for n in sc['S']['notes']:
            L.append(f'   {k}: {n}')
    L.append('3. Arms attached at the shoulders: body armhole vertices to the nearest arm-shell vertex (median / p90 / max, cm); the arm shell comes out of the sleeve hole when small')
    fmt = lambda d: ' / '.join(f'{x * 100:.1f}' for x in d)
    for ch in sorted({sc['S']['char'] for sc in scenes.values()}):
        frame, A = R.bind_frame(ch)
        b = armhole_dist(ch, R.pose_frame(frame, A))
        idle = decode_player('common', 'empty_idle', 0)
        S2 = dict(char=ch, body=idle['body'], left_arm=idle['left_arm'], right_arm=idle['right_arm'], frame=idle['frame'])
        i_ = armhole_dist(ch, pose_player(S2, 0))
        L.append(f'   {ch} bind pose: left {fmt(b["left"])}  right {fmt(b["right"])}   empty-idle clip: left {fmt(i_["left"])}  right {fmt(i_["right"])} '
                 f'-> {ok(max(b["left"][0], b["right"][0], i_["left"][0], i_["right"][0]) < 0.02)} (median under 2 cm)')
    for k, sc in scenes.items():
        S = sc['S']
        med = {s: [] for s in ('left', 'right')}
        for i in range(0, S['n'], 3):
            for s, v in armhole_dist(S['char'], pose_player(S, i)).items():
                med[s].append(v[0])
        L.append(f'   {k}: median armhole-to-arm distance over the clip {np.median(med["left"]) * 100:.1f} cm (left) / {np.median(med["right"]) * 100:.1f} cm (right), '
                 f'worst frame {max(med["left"]) * 100:.1f} / {max(med["right"]) * 100:.1f} cm')
    L.append('   (a head-height eye cannot see the shoulders; the arm clips pose the shells away from the torso by a few cm during the move, which matters for eyes moved back or up)')
    L.append('4. Weapon tip: there is no blade mesh; closest approach of an axis of the right-hand attachment frame to the victim torso (axis, closest, frame, blade length)')
    for k, sc in scenes.items():
        d, sg, ax, f, ln = blade_probe(sc['S'])
        L.append(f'   {k:15s} best axis {sg}{ax}: {d * 100:.1f} cm at frame {f}, blade length {ln:.2f} m')
    L.append('   the best axis, its sign and the length differ between clips (the grip changes), so the right-hand attachment joint stands for the strike point everywhere')
    (out / 'sanity.txt').write_text('\n'.join(L) + '\n', encoding='utf-8')
    return L


# ------------------------------------------------------------------ the page
CSS = """
:root{color-scheme:dark light;--bg:#14151a;--fg:#e8e8ec;--mut:#a0a2ae;--card:#1d1f27;--ok:#7fdc7f;--bad:#ff8080;--warn:#ffc060;--line:#363a48}
@media (prefers-color-scheme:light){:root{--bg:#f5f5f8;--fg:#1b1c20;--mut:#5a5d6a;--card:#fff;--ok:#157a2a;--bad:#c62828;--warn:#a85f00;--line:#d4d6de}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,-apple-system,sans-serif}
main{max-width:1180px;margin:0 auto;padding:8px 16px 64px}
h1{font-size:1.5rem;margin:.8em 0 .2em}h2{font-size:1.25rem;margin:1.6em 0 .3em;border-top:1px solid var(--line);padding-top:.7em}h3{font-size:1rem;margin:1.1em 0 .3em}
p,li{max-width:80ch}.mut{color:var(--mut)}code,pre{font:12.5px/1.45 ui-monospace,Consolas,monospace}
pre{background:var(--card);padding:10px;overflow-x:auto;border:1px solid var(--line);border-radius:6px}
.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:6px;background:var(--card);margin:6px 0}
.scroll img,video{display:block;width:1400px;max-width:none;height:auto}video{width:min(100%,1280px);max-width:100%}
@media (min-width:1000px){.scroll img{width:100%;max-width:2258px}}
table{border-collapse:collapse;font-size:12.5px;white-space:nowrap}td,th{padding:3px 9px;border-bottom:1px solid var(--line);text-align:right}
th{position:sticky;top:0;background:var(--card);font-weight:600}td:first-child,th:first-child{text-align:left}
td.ok{color:var(--ok)}td.bad{color:var(--bad)}td.warn{color:var(--warn)}tr.sub td{background:rgba(127,127,160,.08)}
.legend span{display:inline-block;margin-right:14px}
"""
COLS_SCENE = [('candidate', 'row', '{}'), ('head_min_m', 'head min m', '{:.2f}'), ('head_median_m', 'head med m', '{:.2f}'), ('head_ge_0p40_share', 'head >= 0.40 m', '{:.0%}'),
              ('head_ge_0p40_share_action', '... in action', '{:.0%}'), ('eye_inside_head_share', 'eye in head', '{:.0%}'),
              ('head_cover_gt25deg_share', 'head > 25 deg', '{:.0%}'), ('strike_within35deg_share', 'strike <= 35 deg', '{:.0%}'),
              ('strike_within35deg_share_action', '... in action', '{:.0%}'), ('strike_in_view_share', 'strike in view', '{:.0%}'),
              ('own_nearest_min_m', 'own body min cm', '{:.1f}'), ('neck_stump_visible_share', 'neck stump', '{:.0%}'), ('shoulder_cap_visible_share', 'shoulder cap', '{:.0%}'),
              ('eye_speed_peak_mps', 'speed peak', '{:.1f}'), ('eye_speed_p95_mps', 'speed p95', '{:.1f}'), ('eye_step_max_m', 'step max cm', '{:.1f}'),
              ('anim_dist_max_m', 'leash max cm', '{:.0f}')]
CM = ('own_nearest_min_m', 'eye_step_max_m', 'anim_dist_max_m')


def tone(col, v, ref=None):
    """CSS class of a metric cell: green good, red bad."""
    good = {'head_min_m': lambda: v >= CLEAR_M, 'head_ge_0p40_share': lambda: v >= .9, 'strike_within35deg_share': lambda: v >= .8, 'strike_in_view_share': lambda: v >= .8,
            'neck_stump_visible_share': lambda: v < .02, 'shoulder_cap_visible_share': lambda: v < .02, 'own_nearest_min_m': lambda: v >= .06, 'eye_speed_p95_mps': lambda: v <= SMOOTH_VMAX}
    bad = {'head_min_m': lambda: v < .3, 'head_ge_0p40_share': lambda: v < .5, 'strike_within35deg_share': lambda: v < .5, 'strike_in_view_share': lambda: v < .5,
           'neck_stump_visible_share': lambda: v > .15, 'shoulder_cap_visible_share': lambda: v > .15, 'own_nearest_min_m': lambda: v < .03, 'eye_speed_p95_mps': lambda: v > 1.5 * SMOOTH_VMAX}
    return 'ok' if col in good and good[col]() else 'bad' if col in bad and bad[col]() else ''


def table(recs, cols=COLS_SCENE):
    t = ['<div class="scroll"><table><tr>' + ''.join(f'<th>{c[1]}</th>' for c in cols) + '</tr>']
    for r in recs:
        cellv = lambda c: html.escape(r['candidate']) if c[0] == 'candidate' else '-' if math.isnan(r[c[0]]) else c[2].format(r[c[0]] * (100 if c[0] in CM else 1))
        t.append(f'<tr class="{"sub" if r["candidate"] in ("A", "B0", "B02", "F3") else ""}">' + ''.join(
            f'<td class="{"" if c[0] == "candidate" or math.isnan(r[c[0]]) else tone(c[0], r[c[0]])}">{cellv(c)}</td>' for c in cols) + '</tr>')
    return '\n'.join(t) + '</table></div>'


def summary_recs(recs):
    """One record per candidate over the scenes: the worst case for distances and intrusion, the mean for shares."""
    out = []
    for k in [*ROWS, *METRIC_ONLY]:
        rs = [r for r in recs if r['candidate'] == k]
        g = lambda c, f: float(f([r[c] for r in rs if not math.isnan(r[c])] or [NAN]))
        out.append(dict(scene='all', candidate=k, label=rs[0]['label'], head_min_m=g('head_min_m', min), head_median_m=g('head_median_m', np.mean), head_ge_0p40_share=g('head_ge_0p40_share', np.mean),
                        head_ge_0p40_share_action=g('head_ge_0p40_share_action', np.mean), eye_inside_head_share=g('eye_inside_head_share', max),
                        head_cover_gt25deg_share=g('head_cover_gt25deg_share', np.mean), strike_within35deg_share=g('strike_within35deg_share', np.mean),
                        strike_within35deg_share_action=g('strike_within35deg_share_action', np.mean), strike_in_view_share=g('strike_in_view_share', np.mean),
                        own_nearest_min_m=g('own_nearest_min_m', min), neck_stump_visible_share=g('neck_stump_visible_share', max), shoulder_cap_visible_share=g('shoulder_cap_visible_share', max),
                        eye_speed_peak_mps=g('eye_speed_peak_mps', max), eye_speed_p95_mps=g('eye_speed_p95_mps', max), eye_step_max_m=g('eye_step_max_m', max), anim_dist_max_m=g('anim_dist_max_m', max)))
    return out


def pick_best(recs):
    """The staging row (C..F2) with the best mean score over the scenes: head >= 0.40 m, strike within 35 deg, no neck stump, no shoulder cap (each a share of frames)."""
    def score(k):
        return float(np.mean([(r['head_ge_0p40_share'] + r['strike_within35deg_share'] + (1 - r['neck_stump_visible_share']) + (1 - r['shoulder_cap_visible_share'])) / 4
                              for r in recs if r['candidate'] == k]))
    return max(STAGING_SET, key=score), {k: score(k) for k in STAGING_SET}


def build_html(out, tag, scenes, recs, sanity_lines, best, scores, videos, pose_img, secs, figure):
    P = []
    P.append(f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Takedown camera lab {html.escape(tag)}</title><style>{CSS}</style></head><body><main>')
    P.append(f'<h1>Takedown camera lab: {html.escape(tag)}</h1><p class="mut">Offline render of the game\'s own takedown clips (player body + both arms, the victim\'s paired clip placed by the shared anchor), '
             f'first-person, eight eye placements. Nothing here ran the game or the DLL. Made by <code>python tools/takedown_lab.py {html.escape(tag)}</code> in {secs:.0f} s.</p>')
    P.append('<h2>How to read it</h2><ul>'
             '<li>Each scene has two sheets: rows are eye placements, columns are four moments (entry, contact = the victim head nearest the game camera, the strike, the end). '
             'Row A is the flat game at 90 deg. Every other row is the headset view, 104 x 104 deg, 1:1, with the headset rotation: level or 25 deg down, yaw latched at entry, no roll, and no physical lean (the eye is the animated camera position, smoothed or not, plus the staging offset). '
             'Tap a sheet to open it full size; on a phone it scrolls sideways.</li>'
             '<li>In every tile: white cross = gaze; dashed circle = 35 deg from the gaze; magenta ring = the strike point (right hand attachment); a magenta arrow on the border = it is out of view. '
             'Footer: head distance (and the angle the head covers), strike angle off the gaze, NECK / CAP when the headless neck stump (the coat\'s empty hood and collar) or the inside of a shoulder cap covers at least 1% of the view (metrics.csv has the share of frames and the worst share of the view).</li>'
             f'<li>The victim is {figure}, drawn with depth. The player is the real body and arm meshes (the body is headless). There is no blade mesh. The head metrics use a sphere of {HEAD_R} m at the census head centre.</li></ul>')
    P.append('<div class="legend"><h3>The rows</h3>' + ''.join(f'<span><b>{k}</b> {html.escape(v[0][len(k) + 1:])}</span>' for k, v in ROWS.items()) + '</div>')
    P.append(f'<ul class="mut"><li>B to F2 all use the D2 eye smoother on the animated position: critically damped (time constant {SMOOTH_TAU} s), speed cap {SMOOTH_VMAX} m/s, leash {SMOOTH_LEASH} m. '
             f'B0 (metrics only) is B without it.</li>'
             f'<li>C, D, E ease in over {RAMP_S} s (smoothstep) and are held. D = horizontally away from the victim head. E = 12 cm to the side that clears the head from the eye-to-strike line at their closest approach (latched) plus 8 cm up.</li>'
             f'<li>F = the smallest offset that keeps the head centre {CLEAR_M:.2f} m away, up first (to {AUTO_UP_CAP * 100:.0f} cm), then back, then sideways, total at most {AUTO_CAP * 100:.0f} cm, eased by at most {AUTO_STEP * 100:.0f} cm per frame. '
             f'F3 (metrics only) is the same with 3 cm per frame.</li></ul>')
    summ = summary_recs(recs)
    P.append('<h2>Headline over all scenes</h2><p class="mut">Head distance, eye-in-head and intrusion columns are the worst scene; the shares are means over the scenes. In action = frames where the game camera is within 0.60 m of the victim head. Green = good, red = bad '
             f'(head >= {CLEAR_M:.2f} m in 90% of frames; strike within 35 deg in 80%; neck stump / shoulder cap under 2%; own body nearer than 3 cm is red). '
             f'The head sphere covers more than {HEAD_COVER_DEG:.0f} deg of view whenever it is nearer than {HEAD_R / math.sin(math.radians(HEAD_COVER_DEG / 2)):.2f} m.</p>')
    P.append(table(summ))
    P.append(f'<p class="mut">Best staging row for the videos (mean of: head >= 0.40 m share, strike within 35 deg share, no neck stump, no shoulder cap): <b>{best}</b>. '
             'Scores: ' + ', '.join(f'{k} {v:.2f}' for k, v in scores.items()) + '.</p>')
    P.append('<h2>Sanity checks</h2><pre>' + html.escape('\n'.join(sanity_lines)) + '</pre>')
    if pose_img:
        P.append(f'<p>Third person: the posed player and the victim capsules in the first scene at entry, contact and the strike (three quarter, side, top).</p><div class="scroll"><a href="{pose_img}"><img src="{pose_img}" alt="third person"></a></div>')
    for k, sc in scenes.items():
        S, m = sc['S'], sc['mom']
        P.append(f'<h2>{html.escape(S["spec"]["title"])}</h2><p class="mut">{S["n"]} frames at {S["fps"]:.0f} fps. Moments: ' + ', '.join(f'{n} = frame {i} ({i / S["fps"]:.2f} s)' for n, i in m.items())
                 + (' (strike: the game camera pitches down only a little and early here, so the frame where the hand is nearest the victim neck is used)' if S['spec']['strike'] == 'pitch' and m['strike'] != int(np.argmin(S['cam_R'][:, 2, 0])) else '')
                 + '. ' + ' '.join(S['notes']) + '</p>')
        for name, _ in SHEETS:
            f = f'{k}_sheet_{name}.png'
            P.append(f'<div class="scroll"><a href="{f}"><img src="{f}" alt="{f}" loading="lazy"></a></div>')
        if k in videos:
            P.append(f'<p class="mut">Rows A, B2 and {best} side by side, 2/3 speed (every 2nd frame):</p><div class="scroll"><video src="{videos[k]}" controls loop muted playsinline preload="metadata"></video></div>')
        P.append(f'<div class="scroll"><a href="{k}_paths.png"><img src="{k}_paths.png" alt="paths" loading="lazy"></a></div>')
        P.append(f'<div class="scroll"><a href="{k}_timeline.png"><img src="{k}_timeline.png" alt="timeline" loading="lazy"></a></div>')
        P.append(table([r for r in recs if r['scene'] == k]))
    P.append('<h2>Files</h2><p><a href="metrics.csv">metrics.csv</a> (per scene and row, every metric) and <a href="sanity.txt">sanity.txt</a>.</p></main></body></html>')
    (out / 'index.html').write_text('\n'.join(P), encoding='utf-8')


# ------------------------------------------------------------------ video
def make_video(out, S, rows, shots, rowkeys, title, frames, fps_out=10):
    ff = shutil.which('ffmpeg')
    if not ff:
        print('no ffmpeg on PATH: skipping the video of', S['key'])
        return None
    tmp = out / f'_frames_{S["key"]}'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    for j, i in enumerate(frames):
        cells = [cell(shots[(k, i)], k, rows[k], S, i, '') for k in rowkeys]
        hmax = max(c.shape[0] for c in cells)
        cells = [np.pad(c, ((0, hmax - c.shape[0]), (0, 0), (0, 0)), constant_values=26) for c in cells]
        gap = np.full((hmax, R.GAP, 3), 90, np.uint8)
        bar = Image.new('RGB', (sum(c.shape[1] for c in cells) + R.GAP * (len(cells) - 1), 34), (40, 40, 48))
        text(ImageDraw.Draw(bar), (8, 5), f'{title}   (2/3 speed)', 20, (255, 255, 255))
        Image.fromarray(np.vstack([np.asarray(bar), np.hstack([x for c in zip(cells, [gap] * (len(cells) - 1) + [None]) for x in c if x is not None])])).save(tmp / f'f{j:03d}.png')
    mp4 = out / f'{S["key"]}.mp4'
    r = subprocess.run([ff, '-y', '-loglevel', 'error', '-framerate', str(fps_out), '-i', str(tmp / 'f%03d.png'), '-vf', 'scale=1280:-2', '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-crf', '24', '-movflags', '+faststart', str(mp4)], capture_output=True, text=True)
    shutil.rmtree(tmp, ignore_errors=True)
    if r.returncode:
        print('ffmpeg failed:', r.stderr[:300])
        return None
    return mp4.name


# ------------------------------------------------------------------ driver
def run(tag, scene_keys, workers, video=True, mesh=True):
    out = OUT_ROOT / tag
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    lower_priority()
    scenes = {}
    for k in scene_keys:
        S = load_scene(k)
        rows = build_rows(S)
        for r in rows.values():
            r['pf'] = row_metrics(S, r)
        scenes[k] = dict(S=S, rows=rows, mom=find_moments(S))
        print(f'{k}: {S["n"]} frames, moments {scenes[k]["mom"]}, E moves to the {"left" if S["side"] > 0 else "right"}')
    vdir = None
    if mesh:
        try:
            import victim_pairing as VP
            victim_mesh_assets(VP.V)
            vdir = str(VP.V)
        except Exception as ex:                                              # noqa: BLE001 - say why the capsules are used
            print(f'victim mesh unavailable ({ex!r}): using the capsule figure')
    print('victim figure:', 'base tall NPC body + head mesh (unclothed grey) with a cap' if vdir else 'capsules')
    tasks = []
    for k, sc in scenes.items():
        S, rows = sc['S'], sc['rows']
        keep = set(sc['mom'].values())
        sc['vis_frames'] = sorted(keep | set(range(0, S['n'], 2)))                  # intrusion is evaluated at 15 Hz and at the four moments (held between)
        for i in sc['vis_frames']:
            tiles = [spec(rk, rows[rk], i, *TILE_PX.get(rk, (TILE, TILE))) for rk in ROWS] if i in keep else []
            tasks.append((k, i, tiles, [spec(rk, rows[rk], i, *VIS_PX.get(rk, (96, 96))) for rk in ROWS]))
    print(f'{len(tasks)} frame tasks on {workers} workers ...')
    shots, vshots, videos = {k: {} for k in scenes}, {}, {}
    first = next(iter(scenes))
    with ProcessPoolExecutor(workers, initializer=_worker_init, initargs=({k: light(sc['S'], vdir) for k, sc in scenes.items()},)) as pool:
        pose_futs = [pool.submit(pose_task, (first, scenes[first]['mom'][m])) for m in ('entry', 'contact', 'strike')]
        for k, i, res in pool.map(render_task, tasks, chunksize=2):
            sc = scenes[k]
            for rk, (near, neck, cap, body) in res['vis'].items():
                pf = sc['rows'][rk]['pf']
                for name, v in (('nearest', near), ('neck', neck), ('cap', cap), ('body', body)):
                    pf.setdefault(name, np.zeros(sc['S']['n']))[i] = v
            for rk, img in res['tiles'].items():
                for m, fi in sc['mom'].items():
                    if fi == i:
                        shots[k][(rk, m)] = img
        for sc in scenes.values():                                                   # hold the intrusion numbers on the frames in between
            done = np.zeros(sc['S']['n'], bool)
            done[sc['vis_frames']] = True
            for rk in ROWS:
                pf = sc['rows'][rk]['pf']
                for name in ('nearest', 'neck', 'cap', 'body'):
                    for i in range(1, sc['S']['n']):
                        if not done[i]:
                            pf[name][i] = pf[name][i - 1]
        print(f'stage 1 (tiles and intrusion) done at {time.time() - t0:.0f} s')
        recs = [aggregate(sc['S'], rk, r) for sc in scenes.values() for rk, r in sc['rows'].items()]
        best, scores = pick_best(recs)
        if video:
            vkeys = ['A', 'B2', best]
            vf = {k: sorted(set(range(0, sc['S']['n'], 2)) | {sc['S']['n'] - 1}) for k, sc in scenes.items()}
            vt = [(k, i, [spec(rk, sc['rows'][rk], i, *TILE_PX.get(rk, (TILE, TILE))) for rk in vkeys], []) for k, sc in scenes.items() for i in vf[k]]
            for k, i, res in pool.map(render_task, vt, chunksize=2):
                for rk, img in res['tiles'].items():
                    vshots.setdefault(k, {})[(rk, i)] = img
            print(f'stage 2 (video frames) done at {time.time() - t0:.0f} s')
            for k, sc in scenes.items():
                v = make_video(out, sc['S'], sc['rows'], vshots[k], vkeys, sc['S']['spec']['title'], vf[k])
                if v:
                    videos[k] = v
        pose = None
        rowimg = [np.asarray(f.result()[2]) for f in pose_futs]
        Image.fromarray(np.vstack(rowimg)).save(out / 'third_person.png')
        pose = 'third_person.png'
    for k, sc in scenes.items():
        S, rows, mom = sc['S'], sc['rows'], sc['mom']
        for si, (name, keys) in enumerate(SHEETS):
            make_sheet(S, rows, mom, keys, shots[k], out / f'{k}_sheet_{name}.png', 'level gaze' if si == 0 else 'gaze 25 deg down')
        make_paths(S, rows, mom, out / f'{k}_paths.png')
        make_timeline(S, rows, mom, out / f'{k}_timeline.png')
    write_csv(out / 'metrics.csv', recs)
    lines = sanity(scenes, out)
    build_html(out, tag, scenes, recs, lines, best, scores, videos, pose, time.time() - t0,
               'the game\'s base tall NPC body and head mesh (unclothed, grey, skinned to the same skeleton as the clips) with a flat cap' if vdir else 'a capsule figure from its own skeleton (head sphere, cap, torso, limbs)')
    print('\n'.join(lines))
    print(f'done in {time.time() - t0:.0f} s -> {out}')
    return out, scenes, recs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('tag', help='output folder name under artifacts/takedown-lab/')
    ap.add_argument('--scenes', default=','.join(SCENES), help='comma list of: ' + ', '.join(SCENES))
    ap.add_argument('--workers', type=int, default=min(14, os.cpu_count() or 1))
    ap.add_argument('--no-video', action='store_true', help='skip the MP4s (they need ffmpeg on PATH)')
    ap.add_argument('--capsules', action='store_true', help='draw the victim as capsules instead of the base NPC mesh')
    ap.add_argument('--tau', type=float, default=SMOOTH_TAU, help='D2 smoother time constant, s (default %(default)s)')
    ap.add_argument('--vmax', type=float, default=SMOOTH_VMAX, help='D2 smoother speed cap, m/s (default %(default)s)')
    ap.add_argument('--leash', type=float, default=SMOOTH_LEASH, help='D2 smoother leash, m (default %(default)s)')
    a = ap.parse_args(argv)
    globals().update(SMOOTH_TAU=a.tau, SMOOTH_VMAX=a.vmax, SMOOTH_LEASH=a.leash)
    run(a.tag, [s for s in a.scenes.split(',') if s], a.workers, not a.no_video, not a.capsules)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
