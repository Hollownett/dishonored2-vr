"""Build a self-contained offline asset pack of the Dishonored 2 player full body.

Decodes the extracted raw MD6 skeletons/meshes of Emily and Corvo (Corvo reuses
Emily's three skeletons, see corvo/body.md6), verifies the decoded bind pose
against the read-only native inverse-bind dumps and writes skeletons, per-part
npz payloads, tags, a manifest and a bind-pose preview PNG.

Run from the repository root:  python tools/build_player_asset_pack.py
"""
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / 'artifacts/fullbody-assets/raw/models/characters/player'
PACK = ROOT / 'artifacts/fullbody-assets/pack'
PREVIEW = ROOT / 'artifacts/fullbody-assets/preview'
LAYOUT_DUMP = ROOT / 'artifacts/native-fullbody-layout-23072-readonly.json'
RIG_DUMP = ROOT / 'artifacts/native-fullbody-rig-23072-readonly.json'
sys.path.insert(0, str(ROOT / 'tools'))
from analyse_arm_mesh import decode_mesh  # noqa: E402  (existing native-verified decoder)

PARTS = {'body': 20, 'left_arm': 69, 'right_arm': 69}


# ---------------------------------------------------------------- skeletons
def quat_matrix(q):
    """xyzw unit quaternion -> 3x3 rotation matrix (column-vector convention)."""
    x, y, z, w = q
    n = x * x + y * y + z * z + w * w
    s = 2.0 / n
    return np.array([[1 - s * (y * y + z * z), s * (x * y - z * w), s * (x * z + y * w)],
                     [s * (x * y + z * w), 1 - s * (x * x + z * z), s * (y * z - x * w)],
                     [s * (x * z - y * w), s * (y * z + x * w), 1 - s * (x * x + y * y)]])


