"""Do the game's worker threads call D3D/DXGI/the driver from game-side jobs? Real stack walks of every busy thread, repeatedly.

    python tools/worker_d3d.py <pid> [seconds=12] [out.json]

Each sample suspends one thread for its walk. A stack is a game-side job when it holds exe:492670 (TtarkComponentUpdateTask) or
exe:7a9e0 (TtarkSubmitAndWaitGraph), a render job when it holds the job executor 0x2c5d50 (RenderPassMap's hook point) or the render
side exe:458c0. Prints, per class, how many stacks also pass through d3d11/dxgi/dxcore/the NVIDIA driver, and examples.
"""
import bisect
import collections
import ctypes
import json
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import sample_threads as st  # noqa: E402
import stack_tree as tree  # noqa: E402

GFX = ("d3d11.dll", "dxgi.dll", "dxcore.dll", "nvwgf2umx.dll", "d3d11_3sdklayers.dll")


def main():
    pid = int(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 12
    out_path = sys.argv[3] if len(sys.argv) > 3 else "worker-d3d.json"
    k32 = st.k32; hp = k32.OpenProcess(0x0410, False, pid)
    tree.dbg.SymSetOptions(0x4 | 0x2); tree.dbg.SymInitialize(hp, None, True)
    mods = st.modules(hp); bases = [m[0] for m in mods]
    exe = next(m for m in mods if m[2].lower() == "dishonored2.exe")
    func_of, strings_of = tree.exe_index(r"D:\Steam\steamapps\common\Dishonored2\Dishonored2.exe")

    def key(a, leaf):
        i = bisect.bisect_right(bases, a) - 1
        if i < 0 or a >= mods[i][0] + mods[i][1]: return "?"
        m = mods[i]
        if m is exe:
            f = func_of(a - m[0] - (0 if leaf else 1)); return f"exe:{f:x}" if f is not None else f"exe?{a - m[0]:x}"
        return m[2]
    handles = {t: k32.OpenThread(st.THREAD_ALL, False, t) for t in st.threads(pid)}
    handles = {t: h for t, h in handles.items() if h}
    cpu0 = {t: st.cpu_ms(h) for t, h in handles.items()}; time.sleep(1.0)
    busy = [t for t in handles if st.cpu_ms(handles[t]) - cpu0[t] > 60]  # >6 % of a core over the second
    raw = ctypes.create_string_buffer(ctypes.sizeof(st.CONTEXT) + 16)
    ctx = st.CONTEXT.from_address((ctypes.addressof(raw) + 15) & ~15)
    classes = collections.Counter(); gfx = collections.Counter(); examples = collections.defaultdict(list)
    end = time.perf_counter() + secs; walks = 0
    while time.perf_counter() < end:
        for t in busy:
            h = handles[t]
            if k32.SuspendThread(h) == 0xFFFFFFFF: continue
            chain = []
            try:
                ctx.ContextFlags = 0x10000B
                if k32.GetThreadContext(h, ctypes.addressof(ctx)):
                    fr = tree.STACKFRAME64(); fr.AddrPC.Offset = ctx.Rip; fr.AddrStack.Offset = ctx.Gpr[4]; fr.AddrFrame.Offset = ctx.Gpr[5]
                    fr.AddrPC.Mode = fr.AddrStack.Mode = fr.AddrFrame.Mode = 3
                    for _ in range(64):
                        if not tree.dbg.StackWalk64(0x8664, hp, h, ctypes.byref(fr), ctypes.addressof(ctx), None, tree.FTA, tree.GMB, None): break
                        if not fr.AddrPC.Offset: break
                        chain.append(fr.AddrPC.Offset)
            finally:
                k32.ResumeThread(h)
            walks += 1
            keys = [key(a, i == 0) for i, a in enumerate(chain)]
            ks = set(keys)
            cls = ("game-side job" if ("exe:492670" in ks or "exe:7a9e0" in ks) else
                   "render job" if any(k in ks for k in ("exe:2c5d50", "exe:2c8490", "exe:458c0")) else "other")
            role = "main" if "exe:450d0" in ks else "worker"
            classes[(role, cls)] += 1
            if ks & set(GFX):
                gfx[(role, cls)] += 1
                if len(examples[(role, cls)]) < 3: examples[(role, cls)].append(keys[:16])
        time.sleep(0.02)
    print(f"{walks} walks of {len(busy)} busy threads over {secs:.0f}s")
    for k, n in sorted(classes.items()):
        print(f"  {k[0]:6} {k[1]:14} {n:5} walks, {gfx[k]:4} through d3d11/dxgi/driver")
    for k, ex in examples.items():
        print(f"  examples {k}:")
        for e in ex: print("     " + " <- ".join(e))
    json.dump({"classes": {f"{a}|{b}": n for (a, b), n in classes.items()}, "gfx": {f"{a}|{b}": n for (a, b), n in gfx.items()},
               "examples": {f"{a}|{b}": e for (a, b), e in examples.items()}}, open(out_path, "w"))


if __name__ == "__main__":
    main()
