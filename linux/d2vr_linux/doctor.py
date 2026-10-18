"""Host and Proton-prefix health checks, plus the Steam launch-options line."""
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path

from . import modhome, steam
from .steam import Install

DEFAULT_ICD_DIRS = [Path("/usr/share/vulkan/icd.d"), Path("/etc/vulkan/icd.d")]
WINE_BUILTIN_MARKER = b"Wine builtin DLL"
PREFIX_RUNTIME = r"C:\openxr\wineopenxr64.json"
_VCRT = ("msvcp140", "msvcp140_2", "vcruntime140", "vcruntime140_1")
_NO_GAME = "game not found"


@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    fix: str


def nvidia_gpu_name(proc_root: Path = Path("/proc")) -> "str | None":
    for info in sorted(Path(proc_root).glob("driver/nvidia/gpus/*/information")):
        try:
            text = info.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        m = re.search(r"^Model:[ \t]*(.*)$", text, re.M)
        if m and m.group(1).strip():
            return m.group(1).strip()
    return None


def is_native_dll(path: Path) -> bool:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        return False
    try:
        with open(path, "rb") as f:
            return WINE_BUILTIN_MARKER not in f.read(1024)
    except OSError:
        return False


def prefix_openxr_runtime(prefix: Path) -> "str | None":
    try:
        text = (Path(prefix) / "system.reg").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    in_section = False
    for line in text.splitlines():
        if line.startswith("["):
            in_section = line.startswith(r"[Software\\Khronos\\OpenXR\\1]")
        elif in_section:
            m = re.match(r'^"ActiveRuntime"="(.*)"\s*$', line)
            if m:
                return m.group(1).replace("\\\\", "\\")
    return None


def steamvr_running(proc_root: Path = Path("/proc")) -> bool:
    for comm in Path(proc_root).glob("[0-9]*/comm"):
        try:
            if comm.read_text(encoding="utf-8", errors="replace").strip() == "vrserver":
                return True
        except OSError:
            continue
    return False


def _runtime_json(home: Path) -> Path:
    return Path(home) / ".config/openxr/1/active_runtime.json"


def host_runtime(home: Path) -> tuple:
    path = _runtime_json(home)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError:
        return False, f"{path} not found"
    except ValueError:
        return False, f"{path} is not valid JSON"
    runtime = data.get("runtime") if isinstance(data, dict) else None
    if isinstance(runtime, dict) and runtime.get("VALVE_runtime_is_steamvr") is True:
        return True, f"{path} -> SteamVR"
    return False, f"{path} is not the SteamVR runtime"


