"""Developer-only simulated-VR test runs on Linux (`d2vr-linux sim`).

Runs the real game under Proton with the mod's scripted XR backend (src/simulated_xr.hpp, no headset) by writing a dev probe
config override that the proxy honours, then restores the user's saves and removes every dev knob. Safety rules:

- refuses to start while Dishonored2.exe or SteamVR's vrserver runs (the user's own VR session), and never signals a game it
  did not launch itself;
- backs up the saves and dishonored2Config.cfg before touching anything (never deleted) and marks the backup `restore-pending`
  until a verified restore; a pending backup blocks further runs until `sim --restore`.
"""
import filecmp
import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from . import modhome, steam

SIM_MARKER = b"D2VR-LINUX-SIM-1"  # the proxy carries this once it supports the override and autocontinue knobs
OVERRIDE_NAME = "linux-probe-override.cfg"
AUTOCONTINUE_NAME = "linux-autocontinue"
SETTLE_NAME = "linux-settle-seconds"
SCRIPT_CFG = "simulated-gameplay-script.cfg"
SCRIPT_JSON = "simulated-gameplay-script.json"
SAVE_BASE_REL = Path("drive_c/users/steamuser/Saved Games/Arkane Studios/Dishonored2/base")
PENDING = "restore-pending"
REPO_ROOT = modhome.REPO_ROOT
SCENARIO_DIR = REPO_ROOT / "tools/linux/scenarios"
DEFAULT_ROOT = Path.home() / "d2vr-test-builds"
DEFAULT_FIELDS = ["gameplay", "logic_mask", "game_layers_submitted", "carry"]
DEFAULT_STEAM_CMD = "steam -applaunch 403640"
REFUSAL_MARKERS = ("expected exactly one", "lacks required", "cannot load", "restart the game", "ProbeStatus=",
                   "does not export", "cannot write build", "StartProbe returned")
LEAD_SECONDS = 75  # the save loads inside the session (tools/vr_selftest.py --inject-at-title)
SESSION_PAD_SECONDS = 6
LOOSE_GAME_NAMES = ("dishonored2.exe",)


class SimError(Exception):
    pass


# ---------------------------------------------------------------- processes

def _scan(proc_root):
    """(pid, comm, argv) for every readable process."""
    out = []
    try:
        entries = list(Path(proc_root).iterdir())
    except OSError:
        return out
    for d in entries:
        if not d.name.isdigit():
            continue
        try:
            argv = [a for a in (d / "cmdline").read_bytes().decode("utf-8", "replace").split("\0") if a]
            comm = (d / "comm").read_text(errors="replace").strip()
        except OSError:
            continue
        out.append((int(d.name), comm, argv))
    return out


def _base(arg):
    return re.split(r"[/\\]", arg)[-1].lower()


def game_processes(proc_root, strict=False):
    """pids touching Dishonored2.exe. strict: only the game process itself (comm or argv[0]); loose also wrappers that name it."""
    pids = []
    for pid, comm, argv in _scan(proc_root):
        if pid == os.getpid():
            continue
        if comm.lower().startswith("dishonored2") or (argv and _base(argv[0]) in LOOSE_GAME_NAMES):
            pids.append(pid)
        elif not strict and any(_base(a) in LOOSE_GAME_NAMES for a in argv):
            pids.append(pid)
    return pids


def vr_server_processes(proc_root):
    return [pid for pid, comm, argv in _scan(proc_root)
            if pid != os.getpid() and (comm == "vrserver" or (argv and _base(argv[0]) == "vrserver"))]


def conflicts(proc_root):
    found = []
    g = game_processes(proc_root)
    if g:
        found.append(f"Dishonored2.exe is running (pid {', '.join(map(str, g))})")
    v = vr_server_processes(proc_root)
    if v:
        found.append(f"SteamVR vrserver is running (pid {', '.join(map(str, v))})")
    return found


# ---------------------------------------------------------------- saves

def save_paths(prefix):
    base = Path(prefix) / SAVE_BASE_REL
    return base / "savegame", base / "dishonored2Config.cfg"


def _clear_slots(savegame):
    for p in list(savegame.glob("SLOT*")) + [savegame / "profile.bin"]:
        if p.is_dir() and not p.is_symlink():
            shutil.rmtree(p)
        elif p.exists() or p.is_symlink():
            p.unlink()


