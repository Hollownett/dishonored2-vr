"""build\\dinput8.dll (tools\\build-msvc.cmd build_proxy_win) is the Windows build of linux/proxy/dinput8_proxy.cpp: a 64-bit DLL that exports exactly what the
forwarding needs (DirectInput8Create), carries the identity marker the launcher's ProxyInstall checks, resolves the real dinput8 from the system directory,
and agrees with the launcher on the marker and ini names."""
import re
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
data = (root / "build" / "dinput8.dll").read_bytes()
assert data[:2] == b"MZ", "not a PE file"
pe = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe:pe + 4] == b"PE\0\0", "no PE signature"
machine, nsec, _, _, _, optsize, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
assert machine == 0x8664, "not x64"
assert chars & 0x2000, "not a DLL"
opt = pe + 24
assert struct.unpack_from("<H", data, opt)[0] == 0x20B, "not PE32+"
export_rva, export_size = struct.unpack_from("<II", data, opt + 112)
assert export_rva and export_size, "no export table"
sections = []
sec = opt + optsize
for i in range(nsec):
    vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
    sections.append((va, max(vsize, rawsize), raw))


def off(rva):
    for va, size, raw in sections:
        if va <= rva < va + size:
            return rva - va + raw
    raise AssertionError(f"rva {rva:#x} is in no section")


_, _, _, _, _, _, nfuncs, nnames, _, names_rva, _ = struct.unpack_from("<IIHHIIIIIII", data, off(export_rva))
exports = []
for i in range(nnames):
    p = off(struct.unpack_from("<I", data, off(names_rva) + 4 * i)[0])
    exports.append(data[p:data.index(b"\0", p)].decode("ascii"))
assert exports == ["DirectInput8Create"], f"exports {exports}"

marker = b"D2VR-LINUX-PROXY-1"
assert marker in data, "identity marker missing"
assert "GetSystemDirectoryW".encode("utf-16-le") in data or b"GetSystemDirectoryW" in data, "does not resolve the system directory"
assert "dinput8.dll".encode("utf-16-le") in data, "does not name the real dinput8.dll"

cs = (root / "launcher" / "ProxyInstall.cs").read_text(encoding="utf-8")
core = (root / "linux" / "proxy" / "proxy_core.hpp").read_text(encoding="utf-8")
assert f'Marker = "{marker.decode()}"' in cs
assert re.search(r'IniName = "d2vr-linux\.ini"', cs) and 'kIniName[] = L"d2vr-linux.ini"' in core
print("proxy_win_build_check ok: exports", exports, "x64 dll", len(data), "bytes")
