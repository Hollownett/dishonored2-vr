import contextlib
import io
import json
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

LINUX = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LINUX))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import cli, modhome, steam  # noqa: E402
import test_doctor  # noqa: E402
import test_modhome  # noqa: E402


def run(argv, **kw):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = cli.main(argv, **kw)
    return rc, out.getvalue(), err.getvalue()


class InstallTests(test_modhome.Case):
    def setUp(self):
        super().setUp()
        self.src_json = self.work / "source.json"
        self.src_json.write_text(json.dumps(self.src))
        (self.work / "LICENSE").write_text("lic")
        self.proc = self.tmp / "proc"
        gpu = self.proc / "driver/nvidia/gpus/0000:01:00.0"
        gpu.mkdir(parents=True)
        (gpu / "information").write_text(f"Model: \t\t {test_doctor.GPU}\n")

    def argv(self, z=None):
        return ["install", "--zip", str(z or self.zip), "--proxy", str(self.proxy),
                "--openxr-zip", str(self.oxr_zip)]

    def patched(self):
        stack = contextlib.ExitStack()
        stack.enter_context(mock.patch.object(steam, "GAME_SHA256", self.f.exe_sha256))
        stack.enter_context(mock.patch.object(modhome, "OPENXR_SOURCE", self.src_json))
        return stack

    def test_install_then_uninstall_roundtrip(self):
        with self.patched():
            rc, out, err = run(self.argv(), home=self.f.home, proc_root=self.proc)
        self.assertEqual(rc, 0, err)
        self.assertIn("dinput8.dll", out)
        self.assertIn("Steam launch options:", out)
        self.assertIn(test_doctor.GPU, out)
        self.assertTrue((self.f.game_dir / "dinput8.dll").exists())
        rc, out, err = run(["uninstall"], home=self.f.home)
        self.assertEqual(rc, 0, err)
        self.assertFalse((self.f.game_dir / "dinput8.dll").exists())
        self.assertFalse((self.f.game_dir / "d2vr-linux.ini").exists())
        self.assertIn(f"Mod home left in place: {self.home}", out)
        self.assertTrue(self.home.is_dir())

    def test_install_without_gpu_omits_launch_options(self):
        with self.patched():
            rc, out, err = run(self.argv(), home=self.f.home, proc_root=self.tmp / "empty")
        self.assertEqual(rc, 0, err)
        self.assertNotIn("Steam launch options:", out)

    def test_install_error_exit_1_message(self):
        with self.patched():
            rc, out, err = run(self.argv(self.make_zip(tamper=True, name="bad.zip")),
                               home=self.f.home, proc_root=self.proc)
        self.assertEqual(rc, 1)
        self.assertTrue(err.startswith("d2vr-linux: error: "), err)
        self.assertFalse(self.home.exists())

    def test_steam_not_found_exit_1(self):
        rc, out, err = run(["uninstall"], home=self.tmp / "nohome")
        self.assertEqual(rc, 1)
        self.assertTrue(err.startswith("d2vr-linux: error: "), err)

    def test_start_stop_write_requests_with_mod_home(self):
        h = self.tmp / "mh"
        rc, out, err = run(["start", "--mod-home", str(h)])
        self.assertEqual(rc, 1)  # no build/
        self.assertIn("d2vr-linux: error: ", err)
        (h / "build").mkdir(parents=True)
        rc, out, err = run(["start", "--mod-home", str(h)])
        self.assertEqual(rc, 0, err)
        self.assertTrue((h / "build/linux-start").exists())
        self.assertIn(str(h / "build/linux-start"), out)
        rc, out, err = run(["stop", "--mod-home", str(h)])
        self.assertEqual(rc, 0, err)
        self.assertTrue((h / "build/linux-stop").exists())

    def test_start_without_mod_home_build_fails(self):
        rc, out, err = run(["start"], home=self.f.home)
        self.assertEqual(rc, 1)
        self.assertIn("build", err)
        self.assertFalse((self.home / "build").exists())

    def test_start_default_mod_home_from_prefix(self):
        (self.home / "build").mkdir(parents=True)
        rc, out, err = run(["stop"], home=self.f.home)
        self.assertEqual(rc, 0, err)
        self.assertTrue((self.home / "build/linux-stop").exists())