def _copy_into(src, dst):
    dst.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst, dirs_exist_ok=True, copy_function=shutil.copy2)  # copy2 keeps mtimes: Continue loads the newest slot


def backup_saves(prefix, backup_root, stamp):
    """Copy savegame/ and dishonored2Config.cfg to <backup_root>/<stamp>-sim/ and mark it restore-pending. Never overwrites."""
    savegame, cfg = save_paths(prefix)
    if not savegame.is_dir():
        raise SimError(f"{savegame} does not exist; start the game once so it creates its saves")
    backup = Path(backup_root) / f"{stamp}-sim"
    if backup.exists():
        raise SimError(f"backup {backup} already exists")
    backup.mkdir(parents=True)
    shutil.copytree(savegame, backup / "savegame", copy_function=shutil.copy2)
    if cfg.is_file():
        shutil.copy2(cfg, backup / cfg.name)
    problems = compare_dirs(savegame, backup / "savegame")
    if problems:
        raise SimError("save backup does not match the saves: " + "; ".join(problems[:5]))
    (backup / "info.json").write_text(json.dumps(
        {"savegame": str(savegame), "config": str(cfg), "created": stamp, "files": manifest(backup / "savegame"),
         "config_sha256": steam.sha256_file(cfg) if cfg.is_file() else None}, indent=1) + "\n")
    (backup / PENDING).write_text("Saves of the user are in this folder; a sim run (or crash) left them swapped.\n"
                                  "Run: d2vr-linux sim --restore\n")
    return backup


def manifest(root):
    """{relative path: sha256} of every file under root."""
    root = Path(root)
    return {str(p.relative_to(root)): steam.sha256_file(p) for p in sorted(root.rglob("*")) if p.is_file()}


def compare_dirs(a, b):
    """Differences between two trees (names and bytes), as strings. Empty: identical."""
    out = []

    def walk(cmp, rel):
        out.extend(f"only in {a}: {rel / n}" for n in cmp.left_only)
        out.extend(f"only in {b}: {rel / n}" for n in cmp.right_only)
        out.extend(f"differs: {rel / n}" for n in cmp.diff_files + cmp.funny_files)
        for name, sub in cmp.subdirs.items():
            walk(sub, rel / name)

    cmp = filecmp.dircmp(a, b)
    # filecmp's default is a shallow stat compare; make it byte exact.
    def exact(c):
        same, diff, funny = filecmp.cmpfiles(c.left, c.right, c.common_files, shallow=False)
        c.same_files, c.diff_files, c.funny_files = same, diff, funny
        for sub in c.subdirs.values():
            exact(sub)
    exact(cmp)
    walk(cmp, Path("."))
    return out


def swap_in_test_save(prefix, test_dir):
    test_dir = Path(test_dir)
    savegame, _ = save_paths(prefix)
    _clear_slots(savegame)
    _copy_into(test_dir, savegame)


# ---------------------------------------------------------------- candidate DLL

def is_pe_dll(data):
    """True for a PE image with the DLL characteristic (MZ header, PE signature, IMAGE_FILE_DLL)."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        return False
    off = int.from_bytes(data[0x3C:0x40], "little")
    if off + 24 > len(data) or data[off:off + 4] != b"PE\0\0":
        return False
    return bool(int.from_bytes(data[off + 22:off + 24], "little") & 0x2000)


def installed_dll(mod_home):
    found = sorted(p for p in (Path(mod_home) / "build").glob("d2vr_render_probe_arm_v*.dll") if re.fullmatch(modhome.DLL_RE, p.name))
    if len(found) != 1:
        raise SimError(f"expected exactly one d2vr_render_probe_arm_v*.dll in {Path(mod_home) / 'build'} (found {len(found)})")
    return found[0]


def check_candidate(candidate, installed):
    """Candidate must be a PE DLL carrying the name the mod home's DLL has (the proxy loads it by that name). -> sha256."""
    candidate = Path(candidate)
    if not candidate.is_file():
        raise SimError(f"--candidate-dll {candidate} is not a file")
    if candidate.name != installed.name:
        raise SimError(f"--candidate-dll is named {candidate.name} but the mod home uses {installed.name}; "
                       "the proxy loads that exact name (build the candidate with the same version name)")
    data = candidate.read_bytes()
    if not is_pe_dll(data):
        raise SimError(f"--candidate-dll {candidate} is not a PE DLL")
    return hashlib.sha256(data).hexdigest()


