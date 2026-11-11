"""Defect census of stale_warp_replay outputs on full-frame dumps with rig masks (v1165+).

python tools/stale_warp_defects.py <dump dir> <replay dir> <variant> [<variant> ...] [--dumps N ...] [--tiles 8] [--exits <name>]

Regions (disjoint, first match wins): border (64 px), edge_in (rig within 24 px of non-rig), edge_out (non-rig within 24 px of rig), rig,
near_palm (non-rig, kept point within 25 cm of a drawn palm), rest. Per variant F = (|R-K|+|R-X|-|X-K|)/2 and B4 = max over K, X of R's
best match within +-4 px; "white" counts pixels brighter than 245 where kept and next are both below 200. --tiles ranks the worst 64 px
tiles of the first variant by B4 excess over the second variant's (or by B4 alone with one variant) and names the exit (-<exits>.rgba,
red/10 = WarpPS return in source order) that painted most of each.
"""
import argparse, json
import numpy as np
from scipy.ndimage import binary_dilation, binary_erosion


def best_match(r, t):
    out = np.full(r.shape[:2], 1e9, np.float32)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            out = np.minimum(out, np.abs(r - np.roll(t, (dy, dx), (0, 1))).mean(-1))
    return out


def regions(meta, d, H, W):
    rw, rh = meta['kept_depth_region']
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
    border = np.zeros((H, W), bool); border[:64] = border[-64:] = True; border[:, :64] = border[:, -64:] = True
    s = np.ones((3, 3), bool)
    grow = binary_dilation(rig, s, iterations=24); shrink = binary_erosion(rig, s, iterations=24)
    out, taken = {}, np.zeros((H, W), bool)
    for name, m in (('border', border), ('edge_in', rig & ~shrink), ('edge_out', grow & ~rig), ('rig', rig), ('near_palm', near & ~rig), ('rest', np.ones((H, W), bool))):
        out[name] = m & ~taken; taken |= m
    return out


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('replays'); a.add_argument('variants', nargs='+')
    a.add_argument('--dumps', dest='ns', type=int, nargs='*'); a.add_argument('--tiles', type=int, default=0); a.add_argument('--exits', default='exits-before')
    o = a.parse_args()
    for n in o.ns or range(1, 13):
        d = f'{o.dumps}/stale-warp-{n}-'
        try: meta = json.load(open(d + 'meta.json'))
        except FileNotFoundError: continue
        W, H = meta['image']
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        try: k, x = read(d + 'kept.rgba'), read(d + 'next.rgba')
        except FileNotFoundError: continue
        reg = regions(meta, d, H, W)
        xk = np.abs(x - k).mean(-1)
        print(f'dump {n} (eye {meta["eye"]}, hands {meta["hands_moved_m"]*1000:.1f} mm): ' + ' '.join(f'{g} {m.mean()*100:.1f}%' for g, m in reg.items()), flush=True)
        b4s = {}
        for v in o.variants:
            r = read(d + 'warped.rgba') if v == 'game' else read(f'{o.replays}/stale-warp-{n}-{v}.rgba')
            f = (np.abs(r - k).mean(-1) + np.abs(r - x).mean(-1) - xk) / 2
            b = best_match(r, k); b = np.maximum(b, best_match(r, x)); b4s[v] = b
            white = (r.min(-1) > 245) & (k.max(-1) < 200) & (x.max(-1) < 200)
            print(f'   {v:12s} ' + ' '.join(f'{g} {f[m].mean():.3f}/{b[m].mean():.3f}' for g, m in reg.items() if m.any()) + f' | white {int(white.sum())}', flush=True)
        if o.tiles:
            v0 = o.variants[0]
            T = 64; th, tw = H // T, W // T
            try: ex = np.round(np.fromfile(f'{o.replays}/stale-warp-{n}-{o.exits}.rgba', np.uint8).reshape(H, W, 4)[..., 0] / 10).astype(int)
            except FileNotFoundError: ex = None
            for label, e in [('worst', b4s[v0])] + ([(f'worse than {o.variants[1]}', b4s[v0] - b4s[o.variants[1]])] if len(o.variants) > 1 else []):
              score = e[:th * T, :tw * T].reshape(th, T, tw, T).mean((1, 3))
              print(f'    {label}:')
              for idx in np.argsort(-score.ravel())[:o.tiles]:
                  ty, tx = divmod(idx, tw); sl = (slice(ty * T, ty * T + T), slice(tx * T, tx * T + T))
                  comp = max(reg, key=lambda g: reg[g][sl].sum())
                  msg = f'      tile x {tx*T} y {ty*T}: {score[ty, tx]:+.2f} ({comp})'
                  if ex is not None:
                      uq, c = np.unique(ex[sl], return_counts=True); msg += ' exits ' + ','.join(f'{u}:{cc}' for u, cc in sorted(zip(uq, c), key=lambda t: -t[1])[:3])
                  print(msg, flush=True)


if __name__ == '__main__':
    main()
