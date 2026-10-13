"""Read-only: list pointer fields of a live object with the RTTI class name of each target.

Usage: python tools/rtti_fields.py --pid <pid> --address 0x... [--size 0xb8]
"""
import argparse
import struct
from scan_body_material import attach, try_read

IMAGE_SIZE = 0x41cb000


def rtti_name(read, base, obj):
    """MSVC x64: vtable[-1] -> CompleteObjectLocator; +0x0c = type descriptor RVA; name at +0x10."""
    head = try_read(read, obj, 8)
    if not head:
        return None
    vtable = struct.unpack('<Q', head)[0]
    if not base <= vtable < base + IMAGE_SIZE:
        return None
    col = try_read(read, vtable - 8, 8)
    col = col and struct.unpack('<Q', col)[0]
    if not col or not base <= col < base + IMAGE_SIZE:
        return None
    raw = try_read(read, col, 0x18)
    if not raw or struct.unpack_from('<I', raw, 0)[0] != 1:
        return None
    name = try_read(read, base + struct.unpack_from('<I', raw, 0x0c)[0] + 0x10, 96)
    return name and name.split(b'\0')[0].decode('ascii', 'replace')


def fields(read, base, address, size):
    data = try_read(read, address, size) or b''
    for off in range(0, len(data) - 7, 8):
        ptr = struct.unpack_from('<Q', data, off)[0]
        if 0x10000 <= ptr < 0x7fff00000000:
            yield off, ptr, rtti_name(read, base, ptr)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--address', type=lambda s: int(s, 0), required=True)
    ap.add_argument('--size', type=lambda s: int(s, 0), default=0xb8)
    args = ap.parse_args()
    base, read = attach(args.pid)
    print('self', rtti_name(read, base, args.address))
    for off, ptr, name in fields(read, base, args.address, args.size):
        in_image = base <= ptr < base + IMAGE_SIZE
        print(f'+{off:#06x} {ptr:#x} {"image+" + hex(ptr - base) if in_image else ""} {name or ""}')


if __name__ == '__main__':
    main()
