"""One-shot real call stacks of every thread of a process (dbghelp StackWalk64), e.g. to see a deadlock.

    python tools/dump_stacks.py <pid> [delay_s=0] [out.txt] [mod_dll_for_pdb]

Each thread is suspended only for its own walk. Mod frames are resolved with the DLL's PDB (tools/attribute_mod_hits.py).
"""
import ctypes
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import sample_threads as st  # noqa: E402
import stack_tree as tree  # noqa: E402


def main():
    pid = int(sys.argv[1]); time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 0)
    out = open(sys.argv[3], "w", encoding="utf-8") if len(sys.argv) > 3 else sys.stdout
    resolve = None
    if len(sys.argv) > 4:
        import attribute_mod_hits as amh
        resolve = amh.resolver(sys.argv[4])
    k32 = st.k32; hp = k32.OpenProcess(0x0410, False, pid)
    tree.dbg.SymSetOptions(0x4 | 0x2)
    tree.dbg.SymInitialize(hp, None, True)
    mods = st.modules(hp)
    import bisect
    bases = [m[0] for m in mods]

    def name(a):
        i = bisect.bisect_right(bases, a) - 1
        if i < 0 or a >= mods[i][0] + mods[i][1]: return f"?{a:x}"
        m = mods[i]; off = a - m[0]
        if resolve and m[2].lower().startswith("d2vr_render_probe"):
            fn, where = resolve(off); return f"mod:{fn.split('<')[0][:60]} {where}"
        return f"{m[2]}+{off:x}"
    raw = ctypes.create_string_buffer(ctypes.sizeof(st.CONTEXT) + 16)
    ctx = st.CONTEXT.from_address((ctypes.addressof(raw) + 15) & ~15)
    for tid in st.threads(pid):
        h = k32.OpenThread(st.THREAD_ALL, False, tid)
        if not h: continue
        if k32.SuspendThread(h) == 0xFFFFFFFF: continue
        chain = []
        try:
            ctx.ContextFlags = 0x10000B
            if k32.GetThreadContext(h, ctypes.addressof(ctx)):
                fr = tree.STACKFRAME64(); fr.AddrPC.Offset = ctx.Rip; fr.AddrStack.Offset = ctx.Gpr[4]; fr.AddrFrame.Offset = ctx.Gpr[5]
                fr.AddrPC.Mode = fr.AddrStack.Mode = fr.AddrFrame.Mode = 3
                for _ in range(40):
                    if not tree.dbg.StackWalk64(0x8664, hp, h, ctypes.byref(fr), ctypes.addressof(ctx), None, tree.FTA, tree.GMB, None): break
                    if not fr.AddrPC.Offset: break
                    chain.append(fr.AddrPC.Offset)
        finally:
            k32.ResumeThread(h)
        print(f"tid {tid} cpu {st.cpu_ms(h):.0f} ms", file=out)
        for a in chain[:18]: print("    " + name(a), file=out)
    out.close() if out is not sys.stdout else None


if __name__ == "__main__":
    main()
