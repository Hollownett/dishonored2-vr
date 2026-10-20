import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
import time
import unittest
from unittest import mock
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


class FeatureTests(Case):
    def features(self):
        return json.loads((modhome.DEFAULT_FEATURES).read_text())["features"]

    def test_install_seeds_default_features(self):
        self.install()
        b = self.home / "build"
        for f in self.features():
            want = f["kind"] == "enable" and f["default"]
            self.assertEqual((b / f["file"]).exists(), want, f["id"])
        self.assertTrue((b / "launcher-features.json").is_file())
        self.assertTrue((b / "full-body").exists())
        self.assertFalse((b / "power-gestures").exists())  # default false: not created
        self.assertFalse((b / "no-hand-blink").exists())
        self.assertEqual(set(json.loads((b / "launcher-features.json").read_text())["seeded"]),
                         {f["id"] for f in self.features()})

    def test_install_is_idempotent_with_features(self):
        self.install()
        self.assertEqual(self.install(), [])

    def test_disabled_feature_survives_reinstall(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "launcher-features.json").write_text(json.dumps({"version": 1, "off": ["full-body"], "seeded": ["full-body"]}))
        self.install()
        self.assertFalse((b / "full-body").exists())
        self.install()
        self.assertFalse((b / "full-body").exists())

    def test_handmade_no_knob_adopted(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "no-stealth-contact").write_bytes(b"")
        self.install()
        self.assertTrue((b / "no-stealth-contact").exists())
        self.assertIn("stealth-contact", json.loads((b / "launcher-features.json").read_text())["off"])

    def test_handmade_enable_knob_and_unknown_files_kept(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "power-gestures").write_bytes(b"")
        (b / "hud-scale").write_text("0.75")
        self.install()
        self.assertTrue((b / "power-gestures").exists())
        self.assertEqual((b / "hud-scale").read_text(), "0.75")
        self.assertTrue((b / "fsr4.off").exists())

    def test_corrupt_state_file_replaced(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "launcher-features.json").write_text("{garbage")
        self.install()
        self.assertTrue((b / "launcher-features.json.bad").exists())
        self.assertTrue((b / "full-body").exists())

    def test_requires_dll_knob_skipped_when_dll_lacks_it(self):
        lst = self.tmp / "list.json"
        lst.write_text(json.dumps({"version": 1, "features": [
            {"id": "x", "file": "x-knob", "kind": "enable", "label": "x", "help": "x", "default": True, "requires_dll_knob": "x-knob"}]}))
        modhome.apply_default_features(self.home, lst, dll=b"nothing")
        self.assertFalse((self.home / "build/x-knob").exists())
        modhome.apply_default_features(self.home, lst, dll="x-knob".encode("utf-16-le"))
        self.assertTrue((self.home / "build/x-knob").exists())

    def test_features_file_matches_csharp_list(self):
        import re
        fs = self.features()
        ids = [f["id"] for f in fs]
        self.assertEqual(len(ids), len(set(ids)))
        for f in fs:
            self.assertRegex(f["file"], r"^[a-z0-9-]+$")
            self.assertIn(f["kind"], ("enable", "disable"))
            self.assertIsInstance(f["default"], bool)
            self.assertTrue(f["label"] and f["help"])
        self.assertFalse({"corpse-grab"} & set(ids))
        self.assertIn("hold-y-menu", ids)


FIXTURES = Path(__file__).resolve().parents[2] / "tests/fixtures/launcher_features"


