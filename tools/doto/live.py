"""Read-only live memory access to a running Dishonored_DO.exe (or Dishonored2.exe) for layout checks.

Never writes, suspends or injects: OpenProcess with PROCESS_VM_READ | PROCESS_QUERY_INFORMATION only.

  python tools/doto/live.py views            prepared views + decode with D2 and DOTO renderView layouts
  python tools/doto/live.py game             idGameLocal singleton, player pointer, logic-mask manager
  python tools/doto/live.py rtti 0xADDR      RTTI class of the object at an absolute address
  python tools/doto/live.py fields 0xADDR N  pointer fields of an object with their RTTI classes
"""
import ctypes
import ctypes.wintypes as W
import os
import struct
import sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)
PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400


def find_pid(names=('Dishonored_DO.exe', 'Dishonored2.exe')):
    import subprocess
    out = subprocess.run(['tasklist', '/fo', 'csv', '/nh'], capture_output=True, text=True, errors='replace').stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0] in names:
            return int(parts[1]), parts[0]
    return None, None


class Proc:
    def __init__(self, pid=None):
        if pid is None:
            pid, name = find_pid()
            if pid is None:
                raise SystemExit('no game process')
            self.name = name
        self.pid = pid
        self.h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h:
            raise SystemExit(f'OpenProcess failed {ctypes.get_last_error()}')
        mods = (W.HMODULE * 1)()
        need = W.DWORD()
        psapi.EnumProcessModulesEx(self.h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
        self.base = mods[0]

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            return None
        return buf.raw

    def u64(self, addr):
        b = self.read(addr, 8)
        return struct.unpack('<Q', b)[0] if b else None

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack('<I', b)[0] if b else None

    def f32(self, addr, n=1):
        b = self.read(addr, 4 * n)
        return struct.unpack('<%df' % n, b) if b else None

    def rtti(self, obj):
        vt = self.u64(obj)
        if not vt or not (self.base <= vt < self.base + 0x5000000):
            return None
        col = self.u64(vt - 8)
        if not col:
            return None
        raw = self.read(col, 0x18)
        if not raw or struct.unpack_from('<I', raw, 0)[0] != 1:
            return None
        td = struct.unpack_from('<I', raw, 0x0c)[0]
        name = self.read(self.base + td + 0x10, 96)
        return name.split(b'\0')[0].decode('ascii', 'replace') if name else None


DOTO = {'prepared_views': 0x2c905d0, 'game': 0x3a72280, 'stride': 0x3aa0}
D2 = {'prepared_views': 0x2bc58a0, 'game': 0x39a5290, 'stride': 0x3ab0}


def decode(b, pre, fov, pos, basis):
    hf, vf = struct.unpack_from('<2f', b, pre + fov)
    p = struct.unpack_from('<3f', b, pre + pos)
    m = struct.unpack_from('<9f', b, pre + basis)
    det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6])
    return f'fov=({hf:.3f},{vf:.3f}) pos=({p[0]:.2f},{p[1]:.2f},{p[2]:.2f}) det={det:.3f}'


def cmd_views(p, c):
    hdr = p.read(p.base + c['prepared_views'], 16)
    addr, packed = struct.unpack_from('<QI', hdr, 0)
    count = packed & 0xffffff
    print(f'prepared views at {addr:#x} count={count} packed={packed:#x}')
    for eye in range(min(count, 2)):
        b = p.read(addr + eye * c['stride'], 0x200)
        if not b:
            print('unreadable')
            continue
        print(' viewport', struct.unpack_from('<4i', b, 0))
        print('  D2 layout   ', decode(b, 0x30, 0x9c, 0xa8, 0xcc))
        print('  DOTO -0x10  ', decode(b, 0x30, 0x8c, 0x98, 0xbc))
        for off in range(0x80, 0x150, 4):
            pass


def cmd_game(p, c):
    g = p.u64(p.base + c['game'])
    print(f'game {g:#x} {p.rtti(g)}')
    return g


def main():
    p = Proc()
    c = DOTO if p.name == 'Dishonored_DO.exe' else D2
    print(f'{p.name} pid={p.pid} base={p.base:#x}')
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'views'
    if cmd == 'views':
        cmd_views(p, c)
    elif cmd == 'game':
        cmd_game(p, c)
    elif cmd == 'rtti':
        a = int(sys.argv[2], 0)
        print(p.rtti(a))
    elif cmd == 'fields':
        a = int(sys.argv[2], 0)
        n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x100
        b = p.read(a, n) or b''
        for off in range(0, len(b) - 7, 8):
            v = struct.unpack_from('<Q', b, off)[0]
            r = p.rtti(v) if 0x10000 <= v < 0x7fff00000000 else None
            img = f'image+{v - p.base:#x}' if p.base <= v < p.base + 0x5000000 else ''
            if v and (r or img):
                print(f'+{off:#06x} {v:#x} {img} {r or ""}')


if __name__ == '__main__':
    main()
