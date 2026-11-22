"""Check every {rva,size,0xHASHull} FNV-1a pin of a source tree against a game exe (the brace form port.py regenerates).

    python tools/doto/fnv_pin_check.py                  # build/doto-src/src vs Dishonored_DO.exe
    python tools/doto/fnv_pin_check.py --d2             # src vs Dishonored2.exe
    python tools/doto/fnv_pin_check.py FILE.hpp ...     # only these files (names relative to the tree)
Prints the mismatching pins; exit 1 when any differ.
"""
import re
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[2]
D2 = (r'D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe', ROOT / 'src')
DOTO = (r'D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider\Dishonored_DO.exe', ROOT / 'build' / 'doto-src' / 'src')
PIN = re.compile(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*(\d+|0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]{9,16})ull\s*\}')


def fnv(b):
    h = 14695981039346656037
    for c in b:
        h = ((h ^ c) * 1099511628211) & 0xffffffffffffffff
    return h


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    exe, tree = D2 if '--d2' in sys.argv else DOTO
    pe = pefile.PE(exe, fast_load=True)
    files = [tree / a for a in args] if args else sorted(list(tree.glob('*.hpp')) + list(tree.glob('*.cpp')))
    bad = total = 0
    for f in files:
        text = f.read_text(encoding='utf-8', errors='replace')
        for m in PIN.finditer(text):
            rva, size, want = int(m.group(1), 16), int(m.group(2), 0), int(m.group(3), 16)
            if size > 0x10000:
                continue
            total += 1
            try:
                got = fnv(pe.get_data(rva, size))
            except Exception:
                got = None
            if got != want:
                bad += 1
                line = text.count('\n', 0, m.start()) + 1
                print(f'{f.name}:{line} {{{rva:#x},{size},{want:#x}}} exe {got and hex(got)}')
    print(f'{total} FNV pins checked, {bad} differ')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
