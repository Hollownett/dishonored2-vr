import contextlib
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

from d2vr_linux import cli, modhome, sim  # noqa: E402
from fixtures import make_steam  # noqa: E402

PROXY = b"MZ D2VR-LINUX-PROXY-1 D2VR-LINUX-SIM-1\x00"


def fake_proc(root, pid, comm, argv):
    d = Path(root) / str(pid)
    d.mkdir(parents=True)
    (d / "comm").write_text(comm + "\n")
    (d / "cmdline").write_bytes(b"\0".join(a.encode() for a in argv) + b"\0")


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(__import__("shutil").rmtree, self.tmp, True)
        self.f = make_steam(self.tmp)
        self.proc = self.tmp / "proc"
        self.proc.mkdir()
        self.mod_home = modhome.mod_home_dir(self.f.prefix)
        for sub in (".git", "build", "artifacts"):
            (self.mod_home / sub).mkdir(parents=True)
        (self.mod_home / "build/d2vr_render_probe_arm_v600.dll").write_bytes(b"MZ")
        (self.mod_home / "build/dxgi-probe.cfg").write_text("USER CFG\n")
        (self.f.game_dir / "dinput8.dll").write_bytes(PROXY)
        self.savegame, self.config = sim.save_paths(self.f.prefix)
        (self.savegame / "SLOT01").mkdir(parents=True)
        (self.savegame / "SLOT01/data.bin").write_bytes(b"user slot")
        (self.savegame / "profile.bin").write_bytes(b"user profile")
        (self.savegame / "steam_autocloud.vdf").write_bytes(b"vdf")
        self.config.write_text('seta r_fullscreen "1"\n')
        self.backups = self.tmp / "backups"
        self.runs = self.tmp / "runs"
        self.test_save = self.tmp / "test-save"
        (self.test_save / "SLOT05").mkdir(parents=True)
        (self.test_save / "SLOT05/data.bin").write_bytes(b"tank slot")
        (self.test_save / "profile.bin").write_bytes(b"tank profile")
        self.lines = []

    def args(self, *extra):
        argv = ["--steam-root", str(self.f.steam_root), "sim", "--backup-root", str(self.backups),
                "--out-root", str(self.runs), *extra]
        return argv

    def run_cli(self, *extra, env=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = cli.main(self.args(*extra), home=self.f.home, proc_root=self.proc, sim_env=env)
        return rc, out.getvalue(), err.getvalue()

    def env(self, launch=None):
        clock = {"t": 0.0}

        def sleep(s):
            clock["t"] += s

        e = sim.Env(proc_root=self.proc, sleep=sleep, clock=lambda: clock["t"], launch=launch, out=self.lines.append)
        return e


class Safety(Case):
    def test_refuses_when_game_running(self):
        fake_proc(self.proc, 4242, "Dishonored2.exe", ["Z:\\game\\Dishonored2.exe"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("Dishonored2.exe is running (pid 4242)", err)
        self.assertEqual(list(self.backups.glob("*")) if self.backups.exists() else [], [])

    def test_refuses_when_wrapper_names_the_game(self):
        fake_proc(self.proc, 77, "python3", ["python3", "proton", "waitforexitandrun", "/games/Dishonored2/Dishonored2.exe"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("is running", err)

    def test_refuses_when_vrserver_running(self):
        fake_proc(self.proc, 99, "vrserver", ["/home/u/.steam/SteamVR/bin/linux64/vrserver"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("vrserver is running (pid 99)", err)

    def test_unrelated_text_does_not_trigger(self):
        fake_proc(self.proc, 5, "bash", ["bash", "-c", "echo Dishonored2.exe vrserver"])
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 0, err)

    def test_refuses_with_old_proxy(self):
        (self.f.game_dir / "dinput8.dll").write_bytes(b"MZ D2VR-LINUX-PROXY-1")
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("predates simulated runs", err)

    def test_refuses_leftover_override(self):
        (self.mod_home / "build/linux-probe-override.cfg").write_text("x\n")
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("linux-probe-override.cfg exists", err)

    def test_refuses_with_pending_restore(self):
        (self.backups / "20260101T000000-sim").mkdir(parents=True)
        (self.backups / "20260101T000000-sim" / sim.PENDING).write_text("x")
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn("sim --restore", err)

    def test_dry_run_writes_nothing(self):
        rc, out, err = self.run_cli("--dry-run", "--seconds", "100", "--lead", "60")
        self.assertEqual(rc, 0, err)
        self.assertIn("{pid} 0 6 1 0 0 1 1 0 1 1 100 1 0 0 1 1 1.0 0 0 1 45 90 0 0 2 1 1 1 0 1 0", out)
        self.assertFalse((self.mod_home / "build/linux-probe-override.cfg").exists())
        self.assertFalse(self.backups.exists())
        self.assertEqual((self.mod_home / "build/dxgi-probe.cfg").read_text(), "USER CFG\n")

    def test_session_length_checked(self):
        rc, out, err = self.run_cli("--dry-run", "--lead", "118")
        self.assertEqual(rc, 1)
        self.assertIn("outside 10..120", err)


class Saves(Case):
    def test_backup_swap_restore_roundtrip(self):
        backup = sim.backup_saves(self.f.prefix, self.backups, "T1")
        self.assertTrue((backup / sim.PENDING).exists())
        self.assertEqual((backup / "savegame/profile.bin").read_bytes(), b"user profile")
        self.assertEqual((backup / "dishonored2Config.cfg").read_text(), 'seta r_fullscreen "1"\n')
        sim.swap_in_test_save(self.f.prefix, self.test_save)
        self.assertFalse((self.savegame / "SLOT01").exists())
        self.assertEqual((self.savegame / "SLOT05/data.bin").read_bytes(), b"tank slot")
        self.assertTrue((self.savegame / "steam_autocloud.vdf").exists())  # only SLOT*/profile.bin are swapped
        self.config.write_text("changed by the game\n")
        (self.savegame / "SLOT01").mkdir()
        (self.savegame / "SLOT01/data.bin").write_bytes(b"autosave drift")
        ok, notes = sim.restore_saves(self.f.prefix, backup)
        self.assertTrue(ok, notes)
        self.assertFalse((backup / sim.PENDING).exists())
        self.assertTrue(backup.exists())
        self.assertEqual((self.savegame / "SLOT01/data.bin").read_bytes(), b"user slot")
        self.assertFalse((self.savegame / "SLOT05").exists())
        self.assertEqual(self.config.read_text(), 'seta r_fullscreen "1"\n')

    def test_mtimes_kept(self):
        os.utime(self.savegame / "SLOT01/data.bin", (1_000_000, 1_000_000))
        backup = sim.backup_saves(self.f.prefix, self.backups, "T1")
        self.assertEqual(int((backup / "savegame/SLOT01/data.bin").stat().st_mtime), 1_000_000)

    def test_backup_never_overwrites(self):
        sim.backup_saves(self.f.prefix, self.backups, "T1")
        with self.assertRaises(sim.SimError):
            sim.backup_saves(self.f.prefix, self.backups, "T1")

    def test_failed_verify_keeps_pending(self):
        backup = sim.backup_saves(self.f.prefix, self.backups, "T1")
        (backup / "savegame/SLOT01/data.bin").unlink()  # cannot be restored
        ok, notes = sim.restore_saves(self.f.prefix, backup)
        self.assertFalse(ok)
        self.assertTrue((backup / sim.PENDING).exists())

    def test_cli_restore(self):
        backup = sim.backup_saves(self.f.prefix, self.backups, "T1")
        sim.swap_in_test_save(self.f.prefix, self.test_save)
        (self.mod_home / "build/linux-probe-override.cfg").write_text("x")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, err)
        self.assertIn("restored", out)
        self.assertEqual((self.savegame / "profile.bin").read_bytes(), b"user profile")
        self.assertFalse((self.mod_home / "build/linux-probe-override.cfg").exists())
        self.assertFalse((backup / sim.PENDING).exists())

    def test_restore_refuses_while_game_runs(self):
        sim.backup_saves(self.f.prefix, self.backups, "T1")
        fake_proc(self.proc, 4242, "Dishonored2.exe", ["Dishonored2.exe"])
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 1)
        self.assertIn("refusing to restore", err)


class FullRun(Case):
    def fake_game(self, status=True, returns="0"):
        """launch(): a game process appears, the proxy log shows a session, artifacts are written; SIGTERM ends it."""
        art = self.mod_home / "artifacts"
        state = {"killed": []}

        def launch():
            fake_proc(self.proc, 5000, "Dishonored2.exe", ["Z:\\Dishonored2.exe"])
            (art / "linux-proxy.log").write_text("proxy D2VR-LINUX-PROXY-1\nStartProbe begin x\nStartProbe returned %s\n" % returns)
            if status:
                (art / "xr-live-status.json").write_text(json.dumps({"gameplay": True, "carry": {"held": 2}, "logic_mask": 2}))
            self.during = {
                "override": (self.mod_home / "build/linux-probe-override.cfg").read_text(),
                "script": (self.mod_home / "build/simulated-gameplay-script.cfg").read_text(),
                "auto": (self.mod_home / "build/linux-autocontinue").exists(),
                "slot": (self.savegame / "SLOT05/data.bin").read_bytes() if (self.savegame / "SLOT05").exists() else None,
            }
            self.config.write_text("game rewrote config\n")
            (self.savegame / "SLOT01").mkdir(exist_ok=True)
            (self.savegame / "SLOT01/data.bin").write_bytes(b"autosave drift")

        def kill(pid, sig):
            state["killed"].append((pid, sig))
            import shutil
            shutil.rmtree(self.proc / str(pid), ignore_errors=True)

        e = self.env(launch=launch)
        e.kill = kill
        return e, state

    def test_full_run(self):
        env, state = self.fake_game()
        rc, out, err = self.run_cli("--test-save", str(self.test_save), "--status-field", "carry.held", "--status-field", "nope", env=env)
        text = "\n".join(self.lines)
        self.assertEqual(rc, 0, text + err)
        self.assertEqual(state["killed"], [(5000, signal.SIGTERM)])
        self.assertTrue(self.during["override"].startswith("{pid} 0 6 1"))
        self.assertTrue(self.during["script"].startswith("D2VR_GAMEPLAY_SCRIPT"))
        self.assertTrue(self.during["auto"])
        self.assertEqual(self.during["slot"], b"tank slot")
        # everything put back
        for name in ("linux-probe-override.cfg", "linux-autocontinue", "simulated-gameplay-script.cfg"):
            self.assertFalse((self.mod_home / "build" / name).exists(), name)
        self.assertFalse((self.mod_home / "artifacts/simulated-gameplay-script.json").exists())
        self.assertEqual((self.mod_home / "build/dxgi-probe.cfg").read_text(), "USER CFG\n")
        self.assertEqual((self.savegame / "SLOT01/data.bin").read_bytes(), b"user slot")
        self.assertFalse((self.savegame / "SLOT05").exists())
        self.assertEqual(self.config.read_text(), 'seta r_fullscreen "1"\n')
        self.assertEqual(sim.pending_backups(self.backups), [])
        self.assertEqual(len(list(self.backups.glob("*-sim"))), 1)
        snap = next(self.runs.glob("*"))
        self.assertTrue((snap / "artifacts/xr-live-status.json").exists())
        self.assertTrue((snap / "build/linux-probe-override.cfg").exists())
        self.assertIn("carry.held = 2", text)
        self.assertIn("nope = (absent)", text)
        self.assertIn("result: OK", text)

    def test_failed_start_still_restores(self):
        env, state = self.fake_game(returns="1")
        rc, out, err = self.run_cli(env=env)
        self.assertEqual(rc, 1)
        self.assertIn("StartProbe returned 1", "\n".join(self.lines))
        self.assertEqual((self.savegame / "SLOT01/data.bin").read_bytes(), b"user slot")
        self.assertFalse((self.mod_home / "build/linux-probe-override.cfg").exists())

    def test_game_that_will_not_close_is_forced_then_saves_not_restored_if_still_running(self):
        env, state = self.fake_game()
        env.kill = lambda pid, sig: state["killed"].append((pid, sig))  # never dies
        env.close_timeout = 3
        rc, out, err = self.run_cli(env=env)
        self.assertEqual(rc, 2)
        self.assertEqual([s for _, s in state["killed"]][:2], [signal.SIGTERM, signal.SIGKILL])
        self.assertIn("saves NOT restored", "\n".join(self.lines))
        self.assertEqual(len(sim.pending_backups(self.backups)), 1)
        self.assertFalse((self.mod_home / "build/linux-probe-override.cfg").exists())

    def test_user_files_with_same_names_come_back(self):
        (self.mod_home / "build/linux-settle-seconds").write_text("30\n")
        env, state = self.fake_game()
        rc, out, err = self.run_cli("--settle", "5", env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines))
        self.assertEqual((self.mod_home / "build/linux-settle-seconds").read_text(), "30\n")


class Pure(unittest.TestCase):
    def test_probe_config_matches_launcher_layout(self):
        fields = sim.sim_probe_config(90).split()
        self.assertEqual(len(fields), len(sim.PROBE_FIELDS))
        self.assertEqual(len(modhome_probe()), len(fields))
        named = dict(zip(sim.PROBE_FIELDS, fields))
        self.assertEqual((named["seconds"], named["saved_settings"], named["simulated"], named["gameplay_script"]),
                         ("90", "0", "2", "1"))

    def test_scenario_compiles(self):
        segments = sim.scenario_segments("oil_tank_drop", 60000)
        text, cfg = sim.compile_script(segments, 90)
        self.assertTrue(cfg.startswith("D2VR_GAMEPLAY_SCRIPT 6 90000 "))
        with self.assertRaises(sim.SimError):
            sim.scenario_segments("../x", 0)

    def test_dig(self):
        self.assertEqual(sim.dig({"a": {"b": [1, 2]}}, "a.b.1"), (2, True))
        self.assertEqual(sim.dig({"a": 1}, "a.b"), (None, False))


def modhome_probe():
    # Same positional field count as the headset config (launcher/Probe.cs ProbeConfig, linux/proxy/proxy_core.hpp).
    return ("pid " + "0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0").split()


if __name__ == "__main__":
    unittest.main()
