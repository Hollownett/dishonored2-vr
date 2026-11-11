"""Region scores of stale_warp_replay outputs on full-frame dumps (v1160+): F and B4 over the whole frame, the near field (kept depth
< 1.5 m: the player's arms, body, what the hands hold, the table they reach) and a 64 px border band.

python tools/stale_warp_regions.py <dump dir> <replay dir> <variant> [<variant> ...] [--dumps 1 2 ...]

A variant is a file suffix of the replay dir: stale-warp-N-<variant>.rgba (e.g. before, replay-on); "game" scores the dumped -warped.rgba.
F = (|R-K| + |R-X| - |X-K|)/2, B4 = max over K, X of R's best match within +-4 px (mean absolute RGB levels). Lower is better.
"""
import argparse, json
import numpy as np


def b4(r, k, x):
    def best(t):
        out = np.full(r.shape[:2], 1e9, np.float32)
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                out = np.minimum(out, np.abs(r - np.roll(t, (dy, dx), (0, 1))).mean(-1))
        return out
    return np.maximum(best(k), best(x))


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('replays'); a.add_argument('variants', nargs='+'); a.add_argument('--dumps', dest='ns', type=int, nargs='*')
    o = a.parse_args()
    for n in o.ns or range(1, 13):
        d = f'{o.dumps}/stale-warp-{n}-'
        try: meta = json.load(open(d + 'meta.json'))
        except FileNotFoundError: continue
        W, H = meta['image']; bx = meta['box']
        if bx != [0, 0, W, H]: continue  # full-frame dumps only
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        try: k, x = read(d + 'kept.rgba'), read(d + 'next.rgba')
        except FileNotFoundError: continue
        rw, rh = meta['kept_depth_region']; depth = np.fromfile(d + 'kept-depth.r32', np.float32).reshape(rh, rw)
        m10, m11 = meta['world']['m10'], meta['world']['m11']
        ahead = np.where(depth > 1e-7, m11 / (depth + m10), 0)
        ys, xs = (np.arange(H) * rh // H), (np.arange(W) * rw // W)
        near = (ahead[ys][:, xs] > 0) & (ahead[ys][:, xs] < 1.5)
        band = np.zeros((H, W), bool); band[:64] = band[-64:] = True; band[:, :64] = band[:, -64:] = True
        xk = np.abs(x - k).mean(-1)
        line = f'dump {n}: near {near.mean()*100:4.1f}%'
        for v in o.variants:
            r = read(d + 'warped.rgba') if v == 'game' else read(f'{o.replays}/stale-warp-{n}-{v}.rgba')
            f = (np.abs(r - k).mean(-1) + np.abs(r - x).mean(-1) - xk) / 2
            b = b4(r, k, x)
            line += f' | {v}: F/B4 whole {f.mean():.3f}/{b.mean():.3f} near {f[near].mean():.3f}/{b[near].mean():.3f} border {f[band].mean():.3f}/{b[band].mean():.3f}'
        print(line, flush=True)


if __name__ == '__main__':
    main()
