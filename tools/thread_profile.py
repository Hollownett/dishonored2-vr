"""Per-thread where-is-it profile of a running process, including WHERE threads wait (which caller holds the wait).

For every sample: the RIP's module (or system export) and the first non-system code address found on the stack above it
(the caller that entered the system call or driver). Aggregated per thread: CPU share, wait share, and the top (location,
caller) pairs. The thread that runs the mod's Present hook is marked (its stacks hold d2vr + dxgi addresses).

Read-only apart from the brief SuspendThread/ResumeThread per sample (tools/sample_threads.py does the same).

    python tools/thread_profile.py <pid> [seconds=6] [hz=250] [threads=10]
"""
import bisect
import collections
import ctypes
import ctypes.wintypes as w
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sample_threads as st  # noqa: E402

k32 = st.k32
SYSTEM = {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "win32u.dll", "ucrtbase.dll", "msvcp140.dll", "vcruntime140.dll"}
DEPTH = 6144


def main():
    pid = int(sys.argv[1])
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6
    hz = float(sys.argv[3]) if len(sys.argv) > 3 else 250
    show = int(sys.argv[4]) if len(sys.argv) > 4 else 10
    hp = k32.OpenProcess(0x0410, False, pid)
    mods = st.modules(hp)
    bases = [m[0] for m in mods]
    ex = st.exports(mods)

    def find(addr):
        i = bisect.bisect_right(bases, addr) - 1
        if i >= 0 and addr < mods[i][0] + mods[i][1]:
            return mods[i]
        return None

    def name(addr):
        m = find(addr)
        if not m:
            return "?"
        off = addr - m[0]
        table = ex.get(m[2].lower())
        if table:
            i = bisect.bisect_right(table, (off, chr(0x10FFFF))) - 1
            if i >= 0:
                return f"{m[2][:10]}!{table[i][1]}"
        return f"{m[2][:16]}+{off:x}"

    handles = {}
    for tid in st.threads(pid):
        h = k32.OpenThread(st.THREAD_ALL, False, tid)
        if h:
            handles[tid] = h
    cpu0 = {t: st.cpu_ms(h) for t, h in handles.items()}
    raw = ctypes.create_string_buffer(ctypes.sizeof(st.CONTEXT) + 16)
    ctx = st.CONTEXT.from_address((ctypes.addressof(raw) + 15) & ~15)
    buf = (ctypes.c_uint64 * (DEPTH // 8))()
    where = collections.defaultdict(collections.Counter)
    bymod = collections.defaultdict(collections.Counter)
    mod_hits = collections.defaultdict(collections.Counter)
    # leaf: the running non-system address (module+offset), or the first non-system caller of a system frame (waits, driver)
    leaf = collections.defaultdict(collections.Counter)
    marks = collections.defaultdict(set)
    total = collections.Counter()
    end = time.perf_counter() + secs
    rounds = 0
    while time.perf_counter() < end:
        for tid, h in handles.items():
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            try:
                ctx.ContextFlags = 0x100003
                if not k32.GetThreadContext(h, ctypes.addressof(ctx)):
                    continue
                got = ctypes.c_size_t()
                k32.ReadProcessMemory(hp, ctypes.c_void_p(ctx.Gpr[4]), buf, DEPTH, ctypes.byref(got))
                rip_mod = find(ctx.Rip)
                rip_name = name(ctx.Rip)
                callers = []
                for v in buf[:got.value // 8]:
                    m = find(v)
                    if not m or m[2].lower() in SYSTEM:
                        continue
                    callers.append(v)
                    if len(callers) >= 3:
                        break
                stack_mods = {find(v)[2].lower() for v in buf[:got.value // 8] if find(v)}
                if "dxgi.dll" in stack_mods and any(s.startswith("d2vr_render_probe") for s in stack_mods):
                    marks[tid].add("present-hook")
                chain = " <- ".join(name(c) for c in callers[:2])
                where[tid][f"{rip_name}  <-  {chain}"] += 1
                waiting = rip_mod and rip_mod[2].lower() in SYSTEM and "Wait" in rip_name
                owner = find(callers[0])[2] if callers else (rip_mod[2] if rip_mod else "?")
                running_in = rip_mod[2] if rip_mod and rip_mod[2].lower() not in SYSTEM else "sys<-" + owner
                # raw mod attribution: the mod address that is running, or the mod caller of a system call / driver frame
                for v in ([ctx.Rip] if rip_mod and rip_mod[2].lower().startswith("d2vr_render_probe") else []) + callers:
                    m = find(v)
                    if m and m[2].lower().startswith("d2vr_render_probe"):
                        mod_hits[tid][v - m[0]] += 1
                        break
                bymod[tid][("wait in " + owner) if waiting else ("run in " + running_in)] += 1
                at = ctx.Rip if rip_mod and rip_mod[2].lower() not in SYSTEM else (callers[0] if callers else ctx.Rip)
                am = find(at)
                leaf[tid][("wait:" if waiting else "") + (f"{am[2]}+{at - am[0]:x}" if am else "?")] += 1
                total[tid] += 1
            finally:
                k32.ResumeThread(h)
        rounds += 1
        time.sleep(1 / hz)
    cpu = {t: st.cpu_ms(h) - cpu0[t] for t, h in handles.items()}
    import json
    present = [t for t in handles if "present-hook" in marks[t]]
    dump = {"total": {str(t): total[t] for t in handles}, "cpu_ms": {str(t): cpu[t] for t in handles}, "present": [str(t) for t in present],
            "mod_hits": {str(t): {hex(k): n for k, n in c.items()} for t, c in mod_hits.items()},
            "leaf": {str(t): dict(c) for t, c in leaf.items()}}
    (Path(sys.argv[5]) if len(sys.argv) > 5 else Path("thread-profile.json")).write_text(json.dumps(dump))
    print(f"{rounds} rounds over {secs:.1f}s")
    order = sorted(handles, key=lambda t: (-("present-hook" in marks[t]), -cpu[t]))
    for t in order[:show]:
        tot = total[t] or 1
        print(f"tid {t:6d} cpu {100 * cpu[t] / secs / 1000:5.1f}%  {' '.join(sorted(marks[t]))}")
        print("      by module: " + ", ".join(f"{k} {100 * n / tot:.0f}%" for k, n in bymod[t].most_common(8)))
        for k, n in where[t].most_common(25 if "present-hook" in marks[t] else 5):
            print(f"      {100 * n / tot:5.1f}%  {k}")


if __name__ == "__main__":
    main()