def place_atomic(data, dest):
    """Write bytes next to dest and rename over it, so the file is never half-written."""
    dest = Path(dest)
    tmp = dest.with_name(dest.name + ".d2vr-tmp")
    tmp.write_bytes(data)
    os.replace(tmp, dest)


def backup_dll(backup, installed):
    """Copy the installed DLL into the save backup folder (same restore-pending guard) and record it in info.json."""
    installed, backup = Path(installed), Path(backup)
    data = installed.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    copy = backup / f"installed-{installed.name}"
    copy.write_bytes(data)
    if steam.sha256_file(copy) != digest:
        raise SimError(f"the backup copy {copy} does not match the installed DLL")
    info_path = backup / "info.json"
    info = json.loads(info_path.read_text())
    info["dll"] = {"path": str(installed), "backup": copy.name, "sha256": digest}
    info_path.write_text(json.dumps(info, indent=1) + "\n")
    return digest


def restore_dll(backup, info):
    """Put the backed-up DLL back atomically and verify by SHA-256. -> (ok, notes)."""
    d = info.get("dll")
    if not d:
        return True, []
    src, dest = Path(backup) / d["backup"], Path(d["path"])
    manual = f"cp -f '{src}' '{dest}'"
    try:
        data = src.read_bytes()
        if hashlib.sha256(data).hexdigest() != d["sha256"]:
            return False, [f"the DLL backup {src} no longer matches its recorded hash; restore it by hand: {manual}"]
        place_atomic(data, dest)
        if steam.sha256_file(dest) != d["sha256"]:
            return False, [f"installed DLL differs after restore; run by hand: {manual}"]
    except OSError as e:
        return False, [f"cannot restore the installed DLL ({e}); run by hand: {manual}"]
    return True, [f"installed DLL restored and verified ({d['sha256']})"]


def restore_saves(prefix, backup):
    """Put the backed-up saves and config back, verify, and drop the restore-pending marker only if they match.
    Returns (ok, notes). Extra files the run created in savegame/ are reported, not deleted."""
    backup = Path(backup)
    savegame, cfg = save_paths(prefix)
    if not (backup / "savegame").is_dir():
        return False, [f"{backup / 'savegame'} is missing"]
    _clear_slots(savegame)
    _copy_into(backup / "savegame", savegame)
    if (backup / cfg.name).is_file():
        shutil.copy2(backup / cfg.name, cfg)
    notes, ok = [], True
    try:
        info = json.loads((backup / "info.json").read_text())
        expected = info["files"]
    except (OSError, ValueError, KeyError) as e:
        return False, [f"cannot read {backup / 'info.json'}: {e}"]
    dll_ok, dll_notes = restore_dll(backup, info)
    ok = ok and dll_ok
    notes.extend(dll_notes)
    now = manifest(savegame)
    for rel, digest in expected.items():
        if rel not in now:
            ok = False
            notes.append(f"missing after restore: {rel}")
        elif now[rel] != digest:
            ok = False
            notes.append(f"differs after restore: {rel}")
    notes.extend(f"extra file left in place: {rel}" for rel in now if rel not in expected)
    if info.get("config_sha256") is not None and (not cfg.is_file() or steam.sha256_file(cfg) != info["config_sha256"]):
        ok = False
        notes.append(f"differs after restore: {cfg.name}")
    if ok:
        marker = backup / PENDING
        if marker.exists():
            marker.unlink()
    return ok, notes


def pending_backups(backup_root):
    root = Path(backup_root)
    if not root.is_dir():
        return []
    return sorted(p.parent for p in root.glob(f"*/{PENDING}"))


# ---------------------------------------------------------------- dev knobs

