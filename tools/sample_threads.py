"""Poor-man's sampling profiler for a running process (no admin, no ETW).

Suspends each thread briefly, reads RIP and [RSP], resumes. Reports per-thread CPU
time and the hottest locations; a system-DLL sample is shown with its export name and
the caller found at [RSP] (exact for syscall stubs), so waits and yields are visible.

Usage: python tools/sample_threads.py <pid> [seconds=5] [hz=500] [top=25] [owner-module-substring]
With an owner module (e.g. d2vr_render_probe), samples outside it are also attributed to the first return address into it
found in the top 32 KB of the stack (a heuristic: a stale stack slot can be named), listed as module RVAs per busy thread.
"""
import bisect, collections, ctypes, ctypes.wintypes as w, os, sys, time
try:
    import pefile
except ImportError:  # optional: export tables are parsed directly without it
    pefile = None
import struct

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)


def configure_api():
    signatures = (
        (k32, 'OpenThread', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        (k32, 'OpenProcess', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        (k32, 'CloseHandle', [w.HANDLE], w.BOOL),
        (k32, 'CreateToolhelp32Snapshot', [w.DWORD, w.DWORD], w.HANDLE),
        (k32, 'Thread32First', [w.HANDLE, ctypes.c_void_p], w.BOOL),
        (k32, 'Thread32Next', [w.HANDLE, ctypes.c_void_p], w.BOOL),
        (k32, 'SuspendThread', [w.HANDLE], w.DWORD),
        (k32, 'ResumeThread', [w.HANDLE], w.DWORD),
        (k32, 'GetThreadContext', [w.HANDLE, ctypes.c_void_p], w.BOOL),
        (k32, 'GetThreadTimes', [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4, w.BOOL),
        (k32, 'ReadProcessMemory', [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)], w.BOOL),
        (psapi, 'EnumProcessModulesEx', [w.HANDLE, ctypes.POINTER(w.HMODULE), w.DWORD, ctypes.POINTER(w.DWORD), w.DWORD], w.BOOL),
        (psapi, 'GetModuleBaseNameW', [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD], w.DWORD),
        (psapi, 'GetModuleInformation', [w.HANDLE, w.HMODULE, ctypes.c_void_p, w.DWORD], w.BOOL),
    )
    for api, name, args, result in signatures:
        function = getattr(api, name)
        function.argtypes, function.restype = args, result


configure_api()
THREAD_ALL = 0x0002 | 0x0008 | 0x0010 | 0x0040  # suspend/resume, get context, query info
SYSTEM = ('ntdll.dll', 'kernelbase.dll', 'kernel32.dll', 'win32u.dll')


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD), ('th32OwnerProcessID', w.DWORD),
                ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]


class CONTEXT(ctypes.Structure):  # x64 CONTEXT layout up to Rip; padded to full size
    _fields_ = [('P', ctypes.c_uint64 * 6), ('ContextFlags', w.DWORD), ('MxCsr', w.DWORD), ('seg', w.WORD * 6), ('EFlags', w.DWORD),
                ('Dr', ctypes.c_uint64 * 6), ('Gpr', ctypes.c_uint64 * 16), ('Rip', ctypes.c_uint64), ('rest', ctypes.c_byte * 976)]


class MODINFO(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('size', w.DWORD), ('entry', ctypes.c_void_p)]


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(4, 0)
    if snap == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), 'Cannot snapshot threads')
    try:
        e = THREADENTRY32(); e.dwSize = ctypes.sizeof(e); out = []
        ok = k32.Thread32First(snap, ctypes.byref(e))
        while ok:
            if e.th32OwnerProcessID == pid: out.append(e.th32ThreadID)
            ok = k32.Thread32Next(snap, ctypes.byref(e))
        return out
    finally:
        k32.CloseHandle(snap)


