"""Compare the finger geometry the game solved with (artifacts/finger-geometry.jsonl, one line per hand per binding) with the lab's own.

  tools/hand_pose_lab.exe out.json corvo --write-geometry lab-geometry.jsonl      (the lab's right hand)
  python tools/finger_geometry_diff.py artifacts/finger-geometry.jsonl lab-geometry.jsonl

Prints, per finger, the largest difference of each field (pivot, bones, hinges, hinge axes in the palette's space, existing bend, lowest turn; mm or degrees)
and of the bind transforms of the wrist and the thumb's three joints. Differences above a few millimetres or degrees mean the game's hand is not the lab's.
"""
import json
import math
import sys


def load(path, side):
    rows = [json.loads(line) for line in open(path, encoding='utf-8') if line.strip()]
    rows = [r for r in rows if r.get('side') == side]
    if not rows:
        sys.exit('%s: no line for side %d' % (path, side))
    return rows[-1]


def angle(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot / (na * nb))))) if na * nb > 0 else float('nan')


def dist_mm(a, b):
    return 1000 * math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    game, lab = load(sys.argv[1], 1), load(sys.argv[2], 1)
    print('palm normal: %.2f deg apart' % angle(game['palm_in'], lab['palm_in']))
    names = ('thumb', 'index', 'medium', 'ring', 'pinky')
    for f, name in enumerate(names):
        g, l = game['fingers'][f], lab['fingers'][f]
        print('%-6s pivot %.2f mm | bones mm %s | hinges deg %s | axes deg %s | existing deg %s | lowest deg %s' % (
            name, dist_mm(g['pivot'], l['pivot']),
            ' '.join('%.2f' % dist_mm(a, b) for a, b in zip(g['bone'], l['bone'])),
            ' '.join('%.2f' % angle(a, b) for a, b in zip(g['hinge'], l['hinge'])),
            ' '.join('%.2f' % angle(a, b) for a, b in zip(g['axes'], l['axes'])),
            ' '.join('%.2f' % abs(math.degrees(a - b)) for a, b in zip(g['existing'], l['existing'])),
            ' '.join('%.2f' % abs(math.degrees(a - b)) for a, b in zip(g['lowest'], l['lowest']))))
    for i, name in enumerate(('wrist', 'thumb_01', 'thumb_02', 'thumb_03')):
        g, l = game['bind'][i], lab['bind'][i]
        # column-major 4x4 as the palette holds it: column c is g[4c..4c+2] (the axes), the position g[12..14]
        axes = max(angle(g[c * 4:c * 4 + 3], l[c * 4:c * 4 + 3]) for c in range(3))
        print('bind %-8s position %.2f mm, axes %.2f deg' % (name, dist_mm(g[12:15], l[12:15]), axes))


if __name__ == '__main__':
    main()
