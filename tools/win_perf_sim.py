"""Windows simulated-VR performance run through the installed dinput8 proxy (no headset, no injection).

The Windows counterpart of `linux/d2vr-linux sim` for measurement runs: writes the proxy's developer knobs
(build/linux-probe-override.cfg with the simulated XR backend, build/simulated-gameplay-script.cfg,
build/linux-autocontinue), launches Dishonored 2 through Steam, lets the proxy load the single
build/d2vr_render_probe_arm_v*.dll and Continue into the last save, optionally samples every game thread
(tools/sample_threads.py) during gameplay, copies the run's artifacts, closes the game and puts everything back.

Safety (AGENTS.md): refuses to run while Dishonored 2, SteamVR or another Steam game runs, or when Steam is signed into
another account than the saves; backs up the save folder and dishonored2Config.cfg first and restores them byte for byte
(files the run created are removed); every knob it writes or moves aside is recorded and undone in a finally block.

    python tools/win_perf_sim.py --name r1 --knob native-gpu-timing --aa-mode 3
    python tools/win_perf_sim.py --name r2 --resolution 0.5 --knob native-gpu-timing --aa-mode 3
    python tools/win_perf_sim.py --name r3 --disable full-body --disable hand-body --knob native-gpu-timing
    python tools/win_perf_sim.py --name r1s --sample-threads 10
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD, ART = ROOT / "build", ROOT / "artifacts"
SAVE_BASE = Path(os.environ["USERPROFILE"]) / "Saved Games" / "Arkane Studios" / "Dishonored2" / "base"
OVERRIDE, SCRIPT_CFG, SCRIPT_JSON = "linux-probe-override.cfg", "simulated-gameplay-script.cfg", "simulated-gameplay-script.json"
AUTOCONTINUE, EYE_SIZE, AA_MODE = "linux-autocontinue", "sim-eye-size", "aa-mode"
PROXY_LOG = ART / "linux-proxy.log"
CANDIDATE = re.compile(r"^d2vr_render_probe_arm_v[0-9]+\.dll$")
PROBE_FIELDS = ("pid camera mode paired skip_taa full_render_scale symmetric_frustum controllers headset_size eye_boundary "
                "native_eye_capture seconds arm_upload_observer optical_density arm_palette_copy tracked_arms stable_camera "
                "resolution eye_aa camera_trace turning snap smooth saved_settings menu simulated controller_aim "
                "physical_grab left_aim cleanup_exception gameplay_script weapon_attachment").split()


def probe_config(seconds, resolution):
    """linux/d2vr_linux/sim.py sim_probe_config (xr mode) with headset sizing: the simulated XR backend at the headset eye size."""
    v = dict(pid="{pid}", camera=0, mode=6, paired=1, skip_taa=0, full_render_scale=1, symmetric_frustum=1, controllers=1,
             headset_size=1, eye_boundary=1, native_eye_capture=1, seconds=int(seconds), arm_upload_observer=1,
             optical_density=0, arm_palette_copy=0, tracked_arms=1, stable_camera=1, resolution=format(float(resolution), "g"),
             eye_aa=0, camera_trace=0, turning=1, snap=45, smooth=90, saved_settings=0, menu=0, simulated=2, controller_aim=1,
             physical_grab=1, left_aim=1, cleanup_exception=0, gameplay_script=1, weapon_attachment=0)
    return " ".join(str(v[k]) for k in PROBE_FIELDS) + "\n"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def segments_for(scenario, lead_ms, end_ms):
    """The scenario's segments (tools/linux/scenarios), cut at end_ms."""
    mod = load("d2vr_scenario_" + scenario, ROOT / "tools" / "linux" / "scenarios" / (scenario + ".py"))
    out = []
    for s in mod.build(lead_ms):
        if s["start_ms"] >= end_ms:
            break
        out.append(dict(s, end_ms=min(s["end_ms"], end_ms)))
    return out


def tasklist():
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1:
            rows.append((parts[0].lower(), int(parts[1]) if parts[1].isdigit() else 0))
    return rows


def game_pid():
    return next((pid for name, pid in tasklist() if name == "dishonored2.exe"), None)


def steam_root():
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
        return Path(winreg.QueryValueEx(k, "SteamPath")[0])