def modules(hp):
    arr = (w.HMODULE * 1024)(); need = w.DWORD(); mods = []
    if not psapi.EnumProcessModulesEx(hp, arr, ctypes.sizeof(arr), ctypes.byref(need), 3) or need.value > ctypes.sizeof(arr):
        raise OSError(ctypes.get_last_error(), 'Cannot enumerate modules within bound')
    for m in arr[:need.value // ctypes.sizeof(w.HMODULE)]:
        if not m: continue
        name = ctypes.create_unicode_buffer(260); psapi.GetModuleBaseNameW(hp, w.HMODULE(m), name, 260)
        mi = MODINFO(); psapi.GetModuleInformation(hp, w.HMODULE(m), ctypes.byref(mi), ctypes.sizeof(mi))
        mods.append((mi.base or 0, mi.size, name.value))
    return sorted(mods)


def export_table(path):
    """[(rva, name)] of a PE's named exports, sorted (pefile when installed, else a direct parse of the export directory)."""
    if pefile:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
        return sorted((e.address, (e.name or b'?').decode()) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols)
    data = open(path, 'rb').read()
    pe_off = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe_off + 6)[0]
    opt_size = struct.unpack_from('<H', data, pe_off + 20)[0]
    opt = pe_off + 24
    dd = opt + (112 if struct.unpack_from('<H', data, opt)[0] == 0x20b else 96)
    exp_rva, exp_size = struct.unpack_from('<II', data, dd)
    secs = [struct.unpack_from('<IIII', data, opt + opt_size + i * 40 + 8) for i in range(nsec)]  # vsize, va, rawsize, rawptr
    def off(rva):
        for vs, va, rs, rp in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + rp
        raise ValueError(rva)
    if not exp_rva:
        return []
    e = off(exp_rva)
    nnames = struct.unpack_from('<I', data, e + 24)[0]
    funcs, names, ords = struct.unpack_from('<III', data, e + 28)
    result = []
    for i in range(nnames):
        o = off(struct.unpack_from('<I', data, off(names) + 4 * i)[0])
        ordinal = struct.unpack_from('<H', data, off(ords) + 2 * i)[0]
        func = struct.unpack_from('<I', data, off(funcs) + 4 * ordinal)[0]
        if exp_rva <= func < exp_rva + exp_size:
            continue  # a forwarder ("NTDLL.RtlFoo"): a string inside the export directory, not code
        result.append((func, data[o:data.index(b'\0', o)].decode('ascii', 'replace')))
    return sorted(result)


pe_exports = export_table  # v1394's name for the same reader (Hollow's tools)

def exports(mods):
    out = {}
    for b, sz, name in mods:
        if name.lower() in SYSTEM:
            out[name.lower()] = export_table(os.path.join(os.environ['SystemRoot'], 'System32', name))
    return out


def cpu_ms(h):
    c, e, kt, ut = (w.FILETIME() for _ in range(4))
    if not k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)):
        raise OSError(ctypes.get_last_error(), 'Cannot read thread CPU time')
    f = lambda t: (t.dwHighDateTime << 32 | t.dwLowDateTime) / 1e4
    return f(kt) + f(ut)


def code_ranges(hp, base, size, name):
    # (lo, hi, name) of a loaded module's executable sections, read from its headers in the target process.
    hdr = (ctypes.c_ubyte * 4096)()
    if not k32.ReadProcessMemory(hp, ctypes.c_void_p(base), hdr, 4096, None):
        return [(base, base + size, name)]
    raw = bytes(hdr); pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if pe > 4000:
        return [(base, base + size, name)]
    nsec, opt_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
    out = []
    for i in range(nsec):
        sec = pe + 24 + opt_size + 40 * i
        if sec + 40 > len(raw):
            break
        vsize, va = struct.unpack_from('<II', raw, sec + 8); flags = struct.unpack_from('<I', raw, sec + 36)[0]
        if flags & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            out.append((base + va, base + va + vsize, name))
    return out or [(base, base + size, name)]


