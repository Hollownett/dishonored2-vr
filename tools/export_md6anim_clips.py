"""Decode selected player clips -> artifacts/fullbody-assets/anims/<clip>.json (+ stick-figure preview PNGs).
Usage: python tools/export_md6anim_clips.py [clip_name ...]  (default: curated list)"""
import glob, json, sys, warnings
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).parent))
import decode_md6anim as m

warnings.filterwarnings('ignore')
ROOT = Path(__file__).resolve().parents[1] / 'artifacts/fullbody-assets/anims'
RAW = ROOT / 'raw/models/characters/player/_animations_'
DEFAULT = ['ply_body_empty_idle', 'ply_body_generic_crouch_idle', 'ply_body_generic_walk', 'ply_body_generic_run',
           'ply_body_generic_jumpland_big', 'ply_body_generic_jumplandbig_roll', 'ply_body_generic_mantle_idle',
           'ply_body_generic_mantlelow_idle', 'ply_body_generic_vault_a', 'ply_body_generic_emily_assassination_dropfall_in',
           'ply_body_generic_emily_assassination_dropfall_loop', 'ply_body_generic_emily_takedown_dropfall_loop',
           'ply_body_generic_emily_choke_loop', 'ply_body_generic_emily_chokeback_in',
           'ply_body_generic_corvo_assassination_back', 'ply_body_generic_fatality_knockdown',
           'ply_body_generic_takedown_knockdown_front', 'ply_body_generic_npc_guard_fatality',
           'ply_camera_generic_jump_in', 'ply_camera_generic_jump_loop', 'ply_camera_generic_jump_out',
           'ply_rightarm_sword_idle', 'ply_rightarm_sword_attack_right_a', 'ply_leftarm_empty_idle']


def qmat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def model(Q, T, par):
    R = [None]*len(par); P = [None]*len(par)
    for j in range(len(par)):
        r = qmat(Q[j])
        if par[j] < 0: R[j], P[j] = r, T[j].copy()
        else: R[j], P[j] = R[par[j]] @ r, P[par[j]] + R[par[j]] @ T[j]
    return np.array(P)


def find(name):
    # prefer emily variant for player-specific arms, else first match
    hits = sorted(glob.glob(str(RAW / '*' / '*' / (name + '.md6anim'))), key=lambda s: ('emily' not in s, s))
    return hits[0] if hits else None


def export(name, preview=True):
    f = find(name)
    if not f: print('missing', name); return
    a = m.decode(f)
    kind = a['skeleton'].replace('skeleton_', '').replace('.json', '')
    sk = json.load(open(m.PACK / a['skeleton'])); par = sk['parents']
    Q, T = a['Q'], a['T']
    out = [[list(map(float, Q[fr, j])) + list(map(float, T[fr, j])) for j in range(Q.shape[1])] for fr in range(len(Q))]
    json.dump(dict(skeleton=kind, frame_rate=a['frame_rate'], source=f.split('_animations_')[1].replace(chr(92), '/'),
                   joint_names=sk['joint_names'], frames=out), open(ROOT / (name + '.json'), 'w'))
    # plausibility: translation-length drift vs bind, root path
    bind = np.array([x['trans'] for x in sk['local_bind']])
    bl = np.linalg.norm(bind, axis=1); tl = np.linalg.norm(T, axis=2)
    ok = bl > 1e-3
    drift = np.abs(tl[:, ok] - bl[ok]).max()
    P = np.array([model(Q[fr], T[fr], par) for fr in range(len(Q))])
    info = dict(name=name, kind=kind, frames=len(Q), fps=round(a['frame_rate'], 2), max_len_drift_m=round(float(drift), 4),
                animRot=len(a['animated_rot']), animTrans=len(a['animated_trans']))
    if kind == 'body':
        r = P[:, 1]
        info['root_xyz_min'] = np.round(r.min(0), 3).tolist(); info['root_xyz_max'] = np.round(r.max(0), 3).tolist()
        info['toe_min'] = np.round(np.minimum(P[:, 12], P[:, 17]).min(0), 3).tolist()
    print(info)
    if preview:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        pv = ROOT / 'preview'; pv.mkdir(exist_ok=True)
        idx = np.linspace(0, len(Q)-1, 4).astype(int)
        fig, axs = plt.subplots(1, 4, figsize=(14, 4))
        L = np.abs(P).max() * 1.05
        # body model space: up axis = whichever varies most for root vs feet; plot x vs the axis with largest extent
        up = 2 if kind == 'body' else int(np.argmax(np.ptp(P.reshape(-1, 3), axis=0)))
        h = [i for i in range(3) if i != up][0]
        for ax, fr in zip(axs, idx):
            for j, p in enumerate(par):
                if p >= 0: ax.plot([P[fr, p, h], P[fr, j, h]], [P[fr, p, up], P[fr, j, up]], 'k-', lw=1)
            ax.set_aspect('equal'); ax.set_title('f%d' % fr)
            lim = (P[..., h].min(), P[..., h].max(), P[..., up].min(), P[..., up].max())
            ax.set_xlim(lim[0]-.1, lim[1]+.1); ax.set_ylim(lim[2]-.1, lim[3]+.1)
        fig.suptitle(name); fig.savefig(pv / (name + '.png'), dpi=60); plt.close(fig)


if __name__ == '__main__':
    for n in (sys.argv[1:] or DEFAULT): export(n)