def other_steam_game():
    """Steam's tracked game processes still alive, other than Dishonored 2 (tools/other_game_running.py with this PC's Steam path)."""
    log = steam_root() / "logs" / "gameprocess_log.txt"
    running = {}
    if log.is_file():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.search(r'AppID (\d+) adding PID (\d+) as a tracked process "(.*)"', line)
            if m:
                running[m.group(2)] = (m.group(1), m.group(3))
                continue
            m = re.search(r"AppID (\d+) no longer tracking PID (\d+)", line)
            if m:
                running.pop(m.group(2), None)
    alive = {str(pid): name for name, pid in tasklist()}

    def image(exe):
        m = re.search(r'([^\\/"]+?\.exe)', exe, re.I)
        return m.group(1).strip().lower() if m else ""
    return [(app, pid, exe) for pid, (app, exe) in running.items() if app != "403640" and alive.get(pid) == image(exe)]


def steam_account_ok():
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as k:
            active = int(winreg.QueryValueEx(k, "ActiveUser")[0])
        owner = int(re.search(r'"accountid"\s+"(\d+)"', (SAVE_BASE / "savegame" / "steam_autocloud.vdf").read_text()).group(1))
    except (OSError, AttributeError, ValueError):
        return False
    return active == owner


def conflicts():
    names = {n for n, _ in tasklist()}
    found = []
    if "dishonored2.exe" in names:
        found.append("Dishonored2.exe is running")
    for vr in ("vrserver.exe", "vrcompositor.exe"):
        if vr in names:
            found.append(f"SteamVR is running ({vr})")
    others = other_steam_game()
    if others:
        found.append(f"another Steam game is running: {others}")
    if not steam_account_ok():
        found.append("Steam is signed into another account than the saves belong to")
    return found


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hashes(root):
    return {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob("*")) if p.is_file()}


def backup_saves(dest):
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SAVE_BASE / "savegame", dest / "savegame", dirs_exist_ok=True)
    shutil.copy2(SAVE_BASE / "dishonored2Config.cfg", dest / "dishonored2Config.cfg")
    record = {"savegame": tree_hashes(dest / "savegame"), "config": sha(dest / "dishonored2Config.cfg")}
    (dest / "record.json").write_text(json.dumps(record, indent=1))
    if record["savegame"] != tree_hashes(SAVE_BASE / "savegame"):
        raise SystemExit("save backup does not match the save folder")
    return record


def restore_saves(src, record):
    """Byte-for-byte restore. Returns the list of files that differed (restored or removed)."""
    changed = []
    live = SAVE_BASE / "savegame"
    now = tree_hashes(live)
    for rel, h in record["savegame"].items():
        if now.get(rel) != h:
            (live / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src / "savegame" / rel, live / rel)
            changed.append("restored " + rel)
    for rel in now:
        if rel not in record["savegame"]:
            (live / rel).unlink()
            changed.append("removed new " + rel)
    cfg = SAVE_BASE / "dishonored2Config.cfg"
    if sha(cfg) != record["config"]:
        shutil.copy2(src / "dishonored2Config.cfg", cfg)
        changed.append("restored dishonored2Config.cfg")
    if tree_hashes(live) != record["savegame"] or sha(cfg) != record["config"]:
        raise SystemExit("SAVE RESTORE FAILED: compare " + str(src) + " with " + str(SAVE_BASE))
    return changed


