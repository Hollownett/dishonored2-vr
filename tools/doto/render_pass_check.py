"""Check the render pass table (src/render_pass_table.hpp: ARK_RENDERPASS_* name -> job callback RVA) against a game exe's own
registration calls: each pass name is passed in rdx to the register call, with the job callback stored at [rsp+0x28] just
before it. Prints the exe's (name, callback, order) and every row of the given table header that disagrees.

    python tools/doto/render_pass_check.py                    # DOTO exe vs build/doto-src/src/render_pass_table.hpp
    python tools/doto/render_pass_check.py --d2               # D2 exe vs src/render_pass_table.hpp
"""
import re
import sys
from pathlib import Path

import capstone
import pefile

ROOT = Path(__file__).resolve().parents[2]
D2 = (r'D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe', ROOT / 'src' / 'render_pass_table.hpp')
DOTO = (r'D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider\Dishonored_DO.exe', ROOT / 'build' / 'doto-src' / 'src' / 'render_pass_table.hpp')


def exe_passes(exe):
    pe = pefile.PE(exe, fast_load=True)
    ib = pe.OPTIONAL_HEADER.ImageBase
    data = pe.__data__
    names = {}
    for m in re.finditer(rb'ARK_RENDERPASS_[A-Z0-9_]+\0', data):
        names[pe.get_rva_from_offset(m.start())] = m.group(0)[:-1].decode()
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    code = text.get_data()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    found = []
    # lea rdx,[rip+disp] = 48 8d 15 disp32
    for m in re.finditer(rb'\x48\x8d\x15', code):
        at = m.start()
        disp = int.from_bytes(code[at + 3:at + 7], 'little', signed=True)
        tgt = text.VirtualAddress + at + 7 + disp
        if tgt not in names:
            continue
        # the callback: the nearest 'lea rax,[rip+X]' followed by 'mov [rsp+0x28],rax' within 0x40 bytes before the call
        lo = max(0, at - 0x40)
        cb = None
        window = code[lo:at + 0x40]
        for ins in md.disasm(window, ib + text.VirtualAddress + lo):
            if ins.mnemonic == 'lea' and ins.op_str.startswith('rax, [rip'):
                d = re.search(r'rip ([+-]) (0x[0-9a-f]+)', ins.op_str)
                last_lea = ins.address + ins.size + int(d.group(2), 16) * (1 if d.group(1) == '+' else -1) - ib
            elif ins.mnemonic == 'mov' and ins.op_str == 'qword ptr [rsp + 0x28], rax':
                cb = last_lea if 'last_lea' in dir() else None
            elif ins.mnemonic == 'call' and ins.address - ib > text.VirtualAddress + at:
                break
        found.append((text.VirtualAddress + at, names[tgt], cb))
    found.sort()
    return found


def table(path):
    rows = []
    for m in re.finditer(r'\{"(ARK_RENDERPASS_[A-Z0-9_]+)",\s*(0x[0-9A-Fa-f]+)\}', path.read_text(encoding='utf-8')):
        rows.append((m.group(1), int(m.group(2), 16)))
    return rows


def main():
    exe, hdr = D2 if '--d2' in sys.argv else DOTO
    got = exe_passes(exe)
    rows = table(hdr)
    print(f'{exe}: {len(got)} registrations; {hdr}: {len(rows)} rows')
    by_name = {}
    for i, (site, name, cb) in enumerate(got):
        by_name.setdefault(name, []).append((i, cb))
    # The table's pass id is the mod's own logical id (the D2 registration index, used by name/constant across src/); the exe's
    # own order may differ (DOTO registers an extra ARK_RENDERPASS_UPDATE_CLOTH), so only the callbacks must agree.
    bad = undecoded = shifted = 0
    for i, (name, cb) in enumerate(rows):
        exe_rows = by_name.get(name, [])
        exe_ids = [j for j, _ in exe_rows]
        exe_cbs = [c for _, c in exe_rows]
        if exe_ids and i not in exe_ids:
            shifted += 1
        if exe_cbs and all(c is None for c in exe_cbs):
            undecoded += 1
            continue
        if cb not in exe_cbs:
            bad += 1
            print(f'  row {i:2d} {name:40s} table {cb:#x}  exe id {exe_ids} callback {[hex(c) if c else None for c in exe_cbs]}  CALLBACK DIFFERS')
    extra = [n for _, n, _ in got if n not in {r[0] for r in rows}]
    if extra:
        print('  passes in the exe but not in the table (untagged):', extra)
    print(f'callback mismatches: {bad}; registrations not decoded by this script: {undecoded}; rows at another exe index: {shifted}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