class FileGuard:
    """Remembers a file's original bytes (or absence) the first time it is touched; restore() puts all of them back."""

    def __init__(self):
        self.original = {}

    def remember(self, path):
        path = Path(path)
        if path not in self.original:
            self.original[path] = path.read_bytes() if path.is_file() else None

    def write(self, path, data):
        path = Path(path)
        self.remember(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))

    def restore(self):
        failed = []
        for path, data in self.original.items():
            try:
                if data is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_bytes(data)
            except OSError as e:
                failed.append(f"{path}: {e}")
        self.original.clear()
        return failed


# ---------------------------------------------------------------- probe config and scenario

# modhome / launcher Probe.ProbeConfig field order (tools/inject_probe.py probe_config); the tools/vr_selftest.py SIM_FLAGS profile.
PROBE_FIELDS = ("pid camera mode paired skip_taa full_render_scale symmetric_frustum controllers headset_size eye_boundary "
                "native_eye_capture seconds arm_upload_observer optical_density arm_palette_copy tracked_arms stable_camera "
                "resolution eye_aa camera_trace turning snap smooth saved_settings menu simulated controller_aim "
                "physical_grab left_aim cleanup_exception gameplay_script weapon_attachment").split()


def sim_probe_config(seconds, simulated=2, gameplay_script=1):
    v = dict(pid="{pid}", camera=0, mode=6, paired=1, skip_taa=0, full_render_scale=0, symmetric_frustum=1, controllers=1,
             headset_size=0, eye_boundary=1, native_eye_capture=1, seconds=int(seconds), arm_upload_observer=1,
             optical_density=0, arm_palette_copy=0, tracked_arms=1, stable_camera=1, resolution="1.0", eye_aa=0,
             camera_trace=0, turning=1, snap=45, smooth=90, saved_settings=0, menu=0, simulated=simulated, controller_aim=1,
             physical_grab=1, left_aim=1, cleanup_exception=0, gameplay_script=gameplay_script, weapon_attachment=0)
    return " ".join(str(v[k]) for k in PROBE_FIELDS) + "\n"


