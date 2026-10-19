import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import modhome, steam  # noqa: E402
from fixtures import make_steam  # noqa: E402

LOADER = b"MZ fake openxr loader"
PROXY_BYTES = b"MZ\x90\x00 D2VR-LINUX-PROXY-1 \x00"
SETTINGS = "7 1 1 1 45 90 2 0.5 2 65 1 0 0 0 0 0\n"


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.f = make_steam(self.tmp)
        self.inst = steam.Install(self.f.steam_root, [], self.f.library, self.f.game_dir, self.f.prefix)
        self.work = self.tmp / "work"
        self.work.mkdir()
        self.cache = self.tmp / "cache"
        self.proxy = self.work / "dinput8.dll"
        self.proxy.write_bytes(PROXY_BYTES)
        self.zip = self.make_zip()
        self.oxr_zip, self.src = self.make_openxr()
        self.home = modhome.mod_home_dir(self.f.prefix)

    def make_zip(self, backslash=True, tamper=False, blocked=False, game_sha=None, name="rel.zip"):
        dll = b"MZ mod dll v999"
        manifest = {"dll_name": "d2vr_render_probe_arm_v999.dll", "sha256": sha(dll),
                    "game_sha256": game_sha or self.f.exe_sha256}
        if blocked:
            manifest["launch_blocked"] = True
            manifest["launch_blocked_reason"] = "nope"
        sep = "\\" if backslash else "/"
        p = self.work / name
        with zipfile.ZipFile(p, "w") as z:
            z.writestr(f"d2vr-v999{sep}d2vr_render_probe_arm_v999.dll", dll + (b"X" if tamper else b""))
            z.writestr(f"d2vr-v999{sep}repair-v999-test-candidate.json", json.dumps(manifest))
            z.writestr(f"d2vr-v999{sep}THIRD_PARTY_NOTICES.md", "notices")
            z.writestr(f"d2vr-v999{sep}README.txt", "extra")
        return p

    def make_openxr(self, name="oxr.zip"):
        p = self.work / name
        with zipfile.ZipFile(p, "w") as z:
            z.writestr("x64/bin/openxr_loader.dll", LOADER)
            z.writestr("x64/lib/other.lib", "x")
        src = {"url": "https://example.invalid/dl/" + name, "zip_sha256": sha(p.read_bytes()),
               "zip_member": "x64/bin/openxr_loader.dll", "dll_sha256": sha(LOADER)}
        return p, src

    def install(self, z=None, **kw):
        args = dict(game_sha256=self.f.exe_sha256, openxr_source=self.src)
        args.update(kw)
        return modhome.install(self.inst, z or self.zip, self.proxy, args.pop("openxr_zip", self.oxr_zip),
                               self.cache, **args)

    def assert_nothing_written(self):
        self.assertFalse(self.home.exists())
        self.assertEqual(sorted(os.listdir(self.f.game_dir)), ["Dishonored2.exe"])