def fix_host_runtime(home: Path, steamvr_dir: Path) -> Path:
    path = _runtime_json(home)
    if os.path.lexists(path):
        raise FileExistsError(str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.symlink_to(Path(steamvr_dir) / "steamxr_linux64.json")
    return path


def _mod_home_check(home: Path, source_path: Path) -> tuple:
    if not (home / ".git").is_dir():
        return False, f"{home} has no .git/ (mod not installed)"
    build = home / "build"
    dlls = sorted(p for p in build.glob("d2vr_render_probe_arm_v*.dll") if re.match(modhome.DLL_RE, p.name))
    if len(dlls) != 1:
        return False, f"expected exactly one mod DLL in {build}, found {len(dlls)}"
    dll_hash = steam.sha256_file(dlls[0])
    hashes = []
    for m in sorted((home / "artifacts").glob("repair-v*-test-candidate.json")):
        if re.match(modhome.MANIFEST_RE, m.name):
            try:
                data = json.loads(m.read_text(encoding="utf-8"))
                hashes.append(data.get("sha256") if isinstance(data, dict) else None)
            except (OSError, ValueError):
                pass
    if dll_hash not in hashes:
        return False, f"no artifacts/ manifest matches {dlls[0].name}"
    try:
        want = json.loads(Path(source_path).read_text(encoding="utf-8"))["dll_sha256"]
        loader = home / "third_party/openxr/bin/openxr_loader.dll"
        if steam.sha256_file(loader) != want:
            return False, f"{loader} does not match the pinned OpenXR loader"
    except (OSError, ValueError, KeyError):
        return False, "OpenXR loader missing or unreadable"
    return True, f"{dlls[0].name} in {home}"


def _proxy_check(game_dir: Path) -> tuple:
    proxy = game_dir / modhome.PROXY_NAME
    ini = game_dir / modhome.INI_NAME
    if not proxy.is_file():
        return False, f"{proxy} not installed"
    if not modhome.is_our_proxy(proxy):
        return False, f"{proxy} is another mod's dinput8.dll"
    if not ini.is_file():
        return False, f"{ini} missing"
    return True, f"{proxy} + {modhome.INI_NAME}"


def run_checks(inst: "Install | None", locate_error: str, home: Path, proc_root: Path, icd_dirs: list,
               mod_home: "Path | None" = None, game_sha256: "str | None" = None,
               openxr_source: "Path | None" = None) -> list:
    game_sha256 = steam.GAME_SHA256 if game_sha256 is None else game_sha256
    openxr_source = modhome.OPENXR_SOURCE if openxr_source is None else openxr_source
    out = []

    def add(name, ok, detail, fix):
        out.append(Check(name, bool(ok), detail, "" if ok else fix))

    def no_game(name, fix):
        add(name, False, _NO_GAME, fix)

    install_fix = "d2vr-linux install --zip <d2vr-vNNN-dll.zip>"
    add("Dishonored 2", inst is not None, str(inst.game_dir) if inst else locate_error,
        "Install Dishonored 2 (app 403640) in a Linux Steam library, then start Steam once.")

    game_fixes = {
        "Game build": "Steam updated the game; this mod supports only build 17576867. Use a mod build made for "
                      "the installed version, or restore 17576867 (Steam console: download_depot).",
        "Proton prefix": "Set Proton Experimental for Dishonored 2 and start it once.",
        "Mod home": install_fix,
        "Proxy": "d2vr-linux install --zip …",
    }
    if inst is None:
        for n, fix in game_fixes.items():
            no_game(n, fix)
    else:
        try:
            steam.check_game_exe(inst.game_dir, game_sha256)
            add("Game build", True, "Dishonored2.exe matches build 17576867", "")
        except (steam.SteamError, OSError) as e:
            add("Game build", False, str(e), game_fixes["Game build"])
        add("Proton prefix", inst.prefix.exists(), str(inst.prefix), game_fixes["Proton prefix"])
        mh = Path(mod_home) if mod_home else modhome.mod_home_dir(inst.prefix)
        ok, detail = _mod_home_check(mh, openxr_source)
        add("Mod home", ok, detail, install_fix)
        ok, detail = _proxy_check(inst.game_dir)
        add("Proxy", ok, detail, game_fixes["Proxy"])

    if inst is not None:
        libs = inst.libraries
    else:
        libs = []
        for root in steam.steam_roots(home):
            libs += [l for l in steam.library_paths(root) if l not in libs]
    steamvr = steam.find_steamvr(libs)
    add("SteamVR installed", steamvr is not None, str(steamvr) if steamvr else "not found",
        "Install SteamVR (app 250820) and switch it to the beta branch.")
    running = steamvr_running(proc_root)
    add("SteamVR running", running, "vrserver " + ("running" if running else "not running"),
        "Start SteamVR and connect the Quest through Steam Link BEFORE launching the game "
        "(relaunch the game if it was started first).")
    ok, detail = host_runtime(home)
    add("Host OpenXR runtime", ok, detail,
        "d2vr-linux doctor --fix-openxr (or SteamVR Settings > OpenXR > Set SteamVR as OpenXR runtime)")

    oxr_fix = "Start the game once with Proton Experimental so Proton registers wineopenxr."
    vcrt_fix = "protontricks 403640 vcrun2022"
    d3dc_fix = "protontricks 403640 d3dcompiler_47"
    if inst is None:
        no_game("Prefix OpenXR runtime", oxr_fix)
        no_game("VC++ runtime (native)", vcrt_fix)
        no_game("d3dcompiler_47 (native)", d3dc_fix)
    else:
        rt = prefix_openxr_runtime(inst.prefix)
        json_ok = (inst.prefix / "drive_c/openxr/wineopenxr64.json").is_file()
        add("Prefix OpenXR runtime", rt == PREFIX_RUNTIME and json_ok,
            (rt or "not registered") + ("" if json_ok else " (wineopenxr64.json missing)"), oxr_fix)
        sys32 = inst.prefix / "drive_c/windows/system32"
        bad = [n for n in _VCRT if not is_native_dll(sys32 / f"{n}.dll")]
        add("VC++ runtime (native)", not bad, "native" if not bad else "missing or Wine builtin: " + ", ".join(bad),
            vcrt_fix)
        ok = is_native_dll(sys32 / "d3dcompiler_47.dll")
        add("d3dcompiler_47 (native)", ok, "native" if ok else "missing or Wine builtin", d3dc_fix)

    icd = any((Path(d) / "nvidia_icd.x86_64.json").is_file() for d in icd_dirs)
    name = nvidia_gpu_name(proc_root)
    add("NVIDIA Vulkan", icd and name is not None, name or ("NVIDIA GPU not found in /proc" if icd else "nvidia_icd.x86_64.json not found"),
        "Install the NVIDIA driver's Vulkan ICD (akmod-nvidia / xorg-x11-drv-nvidia).")
    return out


def format_checks(checks: list) -> str:
    lines = []
    for c in checks:
        lines.append(f"{'PASS' if c.ok else 'FAIL'} {c.name}: {c.detail}")
        if not c.ok:
            lines.append(f"      fix: {c.fix}")
    return "\n".join(lines) + "\n"


def launch_options(gpu_name: str) -> str:
    return (f'WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="{gpu_name}" '
            "__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%")