class FeatureCliTests(test_modhome.Case):
    def go(self, *args):
        return run(["feature", *args[:1], *args[1:], "--mod-home", str(self.home)], home=self.f.home)

    def test_list_on_off_roundtrip(self):
        self.install()
        rc, out, err = self.go("list")
        self.assertEqual(rc, 0, err)
        self.assertRegex(out, r"full-body\s+on\s+Full body")
        self.assertRegex(out, r"power-gestures\s+off\s")
        rc, out, err = self.go("off", "full-body")
        self.assertEqual(rc, 0, err)
        self.assertFalse((self.home / "build/full-body").exists())
        self.assertRegex(self.go("list")[1], r"full-body\s+off\s")
        self.install()  # reinstall keeps it off
        self.assertFalse((self.home / "build/full-body").exists())
        self.assertEqual(self.go("on", "full-body")[0], 0)
        self.assertTrue((self.home / "build/full-body").exists())
        self.assertEqual(self.go("on", "power-gestures")[0], 0)
        self.assertTrue((self.home / "build/power-gestures").exists())

    def test_unknown_id_and_missing_home(self):
        self.install()
        rc, out, err = self.go("on", "nope")
        self.assertEqual(rc, 1)
        self.assertIn("unknown feature", err)
        rc, out, err = run(["feature", "list", "--mod-home", str(self.tmp / "nowhere")], home=self.f.home)
        self.assertEqual(rc, 1)


class DoctorTests(test_doctor.Case):
    def go(self, *args):
        with mock.patch.object(steam, "GAME_SHA256", self.f.exe_sha256), \
                mock.patch.object(modhome, "OPENXR_SOURCE", self.source):
            return run(list(args), home=self.home, proc_root=self.proc, icd_dirs=[self.icd])

    def test_doctor_exit_codes(self):
        rc, out, err = self.go("doctor")
        self.assertEqual(rc, 0, out)
        self.assertNotIn("FAIL", out)
        (self.proc / "123/comm").unlink()
        rc, out, err = self.go("doctor")
        self.assertEqual(rc, 1)
        self.assertIn("FAIL SteamVR running", out)

    def test_doctor_without_game_still_runs(self):
        with mock.patch.object(steam, "GAME_SHA256", self.f.exe_sha256), \
                mock.patch.object(modhome, "OPENXR_SOURCE", self.source):
            rc, out, err = run(["doctor"], home=self.tmp / "nohome", proc_root=self.proc, icd_dirs=[self.icd])
        self.assertEqual(rc, 1)
        self.assertIn("FAIL Dishonored 2", out)

    def test_doctor_fix_openxr(self):
        link = self.home / ".config/openxr/1/active_runtime.json"
        link.unlink()
        rc, out, err = self.go("doctor", "--fix-openxr")
        self.assertEqual(rc, 0, out)
        self.assertTrue(link.is_symlink())
        self.assertIn(str(link), out)
        rc, out, err = self.go("doctor", "--fix-openxr")
        self.assertIn("already exists", out)

    def test_doctor_mod_home_option(self):
        rc, out, err = self.go("doctor", "--mod-home", str(self.tmp / "elsewhere"))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL Mod home", out)

    def test_launch_options_exact(self):
        rc, out, err = run(["launch-options"], proc_root=self.proc)
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), (
            f'WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="{test_doctor.GPU}" '
            "__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%"))

    def test_launch_options_no_gpu(self):
        rc, out, err = run(["launch-options"], proc_root=self.tmp / "empty")
        self.assertEqual(rc, 1)
        self.assertIn("no NVIDIA GPU found", err)


class WrapperTests(unittest.TestCase):
    def test_wrapper_runs(self):
        r = subprocess.run([sys.executable, str(LINUX / "d2vr-linux"), "--help"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        self.assertIn("launch-options", r.stdout)

    def test_usage_error_exit_2(self):
        r = subprocess.run([sys.executable, str(LINUX / "d2vr-linux"), "bogus"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)



class SettleHelpTests(unittest.TestCase):
    def test_settle_help_mentions_new_default(self):
        parser = cli.build_parser() if hasattr(cli, "build_parser") else None
        text = parser.format_help() if parser else ""
        if not parser:
            out = io.StringIO()
            with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
                cli.main(["sim", "--help"])
            text = out.getvalue()
        flat = " ".join(text.split())
        # The sim parser nests under "sim"; find the --settle paragraph whichever parser was used.
        self.assertIn("default: 20", flat)
        self.assertIn("--settle 45 restores the old wait", flat)

if __name__ == "__main__":
    unittest.main()
