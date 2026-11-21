"""Same-frame A/B of spacewarp captures (`spacewarp capture N` after an alt command such as `spacewarp altbody2`, game_xr.hpp): per triple
sw-capture-NN-{real,generated,generated-alt,next}.rgba of eye 0. G is the generated frame that was submitted, A the same frame drawn with the
other rule, R the real image both came from, X the next real image (one slot after G).

Scored only around where the two rules draw different pixels (max channel |G - A| > --diff levels, grown by --grow px), per triple and in
total:
  share     the image share where the rules differ (%)
  |.-X|     mean absolute difference from the next real image (8-bit levels)
  flicker   mean((|.-R| + |.-X| - |X-R|) / 2): 0 where the frame lies between the two real images, larger where it shows something
            neither has (a tear, a gap through a leg, a smear)
  progress  over the pixels there that change between R and X: 0.5 * (1 + (mean|.-R| - mean|.-X|) / mean|X-R|). 0 repeats R (the 45 Hz
            judder that reads as a double image when the eye follows), 0.5 halfway (the slot between them), 1 shows X early
Sheets <out>/ab-NN.png: R | A | G | X at the 3 tiles where the rules differ most (outside a 64 px band).

    python tools/spacewarp_capture_ab.py <capture dir> <out dir> [--labels new,old] [--diff 8] [--grow 48] [--tile 160]
"""
import argparse, json, os
import numpy as np
from PIL import Image


def load(stem):
    meta = json.load(open(stem + '.json'))
    w, h, fmt = meta['width'], meta['height'], meta['format']
    img = np.fromfile(stem + '.rgba', np.uint8).reshape(h, w, 4)[..., :3].astype(np.float32)
    if fmt in (87, 88, 90, 91):  # B8G8R8A8 / B8G8R8X8 families: to RGB
        img = img[..., ::-1]
    return img


def grow(mask, r):
    """mask dilated by a (2r+1) square (integral image)"""
    if r <= 0:
        return mask
    c = np.pad(mask.astype(np.int32), ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    h, w = mask.shape
    y0, y1 = np.clip(np.arange(h) - r, 0, h), np.clip(np.arange(h) + r + 1, 0, h)
    x0, x1 = np.clip(np.arange(w) - r, 0, w), np.clip(np.arange(w) + r + 1, 0, w)
    s = c[y1][:, x1] - c[y0][:, x1] - c[y1][:, x0] + c[y0][:, x0]
    return s > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src');ap.add_argument('out')
    ap.add_argument('--labels', default='new,old', help='names of G (submitted) and A (generated-alt)')
    ap.add_argument('--diff', type=float, default=8, help='levels: where max channel |G - A| exceeds this the rules differ')
    ap.add_argument('--grow', type=int, default=48, help='px: the scored region around it (a body part moves up to ~45 px a step)')
    ap.add_argument('--tile', type=int, default=160)
    a = ap.parse_args()
    lg, la = a.labels.split(',')
    os.makedirs(a.out, exist_ok=True)
    print(f'triple  share%   |{lg}-X| |{la}-X|   flicker {lg}/{la}   progress {lg}/{la}')
    acc = {k: 0.0 for k in ('gx', 'ax', 'fg', 'fa', 'px', 'mgr', 'mgx', 'mar', 'max', 'mxr')}
    count, wins = 0, {lg: 0, la: 0}
    n = 0
    while os.path.exists(f'{a.src}/sw-capture-{n:02d}-next.json'):
        stem = f'{a.src}/sw-capture-{n:02d}-'
        if not os.path.exists(stem + 'generated-alt.json'):
            print(f'{n:6d}  no generated-alt');n += 1;continue
        R, G, A, X = (load(stem + k) for k in ('real', 'generated', 'generated-alt', 'next'))
        h, w, _ = G.shape
        band = np.zeros((h, w), bool);band[64:h - 64, 64:w - 64] = True
        rules = (np.abs(G - A).max(axis=2) > a.diff) & band
        if not rules.any():
            print(f'{n:6d}  the rules draw the same frame');n += 1;continue
        region = grow(rules, a.grow) & band
        moving = region & (np.abs(X - R).max(axis=2) > a.diff)
        gx, ax, xr = np.abs(G - X).mean(axis=2), np.abs(A - X).mean(axis=2), np.abs(X - R).mean(axis=2)
        gr, ar = np.abs(G - R).mean(axis=2), np.abs(A - R).mean(axis=2)
        fg, fa = (gr + gx - xr) / 2, (ar + ax - xr) / 2
        px, mv = int(region.sum()), int(moving.sum())
        s = {k: float(v[region].mean()) for k, v in (('gx', gx), ('ax', ax), ('fg', fg), ('fa', fa))}
        if mv:
            m = {k: float(v[moving].mean()) for k, v in (('mgr', gr), ('mgx', gx), ('mar', ar), ('max', ax), ('mxr', xr))}
            pg, pa = (.5 * (1 + (m['mgr'] - m['mgx']) / m['mxr']), .5 * (1 + (m['mar'] - m['max']) / m['mxr'])) if m['mxr'] > 0 else (float('nan'),) * 2
            for k in m:
                acc[k] += m[k] * mv
        else:
            pg = pa = float('nan')
        if abs(s['fg'] - s['fa']) > .01:
            wins[lg if s['fg'] < s['fa'] else la] += 1
        count += 1
        print(f'{n:6d}  {100 * rules.sum() / rules.size:6.2f}  {s["gx"]:7.2f} {s["ax"]:7.2f}   {s["fg"]:6.2f}/{s["fa"]:6.2f}      {pg:5.2f}/{pa:5.2f}')
        for k in ('gx', 'ax', 'fg', 'fa'):
            acc[k] += s[k] * px
        acc['px'] += px
        t = a.tile
        score = np.abs(G - A).max(axis=2) * rules
        tiles = []
        for y in range(64, h - 64 - t + 1, t // 2):
            for x in range(64, w - 64 - t + 1, t // 2):
                tiles.append((float(score[y:y + t, x:x + t].sum()), y, x))
        tiles.sort(reverse=True)
        chosen = []
        for sc, y, x in tiles:
            if sc <= 0 or len(chosen) == 3:
                break
            if all(abs(y - cy) >= t or abs(x - cx) >= t for _, cy, cx in chosen):
                chosen.append((sc, y, x))
        rows = [np.concatenate([img[y:y + t, x:x + t] for img in (R, A, G, X)], axis=1) for _, y, x in chosen]
        if rows:
            sheet = np.concatenate(rows, axis=0).clip(0, 255).astype(np.uint8)
            Image.fromarray(sheet).resize((sheet.shape[1] * 2, sheet.shape[0] * 2), Image.NEAREST).save(f'{a.out}/ab-{n:02d}.png')
        n += 1
    if acc['px']:
        p = acc['px']
        line = (f'total ({count} triples, around where the rules differ): |{lg}-X| {acc["gx"] / p:.2f} |{la}-X| {acc["ax"] / p:.2f}; '
                f'flicker {lg} {acc["fg"] / p:.2f} / {la} {acc["fa"] / p:.2f} (lower per triple: {lg} {wins[lg]}, {la} {wins[la]})')
        if acc['mxr'] > 0:
            line += (f'; progress {lg} {.5 * (1 + (acc["mgr"] - acc["mgx"]) / acc["mxr"]):.2f} / {la} '
                     f'{.5 * (1 + (acc["mar"] - acc["max"]) / acc["mxr"]):.2f} (0.5 = halfway)')
        print(line)
    print(f'sheets: R | {la} | {lg} | X per row')


if __name__ == '__main__':
    main()