def owner_caller(hp, rsp, owner, rip=0):
    # The first stack qword (top 32 KB) that points into the owner module: the owner's frame that led here (heuristic).
    if not owner:
        return 0
    if owner[0] == 'any':
        buf = (ctypes.c_uint64 * 4096)(); got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), buf, ctypes.sizeof(buf), ctypes.byref(got)):
            return 0
        for i in range(got.value // 8):
            for lo, hi, name in owner[1]:
                if lo <= buf[i] < hi and not (lo <= rip < hi):
                    return '%s+%x' % (name, buf[i] - lo)
        return 0
    buf = (ctypes.c_uint64 * 4096)()
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), buf, ctypes.sizeof(buf), ctypes.byref(got)):
        return 0
    lo, hi = owner[0], owner[1]; base = owner[2] if len(owner) > 2 else lo
    for i in range(got.value // 8):
        if lo <= buf[i] < hi:
            return buf[i] - base
    return 0


def sample_thread(hp, h, ctx, samples, owner=None, owned=None):
    # Defer Ctrl+C until the suspend/read/resume transaction finishes.
    import signal
    interrupted = []
    previous = signal.getsignal(signal.SIGINT)
    signal.signal(signal.SIGINT, lambda signum, frame: interrupted.append(True))
    suspended = False
    try:
        suspended = k32.SuspendThread(h) != 0xFFFFFFFF
        if suspended:
            ctx.ContextFlags = 0x100001
            if k32.GetThreadContext(h, ctypes.addressof(ctx)):
                ret = ctypes.c_uint64()
                if k32.ReadProcessMemory(hp, ctypes.c_void_p(ctx.Gpr[4]), ctypes.byref(ret), 8, None):
                    samples[(ctx.Rip, ret.value)] += 1
                else:
                    samples[(ctx.Rip, 0)] += 1
                if owned is not None and (owner[0] == 'any' or not (owner[0] <= ctx.Rip < owner[1])):
                    owned[(ctx.Rip, owner_caller(hp, ctx.Gpr[4], owner, ctx.Rip))] += 1
    finally:
        try:
            if suspended and k32.ResumeThread(h) == 0xFFFFFFFF:
                raise OSError(ctypes.get_last_error(), 'ResumeThread failed; stop profiling immediately')
        finally:
            signal.signal(signal.SIGINT, previous)
    if interrupted:
        raise KeyboardInterrupt


def main():
    pid = int(sys.argv[1]); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
    hz = float(sys.argv[3]) if len(sys.argv) > 3 else 500; top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
    owner_name = sys.argv[5].lower() if len(sys.argv) > 5 else ''
    if pid <= 0 or not 0 < secs <= 30 or not 0 < hz <= 500 or not 1 <= top <= 100:
        raise ValueError('Positive PID, seconds <=30, Hz <=500 and top <=100 required')
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError('Win64 Python required')
    from record_live_session import low_priority
    low_priority()
    hp = k32.OpenProcess(0x0410, False, pid)
    if not hp:
        raise OSError(ctypes.get_last_error(), 'Cannot open process')
    hs = {}
    try:
        mods = modules(hp); ex = exports(mods)
        owner = next(((b, b + sz) for b, sz, name in mods if owner_name and owner_name in name.lower()), None)
        if owner_name == 'any':  # the first return address into any other module's code (system DLLs skipped)
            owner = ('any', [r for b, sz, name in mods if name.lower() not in SYSTEM for r in code_ranges(hp, b, sz, name)])
        elif owner:  # only its code section: a data pointer on the stack is not a caller
            hdr = (ctypes.c_ubyte * 4096)(); k32.ReadProcessMemory(hp, ctypes.c_void_p(owner[0]), hdr, 4096, None)
            raw = bytes(hdr); pe = struct.unpack_from('<I', raw, 0x3c)[0]
            nsec, opt_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
            for i in range(nsec):
                sec = pe + 24 + opt_size + 40 * i
                if raw[sec:sec + 5] == b'.text':
                    vsize, va = struct.unpack_from('<II', raw, sec + 8); owner = (owner[0] + va, owner[0] + va + vsize, owner[0]); break
        for t in threads(pid):
            h = k32.OpenThread(THREAD_ALL, False, t)
            if h: hs[t] = h
        cpu0 = {t: cpu_ms(h) for t, h in hs.items()}
        buf = ctypes.create_string_buffer(ctypes.sizeof(CONTEXT) + 16)
        ctx = CONTEXT.from_address((ctypes.addressof(buf) + 15) & ~15)
        samples = collections.defaultdict(collections.Counter); n = 0
        owned = collections.defaultdict(collections.Counter)
        end = time.perf_counter() + secs
        while time.perf_counter() < end:
            for t, h in hs.items():
                sample_thread(hp, h, ctx, samples[t], owner, owned[t] if owner else None)
            n += 1; time.sleep(1 / hz)
        cpu = {t: cpu_ms(h) - cpu0[t] for t, h in hs.items()}

        def where(rip):
            i = bisect.bisect_right(mods, (rip, 1 << 62, '')) - 1
            if i >= 0 and mods[i][0] <= rip < mods[i][0] + mods[i][1]: return mods[i][2], rip - mods[i][0]
            return '?', rip

        def sym(rip):
            name, off = where(rip); table = ex.get(name.lower())
            if table:
                i = bisect.bisect_right(table, (off, chr(0x10ffff))) - 1
                if i >= 0: return '%s!%s' % (name[:8], table[i][1])
            return '%s+%x' % (name[:12], off)

        print('sample rounds %d over %.1fs; busiest threads:' % (n, secs))
        for t in sorted(cpu, key=lambda t: -cpu[t])[:8]:
            tot = sum(samples[t].values()) or 1; mc = collections.Counter(); sc = collections.Counter()
            for (rip, r), c in samples[t].items():
                name, _ = where(rip); mc[name] += c
                sc[sym(rip) + (' <- ' + sym(r) if name.lower() in SYSTEM else '')] += c
            print('tid %6d cpu %5.1f%%  modules: %s' % (t, 100 * cpu[t] / secs / 1000,
                  ', '.join('%s %.0f%%' % (m, 100 * c / tot) for m, c in mc.most_common(5))))
            for r, c in sc.most_common(top if t == max(cpu, key=cpu.get) else 5): print('      %-72s %.1f%%' % (r, 100 * c / tot))
            if owner:
                oc = collections.Counter()
                for (rip, rva), c in owned[t].items():
                    if rva: oc['%s <- %s' % (sym(rip).split('+')[0].split('!')[0], rva if isinstance(rva, str) else 'owner+%x' % rva)] += c
                for r, c in oc.most_common(12): print('        via %-66s %.1f%%' % (r, 100 * c / tot))
    finally:
        for h in hs.values():
            k32.CloseHandle(h)
        k32.CloseHandle(hp)


if __name__ == '__main__':
    main()
