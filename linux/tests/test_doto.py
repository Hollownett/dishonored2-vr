"""Death of the Outsider profile: constants, --game parsing, locate, release check, install, processes, saves, sim and --slot."""
import contextlib
import hashlib
import io
import json
import os
import signal
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import cli, doctor, modhome, sim, steam  # noqa: E402
import test_modhome  # noqa: E402
import test_sim  # noqa: E402
from fixtures import make_steam, read_fixture  # noqa: E402

DOTO_SHA = "be73d979fdb5a6eacbcd385944c6060b97c997d06ab9d72fb39f275a866b955d"
D2_SHA = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"


def make_doto(tmp: Path, exe_bytes=b"MZdoto"):
    """DOTO in the Steam root's own library (library 1 on the author's machine), prefix present."""
    home = Path(tmp) / "dhome"
    root = home / ".local/share/Steam"
    apps = root / "steamapps"
    game_dir = apps / "common/Dishonored_DeathOfTheOutsider"
    game_dir.mkdir(parents=True)
    acf = read_fixture("appmanifest_403640.acf").replace("403640", "614570").replace('"Dishonored2"', '"Dishonored_DeathOfTheOutsider"')
    (apps / "appmanifest_614570.acf").write_text(acf, encoding="utf-8")
    (game_dir / "Dishonored_DO.exe").write_bytes(exe_bytes)
    prefix = apps / "compatdata/614570/pfx"
    (prefix / "drive_c/windows/system32").mkdir(parents=True)
    return type("F", (), dict(home=home, steam_root=root, library=root, game_dir=game_dir, prefix=prefix,
                             exe_sha256=hashlib.sha256(exe_bytes).hexdigest()))


