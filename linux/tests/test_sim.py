import contextlib
import io
import subprocess
import json
import os
import signal
import sys
import tempfile
import unittest
import unittest.mock
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
        patcher = __import__("unittest").mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(self.tmp / "state")})
        patcher.start()
        self.addCleanup(patcher.stop)

        def real_launcher(*_a, **_k):
            raise AssertionError("a test reached the real Steam launcher; inject a fake with env=self.env(launch=...)")

        for target in (unittest.mock.patch.object(sim.Env, "_launch", real_launcher),
                       unittest.mock.patch.object(sim.Env, "_lock_probe", lambda self: False),
                       unittest.mock.patch.object(sim.subprocess, "Popen", real_launcher)):
            target.start()
            self.addCleanup(target.stop)

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
    def test_refuses_when_session_locked(self):
        e = self.env(launch=lambda: None)
        e.lock_probe = lambda: True
        rc, out, err = self.run_cli(env=e)
        self.assertEqual(rc, 1)
        self.assertIn("session is locked; unlock before running", err)
        self.assertFalse(self.backups.exists())

    def test_locked_session_does_not_stop_a_dry_run(self):
        e = self.env(launch=lambda: None)
        e.lock_probe = lambda: True
        rc, out, err = self.run_cli("--dry-run", env=e)
        self.assertEqual(rc, 0, err)

    def test_unknown_lock_state_warns_and_continues(self):
        e = self.env(launch=lambda: None)
        e.lock_probe = lambda: None
        rc, out, err = self.run_cli("--dry-run", env=e)
        self.assertEqual(rc, 0, err)

    def test_lock_state_from_tools(self):
        def fake(table):
            def run(argv, **_k):
                key = argv[0]
                if key not in table:
                    raise FileNotFoundError(key)
                return subprocess.CompletedProcess(argv, 0, table[key], "")
            return run
        sid = {"XDG_SESSION_ID": "3"}
        self.assertTrue(sim.session_lock_state(fake({"loginctl": "LockedHint=yes\n"}), sid))
        self.assertFalse(sim.session_lock_state(fake({"loginctl": "LockedHint=no\n"}), sid))
        self.assertTrue(sim.session_lock_state(fake({"gdbus": "(true,)\n"}), {}))
        self.assertFalse(sim.session_lock_state(fake({"gdbus": "(false,)\n"}), sid))
        self.assertIsNone(sim.session_lock_state(fake({}), sid))

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


def shutil_error(*a, **k):
    raise RuntimeError("not an OSError")


def fake_pe(tag=b""):
    """Smallest thing is_pe_dll accepts: MZ, e_lfanew=0x40, PE signature, DLL characteristic."""
    hdr = bytearray(0x40 + 24)
    hdr[:2] = b"MZ"
    hdr[0x3C:0x40] = (0x40).to_bytes(4, "little")
    hdr[0x40:0x44] = b"PE\0\0"
    hdr[0x40 + 22:0x40 + 24] = (0x2000).to_bytes(2, "little")
    return bytes(hdr) + tag