class FeatureEvolutionTests(Case):
    def row(self, id, file, kind, default=True):
        return {"id": id, "file": file, "kind": kind, "label": id, "help": id, "default": default}

    def lst(self, *rows, name="l.json"):
        p = self.tmp / name
        p.write_text(json.dumps({"version": 1, "features": list(rows)}))
        return p

    def apply(self, *rows, **kw):
        return modhome.apply_default_features(self.home, self.lst(*rows), **kw)

    def state(self):
        return json.loads((self.home / "build/launcher-features.json").read_text())

    def write_state(self, text):
        b = self.home / "build"
        b.mkdir(parents=True, exist_ok=True)
        (b / "launcher-features.json").write_text(text)

    def test_off_entry_for_unseeded_id_stays_off(self):
        self.write_state('{"version":1,"off":["full-body","hand-blink"],"seeded":[]}')
        modhome.apply_default_features(self.home)
        b = self.home / "build"
        self.assertFalse((b / "full-body").exists())
        self.assertTrue((b / "no-hand-blink").exists())
        self.assertIn("full-body", self.state()["seeded"])
        (b / "full-body").write_bytes(b"")
        modhome.apply_default_features(self.home)
        self.assertFalse((b / "full-body").exists())

    def test_readme_method_turns_feature_off(self):
        self.install()
        b = self.home / "build"
        st = self.state()
        st["off"] = sorted(set(st["off"]) | {"full-body", "vr-glyphs"})
        (b / "launcher-features.json").write_text(json.dumps(st))
        self.install()
        self.assertFalse((b / "full-body").exists())
        self.assertTrue((b / "no-vr-glyphs").exists())

    def test_kind_change_keeps_off(self):
        self.apply(self.row("x", "x-old", "enable"))
        modhome.set_feature(self.home, "x", False, self.lst(self.row("x", "x-old", "enable")))
        self.apply(self.row("x", "x-new", "disable"))
        b = self.home / "build"
        self.assertTrue((b / "x-new").exists())
        self.assertFalse((b / "x-old").exists())
        self.assertIn("x", self.state()["off"])

    def test_file_rename_seeds_new_default(self):
        self.apply(self.row("y", "y-old", "enable"))
        self.apply(self.row("y", "y-new", "enable"))
        self.assertTrue((self.home / "build/y-new").exists())
        self.assertTrue((self.home / "build/y-old").exists())

    def test_removed_row_untouched(self):
        self.apply(self.row("z", "z-knob", "enable"))
        self.apply(self.row("w", "w-knob", "enable"))
        self.assertTrue((self.home / "build/z-knob").exists())
        self.assertIn("z", self.state()["seeded"])

    def test_v1_state_upgrades(self):
        self.write_state('{"version":1,"off":[],"seeded":["full-body"]}')
        modhome.apply_default_features(self.home)
        st = self.state()
        self.assertEqual(st["version"], 2)
        self.assertEqual(st["applied"]["full-body"], {"file": "full-body", "kind": "enable", "off": False})
        self.assertFalse((self.home / "build/full-body").exists())  # seeded earlier: not re-created

    def test_unreadable_state_aborts_untouched(self):
        b = self.home / "build"
        (b / "launcher-features.json").mkdir(parents=True)
        with self.assertRaises(modhome.FeatureError):
            modhome.apply_default_features(self.home)
        self.assertFalse((b / "full-body").exists())

    def test_read_oserror_aborts_untouched(self):
        self.write_state('{"version":1,"off":["full-body"],"seeded":["full-body"]}')
        real = Path.read_bytes
        def boom(self_):
            if self_.name == "launcher-features.json":
                raise PermissionError("locked")
            return real(self_)
        with mock.patch.object(Path, "read_bytes", boom):
            with self.assertRaises(modhome.FeatureError):
                modhome.apply_default_features(self.home)
        self.assertFalse((self.home / "build/launcher-features.json.bad").exists())
        self.assertIn("full-body", self.state()["off"])

    def test_install_survives_bad_feature_list(self):
        with mock.patch.object(modhome, "DEFAULT_FEATURES", self.tmp / "missing.json"):
            with mock.patch.object(sys, "stderr", io.StringIO()) as err:
                changed = self.install()
        self.assertIn("build/d2vr_render_probe_arm_v999.dll", changed)
        self.assertIn("not applied", err.getvalue())

    def test_loader_rejects_bad_names(self):
        for bad in ("../x", "/etc/passwd", "Bad"):
            with self.assertRaises(modhome.FeatureError):
                modhome.load_features(self.lst(self.row("a", bad, "enable")))

    def test_full_body_tuning_survives_off_on(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body").write_text("0.5 0.2")
        modhome.apply_default_features(self.home)
        modhome.set_feature(self.home, "full-body", False)
        self.assertFalse((b / "full-body").exists())
        modhome.set_feature(self.home, "full-body", True)
        self.assertEqual((b / "full-body").read_text(), "0.5 0.2")
        self.assertFalse((b / "full-body.off-by-launcher").exists())

    def test_second_retire_keeps_first_saved_copy(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body").write_text("A")
        modhome.apply_default_features(self.home)
        modhome.set_feature(self.home, "full-body", False)
        (b / "full-body").write_text("B")
        modhome.apply_default_features(self.home)  # off wins: B is set aside too
        self.assertFalse((b / "full-body").exists())
        self.assertEqual((b / "full-body.off-by-launcher").read_text(), "A")
        self.assertEqual((b / "full-body.off-by-launcher.1").read_text(), "B")
        modhome.set_feature(self.home, "full-body", True)
        self.assertEqual((b / "full-body").read_text(), "B")  # newest copy
        self.assertEqual((b / "full-body.off-by-launcher").read_text(), "A")

    def test_restore_never_clobbers_existing_file(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body").write_text("A")
        modhome.apply_default_features(self.home)
        modhome.set_feature(self.home, "full-body", False)
        (b / "full-body").write_text("C")
        modhome.set_feature(self.home, "full-body", True)
        self.assertEqual((b / "full-body").read_text(), "C")
        self.assertTrue((b / "full-body.off-by-launcher").exists())
        # the race: the target appears between the check and the move
        src = b / "x.off-by-launcher"
        src.write_text("S")
        dst = b / "x"
        dst.write_text("D")
        with self.assertRaises(FileExistsError):
            modhome._move_noclobber(src, dst)
        self.assertEqual(dst.read_text(), "D")
        self.assertTrue(src.exists())

    def test_leaving_off_turns_feature_back_on(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body").write_text("0.5 0.2")
        modhome.apply_default_features(self.home)
        modhome.set_feature(self.home, "full-body", False)
        modhome.set_feature(self.home, "hand-blink", False)
        st = self.state()
        st["off"] = []
        (b / "launcher-features.json").write_text(json.dumps(st))
        modhome.apply_default_features(self.home)
        self.assertEqual((b / "full-body").read_text(), "0.5 0.2")
        self.assertFalse((b / "full-body.off-by-launcher").exists())
        self.assertFalse((b / "no-hand-blink").exists())

    def test_renamed_row_migrates_saved_copies(self):
        self.apply(self.row("y", "y-old", "enable"))
        b = self.home / "build"
        (b / "y-old").write_text("T")
        modhome.set_feature(self.home, "y", False, self.lst(self.row("y", "y-old", "enable")))
        self.apply(self.row("y", "y-new", "enable"))
        self.assertFalse((b / "y-old.off-by-launcher").exists())
        self.assertEqual((b / "y-new.off-by-launcher").read_text(), "T")
        modhome.set_feature(self.home, "y", True, self.lst(self.row("y", "y-new", "enable")))
        self.assertEqual((b / "y-new").read_text(), "T")

    def test_version_float_is_corrupt(self):
        self.write_state('{"version":1.0,"off":["full-body"],"seeded":[]}')
        modhome.apply_default_features(self.home)
        self.assertTrue((self.home / "build/launcher-features.json.bad").exists())

    def test_unknown_version_aborts_untouched(self):
        self.write_state('{"version":7,"off":[],"seeded":[]}')
        with self.assertRaises(modhome.FeatureError):
            modhome.apply_default_features(self.home)
        self.assertFalse((self.home / "build/launcher-features.json.bad").exists())
        self.assertFalse((self.home / "build/full-body").exists())

    def test_null_off_never_written_round_trip(self):
        self.write_state('{"version":2,"off":["full-body"],"seeded":["full-body"],"applied":{"full-body":{"file":"full-body","kind":"enable"}}}')
        modhome.set_feature(self.home, "hand-blink", False)
        text = (self.home / "build/launcher-features.json").read_text()
        self.assertNotIn("null", text)
        modhome.apply_default_features(self.home)
        self.assertFalse((self.home / "build/launcher-features.json.bad").exists())
        self.assertIn("full-body", self.state()["off"])
        self.assertFalse((self.home / "build/full-body").exists())
        # a null on read is accepted as unknown
        st = self.state()
        st["applied"]["full-body"]["off"] = None
        (self.home / "build/launcher-features.json").write_text(json.dumps(st))
        modhome.apply_default_features(self.home)
        self.assertFalse((self.home / "build/launcher-features.json.bad").exists())

    def test_bad_archives_not_overwritten(self):
        self.write_state("{g1")
        modhome.apply_default_features(self.home)
        self.write_state("{g2")
        modhome.apply_default_features(self.home)
        b = self.home / "build"
        self.assertEqual((b / "launcher-features.json.bad").read_text(), "{g1")
        self.assertEqual((b / "launcher-features.json.bad.1").read_text(), "{g2")

    def test_restore_picks_newest_by_mtime_with_gap(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body.off-by-launcher").write_text("C")
        (b / "full-body.off-by-launcher.3").write_text("B")
        now = time.time()
        os.utime(b / "full-body.off-by-launcher", (now, now))
        os.utime(b / "full-body.off-by-launcher.3", (now - 7200, now - 7200))
        modhome.set_feature(self.home, "full-body", True)
        self.assertEqual((b / "full-body").read_text(), "C")
        self.assertTrue((b / "full-body.off-by-launcher.3").exists())

    def test_slot_collision_is_typed_error(self):
        b = self.home / "build"
        b.mkdir(parents=True)
        (b / "full-body").write_text("T")
        taken = b / "taken"
        taken.write_text("X")
        with mock.patch.object(modhome, "_free_slot", lambda path: taken):
            with self.assertRaises(modhome.FeatureError):
                modhome.set_feature(self.home, "full-body", False)
        self.assertEqual((b / "full-body").read_text(), "T")
        self.assertEqual(taken.read_text(), "X")

    def test_shared_fixtures(self):
        names = sorted(FIXTURES.glob("*.json"))
        self.assertGreater(len(names), 10)
        for n, p in enumerate(names):
            j = json.loads(p.read_text())
            with self.subTest(p.name):
                if j["kind"] == "list":
                    lp = self.tmp / f"fx{n}.json"
                    lp.write_text(j["content"])
                    if j["expect"] == "ok":
                        modhome.load_features(lp)
                    else:
                        with self.assertRaises(modhome.FeatureError):
                            modhome.load_features(lp)
                elif j["kind"] == "state":
                    shutil.rmtree(self.home, True)
                    self.write_state(j["content"])
                    bad = self.home / "build/launcher-features.json.bad"
                    if j["expect"] == "abort":
                        with self.assertRaises(modhome.FeatureError):
                            modhome.apply_default_features(self.home)
                        self.assertFalse(bad.exists())
                        self.assertFalse((self.home / "build/full-body").exists())
                        self.assertEqual((self.home / "build/launcher-features.json").read_text(), j["content"])
                    else:
                        modhome.apply_default_features(self.home)
                        self.assertEqual(bad.exists(), j["expect"] == "corrupt")
                else:
                    found = modhome.scan_knobs(bytes.fromhex(j["hex"]))
                    for c in j["contains"]:
                        self.assertIn(c, found)
                    for a in j["absent"]:
                        self.assertNotIn(a, found)


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
