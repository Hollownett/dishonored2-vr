"""d2vr-linux command line: install, uninstall, doctor, launch-options, start, stop."""
import argparse
import sys
import urllib.request
from pathlib import Path

from . import doctor, modhome, steam


class CliError(Exception):
    pass


def _parser():
    p = argparse.ArgumentParser(prog="d2vr-linux", description="Dishonored 2 VR mod for Linux (Proton).")
    p.add_argument("--steam-root", type=Path, help="Steam root directory (default: auto-detect)")
    sub = p.add_subparsers(dest="command", required=True, metavar="COMMAND")
    s = sub.add_parser("install", help="install a release zip into the Proton prefix and the proxy into the game")
    s.add_argument("--zip", required=True, type=Path, dest="zip_path", help="d2vr-vNNN-dll.zip release")
    s.add_argument("--proxy", type=Path, default=None, help="dinput8.dll proxy (default: build/linux-proxy)")
    s.add_argument("--openxr-zip", type=Path, default=None, help="OpenXR SDK zip (default: download and verify)")
    sub.add_parser("uninstall", help="remove the proxy and ini from the game folder (mod home is kept)")
    s = sub.add_parser("doctor", help="check the host, prefix and install")
    s.add_argument("--fix-openxr", action="store_true", help="set SteamVR as the host OpenXR runtime")
    s.add_argument("--mod-home", type=Path, help="mod home directory to check")
    sub.add_parser("launch-options", help="print the Steam launch options line")
    for name, text in (("start", "ask the running game to start VR"), ("stop", "ask the running game to stop VR")):
        s = sub.add_parser(name, help=text)
        s.add_argument("--mod-home", type=Path, help="mod home directory")
    return p


def _gpu_launch_line(proc_root):
    gpu = doctor.nvidia_gpu_name(proc_root)
    return doctor.launch_options(gpu) if gpu else None


def _cmd_install(a, home, proc_root, urlopen):
    inst = steam.locate(home, a.steam_root)
    changed = modhome.install(
        inst, a.zip_path, a.proxy or modhome.DEFAULT_PROXY, a.openxr_zip, home / ".cache/d2vr-linux",
        game_sha256=steam.GAME_SHA256, openxr_source=modhome.OPENXR_SOURCE, urlopen=urlopen)
    print("Changed files:" if changed else "Already up to date.")
    for c in changed:
        print(f"  {c}")
    line = _gpu_launch_line(proc_root)
    if line:
        print("Steam launch options:")
        print(line)
    return 0


def _cmd_uninstall(a, home):
    inst = steam.locate(home, a.steam_root)
    removed = modhome.uninstall(inst.game_dir)
    print("Removed files:" if removed else "Nothing to remove.")
    for r in removed:
        print(f"  {r}")
    print(f"Mod home left in place: {modhome.mod_home_dir(inst.prefix)}")
    return 0


def _cmd_doctor(a, home, proc_root, icd_dirs):
    inst, error = None, ""
    try:
        inst = steam.locate(home, a.steam_root)
    except steam.SteamError as e:
        error = str(e)
    if a.fix_openxr:
        if inst is not None:
            libs = inst.libraries
        else:
            roots = [a.steam_root] if a.steam_root else steam.steam_roots(home)
            libs = [l for r in roots for l in steam.library_paths(r)]
        steamvr = steam.find_steamvr(libs)
        if steamvr is None:
            print("Cannot set the OpenXR runtime: SteamVR (app 250820) is not installed.")
        else:
            try:
                print(f"Created {doctor.fix_host_runtime(home, steamvr)}")
            except FileExistsError as e:
                print(f"Not changed: {e} already exists; remove it yourself if it should point at SteamVR.")
    checks = doctor.run_checks(
        inst, error, home, proc_root, icd_dirs, mod_home=a.mod_home,
        game_sha256=steam.GAME_SHA256, openxr_source=modhome.OPENXR_SOURCE)
    print(doctor.format_checks(checks), end="")
    return 0 if all(c.ok for c in checks) else 1


def _cmd_launch_options(proc_root):
    line = _gpu_launch_line(proc_root)
    if line is None:
        raise CliError("no NVIDIA GPU found")
    print(line)
    return 0


def _cmd_request(a, home, name):
    if a.mod_home:
        mod_home = a.mod_home
    else:
        mod_home = modhome.mod_home_dir(steam.locate(home, a.steam_root).prefix)
    if not (mod_home / "build").is_dir():
        raise CliError(f"{mod_home / 'build'} does not exist; run: d2vr-linux install --zip <d2vr-vNNN-dll.zip>")
    print(modhome.write_request(mod_home, name))
    return 0


def main(argv=None, *, home=None, proc_root=Path("/proc"), icd_dirs=None, urlopen=None) -> int:
    args = _parser().parse_args(argv)
    home = Path.home() if home is None else Path(home)
    icd_dirs = doctor.DEFAULT_ICD_DIRS if icd_dirs is None else icd_dirs
    urlopen = urlopen or urllib.request.urlopen
    try:
        if args.command == "install":
            return _cmd_install(args, home, proc_root, urlopen)
        if args.command == "uninstall":
            return _cmd_uninstall(args, home)
        if args.command == "doctor":
            return _cmd_doctor(args, home, proc_root, icd_dirs)
        if args.command == "launch-options":
            return _cmd_launch_options(proc_root)
        return _cmd_request(args, home, modhome.START_TRIGGER if args.command == "start" else modhome.STOP_REQUEST)
    except (CliError, steam.SteamError, modhome.InstallError, OSError) as e:
        print(f"d2vr-linux: error: {e}", file=sys.stderr)
        return 1