class Candidate(FullRun):
    def setUp(self):
        super().setUp()
        self.installed = self.mod_home / "build/d2vr_render_probe_arm_v600.dll"
        self.installed.write_bytes(fake_pe(b"installed"))
        self.original = self.installed.read_bytes()
        cdir = self.tmp / "cand"
        cdir.mkdir()
        self.cand = cdir / "d2vr_render_probe_arm_v600.dll"
        self.cand.write_bytes(fake_pe(b"candidate"))

    def test_candidate_dll_swapped_and_restored(self):
        env, state = self.fake_game()
        seen = {}
        inner = env.launch

        def launch():
            seen["during"] = self.installed.read_bytes()
            inner()

        env.launch = launch
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertEqual(seen["during"], self.cand.read_bytes())
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertEqual(sim.pending_backups(self.backups), [])
        b = next(self.backups.glob("*-sim"))
        self.assertEqual((b / "installed-d2vr_render_probe_arm_v600.dll").read_bytes(), self.original)
        self.assertFalse(list((self.mod_home / "build").glob("*.d2vr-tmp")))

    def test_candidate_restored_after_launch_failure(self):
        def launch():
            self.during_dll = self.installed.read_bytes()
            raise OSError("steam not found")

        env = self.env(launch=launch)
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        self.assertEqual(rc, 1)
        self.assertIn("steam not found", err)
        self.assertEqual(self.during_dll, self.cand.read_bytes())
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_candidate_wrong_name_refused(self):
        wrong = self.tmp / "cand/d2vr_render_probe_arm_v601.dll"
        wrong.write_bytes(fake_pe())
        rc, out, err = self.run_cli("--dry-run", "--candidate-dll", str(wrong))
        self.assertEqual(rc, 1)
        self.assertIn("v601", err)
        self.assertIn("v600", err)
        notpe = self.tmp / "cand/notpe"
        notpe.mkdir()
        bad = notpe / "d2vr_render_probe_arm_v600.dll"
        bad.write_bytes(b"MZ not a dll")
        rc, out, err = self.run_cli("--dry-run", "--candidate-dll", str(bad))
        self.assertEqual(rc, 1)
        self.assertIn("not a PE DLL", err)
        self.assertEqual(self.installed.read_bytes(), self.original)

    def pending(self, stamp="T1", cand=None):
        """An interrupted run: backup + record made, candidate in place."""
        import hashlib
        cand = self.cand.read_bytes() if cand is None else cand
        backup = sim.backup_saves(self.f.prefix, self.backups, stamp)
        sim.backup_dll(backup, self.installed, hashlib.sha256(cand).hexdigest())
        sim.place_atomic(cand, self.installed)
        return backup

    def test_restore_recovers_interrupted_candidate(self):
        backup = self.pending()
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, err + out)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertFalse((backup / sim.PENDING).exists())

    def test_corrupt_backup_gives_no_cp_advice_and_does_not_write(self):
        backup = self.pending()
        (backup / "installed-d2vr_render_probe_arm_v600.dll").write_bytes(b"corrupt")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertNotIn("cp -f", out)
        self.assertIn("fails its hash check", out)
        self.assertEqual(self.installed.read_bytes(), self.cand.read_bytes())
        self.assertTrue((backup / sim.PENDING).exists())

    def test_corrupt_backup_but_destination_already_original_is_success(self):
        backup = self.pending()
        sim.place_atomic(self.original, self.installed)
        (backup / "installed-d2vr_render_probe_arm_v600.dll").write_bytes(b"corrupt")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, out + err)
        self.assertEqual(self.installed.read_bytes(), self.original)

    def test_destination_changed_is_not_overwritten(self):
        import hashlib
        backup = self.pending()
        newer = fake_pe(b"newer install")
        sim.place_atomic(newer, self.installed)
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual(self.installed.read_bytes(), newer)
        self.assertTrue((backup / sim.PENDING).exists())
        self.assertIn(hashlib.sha256(self.cand.read_bytes()).hexdigest(), out)
        self.assertIn(hashlib.sha256(self.original).hexdigest(), out)

    def test_destination_missing_is_not_recreated(self):
        backup = self.pending()
        self.installed.unlink()
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertFalse(self.installed.exists())
        self.assertTrue((backup / sim.PENDING).exists())

    def test_dll_restored_even_if_savegame_missing(self):
        import shutil
        backup = self.pending()
        shutil.rmtree(backup / "savegame")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertTrue((backup / sim.PENDING).exists())

    def test_dll_restored_even_if_info_json_unreadable(self):
        backup = self.pending()
        (backup / "info.json").write_text("{not json")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual(self.installed.read_bytes(), self.original)

    def test_dll_restored_even_if_save_copy_raises(self):
        from unittest import mock
        backup = self.pending()
        with mock.patch.object(sim, "_copy_into", side_effect=OSError("disk full")):
            rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertIn("disk full", out)

    def test_malformed_dll_record_is_reported_not_raised(self):
        backup = self.pending()
        (backup / sim.DLL_RECORD).write_text('{"path": 5}')
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertIn("cannot read the DLL record", out)
        self.assertNotIn("Traceback", err)

    def test_two_pending_backups_original_wins(self):
        import hashlib
        b1 = self.pending("T1")
        cand2 = fake_pe(b"second candidate")
        b2 = sim.backup_saves(self.f.prefix, self.backups, "T2")
        sim.backup_dll(b2, self.installed, hashlib.sha256(cand2).hexdigest())  # captured the first candidate as "installed"
        sim.place_atomic(cand2, self.installed)
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, out + err)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertEqual(sim.pending_backups(self.backups), [])
        self.assertIn("older backup", out)

    def test_every_pending_backup_is_tried_after_a_failure(self):
        import shutil
        b1 = sim.backup_saves(self.f.prefix, self.backups, "T1")
        shutil.rmtree(b1 / "savegame")  # saves-only backup that cannot restore
        b2 = self.pending("T2")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertFalse((b2 / sim.PENDING).exists())
        self.assertTrue((b1 / sim.PENDING).exists())

    def test_lock_held_refuses_run_and_restore(self):
        def never():
            raise RuntimeError("must not launch")

        with sim.run_lock(self.f.home):
            rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=self.env(launch=never))
            self.assertEqual(rc, 1)
            self.assertIn("holds", err)
            self.assertFalse(self.backups.exists())
            self.assertEqual(self.installed.read_bytes(), self.original)
            rc, out, err = self.run_cli("--restore", "--backup-root", str(self.tmp / "elsewhere"))  # independent of --backup-root
            self.assertEqual(rc, 1)
            self.assertIn("holds", err)
        rc, out, err = self.run_cli("--dry-run", "--candidate-dll", str(self.cand))
        self.assertEqual(rc, 0, err)

    def test_lock_released_after_run(self):
        env, state = self.fake_game()
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        self.assertEqual(rc, 0)
        with sim.run_lock(self.f.home):
            pass

    def test_game_still_running_names_the_dll(self):
        env, state = self.fake_game()
        env.kill = lambda pid, sig: state["killed"].append((pid, sig))  # never dies
        env.close_timeout = 3
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        text = "\n".join(self.lines)
        self.assertEqual(rc, 2)
        self.assertIn("STILL INSTALLED", text)
        self.assertIn("d2vr_render_probe_arm_v600.dll", text)
        self.assertIn("cp -f", text)
        self.assertIn("sim --restore", text)
        self.assertEqual(self.installed.read_bytes(), self.cand.read_bytes())
        # after the game is gone, --restore finishes the job
        shutil_rm = __import__("shutil").rmtree
        shutil_rm(self.proc / "5000", ignore_errors=True)
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, out + err)
        self.assertEqual(self.installed.read_bytes(), self.original)

    def test_real_launcher_guard_fails_loudly(self):
        with self.assertRaises(AssertionError):
            sim.Env()._launch()

    def test_lost_dll_record_keeps_marker_and_never_succeeds(self):
        backup = self.pending()
        (backup / sim.DLL_RECORD).unlink()
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertIn("UNKNOWN", out)
        self.assertTrue((backup / sim.PENDING).exists())
        self.assertEqual(self.installed.read_bytes(), self.cand.read_bytes())

    def test_legacy_info_json_dll_entry_is_used(self):
        import hashlib
        backup = self.pending()
        rec = json.loads((backup / sim.DLL_RECORD).read_text())
        (backup / sim.DLL_RECORD).unlink()
        info = json.loads((backup / "info.json").read_text())
        info["dll"] = {"path": rec["path"], "backup": rec["backup"], "sha256": rec["original_sha256"]}
        (backup / "info.json").write_text(json.dumps(info))
        rc, out, err = self.run_cli("--restore")  # destination is the candidate: unknown candidate hash, so check by hand
        self.assertEqual(rc, 2)
        self.assertIn("legacy record", out)
        self.assertTrue((backup / sim.PENDING).exists())
        sim.place_atomic(self.original, self.installed)  # now the original is there: nothing to do
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, out)
        self.assertFalse((backup / sim.PENDING).exists())

    def test_manual_command_is_hash_guarded_and_absent_for_bad_backup(self):
        env, state = self.fake_game()
        env.kill = lambda pid, sig: state["killed"].append((pid, sig))
        env.close_timeout = 3
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        text = "\n".join(self.lines)
        self.assertEqual(rc, 2)
        self.assertIn("sha256sum", text)
        self.assertIn("&& cp -f", text)
        self.assertIn(".d2vr-manual' && mv -f", text)
        self.assertIn("sim --restore", text)

    def test_game_not_closing_with_corrupt_backup_prints_no_copy_command(self):
        env, state = self.fake_game()
        env.kill = lambda pid, sig: state["killed"].append((pid, sig))
        env.close_timeout = 3
        inner = env.launch

        def launch():
            inner()
            next(self.backups.glob("*-sim/installed-*.dll")).write_bytes(b"corrupt")

        env.launch = launch
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        text = "\n".join(self.lines)
        self.assertEqual(rc, 2)
        self.assertNotIn("cp -f", text)
        self.assertIn("do NOT copy", text)

    def test_symlinked_installed_dll_refused(self):
        real = self.tmp / "real-dll.dll"
        real.write_bytes(self.original)
        self.installed.unlink()
        self.installed.symlink_to(real)
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=self.env(launch=lambda: None))
        self.assertEqual(rc, 1)
        self.assertIn("symlink", err)
        self.assertTrue(self.installed.is_symlink())
        self.assertFalse(self.backups.exists())

    def test_symlinked_build_dir_refused(self):
        build = self.mod_home / "build"
        moved = self.tmp / "build-elsewhere"
        build.rename(moved)
        build.symlink_to(moved)
        rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=self.env(launch=lambda: None))
        self.assertEqual(rc, 1)
        self.assertIn("symlink", err)

    def test_candidate_changed_after_planning_is_hashed_as_placed(self):
        import hashlib
        changed = fake_pe(b"rebuilt in the meantime")
        real_compile = sim.compile_script

        def compile_and_change(*a, **k):
            r = real_compile(*a, **k)
            self.cand.write_bytes(changed)  # the harness rebuilt the candidate after the plan hashed it
            return r

        seen = {}
        env, state = self.fake_game()
        inner = env.launch

        def launch():
            seen["during"] = self.installed.read_bytes()
            inner()

        env.launch = launch
        with unittest.mock.patch.object(sim, "compile_script", compile_and_change):
            rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertEqual(seen["during"], changed)
        self.assertEqual(self.installed.read_bytes(), self.original)  # restored: the record holds the hash of what was placed
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_unrestorable_backup_says_how_to_clear_it(self):
        backup = self.pending()
        sim.place_atomic(fake_pe(b"newer install"), self.installed)
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertIn(f"rm '{backup / sim.PENDING}'", out)
        rc, out, err = self.run_cli("--dry-run")  # the refusal for new runs names it too
        self.assertEqual(rc, 1)
        self.assertIn(f"rm '{backup / sim.PENDING}'", err)

    def test_dev_knobs_removed_even_when_restore_fails(self):
        self.pending()
        sim.place_atomic(fake_pe(b"newer install"), self.installed)
        for n in ("linux-probe-override.cfg", "linux-autocontinue", "simulated-gameplay-script.cfg"):
            (self.mod_home / "build" / n).write_text("x")
        (self.mod_home / "artifacts/simulated-gameplay-script.json").write_text("{}")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        for n in ("linux-probe-override.cfg", "linux-autocontinue", "simulated-gameplay-script.cfg"):
            self.assertFalse((self.mod_home / "build" / n).exists(), n)
        self.assertFalse((self.mod_home / "artifacts/simulated-gameplay-script.json").exists())

    def test_snapshot_error_does_not_skip_restore(self):
        env, state = self.fake_game()
        with unittest.mock.patch.object(sim, "snapshot", side_effect=shutil_error):
            rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertEqual(self.savegame.joinpath("SLOT01/data.bin").read_bytes(), b"user slot")
        self.assertIn("snapshot failed", "\n".join(self.lines))
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_close_game_error_does_not_traceback_and_reports(self):
        env, state = self.fake_game()
        with unittest.mock.patch.object(sim, "close_game", side_effect=RuntimeError("boom")):
            rc, out, err = self.run_cli("--candidate-dll", str(self.cand), env=env)
        text = "\n".join(self.lines)
        self.assertIn("closing the game failed", text)
        self.assertEqual(rc, 2)  # game process still listed: nothing restored, message names the DLL
        self.assertIn("STILL INSTALLED", text)

    def test_place_atomic_failure_leaves_no_temp_file(self):
        from unittest import mock
        with mock.patch.object(os, "replace", side_effect=OSError("boom")):
            with self.assertRaises(OSError):
                sim.place_atomic(b"x", self.installed)
        self.assertFalse(list((self.mod_home / "build").glob("*.d2vr-tmp")))
        self.assertEqual(self.installed.read_bytes(), self.original)

    def test_interrupt_is_deferred_during_restore(self):
        got = []
        old = signal.signal(signal.SIGINT, signal.default_int_handler)
        try:
            with sim.defer_sigint(got.append):
                os.kill(os.getpid(), signal.SIGINT)  # would raise KeyboardInterrupt without the shield
            self.assertIs(signal.getsignal(signal.SIGINT), signal.default_int_handler)
        finally:
            signal.signal(signal.SIGINT, old)
        self.assertTrue(got)

    def test_dry_run_prints_hashes(self):
        import hashlib
        rc, out, err = self.run_cli("--dry-run", "--candidate-dll", str(self.cand))
        self.assertEqual(rc, 0, err)
        self.assertIn(hashlib.sha256(self.cand.read_bytes()).hexdigest(), "\n".join(self.lines) + out)
        self.assertIn(hashlib.sha256(self.original).hexdigest(), "\n".join(self.lines) + out)
        self.assertEqual(self.installed.read_bytes(), self.original)
        self.assertFalse(self.backups.exists())


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

    def test_idle_compiles(self):
        segments = sim.scenario_segments("idle", 30000)
        text, cfg = sim.compile_script(segments, 120)
        self.assertTrue(cfg.startswith("D2VR_GAMEPLAY_SCRIPT "))
        prev = 0
        for s in segments:
            self.assertLessEqual(prev, s["start_ms"])
            self.assertLess(s["start_ms"], s["end_ms"])
            prev = s["end_ms"]
            if s.get("when", "gameplay") == "gameplay":
                self.assertFalse({k for k in s["input"] if k != "move_x" or s["input"][k]}, s)
            self.assertNotIn("turn_x", s["input"])
        self.assertLessEqual(prev, 120000)
        gameplay = [s for s in segments if s["start_ms"] >= 30000]
        self.assertEqual(gameplay[-1]["end_ms"] - 30000, 90000)
        self.assertTrue(all(abs(s["head_pitch_degrees"]) <= 10 for s in gameplay))
        import math
        yaws = [2 * math.degrees(math.asin(s["head_pose"][4])) for s in gameplay]  # qy ~ sin(yaw/2) up to the small pitch term
        self.assertAlmostEqual(max(yaws), 20, delta=1.0)
        self.assertAlmostEqual(min(yaws), -20, delta=1.0)
        self.assertTrue(all(s["head_pose"][:3] == [0.0, 1.65, 0.0] for s in gameplay))
        # yaw sin(2*pi*t/10 s): positive -> non-positive at the half period, 5 s in
        flips = [gameplay[i]["start_ms"] - 30000 for i in range(1, len(gameplay)) if yaws[i - 1] > 0 >= yaws[i]]
        self.assertAlmostEqual(flips[0], 5000, delta=300)

    def test_dig(self):
        self.assertEqual(sim.dig({"a": {"b": [1, 2]}}, "a.b.1"), (2, True))
        self.assertEqual(sim.dig({"a": 1}, "a.b"), (None, False))


def modhome_probe():
    # Same positional field count as the headset config (launcher/Probe.cs ProbeConfig, linux/proxy/proxy_core.hpp).
    return ("pid " + "0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0").split()


if __name__ == "__main__":
    unittest.main()
