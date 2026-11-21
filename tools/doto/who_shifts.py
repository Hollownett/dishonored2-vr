"""Find aligned instruction pairs in changed functions where a D2 displacement maps to a different DOTO one.

Usage: python tools/doto/who_shifts.py 0x9c 0xa8 0xcc ... [--to 0x8c]
"""
import argparse, collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import align  # noqa
from translate import Translator  # noqa

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('disp', nargs='+')
    ap.add_argument('--same', action='store_true', help='also list unchanged')
    args = ap.parse_args()
    want = {int(d, 0) for d in args.disp}
    t = Translator()
    align._init(align.D2 if hasattr(align, 'D2') else t.a.path, t.b.path)
    per_fn = collections.defaultdict(list)
    for f, pairs in t.code.items():
        g = t.fn[f]
        ta = align.tokens(align._A, f, t.a.fns[f][1])
        tb = align.tokens(align._B, g, t.b.fns[g][1])
        import struct
        arr = struct.unpack('<%di' % (len(pairs) // 4), pairs)
        amap = {f + o: g + p for o, p in zip(arr[0::2], arr[1::2])}
        bi = {x[1]: x for x in tb}
        for x in ta:
            y = bi.get(amap.get(x[1]))
            if not y:
                continue
            for p, q in zip(x[2], y[2]):
                if p in want and (p != q or args.same):
                    per_fn[f].append((x[1], p, q, x[0].split()[0]))
    for f, rows in sorted(per_fn.items(), key=lambda kv: -len(kv[1])):
        print(f'fn {f:#x}->{t.fn[f]:#x}: ' + ', '.join(f'{p:#x}->{q:#x}' for _, p, q, _ in rows[:12]))

if __name__ == '__main__':
    main()
