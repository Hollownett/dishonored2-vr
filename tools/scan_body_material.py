"""Read-only: locate where the player body's render surface references its material.

Searches the owned body mesh surface (and the body model object) for pointers whose target,
or a pointer one level further, contains the material name. No writes, no engine calls.
Usage: python tools/scan_body_material.py --pid <pid> [--needle player_body_hidden]
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import struct
from pathlib import Path
from inspect_native_hands import hand_state
from rig_fields import animation_snapshot, model_geometry_layout, pointer

BASE_SIZE = 0x41cb000


def attach(pid):
    k = c.WinDLL('kernel32', use_last_error=True)

    class Module(c.Structure):
        _fields_ = [('dwSize', w.DWORD), ('id', w.DWORD), ('pid', w.DWORD), ('g', w.DWORD), ('p', w.DWORD),
                    ('base', c.c_void_p), ('size', w.DWORD), ('h', w.HMODULE), ('name', w.WCHAR * 256), ('path', w.WCHAR * 260)]
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(Module)]
    k.OpenProcess.restype = w.HANDLE
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    snap = k.CreateToolhelp32Snapshot(8, pid)
    m = Module(dwSize=c.sizeof(Module))
    if not k.Module32FirstW(snap, c.byref(m)) or m.name.lower() != 'dishonored2.exe' or m.size != BASE_SIZE:
        raise ValueError('Require pinned Dishonored2 image')
    k.CloseHandle(snap)
    process = k.OpenProcess(0x1010, False, pid)  # VM_READ | QUERY_LIMITED_INFORMATION
    if not process:
        raise OSError(c.get_last_error(), 'read-only open failed')

    def read(at, size):
        if not 0x10000 <= at < 0x0000800000000000 - size or not 0 < size <= 32768:
            raise ValueError('bad read')
        buf = c.create_string_buffer(size); got = c.c_size_t()
        if not k.ReadProcessMemory(process, at, buf, size, c.byref(got)) or got.value != size:
            raise OSError('read failed')
        return buf.raw
    return m.base, read


def try_read(read, at, size):
    try:
        return read(at, size)
    except (OSError, ValueError):
        return None


def scan(read, start, size, needle, depth=2, seen=None):
    """Yield (offset path, address) for pointers in [start, start+size) whose target holds needle."""
    seen = set() if seen is None else seen
    data = try_read(read, start, size)
    if not data:
        return
    for off in range(0, size - 7, 8):
        ptr = struct.unpack_from('<Q', data, off)[0]
        if not 0x10000 <= ptr < 0x7fff00000000 or ptr in seen:
            continue
        seen.add(ptr)
        target = try_read(read, ptr, 0x200)
        if not target:
            continue
        if needle in target:
            yield [hex(off)], ptr, target.index(needle)
        elif depth > 1:
            for path, at, pos in scan(read, ptr, 0x100, needle, depth - 1, seen):
                yield [hex(off)] + path, at, pos


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--needle', default='player_body_hidden')
    args = ap.parse_args()
    base, read = attach(args.pid)
    needle = args.needle.encode()
    meta = json.loads(Path('artifacts/game-probe-rig.json').read_text(encoding='utf-8-sig'))['executable']
    types = {n: meta['engine_type_candidates'][n] for n in ('arkPlayer', 'arkSkinnedRenderModel', 'arkCpntAnimEvolved', 'arkCpntPlayerBody')}
    masks = struct.unpack('<II', read(base + 0x1c70e88 + 16, 8))
    state = hand_state(read, base)
    pose = animation_snapshot(read, pointer(read, state['body'] + 0x58), base, types, masks, 3)
    layout = model_geometry_layout(read, pose, base)
    out = dict(model=hex(pose['model']), skeleton_joints=pose.get('skeleton_joints'), surfaces=[], model_hits=[])
    for s in layout.get('surfaces', []):
        hits = [dict(path=p, target=hex(t), name_offset=hex(o)) for p, t, o in scan(read, s['surface'], 0xb8, needle)]
        out['surfaces'].append(dict(surface=hex(s['surface']), vertices=s.get('vertices'), hits=hits))
    out['model_hits'] = [dict(path=p, target=hex(t), name_offset=hex(o))
                         for p, t, o in scan(read, pose['model'], 0x2000, needle, depth=2)]
    print(json.dumps(out, indent=2))


if __name__ == '__main__':
    main()
