"""v1190 rig-target scores: inside the target's rig silhouette and in the place the hand left (kept rig, not target rig), |R-next| and
|R-kept| per variant, and the share of rig pixels per source (-rigsrc.rgba of the first variant: 1 kept, 2 fresh, 3 kept neighbour,
4 kept hidden from both, 5 fresh neighbour, 6 fresh unmasked).

python tools/stale_warp_rigscore.py <dump dir> <variant>=<replay dir>[:<suffix>] ... --dumps N ...
"""
import argparse, json
import numpy as np


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('variants', nargs='+'); a.add_argument('--dumps', dest='ns', type=int, nargs='+')
    o = a.parse_args()
    variants = []
    for v in o.variants:
        name, path = v.split('=', 1); suffix = 'replay-on'
        if ':' in path[2:]: path, suffix = path.rsplit(':', 1)
        variants.append((name, path, suffix))
    print('dump  hands mm cam mm | rig px  ' + ' | '.join(f'{n}: rig X/K  left X/K' for n, _, _ in variants) + ' | sources kept/fresh/keptN/hidden/freshN/unmasked %')
    for n in o.ns:
        d = f'{o.dumps}/stale-warp-{n}-'
        m = json.load(open(d + 'meta.json')); W, H = m['image']; rw, rh = m['kept_depth_region']
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        k, x = read(d + 'kept.rgba'), read(d + 'next.rgba')
        rt = np.fromfile(d + 'target-rig.rg32f', np.float32).reshape(rh, rw, 2)
        kr = np.fromfile(d + 'kept-rig.r8', np.uint8).reshape(rh, rw)
        ys, xs = np.arange(H) * rh // H, np.arange(W) * rw // W
        rig = ((rt[..., 0] > 1e-7) & (rt[..., 1] > 0))[ys][:, xs]
        left = (kr > 0)[ys][:, xs] & ~rig
        cells = []
        for name, path, suffix in variants:
            r = read(d + 'warped.rgba') if path == 'game' else read(f'{path}/stale-warp-{n}-{suffix}.rgba')
            ex, ek = np.abs(r - x).mean(-1), np.abs(r - k).mean(-1)
            cells.append(f'{ex[rig].mean():5.2f}/{ek[rig].mean():5.2f} {ex[left].mean() if left.any() else 0:5.2f}/{ek[left].mean() if left.any() else 0:5.2f}')
        src = ''
        try:
            s = np.fromfile(f'{variants[0][1]}/stale-warp-{n}-rigsrc.rgba', np.uint8).reshape(H, W, 4)[..., 0]
            code = np.rint(s / 40.0).astype(int)  # sRGB-encoded red: decode approximately
            lin = (s / 255.0) ** 2.2 * 255 / 40  # the target is _SRGB: red was written linear code*40/255
            code = np.rint(lin).astype(int)
            share = [(code[rig] == c).mean() * 100 for c in range(1, 7)]
            src = ' '.join(f'{v:4.1f}' for v in share) + f' (none {(code[rig] == 0).mean()*100:.1f})'
        except FileNotFoundError:
            pass
        print(f'{n:4d}  {m.get("hands_moved_m", 0)*1000:6.1f} {m.get("camera_step_m", 0)*1000:5.1f} | {rig.mean()*100:5.1f}%  ' + ' | '.join(cells) + ' | ' + src, flush=True)


if __name__ == '__main__':
    main()
