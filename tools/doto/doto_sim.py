"""Death of the Outsider: simulated-VR session through the dinput8 proxy (no headset, no injection).

The DOTO counterpart of tools/win_perf_sim.py. Mod home: build/doto-home (its own .git marker, build/, artifacts/,
junctions to the repo's third_party and docs, its own shader-cache). The proxy (build/dinput8.dll) is installed beside
Dishonored_DO.exe with d2vr-linux.ini pointing at that home. A run writes the proxy's developer knobs into the home
(simulated XR backend, gameplay script), launches DOTO through Steam, waits for the session, captures the game window
every --shot-every seconds into the run folder, copies the analysis artifacts, closes the game and undoes every knob.

Safety: refuses while DOTO, Dishonored 2, SteamVR or another Steam game runs, while another game window is in the
foreground, or when input was used in the last --idle-minutes (someone is at the PC). DOTO's savegame folder and
dishonored_DOConfig.cfg are backed up and restored after the run unless --keep-saves (a new-game run keeps its saves).

    python tools/doto/doto_sim.py --name boot1 --dll build/doto/d2vr_render_probe_arm_v145701.dll --seconds 120
"""
import argparse
import ctypes
import ctypes.wintypes as W
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOME = ROOT / "build" / "doto-home"
BUILD, ART = HOME / "build", HOME / "artifacts"
GAME_DIR = Path(r"D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider")
EXE = "dishonored_do.exe"
APP = "614570"
SAVE_BASE = Path(os.environ["USERPROFILE"]) / "Saved Games" / "Arkane Studios" / "Dishonored_DO" / "base"
CFG = SAVE_BASE / "dishonored_DOConfig.cfg"
PROXY_SRC = ROOT / "build" / "dinput8.dll"
PROXY_MARKER = b"D2VR-LINUX-PROXY-1"
CANDIDATE = re.compile(r"^d2vr_render_probe_arm_v[0-9]+\.dll$")

sys.path.insert(0, str(ROOT / "tools"))
import win_perf_sim as W2  # noqa: E402  (probe_config, segments_for, load, tasklist, steam_root)

user32 = ctypes.WinDLL("user32", use_last_error=True)


class LII(ctypes.Structure):
    _fields_ = [("cbSize", W.UINT), ("dwTime", W.DWORD)]


def idle_seconds():
    lii = LII(ctypes.sizeof(LII), 0)
    user32.GetLastInputInfo(ctypes.byref(lii))
    return (ctypes.windll.kernel32.GetTickCount() - lii.dwTime) / 1000.0


def foreground_title():
    h = user32.GetForegroundWindow()
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(h, buf, 256)
    return buf.value


def game_pid():
    return next((pid for name, pid in W2.tasklist() if name == EXE), None)


def other_steam_game():
    log = W2.steam_root() / "logs" / "gameprocess_log.txt"
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
    alive = {str(pid): name for name, pid in W2.tasklist()}

    def image(exe):
        m = re.search(r'([^\\/"]+?\.exe)', exe, re.I)
        return m.group(1).strip().lower() if m else ""
    return [(app, pid, exe) for pid, (app, exe) in running.items() if app != APP and alive.get(pid) == image(exe)]


def foreground_exe():
    """Full image path of the process that owns the foreground window."""
    h = user32.GetForegroundWindow()
    pid = W.DWORD()
    user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    k32 = ctypes.windll.kernel32
    hp = k32.OpenProcess(0x1000, False, pid.value)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not hp:
        return ""
    buf = ctypes.create_unicode_buffer(1024)
    n = W.DWORD(1024)
    ok = k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n))
    k32.CloseHandle(hp)
    return buf.value if ok else ""


GAME_PATH_WORDS = ("steamapps\common", "farmingsimulator", "epic games", "xboxgames", "gog galaxy\games")


def conflicts(idle_minutes):
    names = {n for n, _ in W2.tasklist()}
    found = []
    for exe in (EXE, "dishonored2.exe"):
        if exe in names:
            found.append(f"{exe} is running")
    for vr in ("vrserver.exe", "vrcompositor.exe"):
        if vr in names:
            found.append(f"SteamVR is running ({vr})")
    others = other_steam_game()
    if others:
        found.append(f"another Steam game is running: {others}")
    idle = idle_seconds()
    if idle < idle_minutes * 60:
        found.append(f"someone used the PC {idle:.0f} s ago (need {idle_minutes} min idle)")
    fg = foreground_exe().lower()
    if any(w in fg for w in GAME_PATH_WORDS):
        found.append(f"a game is in the foreground: {fg!r}")
    return found


def junction(link, target):
    if link.exists():
        return
    subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True, check=True)


