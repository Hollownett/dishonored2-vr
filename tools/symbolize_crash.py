"""Resolve the VR DLL frames of a crash record (native-exception-context.txt) to function and source line.

    python tools/symbolize_crash.py artifacts/runs/<run>/native-exception-context.txt

Needs build/<dll>.pdb next to the DLL (builds from v451 on). Game frames are printed as RVAs.
"""
import ctypes as c
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SYMBOL_INFO(c.Structure):
    _fields_ = [('SizeOfStruct', c.c_ulong), ('TypeIndex', c.c_ulong), ('Reserved', c.c_ulonglong * 2), ('Index', c.c_ulong),
                ('Size', c.c_ulong), ('ModBase', c.c_ulonglong), ('Flags', c.c_ulong), ('Value', c.c_ulonglong),
                ('Address', c.c_ulonglong), ('Register', c.c_ulong), ('Scope', c.c_ulong), ('Tag', c.c_ulong),
                ('NameLen', c.c_ulong), ('MaxNameLen', c.c_ulong), ('Name', c.c_char * 512)]


class IMAGEHLP_LINE64(c.Structure):
    _fields_ = [('SizeOfStruct', c.c_ulong), ('Key', c.c_void_p), ('LineNumber', c.c_ulong), ('FileName', c.c_char_p), ('Address', c.c_ulonglong)]


def main():
    record = Path(sys.argv[1])
    text = record.read_text(errors='replace')
    dbg = c.windll.dbghelp
    dbg.SymLoadModuleEx.restype = c.c_ulonglong
    dbg.SymLoadModuleEx.argtypes = [c.c_void_p, c.c_void_p, c.c_char_p, c.c_char_p, c.c_ulonglong, c.c_ulong, c.c_void_p, c.c_ulong]
    dbg.SymFromAddr.argtypes = [c.c_void_p, c.c_ulonglong, c.POINTER(c.c_ulonglong), c.POINTER(SYMBOL_INFO)]
    dbg.SymGetLineFromAddr64.argtypes = [c.c_void_p, c.c_ulonglong, c.POINTER(c.c_ulong), c.POINTER(IMAGEHLP_LINE64)]
    proc = c.c_void_p(0x5eed)
    dbg.SymSetOptions(0x2 | 0x10)  # UNDNAME | LOAD_LINES
    if not dbg.SymInitialize(proc, str(ROOT / 'build').encode(), False):
        sys.exit('SymInitialize failed')
    bases = {}
    for line in text.splitlines():
        m = re.search(r'frame=(\d+) module=(\S+) rva=(0x[0-9a-f]+)', line)
        if not m:
            m0 = re.search(r'module=(\S+) .*rva=(0x[0-9a-f]+) rip=', line)
            if m0 and 'record=0' in line and 'frame=' not in line:
                print('fault', m0.group(1), m0.group(2))
            continue
        frame, module, rva = m.group(1), m.group(2), int(m.group(3), 16)
        where = ''
        if module.startswith('d2vr_render_probe'):
            dll = ROOT / 'build' / module
            if module not in bases:
                base = 0x10000000 * (len(bases) + 1)
                bases[module] = base if dbg.SymLoadModuleEx(proc, None, str(dll).encode(), None, base, dll.stat().st_size, None, 0) else 0
            base = bases[module]
            if base:
                sym = SYMBOL_INFO(SizeOfStruct=88, MaxNameLen=511)  # sizeof(SYMBOL_INFO) with Name[1]
                disp = c.c_ulonglong()
                if dbg.SymFromAddr(proc, base + rva, c.byref(disp), c.byref(sym)):
                    where = f'{sym.Name.decode(errors="replace")}+{disp.value:#x}'
                ln = IMAGEHLP_LINE64(SizeOfStruct=c.sizeof(IMAGEHLP_LINE64))
                d32 = c.c_ulong()
                if dbg.SymGetLineFromAddr64(proc, base + rva, c.byref(d32), c.byref(ln)):
                    where += f'  {Path(ln.FileName.decode(errors="replace")).name}:{ln.LineNumber}'
                if not where:
                    where = '(no symbol: no PDB for this build?)'
        print(f'#{frame:>2} {module} {rva:#x} {where}')
    dbg.SymCleanup(proc)


if __name__ == '__main__':
    main()
