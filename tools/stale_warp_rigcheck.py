"""v1190 sanity check of the engine's rig target: the fresh eye's rig texels (mask > 0), moved into the stale eye's target view by their own
fresh depth (exact for this tick: the same geometry the target was drawn from), must land on the target's rig silhouette with its depth.

python tools/stale_warp_rigcheck.py <dump dir> [--dumps N ...]

Per dump: texels compared, share of moved fresh rig texels that land on target rig (where the target is not hidden by fresh-visible scene
in between it is the same surface), the median depth difference there, and the best integer shift (+-4 texels) that maximises the overlap
(a non-zero best shift = a registration offset of the engine side).
"""
import argparse, json, os
import numpy as np


def load_rt(path, w, h):
    raw = np.fromfile(path, np.uint8)
    if raw.size == w * h * 8:
        return raw.view(np.float32).reshape(h, w, 2)
    hdr = raw[:16].view(np.uint32); rw, rh, fmt, row = int(hdr[0]), int(hdr[1]), int(hdr[2]), int(hdr[3])
    body = raw[16:16 + rh * row].reshape(rh, row)[:, :rw * 8]
    return body.copy().view(np.float32).reshape(rh, rw, 2)


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('--dumps', dest='ns', type=int, nargs='*')
    o = a.parse_args()
    for n in o.ns or range(1, 33):
        d = f'{o.dumps}/stale-warp-{n}-'
        if not os.path.exists(d + 'target-rig.rg32f'): continue
        m = json.load(open(d + 'meta.json'))
        kx, ky, kw, kh = m['kept_depth_box']; fx, fy, fw, fh = m['fresh_depth_box']
        rw, rhh = m['kept_depth_region']
        rt = load_rt(d + 'target-rig.rg32f', kw, kh)
        fd = np.fromfile(d + 'fresh-depth.r32', np.float32).reshape(fh, fw)
        fr = np.fromfile(d + 'fresh-rig.r8', np.uint8).reshape(fh, fw)
        f = m['fresh']; W = np.array(f['world']).reshape(3, 3); sh = np.array(f['shift'])
        back = np.linalg.inv(W); bsh = -back @ sh
        m10, m11 = f['m10'], f['m11']; t10, t11 = m['world']['m10'], m['world']['m11']
        ys, xs = np.nonzero((fr > 0) & (fd > 1e-7))
        D = m11 / (fd[ys, xs] + m10)
        u = (fx + xs + 0.5) / rw; v = (fy + ys + 0.5) / rhh
        h = D[None, :] * (back @ np.stack([u, v, np.ones_like(u)])) + bsh[:, None]
        tu, tv, tz = h[0] / h[2], h[1] / h[2], h[2]
        ix, iy = np.floor(tu * rw).astype(int) - kx, np.floor(tv * rhh).astype(int) - ky
        ok = (ix >= 0) & (iy >= 0) & (ix < kw) & (iy < kh) & (tz > 0)
        ix, iy, tz = ix[ok], iy[ok], tz[ok]
        has = rt[..., 0] > 1e-7
        tgt_z = np.where(has, t11 / (rt[..., 0] + t10), 0)
        land = has[iy, ix]
        dz = np.abs(tgt_z[iy, ix][land] - tz[land])
        best = (0, 0, -1)
        for sy in range(-4, 5):
            for sx in range(-4, 5):
                jx, jy = np.clip(ix + sx, 0, kw - 1), np.clip(iy + sy, 0, kh - 1)
                c = has[jy, jx].mean()
                if c > best[2]: best = (sx, sy, c)
        moved = np.zeros((kh, kw), bool); moved[iy, ix] = True
        print(f'dump {n}: fresh rig texels {ok.sum()}, on the target rig {land.mean()*100:.1f}%, |dz| median {np.median(dz)*1000 if dz.size else 0:.1f} mm '
              f'(p90 {np.percentile(dz, 90)*1000 if dz.size else 0:.1f}), target rig texels {has.sum()}, of them reached by fresh {moved[has].mean()*100:.1f}%, '
              f'best shift {best[0]:+d},{best[1]:+d} ({best[2]*100:.1f}%) | rig_target meta {m.get("rig_target")}', flush=True)


if __name__ == '__main__':
    main()