def stage_home(dll):
    """Make build/doto-home the mod home: exactly one candidate DLL, the .git marker the DLL requires, shared data."""
    (HOME / ".git").mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)
    ART.mkdir(parents=True, exist_ok=True)
    (HOME / "shader-cache").mkdir(exist_ok=True)
    junction(HOME / "third_party", ROOT / "third_party")
    junction(HOME / "docs", ROOT / "docs")
    for p in BUILD.iterdir():
        if CANDIDATE.match(p.name) and p.name != dll.name:
            p.unlink()
    shutil.copy2(dll, BUILD / dll.name)
    pdb = dll.with_name(dll.stem.replace("d2vr_render_probe_arm_v", "dxgi_probe_v") + ".pdb")
    if pdb.exists():
        shutil.copy2(pdb, BUILD / pdb.name)
    art = ART / "game-art"
    if not (art / "manifest.json").exists():
        for src in (Path(os.environ["LOCALAPPDATA"]) / "DishonoredVR" / "artifacts" / "game-art", ROOT / "artifacts" / "game-art"):
            if (src / "manifest.json").exists():
                shutil.copytree(src, art, dirs_exist_ok=True)
                break


def install_proxy(out):
    dst = GAME_DIR / "dinput8.dll"
    data = PROXY_SRC.read_bytes()
    if PROXY_MARKER not in data:
        raise SystemExit(f"{PROXY_SRC} is not the mod's proxy")
    if dst.exists() and PROXY_MARKER not in dst.read_bytes():
        raise SystemExit(f"{dst} is another mod's dinput8.dll: refusing to replace it")
    if not dst.exists() or dst.read_bytes() != data:
        shutil.copy2(PROXY_SRC, dst)
        out(f"proxy installed: {dst}")
    ini = GAME_DIR / "d2vr-linux.ini"
    text = str(HOME) + "\n"
    if not ini.exists() or ini.read_text(encoding="utf-8") != text:
        ini.write_text(text, encoding="utf-8")
        out(f"proxy mod home: {HOME}")


def prepare_config(out, width, height):
    """Windowed at the given size, keep running without focus (the launcher's GameKeys.PrepareConfig cvars)."""
    if not CFG.exists():
        out("no dishonored_DOConfig.cfg yet (the game writes it on first start)")
        return
    text = CFG.read_text(encoding="utf-8", errors="replace")
    want = {"r_fullscreen": "0", "r_windowWidth": str(width), "r_windowHeight": str(height),
            "win_pauseOnFocusLoss": "0", "in_controlInactiveWindow": "1"}
    for k, v in want.items():
        line = f'{k} "{v}"'
        if re.search(rf'^{k} ".*"$', text, re.M):
            text = re.sub(rf'^{k} ".*"$', line, text, flags=re.M)
        else:
            text = text.rstrip("\n") + "\n" + line + "\n"
    CFG.write_text(text, encoding="utf-8")


def backup(dest):
    dest.mkdir(parents=True, exist_ok=True)
    if SAVE_BASE.exists():
        shutil.copytree(SAVE_BASE, dest / "base", dirs_exist_ok=True)


def restore(src, out):
    b = src / "base"
    if not b.exists():
        return
    for p in SAVE_BASE.rglob("*"):
        rel = p.relative_to(SAVE_BASE)
        if p.is_file() and not (b / rel).exists():
            p.unlink()
            out(f"removed run file {rel}")
    shutil.copytree(b, SAVE_BASE, dirs_exist_ok=True)


def find_window():
    hwnds = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(h, buf, 256)
        if "Death of the Outsider" in buf.value and user32.IsWindowVisible(h):
            hwnds.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return hwnds[0] if hwnds else None


def capture(path):
    """PrintWindow of the game window (works while it is not in front)."""
    h = find_window()
    if not h:
        return False
    ps = (
        "Add-Type -AssemblyName System.Drawing;"
        "Add-Type @'\nusing System;using System.Runtime.InteropServices;public class PW{"
        "[DllImport(\"user32.dll\")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);"
        "[DllImport(\"user32.dll\")]public static extern bool GetClientRect(IntPtr h,out RECT r);"
        "public struct RECT{public int L,T,R,B;}}\n'@;"
        f"$h=[IntPtr]{h};$r=New-Object PW+RECT;[void][PW]::GetClientRect($h,[ref]$r);"
        "$b=New-Object System.Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T);$g=[System.Drawing.Graphics]::FromImage($b);"
        "$dc=$g.GetHdc();[void][PW]::PrintWindow($h,$dc,3);$g.ReleaseHdc($dc);"
        f"$s=New-Object System.Drawing.Bitmap $b,([int]($b.Width/2)),([int]($b.Height/2));$s.Save('{path}')")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True)
    return r.returncode == 0


