"""Who is calling into a hot module? Samples the busiest threads of a process and, for every sample whose RIP is inside
<module>, scans the top of that thread's stack for return addresses into other modules (the callers).

Read-only apart from the brief SuspendThread/ResumeThread of tools/sample_threads.py.

    python tools/stack_callers.py <pid> [seconds=5] [module=D3DCOMPILER_47.dll] [stack_bytes=8192]
"""
import collections
import ctypes
import ctypes.wintypes as w
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import sample_threads as st  # noqa: E402

k32 = st.k32


def main():
    pid = int(sys.argv[1])
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
    target = (sys.argv[3] if len(sys.argv) > 3 else "D3DCOMPILER_47.dll").lower()
    depth = int(sys.argv[4]) if len(sys.argv) > 4 else 8192
    hp = k32.OpenProcess(0x0410, False, pid)
    mods = st.modules(hp)
    bases = [m[0] for m in mods]

    def find(addr):
        import bisect
        i = bisect.bisect_right(bases, addr) - 1
        if i >= 0 and addr < mods[i][0] + mods[i][1]:
            return mods[i]
        return None
    hot = next((m for m in mods if m[2].lower() == target), None)
    if not hot:
        sys.exit(f"{target} is not loaded")
    handles = {}
    for tid in st.threads(pid):
        h = k32.OpenThread(st.THREAD_ALL, False, tid)
        if h:
            handles[tid] = h
    ctx = st.CONTEXT()
    callers = collections.Counter()
    first_callers = collections.Counter()
    hits = collections.Counter()
    end = time.time() + secs
    buf = (ctypes.c_uint64 * (depth // 8))()
    while time.time() < end:
        for tid, h in handles.items():
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            try:
                ctx.ContextFlags = 0x100003
                if not k32.GetThreadContext(h, ctypes.addressof(ctx)):
                    continue
                if not (hot[0] <= ctx.Rip < hot[0] + hot[1]):
                    continue
                hits[tid] += 1
                got = ctypes.c_size_t()
                k32.ReadProcessMemory(hp, ctypes.c_void_p(ctx.Gpr[4]), buf, depth, ctypes.byref(got))
                seen_first = False
                seen = set()
                for v in buf[:got.value // 8]:
                    m = find(v)
                    if not m or m[2].lower() == target or m[2].lower() in ("ntdll.dll", "kernelbase.dll", "kernel32.dll", "ucrtbase.dll"):
                        continue
                    key = f"{m[2]}+{v - m[0]:x}"
                    if key in seen:
                        continue
                    seen.add(key)
                    callers[key] += 1
                    if not seen_first:
                        first_callers[key] += 1
                        seen_first = True
            finally:
                k32.ResumeThread(h)
        time.sleep(0.004)
    print(f"samples inside {target}: {dict(hits)}")
    ntdll = ctypes.WinDLL("ntdll")
    for tid in hits:
        start = ctypes.c_uint64()
        ntdll.NtQueryInformationThread(w.HANDLE(handles[tid]), 9, ctypes.byref(start), 8, None)  # ThreadQuerySetWin32StartAddress
        m = find(start.value)
        print(f"thread {tid} start address: " + (f"{m[2]}+{start.value - m[0]:x}" if m else hex(start.value)))
    if hits:  # one ordered stack of the hottest thread
        tid = hits.most_common(1)[0][0]
        h = handles[tid]
        for _ in range(200):
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                break
            try:
                ctx.ContextFlags = 0x100003
                if k32.GetThreadContext(h, ctypes.addressof(ctx)) and hot[0] <= ctx.Rip < hot[0] + hot[1]:
                    got = ctypes.c_size_t()
                    k32.ReadProcessMemory(hp, ctypes.c_void_p(ctx.Gpr[4]), buf, depth, ctypes.byref(got))
                    chain = []
                    for i, v in enumerate(buf[:got.value // 8]):
                        m = find(v)
                        if m:
                            chain.append(f"rsp+{8 * i:x}: {m[2]}+{v - m[0]:x}")
                    print(f"ordered stack of thread {tid} (module pointers only):")
                    print("\n".join("  " + c for c in chain[:60]))
                    break
            finally:
                k32.ResumeThread(h)
            time.sleep(0.01)
    print("first non-system return address above the hot frame:")
    for k, n in first_callers.most_common(15):
        print(f"  {n:6d}  {k}")
    print("any return address on those stacks:")
    for k, n in callers.most_common(30):
        print(f"  {n:6d}  {k}")


if __name__ == "__main__":
    main()