def read_status():
    try:
        return json.loads((ART / "xr-live-status.json").read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return None


def close_game(pid, out):
    if not pid or not game_pid():
        return
    subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
    for _ in range(30):
        if not game_pid():
            return
        time.sleep(1)
    out("game did not close on WM_CLOSE: forcing")
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    for _ in range(30):
        if not game_pid():
            return
        time.sleep(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--scenario", default="idle", help="tools/linux/scenarios/<name>.py (idle, head_turn_walk, ...)")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--lead", type=int, default=60, help="seconds of neutral menu-driving input before the scenario")
    ap.add_argument("--resolution", type=float, default=1.0)
    ap.add_argument("--eye-size", default="2246 2419", help="build/sim-eye-size: the simulated headset's eye size")
    ap.add_argument("--aa-mode", type=int, help="build/aa-mode 0..8")
    ap.add_argument("--knob", action="append", default=[], help="NAME or NAME=VALUE written into build/ for this run")
    ap.add_argument("--disable", action="append", default=[], help="a build/ knob file moved aside for this run")
    ap.add_argument("--sample-threads", type=int, default=0, help="seconds of tools/sample_threads.py during gameplay")
    ap.add_argument("--sample-delay", type=int, default=15, help="seconds of gameplay before sampling")
    ap.add_argument("--during", help="a tool run once at --sample-delay into gameplay, e.g. 'tools/stack_callers.py {pid} 6'; output in during.txt")
    ap.add_argument("--out", default=str(ART / "perf-night"))
    ap.add_argument("--command", action="append", default=[], help="'T LINE': write LINE to artifacts/vr-command.txt T seconds into gameplay (mod test commands, e.g. '30 cvar r_x 1')")
    ap.add_argument("--game-cvar", action="append", default=[], help="NAME=VALUE appended to dishonored2Config.cfg for this run (restored after)")
    ap.add_argument("--autocontinue", action="store_true", help="also arm the proxy's Enter presses (they repeat while it cannot read a >64 KiB status)")
    a = ap.parse_args()
    if not 10 <= a.seconds <= 120:
        sys.exit("--seconds must be 10..120")
    run = Path(a.out) / a.name
    run.mkdir(parents=True, exist_ok=True)
    log_lines = []

    def out(msg):
        line = time.strftime("%H:%M:%S ") + msg
        print(line, flush=True)
        log_lines.append(line)

    bad = conflicts()
    if bad:
        sys.exit("refusing to run: " + "; ".join(bad))
    candidates = [p.name for p in BUILD.iterdir() if CANDIDATE.match(p.name)]
    if len(candidates) != 1:
        sys.exit(f"build/ must hold exactly one d2vr_render_probe_arm_v*.dll for the proxy, found {candidates}")
    out(f"candidate {candidates[0]} sha256 {sha(BUILD / candidates[0])[:16]}")
    backup = run / "save-backup"
    record = backup_saves(backup)
    out(f"saves backed up to {backup} ({len(record['savegame'])} files)")

    written, moved = [], []
    pid = None
    try:
        end_ms = (a.seconds - 6) * 1000
        segs = segments_for(a.scenario, a.lead * 1000, end_ms)
        sgs = load("d2vr_simulated_gameplay_script", ROOT / "tools" / "simulated_gameplay_script.py")
        tmp = ROOT / "artifacts" / "perf-night" / "_script.json"
        tmp.parent.mkdir(parents=True, exist_ok=True)
        tmp.write_text(json.dumps({"segments": segs}, indent=1) + "\n", encoding="utf-8")
        script_json, script_cfg = sgs.compile_script(tmp, tmp.parent, a.seconds)
        planned = {BUILD / OVERRIDE: probe_config(a.seconds, a.resolution), BUILD / SCRIPT_CFG: script_cfg,
                   ART / SCRIPT_JSON: script_json, BUILD / EYE_SIZE: a.eye_size + "\n"}
        if a.autocontinue:
            planned[BUILD / AUTOCONTINUE] = "10\n"
        if a.aa_mode is not None:
            planned[BUILD / AA_MODE] = f"{a.aa_mode}\n"
        for k in a.knob:
            name, _, value = k.partition("=")
            if not re.fullmatch(r"[a-z0-9-]+", name):
                sys.exit(f"bad knob {k!r}")
            planned[BUILD / name] = (value + "\n") if value else ""
        for path in planned:
            if path.exists():
                sys.exit(f"{path} already exists: remove it or run without it (the run would overwrite the user's file)")
        for name in a.disable:
            src = BUILD / name
            if src.exists():
                dst = run / "moved-aside" / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(src), str(dst))
                moved.append((src, dst))
        for path, text in planned.items():
            path.write_text(text if isinstance(text, str) else text, encoding="utf-8")
            written.append(path)
        if a.game_cvar:
            cfg = SAVE_BASE / "dishonored2Config.cfg"
            with open(cfg, "a", encoding="utf-8") as f:
                for kv in a.game_cvar:
                    name, _, value = kv.partition("=")
                    if not re.fullmatch(r"[A-Za-z0-9_]+", name) or not re.fullmatch(r"[A-Za-z0-9_. -]*", value):
                        raise SystemExit(f"bad --game-cvar {kv!r}")
                    f.write(f'\n{name} "{value}"')
                f.write("\n")
            out("game cvars appended: " + ", ".join(a.game_cvar))
        out("knobs: " + ", ".join(p.name for p in written) + (" | moved aside: " + ", ".join(s.name for s, _ in moved) if moved else ""))
        (run / "run-args.json").write_text(json.dumps(vars(a), indent=1))

        started = time.time()
        out("launching steam://rungameid/403640")
        os.startfile("steam://rungameid/403640")
        for _ in range(120):
            pid = game_pid()
            if pid:
                break
            time.sleep(1)
        if not pid:
            raise RuntimeError("the game did not start in 120 s")
        out(f"game pid {pid}")

        def proxy_log():
            try:
                if PROXY_LOG.stat().st_mtime >= started - 2:
                    return PROXY_LOG.read_text(encoding="utf-8", errors="replace")
            except OSError:
                pass
            return ""
        deadline = time.time() + 240
        while time.time() < deadline and "StartProbe begin" not in proxy_log():
            if not game_pid():
                raise RuntimeError("the game exited before the session began")
            time.sleep(2)
        if "StartProbe begin" not in proxy_log():
            raise RuntimeError("no 'StartProbe begin' in linux-proxy.log: " + proxy_log()[-400:])
        out("session started")
        gameplay_at = None
        sampled = False
        pending_commands = list(a.command)
        deadline = time.time() + a.seconds + 150
        while time.time() < deadline:
            text = proxy_log()
            if "StartProbe returned" in text or not game_pid():
                break
            st = read_status()
            if st and st.get("gameplay") and gameplay_at is None:
                gameplay_at = time.time()
                out("gameplay reached")
            if a.sample_threads and gameplay_at and not sampled and time.time() - gameplay_at >= a.sample_delay:
                out(f"sampling threads for {a.sample_threads} s")
                res = subprocess.run([sys.executable, str(ROOT / "tools" / "sample_threads.py"), str(pid), str(a.sample_threads), "500", "40"],
                                     capture_output=True, text=True)
                (run / "threads.txt").write_text(res.stdout + "\n" + res.stderr, encoding="utf-8")
                sampled = True
                out("sampling done")
            if gameplay_at:
                for c in list(pending_commands):
                    when, _, line = c.partition(" ")
                    if time.time() - gameplay_at >= float(when):
                        with open(ART / "vr-command.txt", "a", encoding="utf-8") as f:
                            f.write(line + chr(10))
                        out(f"command at +{when}s: {line}")
                        pending_commands.remove(c)
            if a.during and gameplay_at and not (run / "during.txt").exists() and time.time() - gameplay_at >= a.sample_delay:
                cmd = [sys.executable, *[str(ROOT / x) if x.startswith("tools/") else x.replace("{pid}", str(pid)) for x in a.during.split()]]
                out("during: " + " ".join(cmd))
                res = subprocess.run(cmd, capture_output=True, text=True)
                (run / "during.txt").write_text(res.stdout + "\n" + res.stderr, encoding="utf-8")
                out("during done")
            time.sleep(2)
        out("session over: " + (re.findall(r"StartProbe returned \d+", proxy_log()) or ["(no StartProbe returned)"])[-1])
        time.sleep(3)
        for p in ART.iterdir():  # analysis files only: no image dumps (21 MB each with diagnostics on; 2026-11-19 a night of runs filled C:)
            if (p.is_file() and p.stat().st_mtime >= started - 2 and p.suffix.lower() not in (".rgba", ".dmp", ".bin", ".raw", ".png", ".r32", ".rg16f")
                    and p.stat().st_size <= 20 << 20):
                shutil.copy2(p, run / p.name)
        out(f"artifacts copied to {run}")
    except Exception as e:  # noqa: BLE001 - always clean up below
        out(f"ERROR: {e}")
    finally:
        close_game(pid or game_pid(), out)
        for path in written:
            try:
                path.unlink()
            except OSError as e:
                out(f"could not remove {path}: {e}")
        for src, dst in moved:
            shutil.move(str(dst), str(src))
        time.sleep(5)  # Steam Cloud and the game's file handles
        changed = restore_saves(backup, record)
        out("saves verified unchanged" if not changed else "saves restored: " + "; ".join(changed))
        shutil.rmtree(backup, ignore_errors=True)  # restore_saves raised if the live folder did not match the record byte for byte
        leftovers = [p.name for p in written if p.exists()] + [s.name for s, _ in moved if not s.exists()]
        out("knobs cleaned" if not leftovers else f"KNOB CLEANUP INCOMPLETE: {leftovers}")
        (run / "runner.log").write_text("\n".join(log_lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
