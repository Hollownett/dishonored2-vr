"""Read-only dump of the player's body model material route (what body_renderer.hpp's body_material_route and
billie_native_body read), at the DOTO offsets: model = player+0xf8, flags +0x1c2d, draw records list +0x1c00
(stride 0x80, material at +0), overrides list +0x18a0, descriptor +0x1fd8 -> +8 resource name.

    python tools/doto/body_dump.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live  # noqa: E402


def name_at(p, ptr):
    if not ptr or not (0x10000 <= ptr < 0x7fff00000000):
        return None
    raw = p.read(ptr, 200)
    if not raw:
        return None
    s = raw.split(b'\0')[0]
    return s.decode('ascii', 'replace') if s and all(32 <= c < 127 for c in s) else None


def material_name(p, mat):
    """material -> +8 text pointer (body_renderer.hpp reads value(material+8) then the string)."""
    t = p.u64(mat + 8) if mat else None
    return name_at(p, t)


def lst(p, at):
    raw = p.read(at, 16)
    if not raw:
        return None
    data, cnt, cap = struct.unpack('<QII', raw)
    return data, cnt & 0xffffff, cap >> 8


def dump_model(p, label, model):
    print(f'== {label} model {model:#x} {p.rtti(model)}')
    if not model:
        return
    desc = p.u64(model + 0x1fd8)
    src = p.u64(desc + 8) if desc else None
    print(f'   descriptor +0x1fd8 {desc or 0:#x} resource {name_at(p, src)!r}')
    for off in (0x1c2c, 0x1c2d, 0x1c2e, 0x1c30):
        b = p.read(model + off, 1)
        print(f'   byte +{off:#x} = {b[0] if b else None:#04x}' if b else f'   byte +{off:#x} unreadable')
    rl = p.u64(model + 0x1c00)
    print(f'   records list ptr +0x1c00 {rl or 0:#x}')
    if rl:
        r = lst(p, rl)
        print(f'   records data {r[0]:#x} count {r[1]} cap {r[2]}' if r else '   records unreadable')
        if r:
            for i in range(min(r[1], 12)):
                m = p.u64(r[0] + i * 0x80)
                print(f'     draw[{i}] material {m or 0:#x} {p.rtti(m) if m else ""} {material_name(p, m)!r}')
    o = lst(p, model + 0x18a0)
    print(f'   overrides +0x18a0 data {o[0]:#x} count {o[1]}' if o else '   overrides unreadable')
    if o:
        for i in range(min(o[1], 12)):
            m = p.u64(o[0] + i * 8)
            print(f'     override[{i}] {m or 0:#x} {material_name(p, m)!r}')


def main():
    p = live.Proc()
    b = p.base
    g = p.u64(b + 0x3a72280)
    pl = p.u64(g + 0x327770)
    print(f'player {pl:#x} {p.rtti(pl)}')
    dump_model(p, 'player body (+0xf8)', p.u64(pl + 0xf8))
    dump_model(p, 'player +0x370', p.u64(pl + 0x370))


if __name__ == '__main__':
    main()
