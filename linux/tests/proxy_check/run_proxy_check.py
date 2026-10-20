"""Wine-hosted integration check for the dinput8 proxy: a stand-in game exe loads build/linux-proxy/dinput8.dll,
which loads a fake mod DLL from a temp mod home and drives StartProbe/StopProbe from the CLI's request files."""
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # linux/ -> d2vr_linux
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # linux/tests -> wine_support

import wine_support  # noqa: E402
from d2vr_linux import modhome  # noqa: E402

harness = wine_support.harness
REPO = wine_support.REPO
CLI = REPO / "linux" / "d2vr-linux"
CFG_TAIL = " 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n"


def wait_for(predicate, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.2)
    if not predicate():
        raise AssertionError("timed out after %s s waiting for %r" % (seconds, predicate))


def cli(*args):
    return subprocess.run([sys.executable, str(CLI), *args], cwd=REPO, capture_output=True, text=True, timeout=60)


class ProxyCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not wine_support.toolchain_available():
            raise unittest.SkipTest("clang-cl/xwin/wine toolchain missing: run tools/linux/setup-toolchain.sh")
        cls.built = wine_support.load_proxy_build().build(["proxy", "host", "fake_mod", "fake_mod_lowcaps", "core_check"])
        wine_support.start_wine()

    @classmethod
    def tearDownClass(cls):
        wine_support.stop_wine()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="d2vr-proxy-check-"))
        self.game = self.tmp / "Dishonored 2 test"
        self.home = self.tmp / "mod home"
        self.game.mkdir()
        for sub in (".git", "build", "artifacts"):
            (self.home / sub).mkdir(parents=True)
        shutil.copy2(self.built["host"], self.game / "proxy_host.exe")
        shutil.copy2(self.built["proxy"], self.game / "dinput8.dll")
        (self.game / "d2vr-linux.ini").write_bytes(
            b"\xef\xbb\xbf" + wine_support.to_wine_path(self.home).encode("utf-8") + b"\r\n")
        self.marker = self.home / "artifacts" / "fake-start.txt"
        self.host = None
        self.host_out = None

    def tearDown(self):
        if self.host is not None and self.host.poll() is None:
            (self.game / "host-exit").write_text("1")
            try:
                self.host.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.host.kill()
                self.host.wait()
        if self.host_out is not None:
            self.host_out.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    # ---- helpers

    def install_mod(self, target="fake_mod"):
        shutil.copy2(self.built[target], self.home / "build" / "d2vr_render_probe_arm_v999.dll")

    def launch(self):
        # A file, not a pipe: a wineserver started by this run would inherit a pipe and block reads.
        self.host_out = open(self.tmp / "host-stdout.txt", "wb")
        self.host = subprocess.Popen(["wine", str(self.game / "proxy_host.exe"), "90"],
                                     env=wine_support.wine_env("dinput8=n,b"), cwd=self.game,
                                     stdin=subprocess.DEVNULL, stdout=self.host_out, stderr=subprocess.STDOUT)

    def log(self):
        p = self.home / "artifacts" / "linux-proxy.log"
        return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""

    def host_stdout(self):
        p = self.tmp / "host-stdout.txt"
        return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""

    def wait_for(self, predicate, seconds):
        try:
            wait_for(predicate, seconds)
        except AssertionError as e:
            self.fail("%s\n--- linux-proxy.log\n%s--- host stdout\n%s" % (e, self.log(), self.host_stdout()))

    def finish(self):
        (self.game / "host-exit").write_text("1")
        rc = self.host.wait(timeout=30)
        self.assertEqual(rc, 0, "host rc\n--- linux-proxy.log\n%s--- host stdout\n%s" % (self.log(), self.host_stdout()))

    # ---- tests

    def test_proxy_exports_and_marker(self):
        proxy = self.built["proxy"]
        out = subprocess.run([str(harness.LLVM_BIN / "llvm-readobj"), "--coff-exports", str(proxy)],
                             capture_output=True, text=True).stdout
        self.assertEqual(re.findall(r"Name: (\S+)", out), ["DirectInput8Create"])
        self.assertTrue(modhome.is_our_proxy(proxy))

    def test_stale_trigger_ignored_then_start_and_stop(self):
        self.install_mod()
        (self.home / "build/linux-start").write_text("1")
        (self.home / "build/linux-settle-seconds").write_text("600")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertIn("removed stale linux-start", self.log())
        time.sleep(3)
        self.assertFalse(self.marker.exists())
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(self.marker.exists, 30)
        host_pid, cfg_pid = self.marker.read_text().split()
        self.assertEqual(host_pid, cfg_pid)
        self.assertEqual((self.home / "build/dxgi-probe.cfg").read_bytes(), (host_pid + CFG_TAIL).encode())
        self.assertFalse((self.home / "build/linux-start").exists())
        self.assertIn("hr=0x00000000 iface=1", self.host_stdout())
        self.assertEqual(cli("stop", "--mod-home", str(self.home)).returncode, 0)
        # Two threads log these; their order is not fixed.
        self.wait_for(lambda: "StartProbe returned 0" in self.log() and "StopProbe returned 0" in self.log(), 15)
        self.finish()

    def test_settle_timeout_autostarts_first_session(self):
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("2")
        self.launch()
        self.wait_for(self.marker.exists, 30)
        self.assertIn("settle timeout", self.log())
        self.assertEqual(cli("stop", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "StartProbe returned 0" in self.log(), 15)
        self.finish()

    def test_no_mod_dll_keeps_game_running(self):
        (self.home / "build/linux-settle-seconds").write_text("600")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "expected exactly one d2vr_render_probe_arm_v*.dll in build (found 0)" in self.log(), 30)
        self.assertFalse(self.marker.exists())
        self.assertIn("hr=0x00000000 iface=1", self.host_stdout())
        self.finish()

    def test_missing_capabilities_refused(self):
        self.install_mod("fake_mod_lowcaps")
        (self.home / "build/linux-settle-seconds").write_text("600")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "lacks required 61017" in self.log(), 30)
        self.assertFalse(self.marker.exists())
        self.assertIn("hr=0x00000000 iface=1", self.host_stdout())
        self.finish()

    def test_refused_start_keeps_previous_outputs(self):
        # Probe.Session refuses before it writes the cfg or deletes the last session's outputs (Probe.cs:225-247).
        self.install_mod("fake_mod_lowcaps")
        (self.home / "build/linux-settle-seconds").write_text("600")
        previous = {name: self.home / "artifacts" / name for name in ("dxgi-probe-failure.txt", "dxgi-probe-result.json")}
        for name, path in previous.items():
            path.write_text("previous session " + name)
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "ProbeCapabilities=1 lacks required 61017" in self.log(), 30)
        for name, path in previous.items():
            self.assertEqual(path.read_text(), "previous session " + name)
        self.assertFalse((self.home / "build/dxgi-probe.cfg").exists())
        self.assertNotIn("StartProbe begin", self.log())
        self.finish()

    def test_stop_during_startup_is_kept_until_accepted(self):
        # The fake StopProbe returns 2 for 5 s after StartProbe begins, as the real one does until it is capturing.
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("600")
        (self.home / "build/fake-start-delay-ms").write_text("5000")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(self.marker.exists, 30)
        self.assertEqual(cli("stop", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "retrying while the session starts" in self.log(), 15)
        self.assertTrue((self.home / "build/linux-stop").exists())
        # Without the retry the stop is lost and the fake StartProbe only returns (4) after 60 s.
        self.wait_for(lambda: "StartProbe returned 0" in self.log() and "StopProbe returned 0" in self.log(), 30)
        self.assertEqual(self.log().count("retrying while the session starts"), 1)
        self.wait_for(lambda: not (self.home / "build/linux-stop").exists(), 5)
        self.finish()

    def test_second_session_after_stop(self):
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("600")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        for session in (1, 2):
            self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
            self.wait_for(self.marker.exists, 30)
            host_pid, cfg_pid = self.marker.read_text().split()
            self.assertEqual(host_pid, cfg_pid)
            self.marker.unlink()
            self.assertEqual(cli("stop", "--mod-home", str(self.home)).returncode, 0)
            self.wait_for(lambda: self.log().count("StartProbe returned 0") == session
                          and self.log().count("StopProbe returned 0") == session, 15)
        self.assertEqual(self.log().count("StartProbe begin"), 2)
        self.assertIn("waiting for start (trigger)", self.log())
        self.assertNotIn("restart the game", self.log())
        self.finish()

    def test_probe_override_replaces_config(self):
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("600")
        (self.home / "build/linux-probe-override.cfg").write_text("{pid} 0 6 1 custom {pid}\n")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(self.marker.exists, 30)
        host_pid, _ = self.marker.read_text().split()
        self.assertEqual((self.home / "build/dxgi-probe.cfg").read_text(), "{0} 0 6 1 custom {0}\n".format(host_pid))
        self.assertIn("probe config override: " + host_pid + " 0 6 1 custom " + host_pid, self.log())
        self.assertNotIn("autocontinue", self.log())
        self.assertNotIn("key 0x", self.host_stdout())
        self.finish()

    def test_autocontinue_posts_enter_until_gameplay(self):
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("600")
        (self.home / "build/linux-autocontinue").write_text("1")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: self.host_stdout().count("key 0x0d") >= 3, 40)
        self.assertIn("autocontinue armed: Enter after 1 s", self.log())
        self.assertIn("autocontinue: posted Enter 3 (logic_mask ?)", self.log())
        self.finish()

    def test_autocontinue_stops_at_gameplay(self):
        self.install_mod()
        (self.home / "build/linux-settle-seconds").write_text("600")
        (self.home / "build/linux-autocontinue").write_text("4")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        # The proxy deletes the previous session's status at start; the mod writes a fresh one once in gameplay.
        self.wait_for(lambda: "StartProbe begin" in self.log(), 30)
        (self.home / "artifacts/xr-live-status.json").write_text('{"gameplay":true}')
        self.wait_for(lambda: "gameplay reached; no more keys sent" in self.log(), 30)
        self.assertNotIn("key 0x", self.host_stdout())
        self.finish()

    def test_two_mod_dlls_refused_game_keeps_running(self):
        self.install_mod()
        shutil.copy2(self.built["fake_mod"], self.home / "build" / "d2vr_render_probe_arm_v998.dll")
        (self.home / "build/linux-settle-seconds").write_text("600")
        self.launch()
        self.wait_for(lambda: "game window found" in self.log(), 30)
        self.assertEqual(cli("start", "--mod-home", str(self.home)).returncode, 0)
        self.wait_for(lambda: "expected exactly one d2vr_render_probe_arm_v*.dll in build (found 2)" in self.log(), 30)
        self.assertFalse(self.marker.exists())
        self.assertIn("hr=0x00000000 iface=1", self.host_stdout())
        self.finish()


if __name__ == "__main__":
    unittest.main()
