"""Offline checks: synthetic PE fixtures, never an installed game's files."""
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from probe_game import PE, SIGNATURES, inspect_exe, vdf


def expect_rejected(operation):
    try:
        operation()
    except ValueError:
        return
    raise AssertionError("Invalid binary/manifest accepted")


def fixture() -> bytearray:
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 3, 0, 0, 0, 240, 0x22)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<II", data, optional + 56, 0x4000, 0x200)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 120, 0x2000, 40)
    for i, (name, flags) in enumerate(((b".text", 0x60000020), (b".rdata", 0x40000040), (b".data", 0xC0000040))):
        struct.pack_into("<8sIIIIIIHHI", data, 0x188 + i*40,
                         name, 0x200, 0x1000*(i+1), 0x200, 0x200*(i+1), 0, 0, 0, 0, flags)
    struct.pack_into("<IIIII", data, 0x400, 0x2080, 0, 0, 0x2040, 0x20A0)
    data[0x440:0x44A] = b"d3d11.dll\0"
    tokens = SIGNATURES["ansel_camera_xyz_store"].split()
    signature = bytes(0x21 if t == "??" else int(t, 16) for t in tokens)
    data[0x200:0x200 + len(signature)] = signature
    struct.pack_into("<i", data, 0x204, 0x3000-0x1008)
    return data


def main():
    manifest = vdf('"AppState" { "appid" "403640" "path" "C:\\\\Games" "url" "https://example.com" } // comment')
    assert manifest["AppState"]["path"] == "C:\\Games"
    assert manifest["AppState"]["url"] == "https://example.com"
    for malformed in ('"root" {', '}', '"key"', '"a" "1" "a" "2"', 'root "a"'):
        expect_rejected(lambda: vdf(malformed))
    data = fixture()
    pe = PE(data)
    assert pe.offset(0x1000) == 0x200 and pe.offset(0x3000, 32) == 0x600
    assert pe.imports() == ["d3d11.dll"]
    assert len(pe.signature_matches(SIGNATURES["ansel_camera_xyz_store"])) == 1
    expect_rejected(lambda: pe.offset(0x1200))
    expect_rejected(lambda: pe.bytes(len(data)-1, 2))
    pattern = SIGNATURES["ansel_camera_xyz_store"]
    data[0x300:0x300 + len(pattern.split())] = data[0x200:0x200 + len(pattern.split())]
    assert len(PE(data).signature_matches(pattern)) == 2
    data[0x500:0x500 + len(pattern.split())] = data[0x200:0x200 + len(pattern.split())]
    assert len(PE(data).signature_matches(pattern)) == 2  # non-executable match ignored
    with tempfile.TemporaryDirectory() as folder:
        exe = Path(folder) / "synthetic.exe"
        exe.write_bytes(fixture())
        report = inspect_exe(exe)
        camera = report["camera_research"]["ansel_camera_xyz_store"]
        assert camera["candidate_camera_struct"]["rva"] == 0x3000
        assert report["compatibility"] == "unverified" and not camera["validated_for_patching"]
        assert len(report["sha256"]) == 64
    expect_rejected(lambda: PE(b"MZ"))
    broken = fixture()
    struct.pack_into("<I", broken, 0x188+16, len(broken))
    expect_rejected(lambda: PE(broken))
    broken = fixture()
    struct.pack_into("<I", broken, 0x98+124, 20)
    expect_rejected(lambda: PE(broken).imports())
    with_rtti = fixture()
    struct.pack_into("<II", with_rtti, 0x98+136, 0x2140, 12)
    struct.pack_into("<III", with_rtti, 0x540, 0x1000, 0x1100, 0x2150)
    type_name = b".?AVTestCamera@@\0"
    with_rtti[0x620:0x620+len(type_name)] = type_name
    struct.pack_into("<IIIIII", with_rtti, 0x580, 1, 0, 0, 0x3010, 0x2198, 0x2180)
    struct.pack_into("<QQ", with_rtti, 0x5A0, 0x140002180, 0x140001000)
    pe = PE(with_rtti)
    assert pe.function_at(0x1050)["begin_rva"] == 0x1000
    assert pe.function_at(0x1100) is None
    tables = pe.rtti_vtables("TestCamera")
    assert len(tables) == 1 and tables[0]["vtable_rva"] == 0x21A8
    assert tables[0]["first_virtual_methods"][0]["rva"] == 0x1000
    assert not tables[0]["live_object_verified"]
    call_data = fixture()
    call_data[0x250] = 0xe8
    struct.pack_into("<i", call_data, 0x251, 0x1000-0x1055)
    call_data[0x450] = 0xe8
    struct.pack_into("<i", call_data, 0x451, 0x1000-0x2055)
    callers = PE(call_data).relative_call_candidates(0x1000)
    assert len(callers) == 1 and callers[0]["rva"] == 0x1050
    assert not callers[0]["instruction_boundary_verified"]
    exported = fixture()
    struct.pack_into("<II", exported, 0x98+112, 0x2100, 0x80)
    struct.pack_into("<IIHHIIIIIII", exported, 0x500, 0,0,0,0,0,1,1,1,0x2140,0x2144,0x2148)
    struct.pack_into("<IIH", exported, 0x540, 0x1050,0x2150,0)
    exported[0x550:0x55b] = b"StartProbe\0"
    assert PE(exported).code_export("StartProbe") == 0x1050
    expect_rejected(lambda: PE(exported).code_export("Missing"))
    struct.pack_into("<H", exported, 0x548, 1)
    expect_rejected(lambda: PE(exported).code_export("StartProbe"))
    struct.pack_into("<H", exported, 0x548, 0)
    struct.pack_into("<I", exported, 0x540, 0x2150)
    expect_rejected(lambda: PE(exported).code_export("StartProbe"))
    print("PASS: manifest parsing, PE bounds/imports, executable-only scans, duplicate matches, RIP target, unverified status")


if __name__ == "__main__":
    main()
