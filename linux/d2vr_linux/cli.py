"""d2vr-linux command line: install, uninstall, doctor, launch-options, feature, start, stop, sim (developer)."""
import argparse
import sys
import urllib.request
from pathlib import Path

from . import doctor, modhome, sim, steam


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
    s = sub.add_parser("feature", help="list the feature knobs or switch one on or off")
    fsub = s.add_subparsers(dest="feature_cmd", required=True, metavar="ACTION")
    fl = fsub.add_parser("list", help="print each feature: id, on/off, label")
    fl.add_argument("--mod-home", type=Path, help="mod home directory")
    for nm in ("on", "off"):
        fo = fsub.add_parser(nm, help=f"switch a feature {nm} (it stays {nm} across reinstalls)")
        fo.add_argument("id", help="feature id from 'feature list'")
        fo.add_argument("--mod-home", type=Path, help="mod home directory")
    s = sub.add_parser("sim", help="developer: run the game with the simulated-VR backend and a gameplay script (no headset)",
                       description="Developer-only simulated-VR test run. Refuses while the game or SteamVR vrserver runs. "
                       "Backs up your saves, launches the game through Steam, drives a scripted scenario, snapshots the "
                       "artifacts, closes the game and restores your saves. See linux/README.md.")
    s.add_argument("--scenario", default="oil_tank_drop", help="tools/linux/scenarios/<name>.py (default: oil_tank_drop)")
    s.add_argument("--script", type=Path, help="gameplay script JSON ({\"segments\": [...]}) instead of a scenario")
    s.add_argument("--seconds", type=int, default=0, help="session length 10-120 (default: lead + scenario + 6)")
    s.add_argument("--lead", type=float, default=sim.LEAD_SECONDS, help="neutral seconds before the scenario while the save loads (default 75)")
    s.add_argument("--test-save", type=Path, help="savegame folder (SLOT*/profile.bin) to play instead of your saves")
    s.add_argument("--settle", type=int, default=None, help="proxy settle seconds before the session starts (default: proxy's 45)")
    s.add_argument("--no-autocontinue", dest="autocontinue", action="store_false", help="do not post Enter to the title menus")
    s.add_argument("--autocontinue-delay", type=int, default=10, help="seconds after the session starts before Enter (default 10)")
    s.add_argument("--status-field", action="append", metavar="PATH",
                   help="xr-live-status.json field to print, dotted path (repeatable; default gameplay, logic_mask, game_layers_submitted, carry)")
    s.add_argument("--start-timeout", type=float, default=480, help="seconds to wait for the session to start (default 480)")
    s.add_argument("--close-timeout", type=float, default=30, help="seconds between the polite and the forced close (default 30)")
    s.add_argument("--steam-cmd", default=sim.DEFAULT_STEAM_CMD, help="command that launches the game (default: steam -applaunch 403640)")
    s.add_argument("--backup-root", type=Path, default=None, help="default ~/d2vr-test-builds/save-backups")
    s.add_argument("--out-root", type=Path, default=None, help="default ~/d2vr-test-builds/sim-runs")
    s.add_argument("--mod-home", type=Path, help="mod home directory")
    s.add_argument("--candidate-dll", type=Path, metavar="PATH",
                   help="run this DLL instead of the installed one: the installed DLL is backed up, swapped for the run and restored (verified by SHA-256)")
    s.add_argument("--knob", action="append", metavar="NAME", default=[],
                   help="write an empty build/NAME dev knob for this run (repeatable; [a-z0-9-]+); removed afterwards whatever happens")
    s.add_argument("--probe-mode", choices=list(sim.PROBE_MODES), default="xr",
                   help="xr (default, simulated headset) or the flat diagnostic modes paired / single (no XR, for the single-view measurement)")
    s.add_argument("--vr-setting", action="append", metavar="KEY=VALUE", default=[],
                   help="VR setting for this run only (repeatable): temporal_aa 0-6, resolution 0.5-2.0, mirror 2. Your vr-settings.cfg is never read or changed")
    s.add_argument("--env", action="append", metavar="KEY=VALUE", default=[],
                   help="launch environment (repeatable): DXVK_HUD, DXVK_LOG_LEVEL or __GL_THREADED_OPTIMIZATIONS")
    s.add_argument("--dry-run", action="store_true", help="check everything and print the plan; write and launch nothing")
    s.add_argument("--restore", nargs="?", const=True, default=None, metavar="BACKUP",
                   help="restore your saves from the newest restore-pending backup (or BACKUP) and remove the dev knobs")
    return p


def _cmd_sim(a, home, proc_root, sim_env):
    root = home / "d2vr-test-builds"
    a.backup_root = a.backup_root or root / "save-backups"
    a.out_root = a.out_root or root / "sim-runs"
    env = sim_env or sim.Env(proc_root=proc_root)
    env.steam_cmd = a.steam_cmd
    env.close_timeout = a.close_timeout
    try:
        return sim.run_sim(a, home, env)
    except sim.SimError as e:
        raise CliError(str(e))


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


def _cmd_feature(a, home):
    mod_home = a.mod_home or modhome.mod_home_dir(steam.locate(home, a.steam_root).prefix)
    if not (mod_home / "build").is_dir():
        raise CliError(f"{mod_home / 'build'} does not exist; run: d2vr-linux install --zip <d2vr-vNNN-dll.zip>")
    if a.feature_cmd == "list":
        for fid, on, label in modhome.feature_states(mod_home):
            print(f"{fid:<18} {'on ' if on else 'off'}  {label}")
        return 0
    ids = [f["id"] for f in modhome.load_features()]
    if a.id not in ids:
        raise CliError(f"unknown feature {a.id!r}; known: {', '.join(ids)}")
    modhome.set_feature(mod_home, a.id, a.feature_cmd == "on")
    print(f"{a.id}: {a.feature_cmd}")
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


def main(argv=None, *, home=None, proc_root=Path("/proc"), icd_dirs=None, urlopen=None, sim_env=None) -> int:
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
        if args.command == "feature":
            return _cmd_feature(args, home)
        if args.command == "sim":
            return _cmd_sim(args, home, proc_root, sim_env)
        return _cmd_request(args, home, modhome.START_TRIGGER if args.command == "start" else modhome.STOP_REQUEST)
    except (CliError, sim.SimError, steam.SteamError, modhome.InstallError, modhome.FeatureError, OSError) as e:
        print(f"d2vr-linux: error: {e}", file=sys.stderr)
        return 1
