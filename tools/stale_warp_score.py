"""v1177 region scores: mean absolute RGB difference of a rebuilt frame R to the eye's NEXT own frame X and to its KEPT frame K.

python tools/stale_warp_score.py <dump dir> <variant>=<replay dir>[:<suffix>] ... --dumps N ... [--tiles 6] [--sheet <out.png> --dump N]

A correct rebuilt frame lies between K and X (|R-X| small without R = K: compare |R-K| too). Regions (disjoint, first wins):
border (right/bottom 48 px: the eye images' bright band and its rebuild), rig (kept rig mask, 2 px grown), edge (non-rig within 6 px of a
kept depth step > 15 %), near (non-rig, kept depth < 1.5 m), far (the rest, sky included). "near*" = near non-rig over the whole frame
(the handover's baseline numbers: edge pixels included, border included).
--tiles: the worst 64 px tiles of the last variant by |R-X| excess over the first. --sheet: kept | first | last | next crops of those tiles.
"""
import argparse, json
import numpy as np
from scipy.ndimage import binary_dilation, maximum_filter, minimum_filter


def load_regions(meta, d, H, W):
    rw, rh = meta['kept_depth_region']
    ys, xs = np.arange(H) * rh // H, np.arange(W) * rw // W
    try: rig = (np.fromfile(d + 'kept-rig.r8', np.uint8).reshape(rh, rw) > 0)
    except FileNotFoundError: rig = np.zeros((rh, rw), bool)
    depth = np.fromfile(d + 'kept-depth.r32', np.float32).reshape(rh, rw)
    ahead = np.where(depth > 1e-7, meta['world']['m11'] / (depth + meta['world']['m10']), 1e4)
    step = maximum_filter(ahead, 3) > 1.15 * minimum_filter(ahead, 3)
    rig, ahead, step = rig[ys][:, xs], ahead[ys][:, xs], step[ys][:, xs]
    rig = binary_dilation(rig, iterations=2)
    edge = binary_dilation(step, iterations=6) & ~rig
    border = np.zeros((H, W), bool); border[-48:] = True; border[:, -48:] = True
    near = (ahead < 1.5) & ~rig
    out, taken = {}, np.zeros((H, W), bool)
    for name, m in (('border', border), ('rig', rig), ('edge', edge), ('near', near), ('far', np.ones((H, W), bool))):
        out[name] = m & ~taken; taken |= m
    out['near*'] = near
    return out


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('variants', nargs='+')
    a.add_argument('--dumps', dest='ns', type=int, nargs='*'); a.add_argument('--tiles', type=int, default=0)
    a.add_argument('--sheet'); a.add_argument('--dump', type=int)
    o = a.parse_args()
    variants = []
    for v in o.variants:
        name, path = v.split('=', 1); suffix = 'replay-on'
        if ':' in path[2:]: path, suffix = path.rsplit(':', 1)
        variants.append((name, path, suffix))
    for n in o.ns or range(1, 13):
        d = f'{o.dumps}/stale-warp-{n}-'
        try: meta = json.load(open(d + 'meta.json'))
        except FileNotFoundError: continue
        W, H = meta['image']
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        k, x = read(d + 'kept.rgba'), read(d + 'next.rgba')
        reg = load_regions(meta, d, H, W)
        print(f'dump {n}: |X-K| ' + ' '.join(f'{g} {np.abs(x - k).mean(-1)[m].mean():.2f} ({m.mean()*100:.1f}%)' for g, m in reg.items()))
        rs = {}
        for name, path, suffix in variants:
            r = read(d + 'warped.rgba') if path == 'game' else read(f'{path}/stale-warp-{n}-{suffix}.rgba')
            rs[name] = r
            ex, ek = np.abs(r - x).mean(-1), np.abs(r - k).mean(-1)
            print(f'   {name:>8} |R-X|/|R-K| ' + ' '.join(f'{g} {ex[m].mean():.2f}/{ek[m].mean():.2f}' for g, m in reg.items()), flush=True)
        if o.tiles and len(rs) >= 2:
            names = list(rs); first, last = rs[names[0]], rs[names[-1]]
            e0, e1 = np.abs(first - x).mean(-1), np.abs(last - x).mean(-1)
            T = 64; th, tw = H // T, W // T
            t0 = e0[:th * T, :tw * T].reshape(th, T, tw, T).mean((1, 3)); t1 = e1[:th * T, :tw * T].reshape(th, T, tw, T).mean((1, 3))
            order = np.argsort(-(t1 - t0), axis=None)[:o.tiles]
            worst = [(int(i // tw), int(i % tw)) for i in order]
            print('   worst tiles (y, x px: ' + names[0] + ' -> ' + names[-1] + ' |R-X|): ' + ', '.join(f'({ty*T},{tx*T}: {t0[ty,tx]:.1f}->{t1[ty,tx]:.1f})' for ty, tx in worst))
            best = np.argsort((t1 - t0), axis=None)[:o.tiles]
            print('   best tiles: ' + ', '.join(f'({int(i//tw)*T},{int(i%tw)*T}: {t0.flat[i]:.1f}->{t1.flat[i]:.1f})' for i in best))
            if o.sheet and o.dump == n:
                from PIL import Image
                S = 192  # 3x3 tiles around each worst tile, enlarged 2x
                rows = []
                for ty, tx in worst[:3]:
                    cy, cx = min(max(ty * T + T // 2 - S // 2, 0), H - S), min(max(tx * T + T // 2 - S // 2, 0), W - S)
                    rows.append(np.concatenate([p[cy:cy + S, cx:cx + S] for p in (k, first, last, x)], 1))
                img = np.concatenate(rows, 0).clip(0, 255).astype(np.uint8)
                Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(o.sheet)
                print('   sheet:', o.sheet)


if __name__ == '__main__':
    main()
