"""v1177 crop sheets: kept | variants ... | next for chosen dumps and a crop window (rows: dumps).

python tools/stale_warp_sheet.py <dump dir> <out.png> --dumps 3 4 --box x y w h [--scale 1.0] <variant>=<replay dir>[:<suffix>] ...
A variant path 'game' is the dumped -warped.rgba. The box is in image pixels of the dump's crop (its 'box' meta field).
"""
import argparse, json
import numpy as np
from PIL import Image


def main():
    a = argparse.ArgumentParser(); a.add_argument('dumps'); a.add_argument('out'); a.add_argument('variants', nargs='*')
    a.add_argument('--dumps', dest='ns', type=int, nargs='+'); a.add_argument('--box', type=int, nargs=4); a.add_argument('--scale', type=float, default=1.0)
    o = a.parse_args()
    rows = []
    for n in o.ns:
        d = f'{o.dumps}/stale-warp-{n}-'
        meta = json.load(open(d + 'meta.json')); bw, bh = meta['box'][2], meta['box'][3]
        read = lambda p: np.fromfile(p, np.uint8).reshape(bh, bw, 4)[..., :3]
        x, y, w, h = o.box
        panels = [read(d + 'kept.rgba')]
        for v in o.variants:
            name, path = v.split('=', 1); suffix = 'replay-on'
            if ':' in path[2:]: path, suffix = path.rsplit(':', 1)
            panels.append(read(d + 'warped.rgba') if path == 'game' else read(f'{path}/stale-warp-{n}-{suffix}.rgba'))
        panels.append(read(d + 'next.rgba'))
        rows.append(np.concatenate([p[y:y + h, x:x + w] for p in panels], 1))
    img = np.concatenate(rows, 0)
    Image.fromarray(img).resize((int(img.shape[1] * o.scale), int(img.shape[0] * o.scale)), Image.NEAREST).save(o.out)
    print('sheet:', o.out)


if __name__ == '__main__':
    main()
