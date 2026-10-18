import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import steam  # noqa: E402
from fixtures import make_steam, read_fixture, FIXTURES  # noqa: E402

REPO = Path(__file__).resolve().parents[2]


class Tmp(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def root_with(self, fixture):
        root = self.tmp / "Steam"
        (root / "steamapps").mkdir(parents=True)
        shutil.copy(FIXTURES / fixture, root / "steamapps/libraryfolders.vdf")
        return root


class VdfTests(unittest.TestCase):
    def test_parse_vdf_linux_two_libraries(self):
        d = steam.parse_vdf(read_fixture("libraryfolders_linux.vdf"))
        self.assertEqual(d["libraryfolders"]["1"]["path"], "/mnt/steam ssd/SteamLibrary")
        self.assertEqual(d["libraryfolders"]["1"]["apps"]["403640"], "45263695341")

    def test_parse_vdf_windows_paths_unescaped(self):
        d = steam.parse_vdf(read_fixture("libraryfolders_windows.vdf"))
        self.assertEqual(d["libraryfolders"]["0"]["path"], r"C:\Program Files (x86)\Steam")

    def test_parse_vdf_unterminated_raises(self):
        with self.assertRaises(ValueError):
            steam.parse_vdf('"a" { "b" "c')

    def test_parse_vdf_unbalanced_raises(self):
        with self.assertRaises(ValueError):
            steam.parse_vdf('"a" { "b" "c"')
        with self.assertRaises(ValueError):
            steam.parse_vdf('"a" "b" }')

    def test_parse_vdf_comments_and_escaped_quote(self):
        d = steam.parse_vdf('// hi\n"a" { "b" "x\\"y" // tail\n}')
        self.assertEqual(d, {"a": {"b": 'x"y'}})

    def test_acf_installdir_and_build(self):
        a = steam.parse_vdf(read_fixture("appmanifest_403640.acf"))["AppState"]
        self.assertEqual((a["installdir"], a["buildid"]), ("Dishonored2", "17576867"))


class LibraryTests(Tmp):
    def test_library_paths_skips_windows_style(self):
        root = self.root_with("libraryfolders_windows.vdf")
        self.assertEqual(steam.library_paths(root), [root])

    def test_library_paths_old_format(self):
        root = self.root_with("libraryfolders_old.vdf")
        self.assertEqual(steam.library_paths(root), [root, Path("/games/lib")])

    def test_library_paths_new_format_dedups_root(self):
        root = self.root_with("libraryfolders_linux.vdf")
        text = (root / "steamapps/libraryfolders.vdf").read_text().replace(
            "/home/hollownet/.local/share/Steam", str(root))
        (root / "steamapps/libraryfolders.vdf").write_text(text)
        self.assertEqual(steam.library_paths(root), [root, Path("/mnt/steam ssd/SteamLibrary")])

    def test_steam_roots_dedups_symlinks(self):
        home = self.tmp / "h"
        (home / ".local/share/Steam").mkdir(parents=True)
        (home / ".steam").mkdir()
        (home / ".steam/steam").symlink_to(home / ".local/share/Steam")
        (home / ".steam/root").symlink_to(home / ".local/share/Steam")
        self.assertEqual(steam.steam_roots(home), [(home / ".local/share/Steam").resolve()])


class LocateTests(Tmp):
    def test_locate_finds_game_and_prefix_in_second_library_with_space(self):
        f = make_steam(self.tmp)
        inst = steam.locate(f.home)
        self.assertEqual((inst.game_dir, inst.prefix), (f.game_dir, f.prefix))
        self.assertEqual(inst.game_library, f.library)
        self.assertEqual(inst.steam_root, f.steam_root.resolve())

    def test_find_prefix_missing_explains(self):
        f = make_steam(self.tmp, with_prefix=False)
        with self.assertRaisesRegex(steam.SteamError, "run Dishonored 2 once with Proton"):
            steam.locate(f.home)

    def test_find_game_missing_raises(self):
        f = make_steam(self.tmp)
        (f.library / "steamapps/appmanifest_403640.acf").unlink()
        with self.assertRaises(steam.SteamError):
            steam.locate(f.home)

    def test_find_game_exe_missing_raises(self):
        f = make_steam(self.tmp)
        (f.game_dir / "Dishonored2.exe").unlink()
        with self.assertRaises(steam.SteamError):
            steam.locate(f.home)

    def test_find_steamvr_needs_manifest_and_runtime_json(self):
        f = make_steam(self.tmp)
        libs = [f.library]
        self.assertIsNone(steam.find_steamvr(libs))
        vr = f.library / "steamapps/common/SteamVR"
        vr.mkdir(parents=True)
        (vr / "steamxr_linux64.json").write_text("{}")
        self.assertIsNone(steam.find_steamvr(libs))
        (f.library / "steamapps/appmanifest_250820.acf").write_text('"AppState" {}')
        self.assertEqual(steam.find_steamvr(libs), vr)


class HashTests(Tmp):
    def test_check_game_exe_match_and_mismatch(self):
        f = make_steam(self.tmp)
        self.assertEqual(steam.check_game_exe(f.game_dir, f.exe_sha256), f.exe_sha256)
        with self.assertRaisesRegex(steam.SteamError, "Steam updated the game"):
            steam.check_game_exe(f.game_dir)

    def test_game_pin_matches_windows_sources(self):
        self.assertIn(f'GameHash = "{steam.GAME_SHA256}"', (REPO / "launcher/Probe.cs").read_text())
        self.assertIn(steam.GAME_SHA256, (REPO / "src/game_camera.hpp").read_text())


if __name__ == "__main__":
    unittest.main()
