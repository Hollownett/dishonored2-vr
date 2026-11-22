"""Read-only lookup of engine cvars in the running game: finds each name string in the image, every pointer to it in the
writable sections, and dumps the record around that pointer (name, value string, description, flags), so a cvar's live
value can be checked without the console. ReadProcessMemory only.

    python tools/doto/cvar_probe.py in_controlInactiveWindow win_pauseOnFocusLoss
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live  # noqa: E402


def sections(p):
    dos = p.read(p.base, 0x40)
    nt = struct.unpack_from('<I', dos, 0x3c)[0]
    hdr = p.read(p.base + nt, 0x108)
    count = struct.unpack_from('<H', hdr, 6)[0]
    opt = struct.unpack_from('<H', hdr, 0x14)[0]
    raw = p.read(p.base + nt + 0x18 + opt, 40 * count)
    out = []
    for i in range(count):
        name = raw[40 * i:40 * i + 8].split(b'\0')[0].decode('ascii', 'replace')
        vsize, rva = struct.unpack_from('<II', raw, 40 * i + 8)
        chars = struct.unpack_from('<I', raw, 40 * i + 36)[0]
        out.append((name, rva, vsize, chars))
    return out


def read_big(p, addr, n, chunk=1 << 20):
    parts = []
    for at in range(0, n, chunk):
        b = p.read(addr + at, min(chunk, n - at))
        parts.append(b if b else b'\0' * min(chunk, n - at))
    return b''.join(parts)


def cstr(p, a, n=96):
    if not a or not (0x10000 <= a < 0x7fff00000000):
        return None
    b = p.read(a, n)
    return b.split(b'\0')[0].decode('ascii', 'replace') if b else None


def main():
    names = sys.argv[1:] or ['in_controlInactiveWindow', 'win_pauseOnFocusLoss']
    p = live.Proc()
    print(f'{p.name} pid={p.pid} base={p.base:#x}')
    secs = sections(p)
    images = {s[0]: (s[1], read_big(p, p.base + s[1], s[2])) for s in secs if s[0] in ('.rdata', '.data')}
    for name in names:
        needle = name.encode() + b'\0'
        hits = []
        for sname, (rva, blob) in images.items():
            at = blob.find(needle)
            while at >= 0:
                if at == 0 or blob[at - 1] == 0:
                    hits.append(p.base + rva + at)
                at = blob.find(needle, at + 1)
        print(f'{name}: strings at {[hex(h) for h in hits]}')
        for h in hits:
            ptr = struct.pack('<Q', h)
            for sname, (rva, blob) in images.items():
                at = blob.find(ptr)
                while at >= 0:
                    rec = p.base + rva + at
                    print(f'  pointer in {sname} at {rec:#x} (image+{rec - p.base:#x})')
                    raw = p.read(rec - 0x10, 0x60)
                    for off in range(0, 0x60, 8):
                        q = struct.unpack_from('<Q', raw, off)[0]
                        lo, hi = struct.unpack_from('<ii', raw, off)
                        f0, = struct.unpack_from('<f', raw, off)
                        s = cstr(p, q)
                        print(f'    {off - 0x10:+#06x} {q:#018x} i32=({lo},{hi}) f={f0:.3g} {repr(s) if s is not None and len(s) < 80 else ""}')
                    at = blob.find(ptr, at + 1)


if __name__ == '__main__':
    main()
