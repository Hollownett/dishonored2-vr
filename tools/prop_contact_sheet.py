"""Contact sheet of the hand lab's real-shape grips (v1082): one view of every scene with its grip map entry and finger metrics.

    python tools/prop_contact_sheet.py artifacts/hand-lab/<tag> [--view 6] [--columns 8]

Reads <tag>/frames.json (the lab's scenes and report lines) and <tag>/<scene>-views.png (tools/render_body_ik.py --hand-views montages),
writes <tag>/contact-sheet.png. Only scenes that carry a GRIP MAP line (real shapes) are shown.
"""
import argparse
import json
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TILE, GAP, HEAD = 480, 6, 34  # tools/render_body_ik.py hand montage: 2 rows of 4 tiles under a 34 px title


def font(size):
    for name in ('arial.ttf', 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lab_dir', type=Path)
    ap.add_argument('--view', type=int, default=6, help='montage tile 0-7 (6: front-thumb-45, palm and thumb side)')
    ap.add_argument('--columns', type=int, default=8)
    ap.add_argument('--size', type=int, default=260)
    args = ap.parse_args()
    frames = json.loads((args.lab_dir / 'frames.json').read_text(encoding='utf-8-sig'))['frames']
    cells = []
    for f in frames:
        report = f.get('report', [])
        grip = next((l for l in report if l.startswith('GRIP MAP')), None)
        gap = next((l for l in report if l.startswith('FINGER GAP')), None)
        pen = next((l for l in report if l.startswith('PENETRATION')), None)
        path = args.lab_dir / ('%s-views.png' % re.sub(r'[^A-Za-z0-9_.-]+', '_', f['label']))
        if not grip or not path.is_file():
            continue
        col, row = args.view % 4, args.view // 4
        x, y = col * (TILE + GAP), HEAD + row * (TILE + GAP)
        tile = Image.open(path).convert('RGB').crop((x, y, x + TILE, y + TILE)).resize((args.size, args.size))
        kind = grip.split(':', 1)[1].split('(')[0].strip().split(',')[0]
        touch = re.search(r'touching \(<= 3 mm\) (\d)/5, worst ([-\d.]+)', gap or '')
        pm = re.search(r'fingers ([\d.]+)', pen or '')
        size = re.search(r'real shape ([\d.]+) x ([\d.]+) x ([\d.]+) cm', grip)
        name = f['label'].replace('real_sheet_', '').replace('real_', '')
        cells.append((tile, name, kind, touch, pm, size))
    if not cells:
        raise SystemExit('no real-shape scenes with montages in %s' % args.lab_dir)
    cols = args.columns
    rows = (len(cells) + cols - 1) // cols
    cap = 58
    sheet = Image.new('RGB', (cols * (args.size + 4) + 4, 40 + rows * (args.size + cap + 4)), (22, 22, 26))
    draw = ImageDraw.Draw(sheet)
    touched = sum(int(c[3].group(1)) for c in cells if c[3])
    draw.text((8, 8), 'Grip map contact sheet: %d props, fingers on the render mesh (<= 3 mm) %d of %d' % (len(cells), touched, 5 * len(cells)),
              fill=(240, 240, 240), font=font(20))
    for i, (tile, name, kind, touch, pm, size) in enumerate(cells):
        cx, cy = 4 + (i % cols) * (args.size + 4), 40 + (i // cols) * (args.size + cap + 4)
        sheet.paste(tile, (cx, cy))
        draw.text((cx + 3, cy + args.size + 2), name[:34], fill=(255, 255, 255), font=font(14))
        line2 = '%s  %s cm' % (kind, 'x'.join('%.0f' % float(v) for v in size.groups()) if size else '?')
        draw.text((cx + 3, cy + args.size + 19), line2[:40], fill=(200, 200, 160), font=font(13))
        ok = touch and int(touch.group(1)) >= 4
        line3 = 'fingers %s/5  gap %s mm  pen %s mm' % (touch.group(1) if touch else '?', touch.group(2) if touch else '?', pm.group(1) if pm else '?')
        draw.text((cx + 3, cy + args.size + 36), line3, fill=(140, 220, 140) if ok else (230, 160, 120), font=font(13))
    out = args.lab_dir / 'contact-sheet.png'
    sheet.save(out)
    print('contact sheet: %s (%d props)' % (out, len(cells)))


if __name__ == '__main__':
    main()
