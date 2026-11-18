"""Resolve module offsets of a mod DLL to function + line with its PDB (dbghelp, no extra packages).

    python tools/resolve_symbols.py build/d2vr_render_probe_arm_v1393.dll 5d52dc 5ca278 ...
"""
import ctypes
import ctypes.wintypes as w
import sys
from pathlib import Path

dbghelp = ctypes.WinDLL("dbghelp.dll")


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.ULONG), ("TypeIndex", w.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", w.ULONG),
                ("Size", w.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", w.ULONG), ("Value", ctypes.c_uint64),
                ("Address", ctypes.c_uint64), ("Register", w.ULONG), ("Scope", w.ULONG), ("Tag", w.ULONG),
                ("NameLen", w.ULONG), ("MaxNameLen", w.ULONG), ("Name", ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", w.DWORD), ("FileName", ctypes.c_char_p),
                ("Address", ctypes.c_uint64)]


def main():
    dll = str(Path(sys.argv[1]).resolve())
    proc = w.HANDLE(0x1234)
    dbghelp.SymSetOptions(0x2 | 0x10 | 0x4)  # UNDNAME | LOAD_LINES | DEFERRED_LOADS
    if not dbghelp.SymInitialize(proc, ctypes.c_char_p(str(Path(dll).parent).encode()), False):
        sys.exit("SymInitialize failed")
    base = 0x180000000
    dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
    dbghelp.SymLoadModuleEx.argtypes = [w.HANDLE, w.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, w.DWORD, ctypes.c_void_p, w.DWORD]
    got = dbghelp.SymLoadModuleEx(proc, None, dll.encode(), None, base, 0, None, 0)
    if not got:
        sys.exit("SymLoadModuleEx failed")
    for off in sys.argv[2:]:
        addr = base + int(off, 16)
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = 88  # sizeof(SYMBOL_INFO) on x64
        sym.MaxNameLen = 1024
        disp = ctypes.c_uint64()
        name = "?"
        if dbghelp.SymFromAddr(proc, ctypes.c_uint64(addr), ctypes.byref(disp), ctypes.byref(sym)):
            name = f"{sym.Name.decode(errors='replace')}+0x{disp.value:x}"
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        d32 = w.DWORD()
        where = ""
        if dbghelp.SymGetLineFromAddr64(proc, ctypes.c_uint64(addr), ctypes.byref(d32), ctypes.byref(line)):
            where = f"  {line.FileName.decode(errors='replace')}:{line.LineNumber}"
        print(f"{off:>8}  {name}{where}")


if __name__ == "__main__":
    main()
