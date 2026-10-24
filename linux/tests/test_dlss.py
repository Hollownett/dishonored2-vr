import struct
import sys
import unittest
from pathlib import Path

LINUX = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LINUX))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import dlss, modhome  # noqa: E402
import test_cli  # noqa: E402
import test_modhome  # noqa: E402


def fake_dll(path: Path, version=None):
    """A stand-in nvngx_dlss.dll; with version=(a,b,c,d) it carries a VS_FIXEDFILEINFO block."""
    path.parent.mkdir(parents=True, exist_ok=True)
    data = b"MZ fake"
    if version:
        a, b, c, d = version
        data += b"\0" * 8 + struct.pack("<IIIIII", 0xFEEF04BD, 0x10000, a << 16 | b, c << 16 | d, 0, 0)
    path.write_bytes(data)
    return path


class FinderTests(test_modhome.Case):
    def game(self, lib, rel, version=None):
        return fake_dll(lib / "steamapps/common" / rel / "nvngx_dlss.dll", version)

    def test_dlss_find_writes_wine_path(self):
        dll = self.game(self.f.library, "Cyberpunk 2077/bin/x64")
        (self.home / "build").mkdir(parents=True)
        rc, out, err = test_cli.run(["dlss-find", "--mod-home", str(self.home)], home=self.f.home)
        self.assertEqual(rc, 0, err)
        knob = (self.home / "build/dlss-dll-path").read_text()
        self.assertEqual(knob, "Z:" + str(dll).replace("/", "\\") + "\n")
        self.assertIn("Cyberpunk 2077", out)
        self.assertFalse((self.home / "nvngx_dlss.dll").exists())  # located, never copied

    def test_dlss_find_respects_existing(self):
        self.game(self.f.library, "A")
        (self.home / "build").mkdir(parents=True)
        (self.home / "build/dlss-dll-path").write_text("Z:\\mine\\nvngx_dlss.dll\n")
        rc, out, err = test_cli.run(["dlss-find", "--mod-home", str(self.home)], home=self.f.home)
        self.assertEqual(rc, 0, err)
        self.assertEqual((self.home / "build/dlss-dll-path").read_text(), "Z:\\mine\\nvngx_dlss.dll\n")
        self.assertIn("already", out)
        (self.home / "build/dlss-dll-path").unlink()
        fake_dll(self.home / "nvngx_dlss.dll")
        rc, out, err = test_cli.run(["dlss-find", "--mod-home", str(self.home)], home=self.f.home)
        self.assertEqual(rc, 0, err)
        self.assertFalse((self.home / "build/dlss-dll-path").exists())

    def test_searches_every_library_and_prefers_higher_version(self):
        lo = self.game(self.f.steam_root, "Old", (3, 1, 0, 0))
        hi = self.game(self.f.library, "New", (310, 2, 1, 0))
        libs = dlss.libraries(self.f.home)
        self.assertIn(self.f.library, libs)
        self.assertEqual(dlss.best(dlss.candidates(libs)), hi)
        self.assertEqual(dlss.file_version(lo), (3, 1, 0, 0))
        self.assertEqual(dlss.file_version(fake_dll(self.tmp / "x.dll")), (0, 0, 0, 0))

    def test_unversioned_falls_back_to_newest_then_shortest(self):
        import os
        a = self.game(self.f.library, "A/deeper")
        b = self.game(self.f.library, "B")
        os.utime(a, (1000, 1000)); os.utime(b, (2000, 2000))
        self.assertEqual(dlss.best([a, b]), b)
        os.utime(a, (2000, 2000))
        self.assertEqual(dlss.best([a, b]), b)
        self.assertIsNone(dlss.best([]))

    def test_depth_cap_and_symlinks(self):
        edge = self.game(self.f.library, "B/1/2/3/4")
        self.game(self.f.library, "C/1/2/3/4/5")
        real = self.tmp / "elsewhere"
        fake_dll(real / "nvngx_dlss.dll")
        (self.f.library / "steamapps/common/Linked").symlink_to(real)
        self.assertEqual(dlss.candidates([self.f.library]), [edge])
        for i in range(6):
            self.game(self.f.library, f"g{i}")
        self.assertEqual(len(dlss.candidates([self.f.library], max_files=3)), 3)

    def test_install_finds_dlss(self):
        dll = self.game(self.f.library, "Alan Wake 2")
        t = test_cli.InstallTests("test_install_then_uninstall_roundtrip")
        t.tmp, t.f, t.home, t.work, t.zip, t.proxy, t.oxr_zip, t.src = self.tmp, self.f, self.home, self.work, self.zip, self.proxy, self.oxr_zip, self.src
        t.src_json = self.work / "source.json"
        import json
        t.src_json.write_text(json.dumps(self.src))
        (self.work / "LICENSE").write_text("lic")
        t.proc = self.tmp / "proc"
        with t.patched():
            rc, out, err = test_cli.run(t.argv(), home=self.f.home, proc_root=t.proc)
        self.assertEqual(rc, 0, err)
        self.assertEqual((self.home / "build/dlss-dll-path").read_text(), "Z:" + str(dll).replace("/", "\\") + "\n")
        self.assertIn("DLSS found in Alan Wake 2", out)


if __name__ == "__main__":
    unittest.main()
