import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from d2vr_linux import doctor, modhome, steam  # noqa: E402
from fixtures import FIXTURES, make_steam  # noqa: E402
from test_modhome import LOADER, PROXY_BYTES, sha  # noqa: E402

GPU = "NVIDIA GeForce RTX 3070 Laptop GPU"
NATIVE = b"MZ" + bytes(2000)
VCRT = ("msvcp140", "msvcp140_2", "vcruntime140", "vcruntime140_1")


def by_name(checks, name):
    return next(c for c in checks if c.name == name)


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.f = make_steam(self.tmp)
        self.sys32 = self.f.prefix / "drive_c/windows/system32"
        for n in VCRT + ("d3dcompiler_47",):
            (self.sys32 / f"{n}.dll").write_bytes(NATIVE)
        shutil.copy(FIXTURES / "system.reg", self.f.prefix / "system.reg")
        (self.f.prefix / "drive_c/openxr").mkdir()
        (self.f.prefix / "drive_c/openxr/wineopenxr64.json").write_text("{}")
        # mod home through modhome.install
        work = self.tmp / "work"
        work.mkdir()
        dll = b"MZ mod dll v999"
        manifest = {"dll_name": "d2vr_render_probe_arm_v999.dll", "sha256": sha(dll),
                    "game_sha256": self.f.exe_sha256}
        rel = work / "rel.zip"
        with zipfile.ZipFile(rel, "w") as z:
            z.writestr("d2vr_render_probe_arm_v999.dll", dll)
            z.writestr("repair-v999-test-candidate.json", json.dumps(manifest))
        oxr = work / "oxr.zip"
        with zipfile.ZipFile(oxr, "w") as z:
            z.writestr("x64/bin/openxr_loader.dll", LOADER)
        self.source = work / "source.json"
        self.source.write_text(json.dumps({
            "url": "https://example.invalid/oxr.zip", "zip_sha256": sha(oxr.read_bytes()),
            "zip_member": "x64/bin/openxr_loader.dll", "dll_sha256": sha(LOADER)}))
        (work / "LICENSE").write_text("lic")
        proxy = work / "dinput8.dll"
        proxy.write_bytes(PROXY_BYTES)
        self.inst = steam.Install(self.f.steam_root, [self.f.steam_root, self.f.library],
                                  self.f.library, self.f.game_dir, self.f.prefix)
        modhome.install(self.inst, rel, proxy, oxr, self.tmp / "cache",
                        game_sha256=self.f.exe_sha256, openxr_source=self.source)
        # SteamVR
        self.steamvr = self.f.library / "steamapps/common/SteamVR"
        self.steamvr.mkdir(parents=True)
        (self.steamvr / "steamxr_linux64.json").write_text(json.dumps({"runtime": {"VALVE_runtime_is_steamvr": True}}))
        (self.f.library / "steamapps/appmanifest_250820.acf").write_text('"AppState"\n{\n}\n')
        self.home = self.f.home
        rt = self.home / ".config/openxr/1"
        rt.mkdir(parents=True)
        (rt / "active_runtime.json").symlink_to(self.steamvr / "steamxr_linux64.json")
        self.proc = self.tmp / "proc"
        (self.proc / "123").mkdir(parents=True)
        (self.proc / "123/comm").write_text("vrserver\n")
        gpu = self.proc / "driver/nvidia/gpus/0000:01:00.0"
        gpu.mkdir(parents=True)
        (gpu / "information").write_text(f"Model: \t\t {GPU}\nIRQ: 101\n")
        self.icd = self.tmp / "icd"
        self.icd.mkdir()
        (self.icd / "nvidia_icd.x86_64.json").write_text("{}")

    def run_checks(self, inst="default", **kw):
        args = dict(home=self.home, proc_root=self.proc, icd_dirs=[self.tmp / "nope", self.icd],
                    game_sha256=self.f.exe_sha256, openxr_source=self.source)
        args.update(kw)
        return doctor.run_checks(self.inst if inst == "default" else inst,
                                 args.pop("locate_error", ""), **args)


