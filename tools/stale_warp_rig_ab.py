"""Rig-mask A/B on full-frame dumps with live masks (v1163+): F and B4 of two replay variants per region.

python tools/stale_warp_rig_ab.py <dump dir> <replay dir> <variant A> <variant B> [--dumps 1 2 ...] [--diff]

Regions (from the kept frame): rig = the kept rig mask; near_palm = not rig, its kept point within 25 cm of a drawn palm (what v811's
sphere took: a held glass, the table, the armchair); rest. F = (|R-K|+|R-X|-|X-K|)/2, B4 = max over K, X of R's best match within
+-4 px. --diff also prints where A and B differ (pixels per region) and, with -exits.rgba of the replay dir, which WarpPS return painted them.
"""
import argparse, json
import numpy as np


def best_match(r, t):
    out = np.full(r.shape[:2], 1e9, np.float32)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            out = np.minimum(out, np.abs(r - np.roll(t, (dy, dx), (0, 1))).mean(-1))
    return out


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('replays'); a.add_argument('A'); a.add_argument('B')
    a.add_argument('--dumps', dest='ns', type=int, nargs='*'); a.add_argument('--diff', action='store_true')
    o = a.parse_args()
    for n in o.ns or range(1, 13):
        d = f'{o.dumps}/stale-warp-{n}-'
        try: meta = json.load(open(d + 'meta.json'))
        except FileNotFoundError: continue
        W, H = meta['image']; rw, rh = meta['kept_depth_region']
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        try: k, x = read(d + 'kept.rgba'), read(d + 'next.rgba')
        except FileNotFoundError: continue
        ys, xs = np.arange(H) * rh // H, np.arange(W) * rw // W
        rig = (np.fromfile(d + 'kept-rig.r8', np.uint8).reshape(rh, rw) > 0)[ys][:, xs]
        depth = np.fromfile(d + 'kept-depth.r32', np.float32).reshape(rh, rw)[ys][:, xs]
        ahead = np.where(depth > 1e-7, meta['world']['m11'] / (depth + meta['world']['m10']), 0)
        L, R, U, D = np.tan(meta['kept_fov'])
        u, v = (np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H
        px = (L + (R - L) * u)[None, :] * ahead; py = (U + (D - U) * v)[:, None] * ahead
        near = np.zeros((H, W), bool)
        for palm in meta['palms']:
            if palm[3] > 0: near |= np.sqrt((px - palm[0]) ** 2 + (py - palm[1]) ** 2 + (ahead - palm[2]) ** 2) < 0.25
        near &= ~rig & (ahead > 0)
        regions = {'rig': rig, 'near_palm': near, 'rest': ~rig & ~near}
        xk = np.abs(x - k).mean(-1)
        out = {}
        for name in (o.A, o.B):
            r = read(f'{o.replays}/stale-warp-{n}-{name}.rgba')
            out[name] = (r, (np.abs(r - k).mean(-1) + np.abs(r - x).mean(-1) - xk) / 2, np.maximum(best_match(r, k), best_match(r, x)))
        line = f'dump {n}:'
        for g, m in regions.items():
            line += f' | {g} {m.mean()*100:4.1f}%: ' + ' '.join(f'{nm} {out[nm][1][m].mean():.3f}/{out[nm][2][m].mean():.3f}' for nm in (o.A, o.B))
        print(line, flush=True)
        if o.diff:
            dif = np.abs(out[o.A][0] - out[o.B][0]).max(-1) > 2
            msg = '   differ (>2 levels): ' + ', '.join(f'{g} {int((dif & m).sum())}' for g, m in regions.items())
            try:
                ex = np.round(np.fromfile(f'{o.replays}/stale-warp-{n}-exits.rgba', np.uint8).reshape(H, W, 4)[..., 0] / 10).astype(int)
                uq, c = np.unique(ex[dif], return_counts=True); msg += ' | exits (B) ' + str(dict(zip(uq.tolist(), c.tolist())))
            except FileNotFoundError: pass
            if dif.any():
                yy, xx = np.nonzero(dif); msg += f' | bbox x {xx.min()}-{xx.max()} y {yy.min()}-{yy.max()}'
            print(msg, flush=True)


if __name__ == '__main__':
    main()
