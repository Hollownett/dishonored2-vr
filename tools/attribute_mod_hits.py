"""Aggregate tools/thread_profile.py mod_hits by function (dbghelp + the DLL's PDB).

    python tools/attribute_mod_hits.py <thread-profile.json> <dll> [thread=present]
"""
import collections
import ctypes
import ctypes.wintypes as w
import json
import sys
from pathlib import Path


class SI(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.ULONG), ("TypeIndex", w.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", w.ULONG), ("Size", w.ULONG),
                ("ModBase", ctypes.c_uint64), ("Flags", w.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", w.ULONG),
                ("Scope", w.ULONG), ("Tag", w.ULONG), ("NameLen", w.ULONG), ("MaxNameLen", w.ULONG), ("Name", ctypes.c_char * 1024)]


class LN(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", w.DWORD), ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]


def resolver(dll):
    dbg = ctypes.WinDLL("dbghelp.dll")
    proc = w.HANDLE(0x4321)
    dbg.SymInitialize.argtypes = [w.HANDLE, ctypes.c_char_p, w.BOOL]
    dbg.SymSetOptions(0x2 | 0x10)
    dbg.SymInitialize(proc, None, False)
    dbg.SymLoadModuleEx.restype = ctypes.c_uint64
    dbg.SymLoadModuleEx.argtypes = [w.HANDLE, w.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, w.DWORD, ctypes.c_void_p, w.DWORD]
    dbg.SymLoadModuleEx(proc, None, str(Path(dll).resolve()).encode(), None, 0x180000000, 0, None, 0)
    dbg.SymFromAddr.argtypes = [w.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
    dbg.SymGetLineFromAddr64.argtypes = [w.HANDLE, ctypes.c_uint64, ctypes.POINTER(w.DWORD), ctypes.c_void_p]

    def name(off):
        s = SI(); s.SizeOfStruct = 88; s.MaxNameLen = 1000; d = ctypes.c_uint64()
        fn = s.Name[:s.NameLen].decode(errors="replace") if dbg.SymFromAddr(proc, 0x180000000 + off, ctypes.byref(d), ctypes.byref(s)) else "?"
        ln = LN(); ln.SizeOfStruct = ctypes.sizeof(LN); d32 = w.DWORD()
        where = f"{Path(ln.FileName.decode()).name}:{ln.LineNumber}" if dbg.SymGetLineFromAddr64(proc, 0x180000000 + off, ctypes.byref(d32), ctypes.byref(ln)) else ""
        return fn, where
    return name


def main():
    data = json.loads(Path(sys.argv[1]).read_text())
    name = resolver(sys.argv[2])
    want = sys.argv[3] if len(sys.argv) > 3 else "present"
    tids = data["present"] if want == "present" else [want]
    for t in tids:
        hits = {int(k, 16): n for k, n in data["mod_hits"].get(t, {}).items()}
        total = data["total"][t] or 1
        by_fn = collections.Counter(); lines = collections.defaultdict(collections.Counter)
        for off, n in hits.items():
            fn, where = name(off)
            by_fn[fn] += n; lines[fn][where] += n
        print(f"thread {t}: {sum(hits.values())} of {total} samples attributed to the mod ({100 * sum(hits.values()) / total:.1f}%), cpu {data['cpu_ms'][t]:.0f} ms")
        for fn, n in by_fn.most_common(30):
            top = ", ".join(f"{w} {c}" for w, c in lines[fn].most_common(3))
            print(f"  {100 * n / total:5.1f}%  {fn[:90]}   [{top}]")


if __name__ == "__main__":
    main()