class DoctorTests(Case):
    def test_all_pass_on_healthy_fixture(self):
        checks = self.run_checks()
        self.assertEqual([c.name for c in checks if not c.ok], [])
        self.assertEqual(len(checks), 12)

    def test_doctor_game_hash_fail(self):
        checks = self.run_checks(game_sha256=None)
        c = by_name(checks, "Game build")
        self.assertFalse(c.ok)
        self.assertIn("Steam updated the game", c.fix)

    def test_builtin_stub_counts_as_missing(self):
        (self.sys32 / "vcruntime140.dll").write_bytes(b"MZ" + bytes(60) + b"Wine builtin DLL")
        self.assertFalse(by_name(self.run_checks(), "VC++ runtime (native)").ok)

    def test_symlinked_d3dcompiler_counts_as_missing(self):
        p = self.sys32 / "d3dcompiler_47.dll"
        p.unlink()
        target = self.tmp / "real.dll"
        target.write_bytes(NATIVE)
        p.symlink_to(target)
        self.assertFalse(doctor.is_native_dll(p))
        self.assertFalse(by_name(self.run_checks(), "d3dcompiler_47 (native)").ok)

    def test_missing_dll_is_not_native(self):
        self.assertFalse(doctor.is_native_dll(self.sys32 / "nothere.dll"))

    def test_prefix_runtime_parsed_from_system_reg(self):
        self.assertEqual(doctor.prefix_openxr_runtime(self.f.prefix), r"C:\openxr\wineopenxr64.json")
        self.assertIsNone(doctor.prefix_openxr_runtime(self.tmp / "none"))

    def test_nvidia_name_from_proc(self):
        self.assertEqual(doctor.nvidia_gpu_name(self.proc), GPU)
        self.assertIsNone(doctor.nvidia_gpu_name(self.tmp / "none"))

    def test_steamvr_not_running(self):
        (self.proc / "123/comm").write_text("bash\n")
        self.assertFalse(doctor.steamvr_running(self.proc))
        self.assertFalse(by_name(self.run_checks(), "SteamVR running").ok)

    def test_host_runtime_missing_or_not_steamvr(self):
        link = self.home / ".config/openxr/1/active_runtime.json"
        link.unlink()
        self.assertFalse(doctor.host_runtime(self.home)[0])
        link.write_text(json.dumps({"runtime": {"name": "Monado"}}))
        self.assertFalse(doctor.host_runtime(self.home)[0])

    def test_fix_host_runtime_creates_symlink_and_never_overwrites(self):
        (self.home / ".config/openxr/1/active_runtime.json").unlink()
        p = doctor.fix_host_runtime(self.home, self.steamvr)
        self.assertEqual(p.resolve(), (self.steamvr / "steamxr_linux64.json").resolve())
        self.assertTrue(doctor.host_runtime(self.home)[0])
        with self.assertRaises(FileExistsError):
            doctor.fix_host_runtime(self.home, self.steamvr)

    def test_fix_refuses_dangling_symlink(self):
        shutil.rmtree(self.home / ".config/openxr")
        other = self.tmp / "gone.json"
        (self.home / ".config/openxr/1").mkdir(parents=True)
        (self.home / ".config/openxr/1/active_runtime.json").symlink_to(other)
        with self.assertRaises(FileExistsError):
            doctor.fix_host_runtime(self.home, self.steamvr)

    def test_no_game_still_reports_host_checks(self):
        checks = self.run_checks(None, locate_error="Dishonored 2 not found")
        self.assertEqual(len(checks), 12)
        self.assertTrue(by_name(checks, "NVIDIA Vulkan").ok)
        for n in ("Game build", "Proton prefix", "Mod home", "Proxy", "Prefix OpenXR runtime",
                  "VC++ runtime (native)", "d3dcompiler_47 (native)"):
            c = by_name(checks, n)
            self.assertFalse(c.ok, n)
            self.assertEqual(c.detail, "game not found")

    def test_mod_home_fails_on_two_dlls(self):
        (modhome.mod_home_dir(self.f.prefix) / "build/d2vr_render_probe_arm_v998.dll").write_bytes(b"x")
        self.assertFalse(by_name(self.run_checks(), "Mod home").ok)

    def test_mod_home_fails_on_loader_mismatch(self):
        (modhome.mod_home_dir(self.f.prefix) / "third_party/openxr/bin/openxr_loader.dll").write_bytes(b"bad")
        self.assertFalse(by_name(self.run_checks(), "Mod home").ok)

    def test_proxy_fails_when_foreign(self):
        (self.f.game_dir / "dinput8.dll").write_bytes(b"other mod")
        self.assertFalse(by_name(self.run_checks(), "Proxy").ok)

    def test_nvidia_vulkan_fails_without_icd(self):
        self.assertFalse(by_name(self.run_checks(icd_dirs=[self.tmp / "nope"]), "NVIDIA Vulkan").ok)

    def test_format_checks(self):
        self.assertEqual(doctor.format_checks([doctor.Check("A", True, "ok", ""), doctor.Check("B", False, "bad", "do x")]),
                         "PASS A: ok\nFAIL B: bad\n      fix: do x\n")

    def test_launch_options_exact(self):
        self.assertEqual(doctor.launch_options(GPU),
            'WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="NVIDIA GeForce RTX 3070 Laptop GPU" '
            '__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%')


if __name__ == "__main__":
    unittest.main()