def parse_skeleton(path):
    """MD6SKL: header, 4-wide-padded (joint,parent) u16 edge list, then per joint
    48 bytes = quaternion xyzw (f32x4), translation xyz+pad (f32x4), scale xyz+pad."""
    d = path.read_bytes()
    assert d[:4] == b'60SE', path
    n = struct.unpack_from('<I', d, 0x10)[0]
    start = 0x50  # header ends here in every shipped skeleton (field 0x0c is not the size)
    t0 = None
    for off in range(start, len(d) - 48 * n + 1, 4):
        ok = True
        for j in range(n):
            o = off + 48 * j
            q = np.frombuffer(d, '<f4', 4, o)
            sc = np.frombuffer(d, '<f4', 4, o + 32)
            if abs(float(q @ q) - 1.0) > 1e-3 or np.abs(sc[:3] - 1.0).max() > 1e-4:
                ok = False
                break
        if ok:
            t0 = off
            break
    assert t0 is not None, path
    parents = [-1] * n
    for o in range(start, t0, 4):
        child, parent = struct.unpack_from('<2H', d, o)
        if child == 0 or child >= n or parent >= n:
            continue
        assert parents[child] in (-1, parent), 'conflicting parent for joint %d' % child
        parents[child] = parent
    names_at = d.find(b'\x06\x00\x00\x00origin')
    assert names_at > 0, 'joint name table not found'
    names, o = [], names_at
    for _ in range(n):
        ln = struct.unpack_from('<I', d, o)[0]
        o += 4
        names.append(d[o:o + ln].decode())
        o += ln
    local = []
    for j in range(n):
        o = t0 + 48 * j
        q = struct.unpack_from('<4f', d, o)
        t = struct.unpack_from('<4f', d, o + 16)
        s = struct.unpack_from('<4f', d, o + 32)
        local.append((np.array(t[:3]), np.array(q), np.array(s[:3])))
    model = []
    for j in range(n):
        t, q, s = local[j]
        m = np.eye(4)
        m[:3, :3] = quat_matrix(q) * s[0]
        m[:3, 3] = t
        model.append(model[parents[j]] @ m if parents[j] >= 0 else m)
    return dict(names=names, parents=parents, local=local, model=np.array(model),
                inverse=np.linalg.inv(np.array(model)),
                edge_pairs=(t0 - start) // 4, transform_offset=t0, bytes=len(d))


# ------------------------------------------------------------------- meshes
ATTR_LAYOUT = 'slot1: texcoord0 f16x2 + texcoord1 f16x2 interleaved (8 B) | slot2: normal i8x3n+pad | slot3: tangent i8x3n+handedness'


def half_to_float(bits):
    """Vectorised IEEE-754 binary16 -> float32 (used only for the pack report)."""
    b = np.asarray(bits, np.uint32)
    s = np.where(b & 0x8000, -1.0, 1.0)
    e = ((b >> 10) & 0x1F).astype(np.int32)
    m = (b & 0x3FF).astype(np.float64)
    val = np.where(e == 0, m * 2.0 ** -24, (1024.0 + m) * 2.0 ** (e - 25))
    return (s * val).astype(np.float32)


def parse_mesh(path):
    """MD6MESH, vertex stream 44 B: N*12 positions, then N*16 attributes stored
    per native input slot (slot 1 interleaved half2 texcoord0/1, slot 2 int8 normal, slot 3 int8 tangent),
    then N*16 skin (8 weight bytes + 8 joint bytes).  Indices are u16."""
    d = path.read_bytes()
    assert d[:4] == b'MM6B' and d[-4:] == b'MM6B', path
    o = 4

    def u32():
        nonlocal o
        v = struct.unpack_from('<I', d, o)[0]
        o += 4
        return v

    def name():
        nonlocal o
        ln = u32()
        s = d[o:o + ln].decode()
        o += ln
        return s

    skeleton = name()
    o += 2 + 24
    surfaces = []
    for _ in range(u32()):
        surf, material, kind = name(), name(), u32()
        o += 5
        assert d[o:o + 4] == b'BTRI'
        o += 4
        version, cpu, gpu = u32(), u32(), u32()
        o += 40
        nv, streamed = u32(), struct.unpack_from('<Q', d, o)[0]
        o += 8
        assert cpu == 10 and gpu == 10 and streamed == 0, 'unverified vertex format'
        raw = d[o:o + nv * 44]
        o += nv * 44
        ni = u32()
        index = np.frombuffer(d, '<u2', ni, o).astype(np.uint32)
        o += ni * 2
        bounds = struct.unpack_from('<6f', d, o)
        o += 24
        u32()
        nr = u32()
        ranges = np.frombuffer(d, '<u4', nr * 3, o).reshape(-1, 3).copy()
        o += nr * 12 + 48
        pos = np.frombuffer(raw, '<f4', nv * 3, 0).reshape(nv, 3).astype(np.float32)
        attr = raw[nv * 12:nv * 28]
        # Native vertex elements (native-fullbody-layout report): slot 1 = texcoord0|texcoord1
        # interleaved (8 B/vertex, half2 each), slot 2 = normals, slot 3 = tangents (4 B each).
        tc = np.frombuffer(attr, '<u2', nv * 4, 0).reshape(nv, 4)
        raw0, raw1 = tc[:, :2].copy(), tc[:, 2:].copy()
        uv0 = half_to_float(raw0)
        uv1 = half_to_float(raw1)
        nrm = (np.frombuffer(attr, np.uint8, nv * 4, nv * 8).reshape(nv, 4)
               .astype(np.int8) / np.float32(127.0))
        tan = (np.frombuffer(attr, np.uint8, nv * 4, nv * 12).reshape(nv, 4)
               .astype(np.int8) / np.float32(127.0))
        skin = np.frombuffer(raw, np.uint8, nv * 16, nv * 28).reshape(nv, 16)
        surfaces.append(dict(name=surf, material=material, kind=kind, version=version,
                             vertices=nv, indices=ni, index=index.reshape(-1, 3),
                             positions=pos, uv0=uv0, uv1=uv1, texcoord_raw=raw0,
                             texcoord_raw2=raw1, normals=nrm, tangents=tan,
                             weights=skin[:, :8], joints=skin[:, 8:], ranges=ranges,
                             bounds=bounds))
    return dict(skeleton=skeleton, surfaces=surfaces, footer=len(d) - o)


def merge_surfaces(mesh):
    out = {}
    for key in ('positions', 'uv0', 'uv1', 'texcoord_raw', 'texcoord_raw2', 'normals',
                'tangents', 'weights', 'joints'):
        out[key] = np.concatenate([s[key] for s in mesh['surfaces']])
    starts, counts, istart, icount = [], [], [], []
    index, base = [], 0
    for s in mesh['surfaces']:
        starts.append(base)
        counts.append(s['vertices'])
        istart.append(sum(len(i) for i in index))
        icount.append(len(s['index']))
        index.append(s['index'] + base)
        base += s['vertices']
    out['index'] = np.concatenate(index)
    out['vertex_start'] = np.array(starts, np.uint32)
    out['vertex_count'] = np.array(counts, np.uint32)
    out['index_start'] = np.array(istart, np.uint32)
    out['index_count'] = np.array(icount, np.uint32)
    out['surface_name'] = np.array([s['name'] for s in mesh['surfaces']])
    out['material'] = np.array([s['material'] for s in mesh['surfaces']])
    return out


# -------------------------------------------------------------------- decls
TAG_RE = re.compile(r'tag\s+"([^"]+)"\s*\{\s*trans\s*\(([^)]*)\)\s*rot\s*\(([^)]*)\)'
                    r'\s*parent\s+"([^"]*)"')
PATH_RE = re.compile(r'(skeleton|mesh)\s+"([^"]+)"')


def parse_decl(path):
    text = path.read_text(encoding='utf-8')
    paths = {kind: val for kind, val in PATH_RE.findall(text)}
    tags = []
    for name, trans, rot, parent in TAG_RE.findall(text):
        tags.append(dict(name=name, parent=parent,
                         trans=[float(v) for v in trans.split()],
                         rot_xyzw=[float(v) for v in rot.split()]))
    return dict(paths=paths, tags=tags)


# ------------------------------------------------------------------ preview
def rasterize(parts, azim, elev, w, h, span, center):
    """Tiny numpy z-buffer rasterizer with Lambert shading."""
    fwd = -np.array([math.cos(elev) * math.cos(azim), math.cos(elev) * math.sin(azim),
                     math.sin(elev)])
    right = np.cross(fwd, [0.0, 0.0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    light = (0.55 * (-fwd) + 0.45 * up + 0.35 * right)
    light /= np.linalg.norm(light)
    color = np.ones((h, w, 3), np.float32) * 0.94
    depth = np.full((h, w), np.inf, np.float32)
    for verts, tris, normals, rgb in parts:
        rel = (verts - center)
        px = (rel @ right / span + 0.5) * w
        py = (0.5 - rel @ up / span) * h
        pz = rel @ fwd
        a, b, c = verts[tris[:, 0]], verts[tris[:, 1]], verts[tris[:, 2]]
        fn = np.cross(b - a, c - a)
        fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-20)
        vn = normals[tris].mean(axis=1)
        vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-20)
        if (np.einsum('ij,ij->i', fn, vn) < 0).mean() > 0.5:
            fn = -fn
        keep = (fn @ fwd) < 0                       # back-face cull
        shade = (0.20 + 0.80 * np.abs(vn @ light))[:, None] * np.array(rgb, np.float32)
        for t in np.nonzero(keep)[0]:
            x0, y0, x1, y1, x2, y2 = (px[tris[t, 0]], py[tris[t, 0]],
                                      px[tris[t, 1]], py[tris[t, 1]],
                                      px[tris[t, 2]], py[tris[t, 2]])
            xlo, xhi = int(max(min(x0, x1, x2), 0)), int(min(max(x0, x1, x2) + 1, w))
            ylo, yhi = int(max(min(y0, y1, y2), 0)), int(min(max(y0, y1, y2) + 1, h))
            if xhi - xlo < 1 or yhi - ylo < 1:
                continue
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-9:
                continue
            xs = np.arange(xlo, xhi) + 0.5
            ys = np.arange(ylo, yhi) + 0.5
            gx, gy = np.meshgrid(xs, ys)
            l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
            l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
            l2 = 1.0 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any():
                continue
            z = l0 * pz[tris[t, 0]] + l1 * pz[tris[t, 1]] + l2 * pz[tris[t, 2]]
            sub = depth[ylo:yhi, xlo:xhi]
            hit = inside & (z < sub)
            sub[hit] = z[hit]
            color[ylo:yhi, xlo:xhi][hit] = shade[t]
    return np.clip(color * 255.0, 0, 255).astype(np.uint8)


def write_preview(path, parts, title):
    pts = np.vstack([p[0] for p in parts])
    center = (pts.min(0) + pts.max(0)) / 2.0
    span = float(max((pts.max(0) - pts.min(0))[i] for i in (0, 1, 2))) * 1.12
    views = [(0.0, 0.0), (math.pi / 2, 0.0), (math.radians(38), math.radians(12))]
    tiles = [rasterize(parts, az, el, 420, 760, span, center) for az, el in views]
    gap = np.full((760, 6, 3), 210, np.uint8)
    out = np.hstack([tiles[0], gap, tiles[1], gap, tiles[2]])
    Image.fromarray(out).save(path)
    print('preview %s (%s) %dx%d' % (path.name, title, out.shape[1], out.shape[0]))


# --------------------------------------------------------------------- main
def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    PACK.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)

    skeleton_files = {'body': RAW / 'emily/player_body_emily.md6skl',
                      'left_arm': RAW / 'emily/player_leftarm_emily.md6skl',
                      'right_arm': RAW / 'emily/player_rightarm_emily.md6skl'}
    skeletons = {k: parse_skeleton(p) for k, p in skeleton_files.items()}

    layout = json.loads(LAYOUT_DUMP.read_text())['rig_samples'][0]
    rig = json.loads(RIG_DUMP.read_text())['body_samples'][0]['arms']
    live = {'body': (layout['joint_names'], layout['skeleton_layout']['parent_indices'],
                     layout['skinning_layout']['inverse_bind_rows'], 'layout dump')}
    for arm in rig:
        live[arm['side'] + '_arm'] = (arm['joint_names'], arm['skeleton_layout']['parent_indices'],
                                      arm['skinning_layout']['inverse_bind_rows'], 'rig dump')

    verification, max_error = {}, 0.0
    for key, sk in skeletons.items():
        names, parents, inverse, source = live[key]
        err = float(np.abs(sk['inverse'][:, :3, :] - np.array(inverse).reshape(-1, 3, 4)).max())
        max_error = max(max_error, err)
        verification[key] = dict(source=source, joints=len(names),
                                 names_match=sk['names'] == names,
                                 parents_match=sk['parents'] == parents,
                                 inverse_bind_max_abs_error=err,
                                 passed=(err < 1e-3 and sk['names'] == names
                                         and sk['parents'] == parents))
        print('verify %-9s joints=%2d names=%s parents=%s inverse-bind max|err|=%.3e %s'
              % (key, len(names), sk['names'] == names, sk['parents'] == parents, err,
                 'PASS' if verification[key]['passed'] else 'FAIL'))

    for key, sk in skeletons.items():
        doc = dict(name='player_%s' % key, joint_count=len(sk['names']),
                   source_file=str(skeleton_files[key].relative_to(ROOT)),
                   joint_names=sk['names'], parents=sk['parents'],
                   local_bind=[dict(trans=[float(v) for v in t], rot_xyzw=[float(v) for v in q],
                                    scale=[float(v) for v in s])
                               for t, q, s in sk['local']],
                   model_bind_3x4=[np.round(m[:3], 8).tolist() for m in sk['model']],
                   inverse_bind_3x4=[np.round(m[:3], 8).tolist() for m in sk['inverse']],
                   verification=verification[key])
        (PACK / ('skeleton_%s.json' % key)).write_text(json.dumps(doc, indent=1) + '\n')

    characters = {'emily': 'emily', 'corvo': 'corvo'}
    attach = {}
    for char, folder in characters.items():
        decls = {key: parse_decl(RAW / folder / ('%s.md6' % key)) for key in PARTS}
        tags_doc = {key: decl['tags'] for key, decl in decls.items()}
        (PACK / char).mkdir(parents=True, exist_ok=True)
        (PACK / char / 'tags.json').write_text(json.dumps(
            dict(source={k: str((RAW / folder / ('%s.md6' % k)).relative_to(ROOT)) for k in PARTS},
                 skeletons={k: decls[k]['paths']['skeleton'] for k in PARTS},
                 meshes={k: decls[k]['paths']['mesh'] for k in PARTS},
                 tags=tags_doc), indent=1) + '\n')
        attach[char] = {}
        for key, decl in decls.items():
            mesh_file = RAW / folder / decl['paths']['mesh'].split('/')[-1]
            mesh = parse_mesh(mesh_file)
            assert mesh['skeleton'].split('/')[-1] == skeleton_files[key].name, mesh['skeleton']
            merged = merge_surfaces(mesh)
            sk = skeletons[key]
            # cross-check against the existing native-verified decoder
            reference = decode_mesh(mesh_file.read_bytes(), PARTS[key])
            ref_pos = np.array([v[0] for s in reference['surfaces'] for v in s['vertices']], np.float32)
            ref_w = np.frombuffer(b''.join(v[1] for s in reference['surfaces']
                                           for v in s['vertices']), np.uint8).reshape(-1, 8)
            ref_j = np.frombuffer(b''.join(v[2] for s in reference['surfaces']
                                           for v in s['vertices']), np.uint8).reshape(-1, 8)
            assert np.abs(ref_pos - merged['positions']).max() == 0
            assert np.array_equal(ref_w, merged['weights']) and np.array_equal(ref_j, merged['joints'])
            assert merged['joints'].max() < PARTS[key]
            assert np.array_equal(merged['weights'].astype(int).sum(1) % 255, np.zeros(len(ref_w), int))
            # The ring sits on the middle finger's proximal segment in bind, but is skinned to a
            # thumb muscle helper (and Emily's index_end leaf), so it drifts whenever the thumb
            # moves relative to that finger. Re-bind the ring surface rigidly to medium_02;
            # bind pose is unchanged. Native weights are kept as weights_native/joints_native.
            merged['weights_native'], merged['joints_native'] = merged['weights'].copy(), merged['joints'].copy()
            if key != 'body':
                finger = list(sk['names']).index('%s_medium_02_jnt' % key.split('_')[0])
                for s, material in enumerate(merged['material']):
                    if 'ring' in str(material).split('/')[-1]:
                        a, n = merged['vertex_start'][s], merged['vertex_count'][s]
                        merged['weights'][a:a + n] = 0; merged['weights'][a:a + n, 0] = 255
                        merged['joints'][a:a + n] = 0; merged['joints'][a:a + n, 0] = finger
            np.savez_compressed(PACK / char / ('%s.npz' % key), **merged)
            nrm_len = np.linalg.norm(merged['normals'][:, :3], axis=1)
            uv_frac = np.modf(merged['uv0'])[0]
            uv_in = ((uv_frac >= 0) & (uv_frac < 1)).mean()
            attach_name = '%s_attach_tag' % key if key != 'body' else 'legs_attach_tag'
            tag = next(t for t in decls['body']['tags'] if t['name'] == attach_name)
            if key == 'body':
                assert tag['parent'] == 'origin', tag
            else:
                assert tag['parent'] == 'center_spine03_jnt', tag
                attach[char][key] = tag
            print('mesh %-5s %-9s verts=%5d tris=%6d surfaces=%d footer=%d '
                  'normal|len|=[%.4f,%.4f] uv0 frac in[0,1)=%.3f attach=%s@%s'
                  % (char, key, merged['positions'].shape[0], merged['index'].shape[0],
                     len(mesh['surfaces']), mesh['footer'], nrm_len.min(), nrm_len.max(), uv_in,
                     tag['name'], tag['parent']))

        # ---------------------------------------------------------- preview
        parts = []
        body = np.load(PACK / char / 'body.npz')
        parts.append((body['positions'], body['index'], body['normals'][:, :3], (0.76, 0.68, 0.60)))
        spine = skeletons['body']['names'].index('center_spine03_jnt')
        for key, colour in (('left_arm', (0.55, 0.66, 0.82)), ('right_arm', (0.82, 0.60, 0.58))):
            tag = attach[char][key]
            place = np.eye(4)
            place[:3, :3] = skeletons['body']['model'][spine][:3, :3]
            place[:3, 3] = skeletons['body']['model'][spine][:3, 3]
            offset = np.eye(4)
            offset[:3, :3] = quat_matrix(tag['rot_xyzw'])
            offset[:3, 3] = tag['trans']
            place = place @ offset
            assert skeletons['body']['names'][spine] == tag['parent']
            arm = np.load(PACK / char / ('%s.npz' % key))
            parts.append(((arm['positions'] @ place[:3, :3].T + place[:3, 3]), arm['index'],
                          arm['normals'][:, :3] @ place[:3, :3].T, colour))
        write_preview(PREVIEW / ('%s-bind.png' % char), parts, char)

        # ------------------------------------------------------ proportions
        model = skeletons['body']['model']
        named = {n: model[i] for i, n in enumerate(skeletons['body']['names'])}
        arm_sk = skeletons['left_arm']
        anamed = {n: arm_sk['model'][i] for i, n in enumerate(arm_sk['names'])}

        def to_body(key, joint):
            """place a joint of the left/right arm skeleton into body bind space"""
            tag = attach[char][key]
            place = np.eye(4)
            place[:3, :3] = model[spine][:3, :3] @ quat_matrix(tag['rot_xyzw'])
            place[:3, 3] = model[spine][:3, 3] + model[spine][:3, :3] @ np.array(tag['trans'])
            return place @ np.append(skeletons[key]['model'][
                skeletons[key]['names'].index(joint)][:3, 3], 1.0)

        upper_l, upper_r = to_body('left_arm', 'left_arm_jnt'), to_body('right_arm', 'right_arm_jnt')
        props = dict(
            head_top_m=float(named['center_head_end_jnt'][2, 3]),
            eye_camera_jnt_m=float(named['camera_jnt'][2, 3]),
            head_center_m=float(named['center_head_jnt'][2, 3]),
            pelvis_center_root_m=float(named['center_root_jnt'][2, 3]),
            upper_arm_joint_z_m=float(upper_l[2]),
            shoulder_width_m=float(np.linalg.norm(upper_l[:3] - upper_r[:3])),
            shoulder_half_width_m=float(upper_l[1]),
            femur_length_m=float(np.linalg.norm(named['left_tibia_jnt'][:3, 3]
                                                - named['left_femur_jnt'][:3, 3])),
            tibia_length_m=float(np.linalg.norm(named['left_ankle_jnt'][:3, 3]
                                                - named['left_tibia_jnt'][:3, 3])),
            ankle_height_m=float(named['left_ankle_jnt'][2, 3]),
            upper_arm_m=float(np.linalg.norm(anamed['left_elbow_jnt'][:3, 3]
                                             - anamed['left_arm_jnt'][:3, 3])),
            forearm_m=float(np.linalg.norm(anamed['left_wrist_jnt'][:3, 3]
                                           - anamed['left_elbow_jnt'][:3, 3])),
            wrist_to_palm_m=float(np.linalg.norm(anamed['left_medium_01_jnt'][:3, 3]
                                                 - anamed['left_wrist_jnt'][:3, 3])),
            wrist_to_attachment_m=float(np.linalg.norm(
                anamed['left_hand_attachment_jnt'][:3, 3] - anamed['left_wrist_jnt'][:3, 3])),
            hand_length_m=float(np.linalg.norm(anamed['left_medium_end_jnt'][:3, 3]
                                               - anamed['left_wrist_jnt'][:3, 3])),
            fingertip_span_m=float(np.linalg.norm(
                anamed['left_medium_end_jnt'][:3, 3] - anamed['left_arm_jnt'][:3, 3]) * 2.0
                + np.linalg.norm(upper_l[:3] - upper_r[:3])))
        print('%s proportions (metres):' % char)
        for k, v in props.items():
            print('   %-24s %.4f' % (k, v))
        (PACK / char / 'proportions.json').write_text(json.dumps(props, indent=1) + '\n')

    manifest = dict(
        generator='tools/build_player_asset_pack.py',
        sources=dict(live_dumps=[str(LAYOUT_DUMP.relative_to(ROOT)), str(RIG_DUMP.relative_to(ROOT))],
                     native_dumps_readonly=True, game_install_touched=False),
        skeleton_format=dict(magic='60SE', joint_count_offset='0x10',
                             header_bytes='0x50',
                             hierarchy='u16 (joint,parent) edge list in 4-lane padded order',
                             transform_record='48 B = quat xyzw f32x4, trans xyz+pad f32x4, '
                                              'scale xyz+pad f32x4',
                             root_local='identity; joint 1 translation z = 1.054913 m'),
        mesh_format=dict(
            vertex_stride=44,
            stream='N*12 position f32x3 | N*16 attributes | N*16 skin (8 weight u8, 8 joint u8)',
            attribute_block=ATTR_LAYOUT,
            decoded_attributes={
                'texcoord0/1': 'f16x2 interleaved in native input slot 1 (offsets 0/4, from the live '
                               'vertex-element report). Body texcoord0 wireframe overlays exactly '
                               'onto player_corvo_body_d islands; ~2% of vertices have u>1.',
                'normal': 'i8x3 normalised by 127, 4th byte 0. Unit length measured '
                          '0.9945..1.0059 over all 6 meshes; mean |dot| with the geometric '
                          'smoothed geometric normal -0.998 (winding flipped); confidence high.',
                'tangent': 'i8x3 normalised by 127, 4th byte = handedness (-1/127 or 1/127). '
                           'Unit length and exactly orthogonal to the decoded normal, but it '
                           'does not align with dP/du of either stored UV set '
                           '(mean |dot| 0.62 for every candidate), so its reference UV set is '
                           'unresolved. Confidence high for the packing, low for the meaning.'},
            index='uint16',
            trailing_tables_undecoded='after the 48 B/joint transform array each file carries '
                                      'an n-entry padded u16 table and then a length-prefixed '
                                      'joint name table; the u16 table content and the '
                                      'per-surface footer bytes are still unidentified'),
        verification=verification, max_inverse_bind_error=max_error,
        coordinate_system='Z up, +Y left, +X forward, metres')
    files = sorted(p for p in PACK.rglob('*') if p.is_file() and p.name != 'manifest.json')
    files += sorted(PREVIEW.glob('*.png'))
    manifest['files'] = {str(p.relative_to(ROOT)): dict(bytes=p.stat().st_size, sha256=sha256(p))
                         for p in files}
    (PACK / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    print('verification: max inverse-bind error %.3e -> %s'
          % (max_error, 'PASS' if max_error < 1e-3 else 'FAIL'))
    print('manifest: %d files' % len(manifest['files']))


if __name__ == '__main__':
    main()