def close_game(pid, out):
    if not pid or not game_pid():
        return
    subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
    for _ in range(20):
        if not game_pid():
            return
        time.sleep(1)
    out("game did not close on WM_CLOSE: forcing")
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    for _ in range(20):
        if not game_pid():
            return
        time.sleep(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--dll", required=True, help="the DOTO DLL to stage (build/doto/d2vr_render_probe_arm_vNNNN.dll)")
    ap.add_argument("--scenario", default="idle")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--lead", type=int, default=60)
    ap.add_argument("--resolution", type=float, default=1.0)
    ap.add_argument("--eye-size", default="1600 1728")
    ap.add_argument("--knob", action="append", default=[], help="NAME or NAME=VALUE written into the home's build/ for this run")
    ap.add_argument("--probe", action="append", default=[], help="FIELD=VALUE override of the simulated probe config (e.g. tracked_arms=0)")
    ap.add_argument("--shot-every", type=int, default=10)
    ap.add_argument("--idle-minutes", type=float, default=15)
    ap.add_argument("--keep-saves", action="store_true", help="keep the saves the run made (new game progress)")
    ap.add_argument("--no-session", action="store_true", help="plain game start: no probe override knobs (the proxy waits for a trigger)")
    ap.add_argument("--window", default="1596x993")
    ap.add_argument("--out", default=str(ROOT / "artifacts" / "doto-runs"))
    a = ap.parse_args()
    run = Path(a.out) / a.name
    run.mkdir(parents=True, exist_ok=True)
    lines = []

    def out(msg):
        line = time.strftime("%H:%M:%S ") + msg
        print(line, flush=True)
        lines.append(line)

    bad = conflicts(a.idle_minutes)
    if bad:
        sys.exit("refusing to run: " + "; ".join(bad))
    dll = (ROOT / a.dll).resolve() if not Path(a.dll).is_absolute() else Path(a.dll)
    stage_home(dll)
    install_proxy(out)
    backup(run / "save-backup")
    width, height = (int(x) for x in a.window.split("x"))
    prepare_config(out, width, height)
    out(f"staged {dll.name} in {BUILD}")
    written = []
    pid = None
    started = time.time()
    try:
        if not a.no_session:
            end_ms = (a.seconds - 6) * 1000
            segs = W2.segments_for(a.scenario, a.lead * 1000, end_ms)
            sgs = W2.load("d2vr_simulated_gameplay_script", ROOT / "tools" / "simulated_gameplay_script.py")
            tmp = ROOT / "artifacts" / "doto-runs" / "_script.json"
            tmp.parent.mkdir(parents=True, exist_ok=True)
            tmp.write_text(json.dumps({"segments": segs}, indent=1) + "\n", encoding="utf-8")
            script_json, script_cfg = sgs.compile_script(tmp, tmp.parent, a.seconds)
            cfg = W2.probe_config(a.seconds, a.resolution)
            if a.probe:
                fields = W2.PROBE_FIELDS
                vals = cfg.split()
                for kv in a.probe:
                    k, _, v = kv.partition("=")
                    vals[fields.index(k)] = v
                cfg = " ".join(vals) + "\n"
            planned = {BUILD / W2.OVERRIDE: cfg, BUILD / W2.SCRIPT_CFG: script_cfg, ART / W2.SCRIPT_JSON: script_json,
                       BUILD / W2.EYE_SIZE: a.eye_size + "\n"}
            for k in a.knob:
                name, _, value = k.partition("=")
                planned[BUILD / name] = (value + "\n") if value else ""
            for path, text in planned.items():
                path.write_text(text, encoding="utf-8")
                written.append(path)
            out("knobs: " + ", ".join(p.name for p in written))
        (run / "run-args.json").write_text(json.dumps(vars(a), indent=1))
        out(f"launching steam://rungameid/{APP}")
        os.startfile(f"steam://rungameid/{APP}")
        for _ in range(120):
            pid = game_pid()
            if pid:
                break
            time.sleep(1)
        if not pid:
            raise RuntimeError("the game did not start in 120 s")
        out(f"game pid {pid}")
        proxy_log = ART / "linux-proxy.log"

        def plog():
            try:
                if proxy_log.stat().st_mtime >= started - 2:
                    return proxy_log.read_text(encoding="utf-8", errors="replace")
            except OSError:
                pass
            return ""
        shot = 0
        next_shot = time.time() + 15
        deadline = time.time() + 240 + a.seconds + 60
        began = False
        while time.time() < deadline and game_pid():
            text = plog()
            if not began and "StartProbe begin" in text:
                began = True
                out("session started")
            if began and "StartProbe returned" in text:
                break
            if time.time() >= next_shot:
                shot += 1
                if capture(str(run / f"shot-{shot:03d}.png")):
                    out(f"shot {shot}")
                next_shot = time.time() + a.shot_every
            time.sleep(1)
        out("session over: " + (re.findall(r"StartProbe returned \d+", plog()) or ["(no StartProbe returned)"])[-1])
        time.sleep(3)
        for p in ART.iterdir():
            if (p.is_file() and p.stat().st_mtime >= started - 2 and p.suffix.lower() not in (".rgba", ".dmp", ".bin", ".raw", ".png", ".r32", ".rg16f")
                    and p.stat().st_size <= 20 << 20):
                shutil.copy2(p, run / p.name)
        out(f"artifacts copied to {run}")
    except Exception as e:  # noqa: BLE001
        out(f"ERROR: {e}")
    finally:
        close_game(pid or game_pid(), out)
        for path in written:
            try:
                path.unlink()
            except OSError as e:
                out(f"could not remove {path}: {e}")
        time.sleep(5)
        if not a.keep_saves:
            restore(run / "save-backup", out)
            out("saves and config restored")
        (run / "runner.log").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
