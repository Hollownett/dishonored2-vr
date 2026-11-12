"""v1192 verdict against ground truth: dumps with -truth.rgba, or meta truth_next (v1192 sim-pose-hold: -next.rgba is the stale eye
rendered at exactly the warp's target pose).

python tools/stale_warp_truth.py <dump dir> <variant>=<replay dir>[:<suffix>] ... [--dumps N ...] [--sheets <dir>] [--tiles 4]

Variants: 'game' = the dumped -warped.rgba; otherwise a replay output (suffix replay-on = AFW2, replay-off = the old warp).
Per dump and variant |R - truth| per region (disjoint, first wins): border (right/bottom 48 px), held (rig target class 253), rig (class 255),
vacated (kept rig minus target rig: where a hand or held item left), edge (non-rig within 6 px of a kept depth step > 15 %), near (< 1.5 m),
far. ghost = share of pixels where the kept frame differs from the truth (> 16 levels) and R sits closer to the unwarped kept frame than to
the truth by 8+ levels (something left behind). --sheets: kept | variants | truth at the worst tiles of the last variant.
"""
import argparse, json, os
import numpy as np
from scipy.ndimage import binary_dilation, maximum_filter, minimum_filter


def regions(meta, d, H, W):
    rw, rh = meta['kept_depth_region']
    ys, xs = np.arange(H) * rh // H, np.arange(W) * rw // W
    depth = np.fromfile(d + 'kept-depth.r32', np.float32).reshape(rh, rw)
    ahead = np.where(depth > 1e-7, meta['world']['m11'] / (depth + meta['world']['m10']), 1e4)
    step = maximum_filter(ahead, 3) > 1.15 * minimum_filter(ahead, 3)
    kr = np.fromfile(d + 'kept-rig.r8', np.uint8).reshape(rh, rw) > 0 if os.path.exists(d + 'kept-rig.r8') else np.zeros((rh, rw), bool)
    if os.path.exists(d + 'target-rig.rg32f'):
        rt = np.fromfile(d + 'target-rig.rg32f', np.float32).reshape(rh, rw, 2)
        has = rt[..., 0] > 1e-7; held = has & (rt[..., 1] == 253); rig = has & (rt[..., 1] == 255)
    else:
        held = np.zeros((rh, rw), bool); rig = kr.copy()
    up = lambda m: m[ys][:, xs]
    held, rig, vac, step, ahead, kr = up(held), up(rig), up(kr & ~(held | rig)), up(step), up(ahead), up(kr)
    edge = binary_dilation(step, iterations=6) & ~(rig | held | kr)
    border = np.zeros((H, W), bool); border[-48:] = True; border[:, -48:] = True
    out, taken = {}, np.zeros((H, W), bool)
    for name, m in (('border', border), ('held', held), ('rig', rig), ('vacated', vac), ('edge', edge), ('near', ahead < 1.5), ('far', np.ones((H, W), bool))):
        out[name] = m & ~taken; taken |= m
    return out


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('variants', nargs='+')
    a.add_argument('--dumps', dest='ns', type=int, nargs='*'); a.add_argument('--sheets'); a.add_argument('--tiles', type=int, default=4)
    o = a.parse_args()
    variants = []
    for v in o.variants:
        name, path = v.split('=', 1); suffix = 'replay-on'
        if ':' in path[2:]: path, suffix = path.rsplit(':', 1)
        variants.append((name, path, suffix))
    names = ['border', 'held', 'rig', 'vacated', 'edge', 'near', 'far']
    totals = {v[0]: {g: [0.0, 0] for g in names + ['ghost', 'all']} for v in variants}
    print('dump  |T-K| all | ' + ' | '.join(f'{v[0]}: ' + ' '.join(n[:4] for n in names) + ' ghost%' for v in variants))
    for n in o.ns or range(1, 33):
        d = f'{o.dumps}/stale-warp-{n}-'
        if not os.path.exists(d + 'meta.json'): continue
        meta = json.load(open(d + 'meta.json')); W, H = meta['image']
        # v1192 sim-pose-hold: the dump's `next` is the stale eye rendered at exactly the warp's target pose (meta truth_next)
        truth = d + 'truth.rgba' if os.path.exists(d + 'truth.rgba') else d + 'next.rgba' if meta.get('truth_next') else None
        if not truth: continue
        read = lambda p: np.fromfile(p, np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32)
        k, t = read(d + 'kept.rgba'), read(truth)
        reg = regions(meta, d, H, W)
        tk = np.abs(t - k).mean(-1)
        cells, outs = [], []
        for name, path, suffix in variants:
            r = read(d + 'warped.rgba') if path == 'game' else read(f'{path}/stale-warp-{n}-{suffix}.rgba')
            outs.append(r)
            e, ek = np.abs(r - t).mean(-1), np.abs(r - k).mean(-1)
            ghost = (tk > 16) & (ek + 8 < e)
            vals = []
            for g in names:
                m = reg[g]
                if m.any(): totals[name][g][0] += e[m].sum(); totals[name][g][1] += m.sum(); vals.append(f'{e[m].mean():5.2f}')
                else: vals.append('  -  ')
            totals[name]['ghost'][0] += ghost.sum(); totals[name]['ghost'][1] += (tk > 16).sum()
            totals[name]['all'][0] += e.sum(); totals[name]['all'][1] += e.size
            cells.append(' '.join(vals) + f' {ghost.sum() / max((tk > 16).sum(), 1) * 100:5.1f}')
        print(f'{n:4d}  {tk.mean():6.2f}   | ' + ' | '.join(cells), flush=True)
        if o.sheets:
            from PIL import Image
            os.makedirs(o.sheets, exist_ok=True)
            e = np.abs(outs[-1] - t).mean(-1); T = 64; th, tw = H // T, W // T
            tiles = e[:th * T, :tw * T].reshape(th, T, tw, T).mean((1, 3))
            rows = []
            for idx in np.argsort(-tiles, axis=None)[:o.tiles]:
                ty, tx = int(idx // tw), int(idx % tw); S = 192
                cy, cx = min(max(ty * T + T // 2 - S // 2, 0), H - S), min(max(tx * T + T // 2 - S // 2, 0), W - S)
                rows.append(np.concatenate([p[cy:cy + S, cx:cx + S] for p in [k] + outs + [t]], 1))
            img = np.concatenate(rows, 0).clip(0, 255).astype(np.uint8)
            Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(f'{o.sheets}/truth-{n}.png')
    print('mean  ' + ' | '.join(f'{v}: ' + ' '.join(f'{totals[v][g][0] / max(totals[v][g][1], 1):5.2f}' for g in names) +
                                 f' {totals[v]["ghost"][0] / max(totals[v]["ghost"][1], 1) * 100:5.1f} (all {totals[v]["all"][0] / max(totals[v]["all"][1], 1):.2f})' for v in totals))


if __name__ == '__main__':
    main()
