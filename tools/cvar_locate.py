"""Locate cvar objects (RVA) by name in the pinned Dishonored2.exe via their registration stubs.

Registration stubs look like:  lea rax,[help] ; lea r8,[default] ; lea rdx,[name] ; lea rcx,[cvar] ; ...
Usage: python tools/cvar_locate.py name [name ...]
Optionally --pid PID reads the live int/float values (read-only).
"""
import argparse
import struct
import numpy as np

EXE = r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\Dishonored2.exe'


def sections(exe):
    pe = struct.unpack_from('<I', exe, 0x3c)[0]
    n = struct.unpack_from('<H', exe, pe + 6)[0]
    opt = struct.unpack_from('<H', exe, pe + 20)[0]
    out = []
    for k in range(n):
        o = pe + 24 + opt + k * 40
        vs, va, rs, rp = struct.unpack_from('<IIII', exe, o + 8)
        out.append((exe[o:o + 8].rstrip(b'\0'), va, rs, rp))
    return out


def locate(names):
    exe = open(EXE, 'rb').read()
    secs = sections(exe)
    def rva(off):
        for _, va, rs, rp in secs:
            if rp <= off < rp + rs: return off - rp + va
    text = [s for s in secs if s[0] == b'.text'][0]
    _, tva, trs, trp = text
    b = np.frombuffer(exe[trp:trp + trs], np.uint8)
    # all `lea rdx,[rip+X]` (48 8D 15) and `lea rcx,[rip+X]` (48 8D 0D) targets
    def leas(modrm):
        idx = np.nonzero((b[:-7] == 0x48) & (b[1:-6] == 0x8D) & (b[2:-5] == modrm))[0]
        rel = b[idx + 3].astype(np.int64) | (b[idx + 4].astype(np.int64) << 8) | (b[idx + 5].astype(np.int64) << 16) | (b[idx + 6].astype(np.int64) << 24)
        rel = np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)
        return idx, tva + idx + 7 + rel
    rdx_i, rdx_t = leas(0x15)
    rcx_i, rcx_t = leas(0x0D)
    result = {}
    for name in names:
        pat = name.encode() + b'\0'
        start = 0
        found = None
        while True:
            off = exe.find(pat, start)
            if off < 0: break
            if off == 0 or exe[off - 1] == 0:
                srva = rva(off)
                for i in rdx_i[rdx_t == srva]:
                    nxt = rcx_i[(rcx_i > i) & (rcx_i < i + 24)]
                    if len(nxt):
                        found = int(rcx_t[np.searchsorted(rcx_i, nxt[0])])
                        break
            if found: break
            start = off + 1
        result[name] = found
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('names', nargs='+')
    ap.add_argument('--pid', type=int)
    a = ap.parse_args()
    found = locate(a.names)
    read = None
    if a.pid:
        from scan_body_material import attach
        base, read = attach(a.pid)
    for name, r in found.items():
        line = '%-36s %s' % (name, hex(r) if r else 'NOT FOUND')
        if r and read:
            raw = read(base + r, 0x38)
            ptr = struct.unpack_from('<Q', raw, 0x30)[0]
            check = read(ptr, 48).split(b'\0')[0].decode(errors='replace') if ptr else '?'
            line += '  name@+0x30=%s int=%d float=%g str=%s' % (check, *struct.unpack_from('<if', raw, 0x28), raw[0x10:0x20].split(b'\0')[0])
        print(line)


if __name__ == '__main__':
    main()
