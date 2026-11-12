"""v1197 hand check against truth (sim-pose-hold: -next.rgba is the stale eye at the target pose): per dump the whole-frame mean |R - truth|
and on the truth's own rig pixels (next-rig, else none), for the unwarped kept frame K and each variant.

python tools/stale_warp_handcheck.py <dump dir> <name>=<replay dir>[:<suffix>] ... [--dumps N ...]   (name game: the dumped -warped.rgba)
Exit 1 when a variant's rig error exceeds K's on any dump, or its frame error exceeds K's on a dump whose camera moved."""
import argparse, json, os, sys
import numpy as np


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('variants', nargs='+'); a.add_argument('--dumps', dest='ns', type=int, nargs='*')
    o = a.parse_args(); bad = []
    vs = []
    for v in o.variants:
        name, path = v.split('=', 1); suffix = 'replay-on'
        if ':' in path[2:]: path, suffix = path.rsplit(':', 1)
        vs.append((name, path, suffix))
    print('dump cam_mm hands_mm | K all rig | ' + ' | '.join(f'{v[0]} all rig' for v in vs))
    for n in o.ns or range(1, 33):
        d = f'{o.dumps}/stale-warp-{n}-'
        if not os.path.exists(d + 'meta.json'): continue
        m = json.load(open(d + 'meta.json')); W, H = m['image']; rw, rh = m['kept_depth_region']
        if not m.get('truth_next'): continue
        rd = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        t, k = rd(d + 'next.rgba'), rd(d + 'kept.rgba')
        rig = np.zeros((H, W), bool)
        if os.path.exists(d + 'next-rig.r8'):
            r = np.fromfile(d + 'next-rig.r8', np.uint8).reshape(rh, rw) > 0
            rig = r[np.arange(H) * rh // H][:, np.arange(W) * rw // W]
        e = lambda x: (np.abs(x - t).mean(-1).mean(), np.abs(x - t).mean(-1)[rig].mean() if rig.any() else 0.0)
        ek = e(k); moved = m['camera_step_m'] > 5e-4 or m['camera_turn_deg'] > 0.05
        cells = []
        for name, path, suffix in vs:
            p = d + 'warped.rgba' if name == 'game' else f'{path}/stale-warp-{n}-{suffix}.rgba'
            if not os.path.exists(p): cells.append('   -     -  '); continue
            er = e(rd(p)); flag = ''
            if rig.any() and er[1] > ek[1] + 0.05: flag += 'R'
            if moved and er[0] >= ek[0]: flag += 'F'
            if flag and name != 'game' and name != 'old': bad.append((n, name, flag))
            cells.append(f'{er[0]:5.2f} {er[1]:6.2f}{flag:2}')
        print(f'{n:4d} {m["camera_step_m"] * 1e3:6.1f} {m["hands_moved_m"] * 1e3:6.1f} | {ek[0]:5.2f} {ek[1]:6.2f} | ' + ' | '.join(cells), flush=True)
    print('failures (R: rig worse than K, F: frame not better than K on a camera move):', bad or 'none')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