def _load_tool(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def scenario_segments(name, lead_ms):
    if not re.fullmatch(r"[a-z0-9_]+", name):
        raise SimError(f"bad scenario name {name!r}")
    path = SCENARIO_DIR / f"{name}.py"
    if not path.is_file():
        have = sorted(p.stem for p in SCENARIO_DIR.glob("*.py")) if SCENARIO_DIR.is_dir() else []
        raise SimError(f"unknown scenario {name!r} (have: {', '.join(have) or 'none'})")
    return _load_tool("d2vr_scenario_" + name, path).build(lead_ms)


def compile_script(segments, seconds):
    """Validate with tools/simulated_gameplay_script.py (the Windows runner's own compiler). -> (json text, cfg text)."""
    tools = REPO_ROOT / "tools/simulated_gameplay_script.py"
    if not tools.is_file():
        raise SimError(f"{tools} is missing")
    mod = _load_tool("d2vr_simulated_gameplay_script", tools)
    with tempfile.TemporaryDirectory(prefix="d2vr-sim-script-") as tmp:
        f = Path(tmp) / "script.json"
        f.write_text(json.dumps({"segments": segments}, indent=1) + "\n", encoding="utf-8")
        try:
            return mod.compile_script(f, tmp, seconds)
        except (OSError, ValueError) as e:
            raise SimError(f"invalid gameplay script: {e}")


# ---------------------------------------------------------------- summary

def dig(data, path):
    cur = data
    for part in path.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
            cur = cur[int(part)]
        else:
            return None, False
    return cur, True


def summarize(status_file, fields):
    lines = []
    try:
        data = json.loads(Path(status_file).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return [f"  (no readable {Path(status_file).name}: {e})"]
    for f in fields:
        value, found = dig(data, f)
        text = json.dumps(value, separators=(",", ":")) if found else "(absent)"
        lines.append(f"  {f} = {text[:200]}")
    return lines


# ---------------------------------------------------------------- the run

class Env:
    """Everything the run touches outside the filesystem, injectable for tests."""

    def __init__(self, proc_root=Path("/proc"), sleep=time.sleep, clock=time.monotonic, launch=None, kill=os.kill,
                 out=None):
        self.proc_root, self.sleep, self.clock, self.kill = Path(proc_root), sleep, clock, kill
        self.launch = launch or self._launch
        self.out = out or (lambda s: print(s, flush=True))
        self.steam_cmd = DEFAULT_STEAM_CMD
        self.close_timeout = 30.0

    def _launch(self):
        subprocess.Popen(shlex.split(self.steam_cmd), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)


def _timestamp():
    return time.strftime("%Y%m%dT%H%M%S")


def _wait(env, predicate, seconds, step=1.0):
    end = env.clock() + seconds
    while True:
        if predicate():
            return True
        if env.clock() >= end:
            return False
        env.sleep(step)


def _proxy_supports_sim(inst):
    p = inst.game_dir / modhome.PROXY_NAME
    try:
        data = p.read_bytes()
    except OSError:
        return False, f"{p} is not installed"
    if modhome.PROXY_MARKER not in data:
        return False, f"{p} is not the d2vr Linux proxy"
    if SIM_MARKER not in data:
        return False, (f"the installed {p} predates simulated runs; install the new one: "
                       "python3 linux/proxy/build.py && linux/d2vr-linux install --zip <d2vr-vNNN-dll.zip>")
    return True, ""


def _read(path):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def close_game(env, launched, notes):
    """Polite SIGTERM, forced SIGKILL after env.close_timeout; only for a game this run started."""
    if not launched:
        return True
    strict = game_processes(env.proc_root, strict=True)
    if strict:
        env.out(f"[sim] closing the game (SIGTERM {', '.join(map(str, strict))})")
        for pid in strict:
            try:
                env.kill(pid, signal.SIGTERM)
            except OSError:
                pass
        if not _wait(env, lambda: not game_processes(env.proc_root, strict=True), env.close_timeout):
            left = game_processes(env.proc_root, strict=True)
            env.out(f"[sim] still running after {env.close_timeout:g} s: forcing {', '.join(map(str, left))}")
            for pid in left:
                try:
                    env.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
            notes.append("game force-closed")
    # wrappers (proton, reaper) follow the game; saves are only safe once nothing names Dishonored2.exe any more
    return _wait(env, lambda: not game_processes(env.proc_root), 60)


def snapshot(mod_home, dest, info):
    dest.mkdir(parents=True, exist_ok=True)
    art = mod_home / "artifacts"
    if art.is_dir():
        shutil.copytree(art, dest / "artifacts", dirs_exist_ok=True)
    build = dest / "build"
    build.mkdir(exist_ok=True)
    for name in ("dxgi-probe.cfg", OVERRIDE_NAME, SCRIPT_CFG):
        src = mod_home / "build" / name
        if src.is_file():
            shutil.copy2(src, build / name)
    (dest / "run-info.json").write_text(json.dumps(info, indent=1, default=str) + "\n")


def do_restore(a, home, env):
    """`sim --restore [BACKUP]`: put saves back from the newest (or named) pending backup and clean the dev knobs."""
    inst = steam.locate(home, a.steam_root)
    mod_home = Path(a.mod_home) if a.mod_home else modhome.mod_home_dir(inst.prefix)
    backup_root = Path(a.backup_root)
    bad = conflicts(env.proc_root)
    if bad:
        raise SimError("refusing to restore: " + "; ".join(bad))
    if a.restore is not True:
        pending = [Path(a.restore)]
    else:
        pending = pending_backups(backup_root)
    if not pending:
        env.out("[sim] nothing to restore (no restore-pending backup)")
    for backup in pending:
        ok, notes = restore_saves(inst.prefix, backup)
        for n in notes:
            env.out(f"[sim]   {n}")
        env.out(f"[sim] {'restored' if ok else 'RESTORE MISMATCH, backup kept at'} {backup}")
        if not ok:
            return 2
    for name in (OVERRIDE_NAME, AUTOCONTINUE_NAME, SCRIPT_CFG):
        (mod_home / "build" / name).unlink(missing_ok=True)
    (mod_home / "artifacts" / SCRIPT_JSON).unlink(missing_ok=True)
    return 0


def _plan(a, home, env):
    inst = steam.locate(home, a.steam_root)
    mod_home = Path(a.mod_home) if a.mod_home else modhome.mod_home_dir(inst.prefix)
    problems = []
    bad = conflicts(env.proc_root)
    if bad:
        raise SimError("refusing to run: " + "; ".join(bad) + ". Close it first; the sim rig never touches a running session.")
    pend = pending_backups(a.backup_root)
    if pend:
        raise SimError(f"an earlier sim run left your saves swapped (restore-pending in {pend[0]}); "
                       "run: d2vr-linux sim --restore")
    ok, why = _proxy_supports_sim(inst)
    if not ok:
        problems.append(why)
    dlls = sorted(p.name for p in (mod_home / "build").glob("d2vr_render_probe_arm_v*.dll")) if (mod_home / "build").is_dir() else []
    if len(dlls) != 1:
        problems.append(f"expected exactly one d2vr_render_probe_arm_v*.dll in {mod_home / 'build'} (found {len(dlls)})")
    for name in (OVERRIDE_NAME, AUTOCONTINUE_NAME):
        if (mod_home / "build" / name).exists():
            problems.append(f"{mod_home / 'build' / name} exists (an earlier run?); remove it or run: d2vr-linux sim --restore")
    candidate = None
    if getattr(a, "candidate_dll", None) and len(dlls) == 1:
        try:
            candidate = (Path(a.candidate_dll), check_candidate(a.candidate_dll, mod_home / "build" / dlls[0]),
                         steam.sha256_file(mod_home / "build" / dlls[0]))
        except SimError as e:
            problems.append(str(e))
    if not (mod_home / ".git").is_dir():
        problems.append(f"{mod_home / '.git'} missing (run install)")
    if a.test_save:
        t = Path(a.test_save)
        if not t.is_dir() or not (list(t.glob("SLOT*")) or (t / "profile.bin").exists()):
            problems.append(f"--test-save {t} must be a savegame folder (SLOT* / profile.bin)")
    if problems:
        raise SimError("; ".join(problems))

    lead_ms = int(round(a.lead * 1000))
    if a.script:
        try:
            segments = json.loads(Path(a.script).read_text(encoding="utf-8-sig"))["segments"]
        except (OSError, ValueError, KeyError, TypeError) as e:
            raise SimError(f"cannot read gameplay script {a.script}: {e}")
        length = max(s["end_ms"] for s in segments) / 1000 if segments else 0
    else:
        segments = scenario_segments(a.scenario, lead_ms)
        length = segments[-1]["end_ms"] / 1000
    seconds = a.seconds if a.seconds else int(-(-(length + SESSION_PAD_SECONDS) // 1))
    if not 10 <= seconds <= 120:
        raise SimError(f"session length {seconds} s is outside 10..120 s (lower --lead or shorten the script)")
    if length * 1000 > seconds * 1000:
        raise SimError(f"the script is {length:g} s long but the session is {seconds} s")
    script_json, script_cfg = compile_script(segments, seconds)
    return inst, mod_home, seconds, script_json, script_cfg, candidate


def run_sim(a, home, env):
    if a.restore:
        return do_restore(a, home, env)
    inst, mod_home, seconds, script_json, script_cfg, candidate = _plan(a, home, env)
    override = sim_probe_config(seconds)
    build, art = mod_home / "build", mod_home / "artifacts"
    env.out(f"[sim] scenario {a.script or a.scenario}: {seconds} s session, mod home {mod_home}")
    if a.dry_run:
        env.out("[sim] dry run: nothing written. Override config:")
        env.out("  " + override.strip())
        env.out(f"[sim] script: {script_cfg.splitlines()[0]}")
        if candidate:
            env.out(f"[sim] candidate DLL {candidate[0]}: sha256 {candidate[1]}; installed {installed_dll(mod_home)} "
                    f"sha256 {candidate[2]} is backed up, replaced for the run and restored afterwards")
        return 0

    stamp = _timestamp()
    backup = backup_saves(inst.prefix, a.backup_root, stamp)
    env.out(f"[sim] saves backed up to {backup}")
    guard, notes, launched, rc, reason = FileGuard(), [], False, 1, ""
    run_dir = Path(a.out_root) / stamp
    started = None
    try:
        if candidate:
            target = installed_dll(mod_home)
            before = backup_dll(backup, target)
            if before != candidate[2]:
                raise SimError("the installed DLL changed while planning")
            place_atomic(candidate[0].read_bytes(), target)
            if steam.sha256_file(target) != candidate[1]:
                raise SimError("candidate DLL was not placed correctly")
            env.out(f"[sim] candidate DLL placed (installed {before[:12]} backed up to {backup}; restored afterwards)")
        if a.test_save:
            swap_in_test_save(inst.prefix, a.test_save)
            env.out(f"[sim] test save {a.test_save} swapped in")
        guard.remember(build / "dxgi-probe.cfg")
        guard.write(build / OVERRIDE_NAME, override)
        guard.write(build / SCRIPT_CFG, script_cfg)
        guard.write(art / SCRIPT_JSON, script_json)
        if a.autocontinue:
            guard.write(build / AUTOCONTINUE_NAME, str(a.autocontinue_delay) + "\n")
        if a.settle is not None:
            guard.write(build / SETTLE_NAME, str(a.settle) + "\n")
        bad = conflicts(env.proc_root)
        if bad:
            raise SimError("refusing to launch: " + "; ".join(bad))
        log = art / "linux-proxy.log"
        started = time.time_ns()
        env.out(f"[sim] launching: {env.steam_cmd}")
        env.launch()
        launched = True
        fresh = lambda: log.is_file() and log.stat().st_mtime_ns >= started  # noqa: E731
        begin = lambda: fresh() and "StartProbe begin" in _read(log)  # noqa: E731
        refused = lambda: fresh() and any(m in _read(log) for m in REFUSAL_MARKERS if m != "StartProbe returned")  # noqa: E731
        seen = []

        def game_up():
            if game_processes(env.proc_root):
                seen.append(1)
            return bool(seen)

        def stop_waiting():
            return refused() or (game_up() and not game_processes(env.proc_root))

        env.out("[sim] waiting for the proxy to start the session")
        if not _wait(env, lambda: begin() or stop_waiting(), a.start_timeout, 2.0):
            reason = f"no 'StartProbe begin' in linux-proxy.log after {a.start_timeout:g} s"
        elif not begin():
            reason = "the proxy refused to start the session or the game exited: " + _read(log).strip().splitlines()[-1]
        else:
            env.out(f"[sim] session running ({seconds} s)")
            if not _wait(env, lambda: "StartProbe returned" in _read(log) or not game_processes(env.proc_root),
                         seconds + 150, 2.0):
                reason = "StartProbe did not return in time"
            elif "StartProbe returned" not in _read(log):
                reason = "the game exited during the session"
            else:
                m = re.findall(r"StartProbe returned (\d+)", _read(log))
                rc = 0 if m and m[-1] == "0" else 1
                if rc:
                    reason = f"StartProbe returned {m[-1]}"
    except KeyboardInterrupt:
        reason = "interrupted"
    except SimError as e:
        reason = str(e)
    finally:
        gone = close_game(env, launched, notes)
        try:
            snapshot(mod_home, run_dir, {"scenario": a.script or a.scenario, "seconds": seconds, "override": override,
                                         "return_code": rc, "reason": reason, "notes": notes, "backup": str(backup)})
            env.out(f"[sim] artifacts snapshot: {run_dir}")
        except OSError as e:
            env.out(f"[sim] snapshot failed: {e}")
        failed = guard.restore()
        if failed:
            env.out("[sim] could not reset dev files: " + "; ".join(failed))
        if gone:
            ok, rnotes = restore_saves(inst.prefix, backup)
            for n in rnotes:
                env.out(f"[sim]   {n}")
            env.out("[sim] saves restored and verified" if ok else f"[sim] RESTORE MISMATCH: backup kept at {backup}; fix and run: d2vr-linux sim --restore")
            if not ok:
                rc = 2
        else:
            env.out(f"[sim] the game is still running: saves NOT restored. Close it, then run: d2vr-linux sim --restore ({backup})")
            rc = 2
    env.out(f"[sim] result: {'OK' if rc == 0 else 'FAILED'}{(' (' + reason + ')') if reason else ''}")
    env.out("[sim] status fields (xr-live-status.json):")
    for line in summarize(run_dir / "artifacts" / "xr-live-status.json", a.status_field or DEFAULT_FIELDS):
        env.out(line)
    return rc