class InstallTests(Case):
    def test_install_backslash_zip_layout(self):
        changed = self.install()
        h = self.home
        for rel in (".git", "build/d2vr_render_probe_arm_v999.dll", "artifacts/repair-v999-test-candidate.json",
                    "third_party/openxr/bin/openxr_loader.dll", "third_party/openxr/bin/LICENSE",
                    "third_party/openxr/bin/source.json", "build/fsr4.off", "THIRD_PARTY_NOTICES.md"):
            self.assertTrue((h / rel).exists(), rel)
        self.assertTrue((h / ".git").is_dir())
        self.assertEqual((h / "third_party/openxr/bin/openxr_loader.dll").read_bytes(), LOADER)
        self.assertEqual((self.f.game_dir / "d2vr-linux.ini").read_bytes(),
                         b"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR\r\n")
        self.assertTrue(modhome.is_our_proxy(self.f.game_dir / "dinput8.dll"))
        self.assertIn("dinput8.dll", changed)
        self.assertFalse(list(h.rglob("*.tmp")))

    def test_install_forward_slash_zip(self):
        self.install(self.make_zip(backslash=False, name="fwd.zip"))
        self.assertTrue((self.home / "build/d2vr_render_probe_arm_v999.dll").exists())

    def test_install_idempotent(self):
        self.install()
        paths = [p for p in list(self.home.rglob("*")) + list(self.f.game_dir.iterdir()) if p.is_file()]
        before = {p: p.stat().st_mtime_ns for p in paths}
        self.assertEqual(self.install(), [])
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in paths})

    def test_install_removes_older_mod_dlls_keeps_settings(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "d2vr_render_probe_arm_v998.dll").write_bytes(b"old")
        (b / "vr-settings.cfg").write_text(SETTINGS)
        self.install()
        self.assertFalse((b / "d2vr_render_probe_arm_v998.dll").exists())
        self.assertTrue((b / "d2vr_render_probe_arm_v999.dll").exists())
        self.assertEqual((b / "vr-settings.cfg").read_text(), SETTINGS)

    def test_install_wrong_game_hash_writes_nothing(self):
        with self.assertRaisesRegex(modhome.InstallError, "Steam updated the game"):
            modhome.install(self.inst, self.zip, self.proxy, self.oxr_zip, self.cache, openxr_source=self.src)
        self.assert_nothing_written()

    def test_install_tampered_dll_writes_nothing(self):
        with self.assertRaisesRegex(modhome.InstallError, "sha256"):
            self.install(self.make_zip(tamper=True, name="bad.zip"))
        self.assert_nothing_written()

    def test_install_launch_blocked_and_wrong_game_sha_manifest_refused(self):
        with self.assertRaisesRegex(modhome.InstallError, "nope"):
            self.install(self.make_zip(blocked=True, name="b.zip"))
        with self.assertRaisesRegex(modhome.InstallError, "different game build"):
            self.install(self.make_zip(game_sha="0" * 64, name="g.zip"))
        self.assert_nothing_written()

    def test_zip_without_exactly_one_manifest_refused(self):
        p = self.work / "none.zip"
        with zipfile.ZipFile(p, "w") as z:
            z.writestr("a.txt", "x")
        with self.assertRaisesRegex(modhome.InstallError, "manifest"):
            self.install(p)
        self.assert_nothing_written()

    def test_install_refuses_foreign_dinput8(self):
        (self.f.game_dir / "dinput8.dll").write_bytes(b"MZ other loader")
        with self.assertRaisesRegex(modhome.InstallError, "another mod's dinput8.dll"):
            self.install()
        self.assertEqual((self.f.game_dir / "dinput8.dll").read_bytes(), b"MZ other loader")
        self.assertFalse(self.home.exists())

    def test_install_missing_proxy_explains_build(self):
        with self.assertRaisesRegex(modhome.InstallError, "python3 linux/proxy/build.py"):
            modhome.install(self.inst, self.zip, self.work / "nope.dll", self.oxr_zip, self.cache,
                            game_sha256=self.f.exe_sha256, openxr_source=self.src)
        self.assert_nothing_written()

    def test_install_foreign_proxy_build_refused(self):
        other = self.work / "other.dll"
        other.write_bytes(b"MZ not ours")
        with self.assertRaisesRegex(modhome.InstallError, "python3 linux/proxy/build.py"):
            modhome.install(self.inst, self.zip, other, self.oxr_zip, self.cache,
                            game_sha256=self.f.exe_sha256, openxr_source=self.src)
        self.assert_nothing_written()

    def test_install_downloads_loader_when_no_zip_given(self):
        calls = []

        def opener(url, **kw):
            calls.append(url)
            return io.BytesIO(self.oxr_zip.read_bytes())

        modhome.install(self.inst, self.zip, self.proxy, None, self.cache, game_sha256=self.f.exe_sha256,
                        openxr_source=self.write_src(), urlopen=opener)
        self.assertEqual(calls, [self.src["url"]])
        self.assertEqual((self.home / "third_party/openxr/bin/openxr_loader.dll").read_bytes(), LOADER)

    def test_refused_install_never_downloads(self):
        calls = []

        def opener(url, **kw):
            calls.append(url)
            return io.BytesIO(b"")

        for proxy, foreign in ((self.work / "nope.dll", False), (self.proxy, True)):
            if foreign:
                (self.f.game_dir / "dinput8.dll").write_bytes(b"MZ other")
            with self.assertRaises(modhome.InstallError):
                modhome.install(self.inst, self.zip, proxy, None, self.cache, game_sha256=self.f.exe_sha256,
                                openxr_source=self.src, urlopen=opener)
        self.assertEqual(calls, [])

    def write_src(self):
        p = self.work / "source.json"
        p.write_text(json.dumps(self.src))
        (self.work / "LICENSE").write_text("Apache")
        return p


