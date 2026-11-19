"""Spacewarp in-game captures (`spacewarp capture N`, game_xr.hpp): per triple sw-capture-NN-{real,generated,next}.rgba of eye 0 (the real
image, the frame generated from it for the next display slot, the real image one slot after that), scored and drawn.

    python tools/spacewarp_capture_sheet.py <dir> <out dir>

Per triple, mean absolute difference in 8-bit levels: |G-R| and |G-X| (G generated, R real before it, X real after it), |X-R| (how much the
two real frames differ), and flicker F = mean((|G-R| + |G-X| - |X-R|) / 2): 0 where G lies between R and X, larger where it shows
something neither real frame has (a tear, a ghost, a smear). Showing R again (no spacewarp) would score |G-X| = |X-R|. The sheet
(<out>/sw-capture-NN.png) shows R | G | X at the 4 worst 64 px tiles of F, outside a 64 px edge band.
"""
import json, os, sys
import numpy as np
from PIL import Image


def load(stem):
    meta = json.load(open(stem + '.json'))
    w, h, fmt = meta['width'], meta['height'], meta['format']
    img = np.fromfile(stem + '.rgba', np.uint8).reshape(h, w, 4)[..., :3].astype(np.float32)
    if fmt in (87, 88, 90, 91):  # B8G8R8A8 / B8G8R8X8 families: to RGB
        img = img[..., ::-1]
    return img


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    print('triple  |G-R|  |G-X|  |X-R|   flicker')
    n = 0
    while os.path.exists(f'{src}/sw-capture-{n:02d}-next.json'):
        stem = f'{src}/sw-capture-{n:02d}-'
        R, G, X = load(stem + 'real'), load(stem + 'generated'), load(stem + 'next')
        gr, gx, xr = np.abs(G - R).mean(-1), np.abs(G - X).mean(-1), np.abs(X - R).mean(-1)
        f = (gr + gx - xr) / 2
        print(f'{n:6d}  {gr.mean():5.2f}  {gx.mean():5.2f}  {xr.mean():5.2f}   {f.mean():5.2f}')
        H, W = f.shape; T = 64; th, tw = H // T, W // T
        tiles = f[:th * T, :tw * T].reshape(th, T, tw, T).mean((1, 3))
        tiles[0, :] = tiles[-1, :] = tiles[:, 0] = tiles[:, -1] = -1
        rows = []
        for idx in np.argsort(-tiles, axis=None)[:4]:
            ty, tx = int(idx // tw), int(idx % tw); S = 192
            cy, cx = min(max(ty * T + T // 2 - S // 2, 0), H - S), min(max(tx * T + T // 2 - S // 2, 0), W - S)
            rows.append(np.concatenate([p[cy:cy + S, cx:cx + S] for p in (R, G, X)], 1))
        Image.fromarray(np.concatenate(rows, 0).clip(0, 255).astype(np.uint8)).save(f'{out}/sw-capture-{n:02d}.png')
        n += 1
    if not n:
        print('no complete triples')


if __name__ == '__main__':
    main()
