"""Compare rebuilt-eye captures of two warps (e.g. the built-in warp and PureDark's AFW) with the capture sheet's metrics.

Each folder holds `afw-capture-NN-{rebuilt,native,held}.rgba` + `.json` (the mod's `afw capture N` command; v1414 also captures the
built-in warp). Per triple, against the eye's NEXT native frame (one alternation later, so even a perfect rebuild differs by one
frame of motion):
  mae          mean absolute error, whole eye (0-255, luma)
  hand_mae     the same in the hand box (lower left: x < 55 %, y > 52 %), where the first-person arm and weapon are
  hand_ghost   share of hand-box pixels off by more than 48 (visible ghosting / doubling / cracks)
and the same three for the HELD frame (the eye's previous native image: what that eye would show with no warp). Lower is better;
a warp is worth it where it beats `held`. Channel order is taken from each capture's DXGI format, so BGRA vs RGBA cannot pass for
warp error; non-8-bit formats are reported and skipped. Pillow only.

usage: python tools/afw_compare.py <folder A> [<folder B> ...] [--sheet out.png]
"""
import glob, json, os, re, sys
from PIL import Image, ImageChops

RGBA8, BGRA8 = {27, 28, 29}, {87, 90, 91}  # DXGI R8G8B8A8_TYPELESS/UNORM(_SRGB), B8G8R8A8_UNORM/TYPELESS/(_SRGB)


def load(path):
    meta = json.load(open(path[:-5] + '.json'))
    w, h, fmt = meta['width'], meta['height'], int(meta.get('format', 28))
    if fmt not in RGBA8 | BGRA8:
        raise ValueError(f'{os.path.basename(path)}: DXGI format {fmt} is not 8-bit RGBA/BGRA')
    img = Image.frombytes('RGBA', (w, h), open(path, 'rb').read()[: w * h * 4])
    if fmt in BGRA8:
        b, g, r, a = img.split(); img = Image.merge('RGBA', (r, g, b, a))
    return img.convert('RGB')


def metrics(a, b):
    w, h = a.size; hand = (0, int(h * .52), int(w * .55), h)
    d = ImageChops.difference(a, b).convert('L')
    def stat(img):
        hist = img.histogram(); n = sum(hist)
        return sum(i * c for i, c in enumerate(hist)) / n, sum(hist[49:]) / n
    whole, _ = stat(d); hm, hg = stat(d.crop(hand))
    return whole, hm, hg


def score(folder):
    rows = []
    for n in sorted({re.search(r'afw-capture-(\d+)-', p).group(1) for p in glob.glob(os.path.join(folder, 'afw-capture-*-rebuilt.rgba'))}):
        base = os.path.join(folder, f'afw-capture-{n}')
        try:
            native = load(base + '-native.rgba'); rebuilt = load(base + '-rebuilt.rgba')
        except (OSError, ValueError) as e:
            print('  skip', n, e); continue
        if rebuilt.size != native.size:
            print('  skip', n, 'size mismatch', rebuilt.size, native.size); continue
        row = dict(zip(('mae', 'hand_mae', 'hand_ghost'), metrics(rebuilt, native)))
        if os.path.exists(base + '-held.rgba'):
            row.update(zip(('held_mae', 'held_hand_mae', 'held_hand_ghost'), metrics(load(base + '-held.rgba'), native)))
        rows.append((n, row))
    return rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out = {}
    for folder in args:
        rows = score(folder); keys = ('mae', 'hand_mae', 'hand_ghost', 'held_mae', 'held_hand_mae', 'held_hand_ghost')
        mean = {k: sum(r[k] for _, r in rows if k in r) / max(1, sum(1 for _, r in rows if k in r)) for k in keys}
        out[folder] = {'triples': len(rows), 'mean': mean, 'rows': {n: r for n, r in rows}}
        print(f'{folder}: {len(rows)} triples')
        print('   rebuilt: mae %.2f  hand_mae %.2f  hand_ghost %.2f%%' % (mean['mae'], mean['hand_mae'], 100 * mean['hand_ghost']))
        print('   held   : mae %.2f  hand_mae %.2f  hand_ghost %.2f%%' % (mean['held_mae'], mean['held_hand_mae'], 100 * mean['held_hand_ghost']))
    json.dump(out, open('afw-compare.json', 'w'), indent=1)


if __name__ == '__main__':
    main()