class OpenXrTests(Case):
    def test_openxr_zip_hash_mismatch_refused(self):
        bad = dict(self.src, zip_sha256="0" * 64)
        with self.assertRaisesRegex(modhome.InstallError, "sha256"):
            modhome.read_openxr_loader(bad, self.oxr_zip)

    def test_openxr_dll_hash_mismatch_refused(self):
        bad = dict(self.src, dll_sha256="0" * 64)
        with self.assertRaisesRegex(modhome.InstallError, "sha256"):
            modhome.read_openxr_loader(bad, self.oxr_zip)

    def test_openxr_missing_member_refused(self):
        bad = dict(self.src, zip_member="x64/bin/missing.dll")
        with self.assertRaisesRegex(modhome.InstallError, "missing.dll"):
            modhome.read_openxr_loader(bad, self.oxr_zip)

    def test_fetch_openxr_uses_cache_and_verifies(self):
        calls = []
        data = self.oxr_zip.read_bytes()

        def opener(url, **kw):
            calls.append(url)
            return io.BytesIO(data)

        p = modhome.fetch_openxr_zip(self.src, self.cache, opener)
        self.assertEqual(p, self.cache / "oxr.zip")
        self.assertEqual(modhome.fetch_openxr_zip(self.src, self.cache, opener), p)
        self.assertEqual(len(calls), 1)  # second call hit the cache

        shutil.rmtree(self.cache)
        with self.assertRaisesRegex(modhome.InstallError, "sha256"):
            modhome.fetch_openxr_zip(self.src, self.cache, lambda url, **kw: io.BytesIO(b"garbage"))
        self.assertEqual(os.listdir(self.cache), [])

    def fetch_fails(self, opener, pattern):
        with self.assertRaisesRegex(modhome.InstallError, pattern):
            modhome.fetch_openxr_zip(self.src, self.cache, opener)
        self.assertEqual(os.listdir(self.cache) if self.cache.exists() else [], [])

    def test_fetch_network_error_is_install_error(self):
        def opener(url, **kw):
            raise OSError("offline")
        self.fetch_fails(opener, "--openxr-zip")

    def test_fetch_timeout_is_install_error_and_passes_timeout(self):
        import socket
        seen = {}

        def opener(url, **kw):
            seen.update(kw)
            raise socket.timeout("timed out")
        self.fetch_fails(opener, "timed out")
        self.assertEqual(seen, {"timeout": 30})

    def test_fetch_urlerror_is_install_error(self):
        import urllib.error

        def opener(url, **kw):
            raise urllib.error.URLError("no route")
        self.fetch_fails(opener, "no route")

    def test_fetch_over_size_cap_aborts(self):
        class Endless(io.RawIOBase):
            def read(self, n=-1):
                return b"\0" * n

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        old = modhome.MAX_DOWNLOAD
        modhome.MAX_DOWNLOAD = 1 << 20
        self.addCleanup(setattr, modhome, "MAX_DOWNLOAD", old)
        self.fetch_fails(lambda url, **kw: Endless(), "exceeds")

    def test_fetch_hash_mismatch_leaves_no_cache_file(self):
        self.fetch_fails(lambda url, **kw: io.BytesIO(b"garbage"), "sha256")


class UninstallTests(Case):
    def test_uninstall_removes_only_our_files(self):
        self.install()
        self.assertEqual(sorted(modhome.uninstall(self.f.game_dir)), ["d2vr-linux.ini", "dinput8.dll"])
        self.assertEqual(sorted(os.listdir(self.f.game_dir)), ["Dishonored2.exe"])
        self.assertTrue(self.home.exists())

    def test_uninstall_keeps_foreign_dinput8(self):
        (self.f.game_dir / "dinput8.dll").write_bytes(b"MZ other loader")
        (self.f.game_dir / "d2vr-linux.ini").write_text("x")
        self.assertEqual(modhome.uninstall(self.f.game_dir), ["d2vr-linux.ini"])
        self.assertEqual((self.f.game_dir / "dinput8.dll").read_bytes(), b"MZ other loader")

    def test_uninstall_nothing_installed(self):
        self.assertEqual(modhome.uninstall(self.f.game_dir), [])


class RequestTests(Case):
    def test_write_request_atomic(self):
        p = modhome.write_request(self.home, modhome.START_TRIGGER)
        self.assertEqual(p, self.home / "build/linux-start")
        self.assertTrue(p.read_text().isdigit())
        self.assertFalse(list((self.home / "build").glob("*.tmp")))


if __name__ == "__main__":
    unittest.main()
