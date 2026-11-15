"""AFW capture sheet (like the Dishonored 1 AFW sheet): each rebuilt held eye | the same eye's next native frame | difference.

The mod's `afw capture N` command (or `--every "afw capture 2"` on vr_selftest.py) writes artifacts/afw-capture-NN-{rebuilt,native}.rgba.
The native frame is one alternation later, so a perfect rebuild still differs by one frame of motion; what matters is the excess,
concentrated on edges (ghosts, doubles, cracks). Metrics per pair: mean absolute error (0-255) for the whole eye and for the hand box
(lower left, where the first-person arm and weapon are), and the fraction of hand-box pixels off by more than 48 (visible ghosting).

usage: python tools/afw_capture_sheet.py [artifacts dir] [out.png] [--crop hand|full]
"""
import glob, json, os, re, sys
from PIL import Image, ImageChops, ImageDraw

art = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else 'artifacts'
out = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else os.path.join(art, 'afw-capture-sheet.png')
crop_mode = 'hand' if '--crop' in sys.argv and sys.argv[sys.argv.index('--crop') + 1] == 'hand' else 'full'

def load(path):
    meta = json.load(open(path[:-5] + '.json'))
    w, h = meta['width'], meta['height']
    return Image.frombytes('RGBA', (w, h), open(path, 'rb').read()[: w * h * 4]).convert('RGB')

def heat(diff):
    g = diff.convert('L').point(lambda v: min(255, v * 3))
    return Image.merge('RGB', (g, g.point(lambda v: v // 3), Image.new('L', g.size, 0)))

pairs = sorted({re.search(r'afw-capture-(\d+)-', p).group(1) for p in glob.glob(os.path.join(art, 'afw-capture-*-rebuilt.rgba'))})
if not pairs: sys.exit('no afw-capture-*.rgba in ' + art)
rows, report = [], []
for n in pairs:
    r, v = load(os.path.join(art, f'afw-capture-{n}-rebuilt.rgba')), load(os.path.join(art, f'afw-capture-{n}-native.rgba'))
    w, h = r.size
    hand = (0, int(h * .52), int(w * .55), h)
    d = ImageChops.difference(r, v)
    def mae(img, box=None):
        px = list((img.crop(box) if box else img).convert('L').tobytes()); return sum(px) / len(px), sum(1 for p in px if p > 48) / len(px)
    whole, _ = mae(d); hand_mae, hand_ghost = mae(d, hand)
    row = {'pair': n, 'mae': round(whole, 2), 'hand_mae': round(hand_mae, 2), 'hand_ghost_fraction': round(hand_ghost, 4)}
    held = os.path.join(art, f'afw-capture-{n}-held.rgba')
    if os.path.exists(held):  # the eye's previous native image: what the held eye shows without AFW (the baseline to beat)
        hd = ImageChops.difference(load(held), v); row['held_mae'], _ = mae(hd); row['held_hand_mae'], row['held_hand_ghost_fraction'] = mae(hd, hand)
        row = {k: round(x, 4) if isinstance(x, float) else x for k, x in row.items()}
    report.append(row)
    box = hand if crop_mode == 'hand' else (0, 0, w, h)
    tiles = [im.crop(box).resize((300, int(300 * (box[3] - box[1]) / (box[2] - box[0])))) for im in (r, v, heat(d))]
    rows.append((n, tiles, hand_mae, hand_ghost))
tw, th = rows[0][1][0].size
sheet = Image.new('RGB', (tw * 3 + 8, (th + 4) * len(rows)), (24, 24, 24))
dr = ImageDraw.Draw(sheet)
for i, (n, tiles, hm, hg) in enumerate(rows):
    for j, t in enumerate(tiles): sheet.paste(t, (j * (tw + 4), i * (th + 4)))
    for j, label in enumerate((f'p{n} rebuilt', 'next native', f'diff  hand {hm:.1f}  ghost {hg*100:.1f}%')):
        dr.text((j * (tw + 4) + 4, i * (th + 4) + 3), label, fill=(255, 255, 0))
sheet.save(out)
mean = lambda k: round(sum(p[k] for p in report if k in p) / max(1, sum(1 for p in report if k in p)), 3)
summary = {'pairs': report}
for k in ('mae', 'hand_mae', 'hand_ghost_fraction', 'held_mae', 'held_hand_mae', 'held_hand_ghost_fraction'): summary['mean_' + k] = mean(k)
json.dump(summary, open(out[:-4] + '.json', 'w'), indent=1)
print(out, json.dumps({k: v for k, v in summary.items() if k.startswith('mean_')}))
