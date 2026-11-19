"""Call tree of one thread of a running process from real stack walks (dbghelp StackWalk64 + each module's unwind tables).

Samples the game's render/main thread (the busiest thread with the mod's Present hook, or a given tid), suspending only that thread
for each walk, and prints an inclusive-time call tree of Dishonored2.exe functions (grouped by the exe's .pdata) with the strings each
function references as a naming hint. Read-only apart from the brief SuspendThread/ResumeThread.

    python tools/stack_tree.py <pid> [seconds=10] [hz=200] [out.json] [tid]
"""
import bisect
import collections
import ctypes
import ctypes.wintypes as w
import json
import re
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import sample_threads as st  # noqa: E402

k32 = st.k32
dbg = ctypes.WinDLL("dbghelp.dll")


class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_uint64), ("Segment", w.WORD), ("Mode", ctypes.c_int)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64), ("AddrStack", ADDRESS64),
                ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_uint64 * 4), ("Far", w.BOOL),
                ("Virtual", w.BOOL), ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", ctypes.c_byte * 512)]  # KdHelp padded generously


dbg.SymInitialize.argtypes = [w.HANDLE, ctypes.c_char_p, w.BOOL]
dbg.SymSetOptions.argtypes = [w.DWORD]
dbg.StackWalk64.argtypes = [w.DWORD, w.HANDLE, w.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p, ctypes.c_void_p,
                            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
dbg.StackWalk64.restype = w.BOOL
FTA = ctypes.cast(dbg.SymFunctionTableAccess64, ctypes.c_void_p)
GMB = ctypes.cast(dbg.SymGetModuleBase64, ctypes.c_void_p)


def exe_index(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]; nsec = struct.unpack_from("<H", d, pe + 6)[0]; optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24; secs = []
    for i in range(nsec):
        o = opt + optsz + i * 40; secs.append(struct.unpack_from("<IIII", d, o + 8))

    def off(rva):
        for vsz, va, rsz, raw in secs:
            if va <= rva < va + max(vsz, rsz): return raw + rva - va
    exc_rva, exc_sz = struct.unpack_from("<II", d, opt + 112 + 3 * 8)
    fo = off(exc_rva); funcs = sorted(struct.unpack_from("<II", d, fo + i * 12) for i in range(exc_sz // 12))
    begins = [f[0] for f in funcs]

    def func_of(rva):
        i = bisect.bisect_right(begins, rva) - 1
        return funcs[i][0] if i >= 0 and rva < funcs[i][1] else None

    def strings_of(fb):
        i = bisect.bisect_left(begins, fb); fe = funcs[i][1]; o = off(fb); code = d[o:o + (fe - fb)]; out = []
        for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
            tgt = fb + m.end() + 4 + struct.unpack_from("<i", code, m.end())[0]; so = off(tgt)
            if so is None: continue
            s = re.match(rb"[\x20-\x7e]{5,60}\x00", d[so:so + 61])
            if s and s.group(0)[:-1].decode() not in out: out.append(s.group(0)[:-1].decode())
        return out[:3]
    return func_of, strings_of


def main():
    pid = int(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10; hz = float(sys.argv[3]) if len(sys.argv) > 3 else 200
    out_path = sys.argv[4] if len(sys.argv) > 4 else "stack-tree.json"; want_tid = int(sys.argv[5]) if len(sys.argv) > 5 else None
    hp = k32.OpenProcess(0x0410, False, pid)
    dbg.SymSetOptions(0x4 | 0x2)  # SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME
    if not dbg.SymInitialize(hp, None, True): raise SystemExit("SymInitialize failed")
    mods = st.modules(hp); bases = [m[0] for m in mods]

    def find(addr):
        i = bisect.bisect_right(bases, addr) - 1
        return mods[i] if i >= 0 and addr < mods[i][0] + mods[i][1] else None
    exe = next(m for m in mods if m[2].lower() == "dishonored2.exe")
    # the render/main thread: the busiest thread over a short CPU window
    tids = st.threads(pid); handles = {t: k32.OpenThread(st.THREAD_ALL, False, t) for t in tids}
    cpu0 = {t: st.cpu_ms(h) for t, h in handles.items() if h}; time.sleep(1.0)
    busiest = sorted(cpu0, key=lambda t: st.cpu_ms(handles[t]) - cpu0[t], reverse=True)
    tid = want_tid or busiest[0]; h = handles[tid]
    raw = ctypes.create_string_buffer(ctypes.sizeof(st.CONTEXT) + 16)
    ctx = st.CONTEXT.from_address((ctypes.addressof(raw) + 15) & ~15)
    stacks = []; times = []; t0 = time.perf_counter(); end = t0 + secs
    while time.perf_counter() < end:
        if k32.SuspendThread(h) == 0xFFFFFFFF: break
        try:
            ctx.ContextFlags = 0x10000B  # CONTEXT_FULL (AMD64)
            if not k32.GetThreadContext(h, ctypes.addressof(ctx)): continue
            fr = STACKFRAME64(); fr.AddrPC.Offset = ctx.Rip; fr.AddrStack.Offset = ctx.Gpr[4]; fr.AddrFrame.Offset = ctx.Gpr[5]
            fr.AddrPC.Mode = fr.AddrStack.Mode = fr.AddrFrame.Mode = 3  # AddrModeFlat
            chain = []
            for _ in range(96):
                if not dbg.StackWalk64(0x8664, hp, h, ctypes.byref(fr), ctypes.addressof(ctx), None, FTA, GMB, None): break
                if not fr.AddrPC.Offset: break
                chain.append(fr.AddrPC.Offset)
            stacks.append(chain); times.append(round(time.perf_counter() - t0, 3))
        finally:
            k32.ResumeThread(h)
        time.sleep(1 / hz)
    # each frame as module+offset; exe frames grouped to their function start
    func_of, strings_of = exe_index(exe[3] if len(exe) > 3 else r"D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe")

    def key(a, leaf):
        m = find(a)
        if not m: return "?"
        if m is exe:
            f = func_of(a - m[0] if leaf else a - m[0] - 1)
            return f"exe:{f:x}" if f is not None else f"exe?{a - m[0]:x}"
        if m[2].lower().startswith("d2vr_render_probe"): return f"{m[2]}+{a - m[0] - (0 if leaf else 1):x}"  # mod frames keep their offset (PDB-resolvable)
        return m[2]
    keyed = [[key(a, i == 0) for i, a in enumerate(s)] for s in stacks]
    json.dump({"tid": tid, "samples": len(stacks), "times": times, "stacks": keyed}, open(out_path, "w"))
    n = len(keyed)
    # call tree from the outermost exe frame inward (dedupe recursion per stack)
    tree = {}
    for s in keyed:
        node = tree; seen = set()
        for k in reversed(s):
            if k in seen: continue
            seen.add(k); child = node.setdefault(k, {"n": 0, "c": {}}); child["n"] += 1; node = child["c"]
    names = {}

    def name(k):
        if k.startswith("exe:"):
            if k not in names: names[k] = strings_of(int(k[4:], 16))
            return f"{k} {names[k]}" if names[k] else k
        return k

    def show(node, depth, path_n):
        for k, v in sorted(node.items(), key=lambda kv: -kv[1]["n"]):
            if v["n"] / n < 0.02 or depth > 14: continue
            print(f"{'  ' * depth}{100 * v['n'] / n:5.1f}%  {name(k)}")
            show(v["c"], depth + 1, v["n"])
    print(f"tid {tid}: {n} stack walks over {secs:.0f}s; mean depth {sum(map(len, keyed)) / max(1, n):.0f}")
    show(tree, 0, n)


if __name__ == "__main__":
    main()
