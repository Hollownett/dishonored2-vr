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
        self.real_home = self.tmp / "realhome"
        rh = unittest.mock.patch.object(sim, "real_home", lambda: self.real_home)
        rh.start()
        self.addCleanup(rh.stop)

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

    def write_record(self, files):
        """A hard-killed run's record: files = {relative path: bytes the rig wrote}."""
        import hashlib
        rec = {rel: hashlib.sha256(data).hexdigest() for rel, data in files.items()}
        (self.mod_home / "build" / sim.KNOBS_NAME).write_text(json.dumps({"version": 1, "files": rec}))
        for rel, data in files.items():
            (self.mod_home / rel).write_bytes(data)

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
        self.write_record({"build/linux-probe-override.cfg": b"x"})
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

        with sim.run_lock(self.mod_home):
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
        with sim.run_lock(self.mod_home):
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
        self.write_record({"build/linux-probe-override.cfg": b"x", "build/linux-autocontinue": b"x",
                           "build/simulated-gameplay-script.cfg": b"x", "artifacts/simulated-gameplay-script.json": b"{}"})
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


class Knobs(FullRun):
    """--knob, --probe-mode, --vr-setting and --env. Every test injects the fake launcher (fake_game)."""

    def watched(self, returns="0"):
        env, state = self.fake_game(returns=returns)
        inner, seen = env.launch, {}

        def launch():
            build = self.mod_home / "build"
            seen["files"] = sorted(p.name for p in build.iterdir())
            seen["contents"] = {p.name: p.read_text() for p in build.iterdir() if p.is_file() and p.suffix != ".dll"}
            seen["env"] = dict(env.launch_env)
            inner()

        env.launch = launch
        return env, seen

    def fields(self):
        return dict(zip(sim.PROBE_FIELDS, self.during["override"].split()))

    def test_knob_written_and_removed(self):
        env, seen = self.watched()
        rc, out, err = self.run_cli("--knob", "native-gpu-timing", "--knob", "frame-split-trace", env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertEqual(seen["contents"]["native-gpu-timing"], "")
        self.assertIn("frame-split-trace", seen["files"])
        for n in ("native-gpu-timing", "frame-split-trace", sim.KNOBS_NAME):
            self.assertFalse((self.mod_home / "build" / n).exists(), n)

    def test_knob_removed_after_failed_run(self):
        env, seen = self.watched(returns="1")
        rc, out, err = self.run_cli("--knob", "native-gpu-timing", env=env)
        self.assertEqual(rc, 1)
        self.assertIn("native-gpu-timing", seen["files"])
        self.assertFalse((self.mod_home / "build/native-gpu-timing").exists())
        self.assertFalse((self.mod_home / "build" / sim.KNOBS_NAME).exists())

    def test_knob_removed_after_launch_raises(self):
        env, seen = self.watched()

        def boom():
            raise RuntimeError("steam missing")

        env.launch = boom
        with self.assertRaises(RuntimeError):
            self.run_cli("--knob", "native-gpu-timing", env=env)
        self.assertFalse((self.mod_home / "build/native-gpu-timing").exists())

    def test_existing_knob_refused_and_untouched(self):
        for name in ("native-gpu-timing", "aa-mode"):
            (self.mod_home / "build" / name).write_text("5\n")
        for extra in (["--knob", "native-gpu-timing"], ["--vr-setting", "temporal_aa=3"]):
            env, seen = self.watched()
            rc, out, err = self.run_cli(*extra, env=env)
            self.assertEqual(rc, 1, extra)
            self.assertIn("already exists", err)
            self.assertIn(extra[1] if extra[0] == "--knob" else "aa-mode", err)
            self.assertNotIn("files", seen, "the game must not launch")
        for name in ("native-gpu-timing", "aa-mode"):
            self.assertEqual((self.mod_home / "build" / name).read_text(), "5\n")
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_existing_knob_refused_even_in_a_dry_run(self):
        (self.mod_home / "build/native-gpu-timing").write_text("1")
        self.assertEqual(self.run_cli("--dry-run", "--knob", "native-gpu-timing")[0], 1)

    # ---- the probe's StartProbe rules for the override line, encoded (dxgi_probe.cpp ~7632-7930); update with the probe
    def probe_rejects(self, line):
        f = dict(zip(sim.PROBE_FIELDS, line.split()))
        i = lambda k: int(f[k])  # noqa: E731
        mode = i("mode")
        if not 0 <= mode <= 6:
            return "Unknown diagnostic mode"
        xr, head = mode in (5, 6), mode == 6
        stereo = (0 < mode < 4) or xr
        single, paired = mode == 2, mode == 3 or xr
        trace = i("paired")  # the fourth field is the GPU trace flag
        if trace not in (0, 1) or (trace and (not stereo or single)):
            return "Unknown GPU diagnostic mode"
        for k in ("skip_taa", "symmetric_frustum", "controllers"):
            if i(k) and not head:
                return k + " requires head-tracked XR mode"
        if i("full_render_scale") and not xr:
            return "Full render scale requires XR mode"
        if i("headset_size") and not (head and i("full_render_scale")):
            return "headset sizing"
        if i("eye_boundary") and not paired:
            return "eye observation requires paired"
        if i("native_eye_capture") and not i("eye_boundary"):
            return "native eye capture requires the observer"
        if i("seconds") == 0 and not paired:
            return "Continuous sessions require paired rendering"
        if i("arm_upload_observer") and not paired:
            return "arm observer requires paired"
        if i("tracked_arms") and not (head and i("controllers") and i("arm_upload_observer")):
            return "tracked arms"
        if i("stable_camera") and not head:
            return "comfort requires head"
        if float(f["resolution"]) != 1.0 and not i("headset_size"):
            return "resolution needs headset sizing"
        if i("eye_aa") and not xr:
            return "eye AA requires XR"
        if i("turning") and not i("controllers"):
            return "turning requires controllers"
        if i("simulated") and (not xr or i("seconds") == 0 or i("saved_settings")):
            return "simulated VR needs a timed XR run without saved settings"
        if i("simulated") == 2 and not i("controllers"):
            return "simulated input needs controllers"
        for k in ("controller_aim", "physical_grab"):
            if i(k) and not (head and i("controllers")):
                return k + " requires tracked controllers"
        if i("left_aim") and not (head and i("controllers") and xr and paired):
            return "left aim"
        if i("gameplay_script") and not (i("simulated") == 2 and xr and paired and i("native_eye_capture") and head and i("controllers")):
            return "gameplay script"
        if i("weapon_attachment"):
            return "attachment"
        if paired and not trace:
            return "Paired rendering requires its target hooks"
        return None

    def test_probe_override_lines_pass_the_probes_validation(self):
        for mode in ("xr", "paired", "single"):
            self.assertIsNone(self.probe_rejects(sim.sim_probe_config(90, simulated=2 if mode == "xr" else 0,
                                                                      gameplay_script=1 if mode == "xr" else 0, probe_mode=mode)), mode)
        self.assertIsNone(self.probe_rejects(sim.sim_probe_config(90, resolution=1.2)))
        # the validator itself catches the bug this round fixed
        bad = dict(zip(sim.PROBE_FIELDS, sim.sim_probe_config(90, probe_mode="paired").split()))
        bad["paired"] = "0"
        self.assertEqual(self.probe_rejects(" ".join(bad[k] for k in sim.PROBE_FIELDS)), "Paired rendering requires its target hooks")

    def test_paired_override_pins_the_gpu_trace_flag(self):
        f = dict(zip(sim.PROBE_FIELDS, sim.sim_probe_config(90, probe_mode="paired").split()))
        self.assertEqual((f["mode"], f["paired"]), ("3", "1"))
        f = dict(zip(sim.PROBE_FIELDS, sim.sim_probe_config(90, probe_mode="single").split()))
        self.assertEqual((f["mode"], f["paired"]), ("2", "0"))

    def test_dxgi_probe_cfg_symlink_refused(self):
        cfg = self.mod_home / "build/dxgi-probe.cfg"
        cfg.unlink()
        user = self.tmp / "my-probe.cfg"
        user.write_text("USER CFG\n")
        cfg.symlink_to(user)
        env, seen = self.watched()
        rc, out, err = self.run_cli(env=env)
        self.assertEqual(rc, 1)
        self.assertIn("dxgi-probe.cfg is a symlink", err)
        self.assertNotIn("files", seen)
        self.assertTrue(cfg.is_symlink())
        self.assertEqual(user.read_text(), "USER CFG\n")
        self.assertEqual(self.run_cli("--dry-run")[0], 1)

    def test_stale_artifacts_are_not_snapshotted(self):
        art = self.mod_home / "artifacts"
        old = art / "xr-live-status.json"
        old.write_text(json.dumps({"gameplay": True, "frame_split": {"adapter": {"name": "NVIDIA OLD"}, "split": {"frames": 5}}}))
        (art / "dxgi-probe-result.json").write_text("{}")
        past = __import__("time").time() - 3600
        for p in (old, art / "dxgi-probe-result.json"):
            os.utime(p, (past, past))
        env, state = self.fake_game(status=False)  # the game writes only its proxy log this time
        rc, out, err = self.run_cli(env=env)
        snap = next(self.runs.glob("*"))
        self.assertFalse((snap / "artifacts/xr-live-status.json").exists())
        self.assertFalse((snap / "artifacts/dxgi-probe-result.json").exists())
        self.assertTrue((snap / "artifacts/linux-proxy.log").exists())
        self.assertEqual((snap / "adapter.txt").read_text().strip(), "null")

    def test_unlaunched_run_snapshots_no_artifacts(self):
        (self.mod_home / "artifacts/xr-live-status.json").write_text("{}")
        sim.snapshot(self.mod_home, self.tmp / "snap", {}, since_ns=None)
        self.assertFalse(list((self.tmp / "snap/artifacts").glob("*")) if (self.tmp / "snap/artifacts").exists() else [])

    def test_restore_cleanup_runs_inside_defer_sigint(self):
        self.write_record({"build/foo": b""})
        order = []
        real_defer, real_release = sim.defer_sigint, sim.release_recorded

        @__import__("contextlib").contextmanager
        def defer(out=None):
            order.append("enter")
            yield
            order.append("exit")

        def release(mod_home):
            order.append("release")
            return real_release(mod_home)

        with unittest.mock.patch.object(sim, "defer_sigint", defer), unittest.mock.patch.object(sim, "release_recorded", release):
            self.assertEqual(self.run_cli("--restore")[0], 0)
        self.assertEqual(order[-3:], ["enter", "release", "exit"])

    def test_rig_owned_knob_names_refused(self):
        for bad in (sim.KNOBS_NAME, sim.AUTOCONTINUE_NAME, sim.SETTLE_NAME):
            env, seen = self.watched()
            rc, out, err = self.run_cli("--knob", bad, env=env)
            self.assertEqual(rc, 1, bad)
            self.assertIn("rig itself owns", err)
            self.assertNotIn("files", seen)

    def test_symlink_target_refused(self):
        outside = self.tmp / "outside"
        outside.write_text("user data")
        (self.mod_home / "build/native-gpu-timing").symlink_to(outside)
        env, seen = self.watched()
        rc, out, err = self.run_cli("--knob", "native-gpu-timing", env=env)
        self.assertEqual(rc, 1)
        self.assertEqual(outside.read_text(), "user data")
        self.assertTrue((self.mod_home / "build/native-gpu-timing").is_symlink())
        # a dangling link, and a link where a rig file goes
        (self.mod_home / "build/native-gpu-timing").unlink()
        (self.mod_home / "build/native-gpu-timing").symlink_to(self.tmp / "nowhere")
        self.assertEqual(self.run_cli("--knob", "native-gpu-timing", env=env)[0], 1)
        (self.mod_home / "build/native-gpu-timing").unlink()
        (self.mod_home / "build/linux-settle-seconds").symlink_to(outside)
        rc, out, err = self.run_cli("--settle", "5", env=env)
        self.assertEqual(rc, 1)
        self.assertIn("symlink", err)
        self.assertEqual(outside.read_text(), "user data")
        self.assertNotIn("files", seen)

    def test_record_lists_every_written_file_with_its_hash(self):
        import hashlib
        env, seen = self.watched()
        rc, out, err = self.run_cli("--knob", "native-gpu-timing", "--settle", "5", env=env)
        self.assertEqual(rc, 0, err)
        rec = json.loads(seen["contents"][sim.KNOBS_NAME])["files"]
        self.assertEqual(set(rec), {"build/linux-probe-override.cfg", "build/simulated-gameplay-script.cfg",
                                    "artifacts/simulated-gameplay-script.json", "build/linux-autocontinue",
                                    "build/linux-settle-seconds", "build/native-gpu-timing"})
        self.assertEqual(rec["build/linux-settle-seconds"], hashlib.sha256(b"5\n").hexdigest())
        self.assertEqual(rec["build/native-gpu-timing"], hashlib.sha256(b"").hexdigest())

    def test_restore_removes_recorded_files_after_a_hard_kill(self):
        self.write_record({"build/native-gpu-timing": b"", "build/linux-settle-seconds": b"5\n",
                           "build/linux-probe-override.cfg": b"x", "artifacts/simulated-gameplay-script.json": b"{}"})
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0, out + err)
        for rel in ("build/native-gpu-timing", "build/linux-settle-seconds", "build/linux-probe-override.cfg",
                    "artifacts/simulated-gameplay-script.json", "build/" + sim.KNOBS_NAME):
            self.assertFalse((self.mod_home / rel).exists(), rel)

    def test_restore_leaves_a_file_that_changed_since(self):
        self.write_record({"build/foo": b"", "build/bar": b""})
        (self.mod_home / "build/foo").write_text("the user's own content")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertEqual((self.mod_home / "build/foo").read_text(), "the user's own content")
        self.assertFalse((self.mod_home / "build/bar").exists())
        self.assertIn("left alone", "\n".join(self.lines) + out)
        self.assertTrue((self.mod_home / "build" / sim.KNOBS_NAME).exists())

    def test_restore_ignores_unsafe_record_paths_and_symlinks(self):
        (self.mod_home / "evil").write_text("keep")
        link_target = self.tmp / "target"
        link_target.write_text("")
        (self.mod_home / "build/lnk").symlink_to(link_target)
        import hashlib
        empty = hashlib.sha256(b"").hexdigest()
        (self.mod_home / "build" / sim.KNOBS_NAME).write_text(json.dumps({"files": {
            "build/../evil": empty, "../evil": empty, "build/a/b": empty, "build/lnk": empty}}))
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 2)
        self.assertTrue((self.mod_home / "evil").exists() and link_target.exists())
        self.assertTrue((self.mod_home / "build/lnk").is_symlink())

    def test_restore_without_a_record_deletes_nothing(self):
        (self.mod_home / "build/linux-settle-seconds").write_text("30\n")
        (self.mod_home / "build/linux-autocontinue").write_text("10\n")
        rc, out, err = self.run_cli("--restore")
        self.assertEqual(rc, 0)
        self.assertTrue((self.mod_home / "build/linux-settle-seconds").exists())

    def test_leftover_record_refuses_a_new_run(self):
        self.write_record({"build/foo": b""})
        rc, out, err = self.run_cli("--dry-run")
        self.assertEqual(rc, 1)
        self.assertIn(sim.KNOBS_NAME, err)

    def test_guard_restore_is_atomic_and_leaves_no_temp(self):
        target = self.mod_home / "build/linux-settle-seconds"
        target.write_text("30\n")
        g = sim.FileGuard()
        g.write(target, "5\n")
        calls = []
        real = sim.place_atomic
        with unittest.mock.patch.object(sim, "place_atomic", lambda d, p, e=None: (calls.append(p), real(d, p, e))[1]):
            self.assertEqual(g.restore(), [])
        self.assertEqual(calls, [target])
        self.assertEqual(target.read_text(), "30\n")
        self.assertFalse(list((self.mod_home / "build").glob("*.d2vr-tmp")))

    def test_settle_file_removed_after_hard_kill_via_record(self):
        self.write_record({"build/linux-settle-seconds": b"5\n"})
        self.assertEqual(self.run_cli("--restore")[0], 0)
        self.assertFalse((self.mod_home / "build/linux-settle-seconds").exists())

    def test_users_own_settle_file_comes_back_after_a_normal_run(self):
        (self.mod_home / "build/linux-settle-seconds").write_text("30\n")
        env, seen = self.watched()
        self.assertEqual(self.run_cli("--settle", "5", env=env)[0], 0)
        self.assertEqual(seen["contents"]["linux-settle-seconds"], "5\n")
        self.assertEqual((self.mod_home / "build/linux-settle-seconds").read_text(), "30\n")

    def test_lock_path_ignores_xdg_state_home_and_home(self):
        a = sim.lock_path(self.mod_home)
        with unittest.mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(self.tmp / "other"), "HOME": str(self.tmp / "elsewhere")}):
            self.assertEqual(sim.lock_path(self.mod_home), a)
        os.environ.pop("XDG_STATE_HOME", None)
        self.assertEqual(sim.lock_path(self.mod_home), a)
        self.assertEqual(a.parent, self.real_home / ".local/state/d2vr")
        self.assertNotEqual(sim.lock_path(self.tmp / "another-mod-home"), a)
        self.assertFalse(str(a).startswith(str(self.mod_home)))

    def test_lock_is_shared_across_environments(self):
        with sim.run_lock(self.mod_home):
            with unittest.mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(self.tmp / "other")}):
                with self.assertRaises(sim.SimError):
                    with sim.run_lock(self.mod_home):
                        pass


    def test_user_knobs_recorded_and_warned(self):
        for n in ("fsr", "aa-mode", "harmless-note"):
            (self.mod_home / "build" / n).write_text("1")
        env, seen = self.watched()
        rc, out, err = self.run_cli("--knob", "native-gpu-timing", env=env)
        self.assertEqual(rc, 0, err)
        info = json.loads((next(self.runs.glob("*")) / "run-info.json").read_text())
        self.assertEqual(info["user_knobs"], ["aa-mode", "fsr", "harmless-note"])
        self.assertEqual(info["rig_knobs"], ["native-gpu-timing"])
        text = "\n".join(self.lines)
        self.assertIn("warning", text)
        self.assertIn("aa-mode, fsr", text)
        self.assertNotIn("harmless-note,", text)
        self.assertNotIn("native-gpu-timing,", text.split("warning")[1])

    def test_flat_run_adapter_and_summary_come_from_the_result_file(self):
        env, state = self.fake_game(status=False)
        inner = env.launch

        def launch():
            inner()
            (self.mod_home / "artifacts/dxgi-probe-result.json").write_text(json.dumps(
                {"presents": 100, "frame_split": {"adapter": {"name": "NVIDIA X", "vendor": 4318, "vram_mb": 8192}, "split": {"frames": 90}}}))

        env.launch = launch
        rc, out, err = self.run_cli("--probe-mode", "single", "--status-field", "frame_split.split.frames", env=env)
        self.assertEqual(rc, 0, err)
        snap = next(self.runs.glob("*"))
        self.assertEqual(json.loads((snap / "adapter.txt").read_text())["name"], "NVIDIA X")
        self.assertIn("frame_split.split.frames = 90", "\n".join(self.lines))

    def test_game_cvar_appended_and_restored(self):
        env, state = self.fake_game()
        inner, seen = env.launch, {}

        def launch():
            seen["cfg"] = self.config.read_text()
            inner()

        env.launch = launch
        before = self.config.read_bytes()
        rc, out, err = self.run_cli("--game-cvar", "r_shadowQualityPreset=0", "--game-cvar", "r_x=1.5", env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertEqual(seen["cfg"], 'seta r_fullscreen "1"\nr_shadowQualityPreset "0"\nr_x "1.5"\n')
        self.assertEqual(self.config.read_bytes(), before)
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_game_cvar_name_rejected(self):
        for bad in ("a b=1", "1x=1", "../x=1", "x", "x=", 'x="1"', "x=1;2", "x=" + "a" * 40, "=1", "x=1\n"):
            env, seen = self.watched()
            rc, out, err = self.run_cli("--game-cvar=" + bad, env=env)
            self.assertEqual(rc, 1, repr(bad))
            self.assertIn("--game-cvar", err)
            self.assertNotIn("files", seen, "the game must not launch")
        self.assertEqual(self.config.read_text(), 'seta r_fullscreen "1"\n')
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_knob_name_rejected(self):
        for bad in ("../x", "a/b", "Up", "a.b", "", "x\n", "-x/.."):
            env, seen = self.watched()
            rc, out, err = self.run_cli("--knob=" + bad, env=env)
            self.assertEqual(rc, 1, repr(bad))
            self.assertIn("--knob", err)
            self.assertNotIn("files", seen, "the game must not launch")
        self.assertFalse((self.mod_home.parent / "x").exists())
        self.assertEqual(sim.pending_backups(self.backups), [])

    def test_dry_run_lists_knob_and_writes_nothing(self):
        rc, out, err = self.run_cli("--dry-run", "--knob", "native-gpu-timing", "--probe-mode", "single",
                                    "--vr-setting", "temporal_aa=3", "--env", "DXVK_HUD=fps")
        self.assertEqual(rc, 0, err)
        for text in ("knob build/native-gpu-timing", "knob build/aa-mode = 3", "probe mode single", "env DXVK_HUD=fps"):
            self.assertIn(text, out)
        self.assertEqual(sorted(p.name for p in (self.mod_home / "build").iterdir()), ["d2vr_render_probe_arm_v600.dll", "dxgi-probe.cfg"])

    def test_probe_mode_default_is_xr(self):
        env, seen = self.watched()
        self.assertEqual(self.run_cli(env=env)[0], 0)
        f = self.fields()
        self.assertEqual((f["mode"], f["simulated"], f["controllers"], f["gameplay_script"]), ("6", "2", "1", "1"))

    def test_probe_mode_paired_sets_flag_3(self):
        env, seen = self.watched()
        self.assertEqual(self.run_cli("--probe-mode", "paired", env=env)[0], 0)
        f = self.fields()
        self.assertEqual(f["mode"], "3")
        # flat, no XR: nothing that needs head tracking, controllers or the simulated backend
        for k in ("simulated", "controllers", "gameplay_script", "tracked_arms", "stable_camera", "left_aim", "physical_grab",
                  "controller_aim", "turning", "symmetric_frustum", "saved_settings", "headset_size"):
            self.assertEqual(f[k], "0", k)
        self.assertEqual((f["eye_boundary"], f["native_eye_capture"]), ("1", "1"))

    def test_probe_mode_single_sets_flag_2(self):
        env, seen = self.watched()
        self.assertEqual(self.run_cli("--probe-mode", "single", env=env)[0], 0)
        f = self.fields()
        self.assertEqual(f["mode"], "2")
        for k in ("simulated", "controllers", "eye_boundary", "native_eye_capture", "arm_upload_observer", "paired"):
            self.assertEqual(f[k], "0", k)

    def test_probe_mode_unknown_refused(self):
        with self.assertRaises(SystemExit):  # argparse choices
            self.run_cli("--probe-mode", "stereo")

    def test_env_key_not_allowed_refused(self):
        for bad in ("LD_PRELOAD=/x.so", "PATH=/tmp", "DXVK_HUD", "DXVK_HUD=fps;rm", "dxvk_hud=fps"):
            env, seen = self.watched()
            rc, out, err = self.run_cli("--env", bad, env=env)
            self.assertEqual(rc, 1, bad)
            self.assertIn("--env", err)
            self.assertNotIn("files", seen)

    def test_env_added_to_launch(self):
        env, seen = self.watched()
        rc, out, err = self.run_cli("--env", "DXVK_HUD=fps,frametimes", "--env", "__GL_THREADED_OPTIMIZATIONS=0", env=env)
        self.assertEqual(rc, 0, err)
        self.assertEqual(seen["env"], {"DXVK_HUD": "fps,frametimes", "__GL_THREADED_OPTIMIZATIONS": "0"})

    def test_vr_setting_applied_and_user_cfg_untouched(self):
        cfg = self.mod_home / "build/vr-settings.cfg"
        cfg.write_text("7 1.5 1 0 45 90 2 0.5 2 65 1 0 0 0 0 0\n")
        before = (cfg.read_bytes(), cfg.stat().st_mtime_ns)
        env, seen = self.watched()
        rc, out, err = self.run_cli("--vr-setting", "temporal_aa=4", "--vr-setting", "resolution=1.2", "--vr-setting", "mirror=2", env=env)
        self.assertEqual(rc, 0, "\n".join(self.lines) + err)
        self.assertEqual(seen["contents"]["aa-mode"], "4\n")
        f = self.fields()
        self.assertEqual((f["resolution"], f["headset_size"], f["full_render_scale"], f["saved_settings"]), ("1.2", "1", "1", "0"))
        self.assertEqual((cfg.read_bytes(), cfg.stat().st_mtime_ns), before)
        self.assertFalse((self.mod_home / "build/aa-mode").exists())

    def test_vr_setting_values_validated(self):
        for bad in ("temporal_aa=7", "temporal_aa=-1", "temporal_aa=x", "resolution=0.4", "resolution=2.5", "resolution=nan",
                    "mirror=3", "mirror=0", "sharpness=1", "resolution", "temporal_aa=1.5"):
            env, seen = self.watched()
            rc, out, err = self.run_cli("--vr-setting", bad, env=env)
            self.assertEqual(rc, 1, bad)
            self.assertNotIn("files", seen)

    def test_vr_setting_conflicts_refused(self):
        env, seen = self.watched()
        self.assertEqual(self.run_cli("--vr-setting", "temporal_aa=1", "--knob", "aa-mode", env=env)[0], 1)
        self.assertEqual(self.run_cli("--vr-setting", "resolution=1.2", "--probe-mode", "paired", env=env)[0], 1)
        self.assertNotIn("files", seen)

    def test_adapter_txt_in_snapshot(self):
        env, state = self.fake_game()
        inner = env.launch

        def launch():
            inner()
            (self.mod_home / "artifacts/xr-live-status.json").write_text(json.dumps(
                {"gameplay": True, "frame_split": {"adapter": {"name": "NVIDIA GeForce RTX 3070 Laptop GPU", "vendor": 4318, "vram_mb": 8192}, "split": {}}}))

        env.launch = launch
        self.assertEqual(self.run_cli(env=env)[0], 0)
        snap = next(self.runs.glob("*"))
        self.assertEqual(json.loads((snap / "adapter.txt").read_text())["name"], "NVIDIA GeForce RTX 3070 Laptop GPU")

    def test_adapter_txt_null_without_block(self):
        env, state = self.fake_game()
        self.assertEqual(self.run_cli(env=env)[0], 0)
        self.assertEqual((next(self.runs.glob("*")) / "adapter.txt").read_text().strip(), "null")


class Pure(unittest.TestCase):
    def test_real_home_is_the_account_home_not_the_environment(self):
        import pwd
        with unittest.mock.patch.dict(os.environ, {"HOME": "/nonexistent-home"}):
            self.assertEqual(sim.real_home(), Path(pwd.getpwuid(os.getuid()).pw_dir))

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

    def test_crouch_walk_compiles(self):
        segments = sim.scenario_segments("crouch_walk", 28000)
        text, cfg = sim.compile_script(segments, 120)
        self.assertTrue(cfg.startswith("D2VR_GAMEPLAY_SCRIPT 6 120000 "))
        prev = 0
        for s in segments:
            self.assertLessEqual(prev, s["start_ms"])
            self.assertLess(s["start_ms"], s["end_ms"])
            prev = s["end_ms"]
        self.assertEqual(prev, 28000 + 90000)
        gameplay = [s for s in segments if s.get("when", "gameplay") == "gameplay" and s["start_ms"] >= 28000]
        walking = [s for s in gameplay if s["input"].get("move_y")]
        crouched = [s for s in gameplay if s["head_pose"][1] < 1.4]
        self.assertEqual(len(crouched), 3)
        self.assertEqual(walking[0]["start_ms"], 28000)
        self.assertEqual(walking[-1]["end_ms"], 28000 + 10000 + 3 * 5000)
        self.assertEqual(gameplay[-1]["input"]["move_y"], 0.0)
        self.assertLessEqual(prev, 120000)

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
