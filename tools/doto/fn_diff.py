"""Side-by-side diff of a D2 function and its DOTO counterpart (aligned instructions).

Usage: python tools/doto/fn_diff.py <d2 rva> [--all] [--grep 0x140]
Prints only differing aligned instructions unless --all.
"""
import argparse
import difflib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capstone import CS_ARCH_X86, CS_MODE_64, Cs  # noqa: E402
from translate import Translator  # noqa: E402


def listing(img, begin, end):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    return [(i.address, i.mnemonic, i.op_str) for i in md.disasm(img.bytes(begin, end - begin), begin)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rva')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--grep')
    ap.add_argument('--doto', help='DOTO function begin when the map has none')
    args = ap.parse_args()
    t = Translator()
    x = int(args.rva, 0)
    f = t.a.function_of(x) or x
    g = int(args.doto, 0) if args.doto else t.fn.get(f)
    if g is None:
        y, how, why = t.rva(f)
        g = y
    if g is None:
        raise SystemExit(f'{f:#x}: no DOTO function')
    fa = t.a.fns.get(f, (f, f + 0x400))
    fb = t.b.fns.get(g, (g, g + 0x400))
    la = listing(t.a, f, fa[1])
    lb = listing(t.b, g, fb[1])
    print(f'D2 {f:#x}..{fa[1]:#x} ({len(la)} insns)  DOTO {g:#x}..{fb[1]:#x} ({len(lb)} insns)  how={t.how.get(f)}')
    sm = difflib.SequenceMatcher(None, [i[1] for i in la], [i[1] for i in lb], autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == 'equal':
            for k in range(i2 - i1):
                a, b = la[i1 + k], lb[j1 + k]
                differ = a[2] != b[2] and not ('rip' in a[2] and 'rip' in b[2]) and not (a[1].startswith(('call', 'j')) )
                line = f'{a[0]:#9x} {a[1]:8s} {a[2]:44s} | {b[0]:#9x} {b[1]:8s} {b[2]}'
                if args.all or differ or (args.grep and (args.grep in a[2] or args.grep in b[2])):
                    print(('* ' if differ else '  ') + line)
        else:
            for k in range(max(i2 - i1, j2 - j1)):
                a = la[i1 + k] if i1 + k < i2 else None
                b = lb[j1 + k] if j1 + k < j2 else None
                left = f'{a[0]:#9x} {a[1]:8s} {a[2]:44s}' if a else ' ' * 63
                right = f'{b[0]:#9x} {b[1]:8s} {b[2]}' if b else ''
                print(f'{op[0]} {left} | {right}')


if __name__ == '__main__':
    main()