class ProfileTests(unittest.TestCase):
    def test_doto_constants(self):
        p = steam.DOTO
        self.assertEqual((p.id, p.appid, p.exe, p.build, p.sha256), ("doto", "614570", "Dishonored_DO.exe", "17712087", DOTO_SHA))
        self.assertEqual(p.steam_cmd, "steam -applaunch 614570")
        self.assertEqual(p.config_name, "dishonored_DOConfig.cfg")
        self.assertEqual(p.save_base_rel, Path("drive_c/users/steamuser/Saved Games/Arkane Studios/Dishonored_DO/base"))
        self.assertEqual(p.mod_home_rel, Path("drive_c/users/steamuser/AppData/Local/DishonoredVR-DOTO"))
        self.assertEqual(p.mod_home_win, r"C:\users\steamuser\AppData\Local\DishonoredVR-DOTO")
        self.assertEqual(p.process_prefix, "dishonored_do")

    def test_d2_profile_is_todays_values(self):
        p = steam.D2
        self.assertEqual((p.appid, p.exe, p.build, p.sha256), (steam.GAME_APPID, steam.GAME_EXE, steam.GAME_BUILD, steam.GAME_SHA256))
        self.assertEqual(p.sha256, D2_SHA)
        self.assertEqual(p.mod_home_win, modhome.MOD_HOME_WIN)
        self.assertEqual(p.mod_home_rel, modhome.MOD_HOME_REL)
        self.assertEqual(p.save_base_rel, sim.SAVE_BASE_REL)
        self.assertEqual(p.steam_cmd, sim.DEFAULT_STEAM_CMD)
        self.assertEqual(p.config_name, "dishonored2Config.cfg")
        self.assertEqual(steam.PROFILES, {"d2": steam.D2, "doto": steam.DOTO})
        with self.assertRaises(Exception):
            p.exe = "x"  # frozen

    def test_game_option_parsing(self):
        parse = cli._parser().parse_args
        self.assertEqual(parse(["doctor"]).game, "d2")
        self.assertEqual(parse(["--game", "doto", "doctor"]).game, "doto")
        self.assertEqual(parse(["--game", "doto", "sim", "--slot", "SLOT02"]).slot, "SLOT02")
        self.assertIsNone(parse(["sim"]).slot)
        self.assertIsNone(parse(["sim"]).steam_cmd)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parse(["--game", "d3", "doctor"])
        self.assertIs(cli._profile(parse(["--game", "doto", "stop"])), steam.DOTO)

    def test_locate_per_profile(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(__import__("shutil").rmtree, tmp, True)
        f = make_doto(tmp)
        inst = steam.locate(f.home, None, steam.DOTO)
        self.assertEqual(inst.game_dir, f.game_dir)
        self.assertEqual(inst.prefix, f.prefix)
        with self.assertRaisesRegex(steam.SteamError, "403640"):
            steam.locate(f.home)  # D2 is not in this tree
        self.assertEqual(steam.check_game_exe(f.game_dir, f.exe_sha256, steam.DOTO), f.exe_sha256)
        with self.assertRaisesRegex(steam.SteamError, "Dishonored_DO.exe hash .* build 17712087"):
            steam.check_game_exe(f.game_dir, profile=steam.DOTO)  # fake exe is not the pinned build


class ReleaseAndInstall(test_modhome.Case):
    def setUp(self):
        super().setUp()
        self.d = make_doto(self.tmp)
        self.dinst = steam.Install(self.d.steam_root, [], self.d.library, self.d.game_dir, self.d.prefix)

    def test_manifest_sha_checked_per_profile(self):
        doto_zip = self.make_zip(game_sha=DOTO_SHA, name="doto.zip")
        d2_zip = self.make_zip(game_sha=D2_SHA, name="d2.zip")
        self.assertEqual(modhome.read_release_zip(doto_zip, profile=steam.DOTO).dll_name, "d2vr_render_probe_arm_v999.dll")
        self.assertEqual(modhome.read_release_zip(d2_zip).dll_name, "d2vr_render_probe_arm_v999.dll")
        with self.assertRaisesRegex(modhome.InstallError, "different game build"):
            modhome.read_release_zip(d2_zip, profile=steam.DOTO)
        with self.assertRaisesRegex(modhome.InstallError, "different game build"):
            modhome.read_release_zip(doto_zip)  # D2 default

    def test_install_doto_uses_its_mod_home_and_ini(self):
        z = self.make_zip(game_sha=self.d.exe_sha256, name="doto.zip")
        changed = modhome.install(self.dinst, z, self.proxy, self.oxr_zip, self.cache, game_sha256=self.d.exe_sha256,
                                  openxr_source=self.src, profile=steam.DOTO)
        mh = modhome.mod_home_dir(self.d.prefix, steam.DOTO)
        self.assertEqual(mh.name, "DishonoredVR-DOTO")
        self.assertTrue((mh / "build/d2vr_render_probe_arm_v999.dll").is_file())
        self.assertEqual((self.d.game_dir / "d2vr-linux.ini").read_bytes(), b"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR-DOTO\r\n")
        self.assertIn("d2vr-linux.ini", changed)
        self.assertFalse(self.home.exists())  # the D2 mod home is untouched

    def test_install_doto_wrong_exe_refused(self):
        z = self.make_zip(game_sha=DOTO_SHA, name="doto.zip")
        with self.assertRaisesRegex(modhome.InstallError, "Dishonored_DO.exe"):
            modhome.install(self.dinst, z, self.proxy, self.oxr_zip, self.cache, openxr_source=self.src, profile=steam.DOTO)

    def test_doctor_messages_follow_profile(self):
        checks = doctor.run_checks(None, "nope", self.d.home, self.tmp / "proc", [], openxr_source=self.src, profile=steam.DOTO)
        self.assertEqual(checks[0].name, steam.DOTO.name)
        text = doctor.format_checks(checks)
        self.assertIn("614570", text)
        self.assertIn("protontricks 614570 vcrun2022", text)
        self.assertNotIn("403640", text)


class DotoSim(test_sim.Case):
    """The sim cases of test_sim on a DOTO tree (Steam root = library)."""

    def setUp(self):
        super().setUp()
        self.f = make_doto(self.tmp)
        self.mod_home = modhome.mod_home_dir(self.f.prefix, steam.DOTO)
        for sub in (".git", "build", "artifacts"):
            (self.mod_home / sub).mkdir(parents=True)
        (self.mod_home / "build/d2vr_render_probe_arm_v600.dll").write_bytes(b"MZ")
        (self.f.game_dir / "dinput8.dll").write_bytes(test_sim.PROXY)
        self.savegame, self.config = sim.save_paths(self.f.prefix, steam.DOTO)
        for slot, age in (("SLOT01", 3000), ("SLOT02", 1000), ("SLOT03", 2000)):
            (self.savegame / slot).mkdir(parents=True)
            p = self.savegame / slot / "data.bin"
            p.write_bytes(slot.encode())
            os.utime(p, (1_700_000_000 - age, 1_700_000_000 - age))
        (self.savegame / "profile.bin").write_bytes(b"user profile")
        self.config.write_text('seta r_fullscreen "1"\n')

    def args(self, *extra):
        return ["--game", "doto", *super().args(*extra)]

    def fake_game(self, comm="Dishonored_DO.e"):
        art = self.mod_home / "artifacts"
        state = {"killed": [], "mtimes": None}

        def launch():
            test_sim.fake_proc(self.proc, 6000, comm, ["Z:\\Dishonored_DO.exe"])
            (art / "linux-proxy.log").write_text("StartProbe begin x\nStartProbe returned 0\n")
            (art / "xr-live-status.json").write_text(json.dumps({"gameplay": True}))
            state["mtimes"] = {s.name: (s / "data.bin").stat().st_mtime_ns for s in self.savegame.glob("SLOT*")}
            state["cmd"] = self.env_cmd

        def kill(pid, sig):
            state["killed"].append((pid, sig))
            import shutil
            shutil.rmtree(self.proc / str(pid), ignore_errors=True)

        e = self.env(launch=launch)
        e.kill = kill
        self.env_cmd = e
        return e, state

    def test_paths(self):
        self.assertEqual(self.savegame, self.f.prefix / "drive_c/users/steamuser/Saved Games/Arkane Studios/Dishonored_DO/base/savegame")
        self.assertEqual(self.config.name, "dishonored_DOConfig.cfg")
        self.assertEqual(self.config.parent, self.savegame.parent)
        self.assertEqual(self.mod_home, self.f.prefix / "drive_c/users/steamuser/AppData/Local/DishonoredVR-DOTO")

    def test_process_conflicts(self):
        test_sim.fake_proc(self.proc, 11, "Dishonored_DO.e", ["Z:\\g\\Dishonored_DO.exe"])
        self.assertEqual(sim.game_processes(self.proc, profile=steam.DOTO), [11])
        self.assertEqual(sim.game_processes(self.proc), [])  # D2 does not see it
        self.assertEqual(sim.conflicts(self.proc, steam.DOTO), ["Dishonored_DO.exe is running (pid 11)"])
        self.assertEqual(sim.conflicts(self.proc), [])
        test_sim.fake_proc(self.proc, 12, "python3", ["python3", "proton", "run", "/g/Dishonored_DeathOfTheOutsider/Dishonored_DO.exe"])
        self.assertEqual(sorted(sim.game_processes(self.proc, profile=steam.DOTO)), [11, 12])
        self.assertEqual(sim.game_processes(self.proc, strict=True, profile=steam.DOTO), [11])
        test_sim.fake_proc(self.proc, 13, "Dishonored2.exe", ["Z:\\Dishonored2.exe"])
        self.assertEqual(sim.game_processes(self.proc), [13])
        self.assertNotIn(13, sim.game_processes(self.proc, profile=steam.DOTO))

    def test_cli_refuses_while_doto_runs_and_keeps_vrserver_check(self):
        test_sim.fake_proc(self.proc, 4242, "Dishonored_DO.e", ["Z:\\Dishonored_DO.exe"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("Dishonored_DO.exe is running (pid 4242)", err)
        import shutil
        shutil.rmtree(self.proc / "4242")
        test_sim.fake_proc(self.proc, 99, "vrserver", ["/x/vrserver"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("vrserver is running (pid 99)", err)

    def test_d2_process_does_not_block_doto(self):
        test_sim.fake_proc(self.proc, 5, "Dishonored2.exe", ["Z:\\Dishonored2.exe"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 0, err)

    def test_dry_run_slot_and_defaults(self):
        rc, out, err = self.run_cli("--dry-run", "--slot", "SLOT01")
        self.assertEqual(rc, 0, err)
        self.assertIn("slot SLOT01 would be made the newest slot", "\n".join(self.lines) + out)
        self.assertFalse(self.backups.exists() and list(self.backups.glob("*")))

    def test_slot_validation(self):
        for bad, msg in (("SLOT9", "must look like SLOT03"), ("../x", "must look like"), ("SLOT09", "does not exist")):
            rc, out, err = self.run_cli("--dry-run", "--slot", bad)
            self.assertEqual(rc, 1, bad)
            self.assertIn(msg, err)
        rc, out, err = self.run_cli("--dry-run", "--slot", "SLOT05", "--test-save", str(self.test_save))
        self.assertEqual(rc, 0, err)  # the slot is looked up in the test save when one is given
        rc, out, err = self.run_cli("--dry-run", "--slot", "SLOT01", "--test-save", str(self.test_save))
        self.assertEqual(rc, 1)

    def test_full_run_uses_doto_command_and_slot_then_restores_everything(self):
        env, state = self.fake_game()
        before = {p.relative_to(self.savegame): (p.stat().st_mtime_ns, p.read_bytes()) for p in self.savegame.rglob("*") if p.is_file()}
        rc, out, err = self.run_cli("--slot", "SLOT01", env=env)
        text = "\n".join(self.lines)
        self.assertEqual(rc, 0, text + err)
        self.assertEqual(env.steam_cmd, "steam -applaunch 614570")
        self.assertIn("launching: steam -applaunch 614570", text)
        self.assertEqual(state["killed"], [(6000, signal.SIGTERM)])
        during = state["mtimes"]
        self.assertGreater(during["SLOT01"], max(during["SLOT02"], during["SLOT03"]))  # Continue now picks SLOT01
        after = {p.relative_to(self.savegame): (p.stat().st_mtime_ns, p.read_bytes()) for p in self.savegame.rglob("*") if p.is_file()}
        self.assertEqual(after, before)  # bytes and mtimes back
        self.assertEqual(self.config.read_text(), 'seta r_fullscreen "1"\n')
        self.assertEqual(sim.pending_backups(self.backups), [])
        info = json.loads(next(self.backups.glob("*-sim/info.json")).read_text())
        self.assertEqual(info["game"], "doto")
        self.assertTrue(next(self.backups.glob("*-sim")).joinpath("dishonored_DOConfig.cfg").is_file())

    def test_no_slot_leaves_mtimes_alone(self):
        env, state = self.fake_game()
        rc, out, err = self.run_cli(env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertLess(state["mtimes"]["SLOT01"], state["mtimes"]["SLOT02"])

    def test_select_slot_function(self):
        d = sim.select_slot(self.f.prefix, "SLOT03", steam.DOTO, now_ns=1)  # "now" is older than the others: still ends newest
        self.assertEqual(d.name, "SLOT03")
        newest = max(p.stat().st_mtime_ns for p in self.savegame.glob("SLOT0[12]/*"))
        self.assertGreater((d / "data.bin").stat().st_mtime_ns, newest)
        with self.assertRaises(sim.SimError):
            sim.select_slot(self.f.prefix, "SLOT07", steam.DOTO)
        with self.assertRaises(sim.SimError):
            sim.select_slot(self.f.prefix, "x", steam.DOTO)

    def test_restore_refuses_other_games_backup(self):
        backup = sim.backup_saves(self.f.prefix, self.backups, "T1", steam.DOTO)
        ok, notes = sim.restore_saves(self.f.prefix, backup)  # D2 profile
        self.assertFalse(ok)
        self.assertIn("--game doto", " ".join(notes))
        ok, notes = sim.restore_saves(self.f.prefix, backup, profile=steam.DOTO)
        self.assertTrue(ok, notes)

    def test_cli_restore_picks_its_own_games_backups(self):
        sim.backup_saves(self.f.prefix, self.backups, "T1", steam.DOTO)
        (self.savegame / "profile.bin").write_bytes(b"swapped")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, err + "\n".join(self.lines))
        self.assertEqual((self.savegame / "profile.bin").read_bytes(), b"user profile")


class D2Unchanged(test_sim.Case):
    def test_d2_default_command_and_message(self):
        env = self.env()
        rc, out, err = self.run_cli("--dry-run", env=env)
        self.assertEqual(rc, 0, err)
        self.assertEqual(env.steam_cmd, "steam -applaunch 403640")
        self.assertEqual(env.profile, steam.D2)


if __name__ == "__main__":
    unittest.main()
