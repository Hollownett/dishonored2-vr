"""Developer-only simulated-VR test runs on Linux (`d2vr-linux sim`).

Runs the real game under Proton with the mod's scripted XR backend (src/simulated_xr.hpp, no headset) by writing a dev probe
config override that the proxy honours, then restores the user's saves and removes every dev knob. Safety rules:

- refuses to start while Dishonored2.exe or SteamVR's vrserver runs (the user's own VR session), and never signals a game it
  did not launch itself;
- backs up the saves and dishonored2Config.cfg before touching anything (never deleted) and marks the backup `restore-pending`
  until a verified restore; a pending backup blocks further runs until `sim --restore`.
"""
import contextlib
import fcntl
import filecmp
import hashlib
import importlib.util
import json
import os
import pwd
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
KNOBS_NAME = "linux-sim-knobs"  # JSON record of every file the run wrote (relative path -> sha256), so `sim --restore` can remove them after a hard kill
RESERVED_KNOBS = (KNOBS_NAME, AUTOCONTINUE_NAME, SETTLE_NAME, "linux-probe-override", "simulated-gameplay-script")
RENDER_KNOB_RE = re.compile(r"(aa-|fsr|governor|no-governor|native-gpu|no-mip|force-taa|taa-|upscale|render-|resolution|sharp|no-vr-cvar)")
NOT_KNOBS = ("dxgi-probe.cfg", "vr-settings.cfg")
AA_MODE_KNOB = "aa-mode"  # the probe reads build/aa-mode (0..6) as the temporal AA preference
KNOB_RE = re.compile(r"[a-z0-9-]+")
PROBE_MODES = {"xr": 6, "paired": 3, "single": 2}  # dxgi_probe.cpp stereo_flag: 2 single eye, 3 paired (both flat, no XR), 6 head-tracked XR
ENV_KEYS = ("DXVK_HUD", "DXVK_LOG_LEVEL", "__GL_THREADED_OPTIMIZATIONS")
VR_SETTING_KEYS = ("temporal_aa", "resolution", "mirror")
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


def _fsync_dir(path):
    try:
        fd = os.open(str(path), os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    except OSError:
        pass
    finally:
        os.close(fd)


def _write_durable(path, data):
    with open(path, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def place_atomic(data, dest, expect_sha=None):
    """Write bytes (fsynced) next to dest and rename over it, so the file is never half-written. No temp file is left on failure."""
    dest = Path(dest)
    tmp = dest.with_name(dest.name + ".d2vr-tmp")
    try:
        _write_durable(tmp, data)
        if expect_sha is not None and _sha(tmp) != expect_sha:
            raise SimError(f"{tmp} does not hold the expected bytes ({expect_sha}); nothing was replaced")
        os.replace(tmp, dest)
    except BaseException:
        with contextlib.suppress(OSError):
            tmp.unlink()
        raise
    _fsync_dir(dest.parent)


def _sha(path):
    try:
        return steam.sha256_file(path)
    except OSError:
        return None


DLL_RECORD = "dll.json"


def backup_dll(backup, installed, candidate_sha):
    """Copy the installed DLL into the save backup folder (same restore-pending guard) and write dll.json (atomically,
    fsynced) naming the resolved path, the original hash and the candidate hash. Everything is durable before the swap."""
    installed, backup = Path(installed).resolve(), Path(backup)
    data = installed.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    copy = backup / f"installed-{installed.name}"
    _write_durable(copy, data)
    if steam.sha256_file(copy) != digest:
        raise SimError(f"the backup copy {copy} does not match the installed DLL")
    rec = {"path": str(installed), "backup": copy.name, "original_sha256": digest, "candidate_sha256": candidate_sha}
    tmp = backup / (DLL_RECORD + ".tmp")
    _write_durable(tmp, (json.dumps(rec, indent=1) + "\n").encode())
    os.replace(tmp, backup / DLL_RECORD)
    _fsync_dir(backup)
    return digest


def manual_restore_command(src, dest, orig, cand):
    """A hash-guarded shell command: copies only if the destination is still the candidate and the backup is the original;
    copy-then-rename so an interrupted copy never truncates the installed DLL."""
    return (f"[ \"$(sha256sum '{dest}' | cut -d' ' -f1)\" = {cand} ] && [ \"$(sha256sum '{src}' | cut -d' ' -f1)\" = {orig} ] "
            f"&& cp -f '{src}' '{dest}.d2vr-manual' && mv -f '{dest}.d2vr-manual' '{dest}'")


def clear_hint(backup):
    return (f"If you have checked by hand that the installed DLL and saves are as you want them, clear this backup with: "
            f"rm '{Path(backup) / PENDING}'  (until then sim runs are refused)")


def read_dll_record(backup):
    """-> (record or None, problem or None). Malformed, legacy and lost records are a problem or a flagged record, never
    'no DLL was swapped'. A legacy (b02bd75) info.json entry becomes a record with candidate_sha256 == ''."""
    backup = Path(backup)
    p = backup / DLL_RECORD
    if not p.exists():
        try:
            leg = json.loads((backup / "info.json").read_text()).get("dll")
        except (OSError, ValueError, AttributeError):
            leg = None
        if isinstance(leg, dict) and all(isinstance(leg.get(k), str) for k in ("path", "backup", "sha256")):
            return {"path": leg["path"], "backup": leg["backup"], "original_sha256": leg["sha256"], "candidate_sha256": ""}, None
        if list(backup.glob("installed-*.dll")):
            return None, (f"DLL state UNKNOWN: {backup} holds a DLL backup but no readable DLL record ({p.name}); the installed DLL "
                          f"may still be the candidate. Compare it with {backup}/installed-*.dll by hand")
        return None, None
    try:
        rec = json.loads(p.read_text())
        for k in ("path", "backup", "original_sha256", "candidate_sha256"):
            if not isinstance(rec[k], str) or not rec[k]:
                raise ValueError(k)
        return rec, None
    except (OSError, ValueError, KeyError, TypeError) as e:
        return None, f"cannot read the DLL record {p} ({e}); DLL state UNKNOWN, the installed DLL may still be the candidate: compare it with {backup}/installed-*.dll"


def restore_dll(backup, seen=None, known=()):
    """Put the original DLL back, but only when the destination still holds this run's candidate. -> (ok, notes).
    Already the original: success. Anything else (newer install, missing file): nothing is written, the caller keeps the
    pending marker. known: candidate hashes of every pending run, so a chain of interrupted runs still counts as ours. seen (path -> (ok, backup)) makes only the oldest backup per path act; later ones write nothing."""
    rec, problem = read_dll_record(backup)
    if problem:
        return False, [problem]
    if rec is None:
        return True, []
    dest, src = Path(rec["path"]), Path(backup) / rec["backup"]
    orig, cand = rec["original_sha256"], rec["candidate_sha256"]
    if seen is not None:
        if rec["path"] in seen:
            first_ok, first = seen[rec["path"]]
            return first_ok, [f"DLL {dest.name}: not touched, the older backup {first} owns the original" + ("" if first_ok else " (its restore did not complete)")]
        seen[rec["path"]] = (False, Path(backup))
    cur = _sha(dest)

    def done(ok, notes):
        if seen is not None:
            seen[rec["path"]] = (ok, Path(backup))
        return ok, notes

    if cur == orig:
        return done(True, [f"installed DLL {dest.name} is already the original ({orig})"])
    if not cand:
        return done(False, [f"DLL state UNKNOWN: legacy record without a candidate hash; {dest} holds {cur or 'no file'}, the original "
                            f"is {orig} (kept in {src}). Left alone; check by hand"])
    if cur != cand and cur not in known:
        return done(False, [f"NOT restoring {dest}: it holds {cur or 'no file'}, which is neither this run's candidate ({cand}) "
                            f"nor the original ({orig}); something installed over it. Left alone; the pending marker stays. "
                            f"The original is kept in {src}"])
    if _sha(src) != orig:
        return done(False, [f"the DLL backup {src} fails its hash check (expected {orig}); NOT copying it. The installed DLL is "
                            f"still the candidate ({cand}); reinstall the mod (d2vr-linux install --zip ...) to replace it"])
    try:
        place_atomic(src.read_bytes(), dest)
    except OSError as e:
        return done(False, [f"cannot restore the installed DLL ({e}); once the game has exited run: {manual_restore_command(src, dest, orig, cand)}"])
    if _sha(dest) != orig:
        return done(False, [f"installed DLL differs after restore; check it by hand against {src} ({orig})"])
    return done(True, [f"installed DLL restored and verified ({orig})"])


def restore_saves(prefix, backup, seen=None, known=()):
    """Restore the DLL first (it needs only dll.json), then put the saves and config back and verify; drop the restore-pending
    marker only if both match. Returns (ok, notes). Extra files the run created in savegame/ are reported, not deleted."""
    backup = Path(backup)
    dll_ok, notes = restore_dll(backup, seen, known)
    try:
        ok, snotes = _restore_save_files(prefix, backup)
    except Exception as e:  # noqa: BLE001 - the DLL is already handled; report and keep the marker
        ok, snotes = False, [f"saves could not be restored ({type(e).__name__}: {e})"]
    notes = notes + snotes
    ok = ok and dll_ok
    if ok:
        marker = backup / PENDING
        if marker.exists():
            marker.unlink()
    return ok, notes


def _restore_save_files(prefix, backup):
    savegame, cfg = save_paths(prefix)
    if not (backup / "savegame").is_dir():
        return False, [f"{backup / 'savegame'} is missing"]
    try:
        info = json.loads((backup / "info.json").read_text())
        expected = info["files"]
    except (OSError, ValueError, KeyError) as e:
        return False, [f"cannot read {backup / 'info.json'}: {e}"]
    _clear_slots(savegame)
    _copy_into(backup / "savegame", savegame)
    if (backup / cfg.name).is_file():
        shutil.copy2(backup / cfg.name, cfg)
    notes, ok = [], True
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
    return ok, notes


# ---------------------------------------------------------------- run lock and signal shield

def real_home():
    """The account's home directory, not $HOME or $XDG_STATE_HOME: every run on one mod home must find the same lock."""
    return Path(pwd.getpwuid(os.getuid()).pw_dir)


def lock_path(mod_home):
    key = hashlib.sha256(str(Path(mod_home).resolve()).encode()).hexdigest()[:16]
    return real_home() / ".local/state/d2vr" / f"sim-{key}.lock"


@contextlib.contextmanager
def run_lock(mod_home):
    """One exclusive flock per mod home for the whole run or restore, independent of --backup-root, the environment and the mod home itself."""
    path = lock_path(mod_home)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(path), os.O_RDWR | os.O_CREAT, 0o600)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise SimError(f"another sim run or restore holds {path}; wait for it to finish (nothing was touched)")
        yield
    finally:
        os.close(fd)  # closing releases the flock


@contextlib.contextmanager
def defer_sigint(out=None):
    """Hold SIGINT/SIGTERM back until the block ends (main thread only)."""
    got = []
    old = {}
    try:
        for sig in (signal.SIGINT, signal.SIGTERM):
            old[sig] = signal.signal(sig, lambda n, f: got.append(n))
    except ValueError:  # not the main thread
        old = {}
    try:
        yield
    finally:
        for sig, h in old.items():
            signal.signal(sig, h)
        if got and out:
            out("[sim] an interrupt arrived while restoring; it was held back until the restore finished")


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
                    place_atomic(data, path)
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


def sim_probe_config(seconds, simulated=2, gameplay_script=1, probe_mode="xr", resolution=None):
    """The override config. probe_mode paired/single are the flat diagnostic stereo modes 3 and 2: no XR, so every field that
    needs head tracking, controllers or the simulated backend is off (the probe rejects the config otherwise).
    resolution (XR only) needs headset sizing and the full render scale, which the probe requires for any value but 1.0."""
    if probe_mode not in PROBE_MODES:
        raise SimError(f"unknown --probe-mode {probe_mode!r} (xr, paired or single)")
    v = dict(pid="{pid}", camera=0, mode=6, paired=1, skip_taa=0, full_render_scale=0, symmetric_frustum=1, controllers=1,
             headset_size=0, eye_boundary=1, native_eye_capture=1, seconds=int(seconds), arm_upload_observer=1,
             optical_density=0, arm_palette_copy=0, tracked_arms=1, stable_camera=1, resolution="1.0", eye_aa=0,
             camera_trace=0, turning=1, snap=45, smooth=90, saved_settings=0, menu=0, simulated=simulated, controller_aim=1,
             physical_grab=1, left_aim=1, cleanup_exception=0, gameplay_script=gameplay_script, weapon_attachment=0)
    if probe_mode != "xr":
        if resolution is not None:
            raise SimError("the resolution setting needs --probe-mode xr")
        # the fourth field is the GPU trace flag: the probe rejects stereo mode 3 without it ("Paired rendering requires its target
        # hooks") and rejects it with mode 2
        v.update(mode=PROBE_MODES[probe_mode], paired=1 if probe_mode == "paired" else 0, symmetric_frustum=0, controllers=0, tracked_arms=0, stable_camera=0,
                 turning=0, simulated=0, controller_aim=0, physical_grab=0, left_aim=0, gameplay_script=0, weapon_attachment=0)
        if probe_mode == "single":  # the eye observers, native capture and arm observer need paired rendering
            v.update(eye_boundary=0, native_eye_capture=0, arm_upload_observer=0)
    elif resolution is not None:
        v.update(resolution=format(float(resolution), "g"), full_render_scale=1, headset_size=1)
    return " ".join(str(v[k]) for k in PROBE_FIELDS) + "\n"


def parse_knobs(names):
    out = []
    for n in names or ():
        if not KNOB_RE.fullmatch(n):
            raise SimError(f"--knob {n!r} must match [a-z0-9-]+ (a file name under build/, no path)")
        if n not in out:
            out.append(n)
    return out


def parse_env(pairs):
    out = {}
    for item in pairs or ():
        key, eq, value = item.partition("=")
        if not eq or key not in ENV_KEYS:
            raise SimError(f"--env {item!r}: use KEY=VALUE with KEY one of {', '.join(ENV_KEYS)}")
        if not re.fullmatch(r"[A-Za-z0-9_.,:=+-]*", value):
            raise SimError(f"--env {key}: the value has characters outside [A-Za-z0-9_.,:=+-]")
        out[key] = value
    return out


def parse_vr_settings(pairs):
    """KEY=VALUE -> {key: value}, validated like vr_preferences.hpp (temporal_aa 0..6, resolution 0.5..2, mirror 0..2)."""
    out = {}
    for item in pairs or ():
        key, eq, value = item.partition("=")
        if not eq or key not in VR_SETTING_KEYS:
            raise SimError(f"--vr-setting {item!r}: use KEY=VALUE with KEY one of {', '.join(VR_SETTING_KEYS)}")
        try:
            if key == "resolution":
                num = float(value)
                if not (0.5 <= num <= 2.0):  # also false for nan
                    raise ValueError
                out[key] = num
            else:
                if not re.fullmatch(r"[0-9]+", value) or int(value) > (6 if key == "temporal_aa" else 2):
                    raise ValueError
                out[key] = int(value)
        except ValueError:
            raise SimError(f"--vr-setting {item!r}: " + {"resolution": "resolution must be 0.5 to 2.0",
                           "temporal_aa": "temporal_aa must be an integer 0 to 6",
                           "mirror": "mirror must be 0 (right eye), 1 (left eye) or 2 (both)"}[key])
    if out.get("mirror", 2) != 2:
        raise SimError("--vr-setting mirror: a simulated run cannot apply a mirror mode (the probe reads it only from saved "
                       "settings, which the simulated backend rejects); only mirror=2, the default, is accepted")
    return out


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

def session_lock_state(run=subprocess.run, environ=os.environ):
    """True if the desktop session is locked, False if not, None if neither tool answered. A locked GNOME session
    throttles the game window to ~1 fps, so the menu key presses never land and the run fails after minutes."""
    def ask(argv):
        try:
            r = run(argv, capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            return None
        return r.stdout if r.returncode == 0 else None
    sid = environ.get("XDG_SESSION_ID")
    if sid:
        out = ask(["loginctl", "show-session", sid, "-p", "LockedHint"])
        if out and "LockedHint=" in out:
            return "LockedHint=yes" in out
    out = ask(["gdbus", "call", "--session", "--dest", "org.gnome.ScreenSaver", "--object-path",
               "/org/gnome/ScreenSaver", "--method", "org.gnome.ScreenSaver.GetActive"])
    if out and "true" in out.lower():
        return True
    if out and "false" in out.lower():
        return False
    return None


class Env:
    """Everything the run touches outside the filesystem, injectable for tests."""

    def __init__(self, proc_root=Path("/proc"), sleep=time.sleep, clock=time.monotonic, launch=None, kill=os.kill,
                 out=None, lock_probe=None):
        self.proc_root, self.sleep, self.clock, self.kill = Path(proc_root), sleep, clock, kill
        self.launch = launch or self._launch
        self.lock_probe = lock_probe or self._lock_probe
        self.out = out or (lambda s: print(s, flush=True))
        self.steam_cmd = DEFAULT_STEAM_CMD
        self.close_timeout = 30.0
        self.launch_env = {}  # --env pairs, added to the launch environment

    def _lock_probe(self):
        return session_lock_state()

    def _launch(self):
        subprocess.Popen(shlex.split(self.steam_cmd), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True, env={**os.environ, **self.launch_env})


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


ARTIFACT_SLACK_NS = 2_000_000_000  # file systems stamp mtimes from a coarse clock; a previous run is minutes older


def snapshot(mod_home, dest, info, since_ns=None):
    """Copy this run's artifacts. Only files modified at or after the launch (minus a small slack) are copied, so a run that
    failed before the game wrote anything never carries the previous run's status; since_ns None (never launched): none."""
    dest.mkdir(parents=True, exist_ok=True)
    art = mod_home / "artifacts"
    if art.is_dir():
        cutoff = None if since_ns is None else since_ns - ARTIFACT_SLACK_NS

        def stale(directory, names):
            skip = []
            for n in names:
                p = Path(directory) / n
                if p.is_dir() and not p.is_symlink():
                    continue
                try:
                    if cutoff is None or p.stat().st_mtime_ns < cutoff:
                        skip.append(n)
                except OSError:
                    skip.append(n)
            return skip

        shutil.copytree(art, dest / "artifacts", dirs_exist_ok=True, ignore=stale)
    build = dest / "build"
    build.mkdir(exist_ok=True)
    for name in ("dxgi-probe.cfg", OVERRIDE_NAME, SCRIPT_CFG):
        src = mod_home / "build" / name
        if src.is_file():
            shutil.copy2(src, build / name)
    (dest / "run-info.json").write_text(json.dumps(info, indent=1, default=str) + "\n")
    write_adapter_txt(dest)


def adapter_of(doc):
    """The adapter block of a status or result document: frame_split.adapter (current layout), else a top-level adapter."""
    if not isinstance(doc, dict):
        return None
    fs = doc.get("frame_split")
    if isinstance(fs, dict) and "adapter" in fs:
        return fs["adapter"]
    return doc.get("adapter")


def write_adapter_txt(dest):
    """adapter.txt: the adapter block ({name, vendor, vram_mb}) as one JSON line, or `null`. From xr-live-status.json, else
    dxgi-probe-result.json (the only file a flat run writes)."""
    adapter = None
    for name in ("xr-live-status.json", "dxgi-probe-result.json"):
        try:
            adapter = adapter_of(json.loads((dest / "artifacts" / name).read_text(encoding="utf-8-sig")))
        except (OSError, ValueError):
            adapter = None
        if adapter is not None:
            break
    (dest / "adapter.txt").write_text(json.dumps(adapter) + "\n")


# ---- the record of files the rig wrote: sha256 per path, so a restore after a hard kill removes only what is still ours

def _rel_ok(rel):
    parts = rel.split("/")
    return len(parts) == 2 and parts[0] in ("build", "artifacts") and parts[1] not in ("", ".", "..") and "\\" not in rel and "\0" not in rel


def read_record(build):
    """-> ({relative path: sha256}, problem or None). A missing record is ({}, None)."""
    try:
        data = json.loads((build / KNOBS_NAME).read_text())
        files = data["files"]
        if not isinstance(files, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in files.items()):
            raise ValueError("files")
        return {k: v for k, v in files.items() if _rel_ok(k)}, None
    except FileNotFoundError:
        return {}, None
    except (OSError, ValueError, KeyError, TypeError) as e:
        return {}, f"cannot read {build / KNOBS_NAME} ({e}); remove the dev files in build/ by hand if you are sure they are not yours"


def release_recorded(mod_home):
    """Remove the files the record lists, but only those whose bytes still match the recorded sha256 (and are not symlinks).
    -> (all_handled, notes). The record is dropped only when everything it lists is gone or was reported."""
    mod_home = Path(mod_home)
    files, problem = read_record(mod_home / "build")
    notes, ok = ([problem] if problem else []), problem is None
    for rel, digest in sorted(files.items()):
        path = mod_home / rel
        if path.is_symlink():
            notes.append(f"{path} is a symlink; left alone")
            ok = False
        elif not path.exists():
            continue
        elif _sha(path) == digest:
            path.unlink()
        else:
            notes.append(f"{path} no longer holds what the run wrote (you or another tool changed it); left alone")
            ok = False
    if ok:
        (mod_home / "build" / KNOBS_NAME).unlink(missing_ok=True)
    return ok, notes


def check_run_files(mod_home, knobs, settings, a):
    """Refuse (before anything is written) to touch a file the user owns or a symlink: -> list of problems."""
    build, art = mod_home / "build", mod_home / "artifacts"
    problems = []
    for n in knobs:
        if n in RESERVED_KNOBS or n.removeprefix("linux-").startswith(("probe-override", "sim-knobs")):
            problems.append(f"--knob {n} is a file the rig itself owns; pick another name")
        elif os.path.lexists(build / n):
            what = "--vr-setting temporal_aa" if n == AA_MODE_KNOB and "temporal_aa" in settings else f"--knob {n}"
            problems.append(f"{build / n} already exists and {what} would overwrite it; remove it or drop the option")
    targets = [build / "dxgi-probe.cfg", build / OVERRIDE_NAME, build / SCRIPT_CFG, art / SCRIPT_JSON, build / AUTOCONTINUE_NAME, build / SETTLE_NAME,
               build / KNOBS_NAME, *(build / n for n in knobs)]
    for t in targets:
        if t.is_symlink():
            problems.append(f"{t} is a symlink; refusing to write through it")
    return problems


def user_knob_files(build, rig_names):
    """Names in build/ that the run did not write itself: the probe reads many of them unconditionally."""
    try:
        names = sorted(p.name for p in Path(build).iterdir() if p.name not in rig_names and p.name not in NOT_KNOBS
                       and not p.name.endswith((".dll", ".d2vr-tmp")) and not p.name.startswith("."))
    except OSError:
        return []
    return names


def do_restore(a, home, env):
    """`sim --restore [BACKUP]`: restore the DLL and saves of every pending (or the named) backup, oldest first, and clean
    the dev knobs. All backups are tried; the oldest backup owns the original DLL per path."""
    inst = steam.locate(home, a.steam_root)
    mod_home = Path(a.mod_home) if a.mod_home else modhome.mod_home_dir(inst.prefix)
    with run_lock(mod_home):
        return _do_restore(a, home, env)


def _do_restore(a, home, env):
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
    failed, seen = False, {}
    known = {r["candidate_sha256"] for r in (read_dll_record(p)[0] for p in pending) if r}
    with defer_sigint(env.out):
        for backup in sorted(pending, key=lambda p: p.name):
            ok, notes = restore_saves(inst.prefix, backup, seen, known)
            for n in notes:
                env.out(f"[sim]   {n}")
            env.out(f"[sim] {'restored' if ok else 'RESTORE INCOMPLETE, backup kept at'} {backup}")
            if not ok:
                env.out(f"[sim]   {clear_hint(backup)}")
            failed = failed or not ok
    # dev knobs would change the real game: gone, but only the files the record proves the rig wrote and nobody changed since
    with defer_sigint(env.out):
        released, notes = release_recorded(mod_home)
    for n in notes:
        env.out(f"[sim]   {n}")
    if not released:
        env.out(f"[sim] some dev files in {mod_home / 'build'} were left alone (see above); check them by hand")
    return 2 if failed or not released else 0


def _plan(a, home, env):
    inst = steam.locate(home, a.steam_root)
    mod_home = Path(a.mod_home) if a.mod_home else modhome.mod_home_dir(inst.prefix)
    problems = []
    bad = conflicts(env.proc_root)
    if bad:
        raise SimError("refusing to run: " + "; ".join(bad) + ". Close it first; the sim rig never touches a running session.")
    if not a.dry_run:
        locked = env.lock_probe()
        if locked:
            raise SimError("session is locked; unlock before running")
        if locked is None:
            env.out("[sim] warning: cannot tell whether the session is locked (no loginctl / gdbus answer); continuing")
    pend = pending_backups(a.backup_root)
    if pend:
        raise SimError(f"an earlier sim run left your saves swapped (restore-pending in {pend[0]}); "
                       "run: d2vr-linux sim --restore. " + clear_hint(pend[0]))
    ok, why = _proxy_supports_sim(inst)
    if not ok:
        problems.append(why)
    dlls = sorted(p.name for p in (mod_home / "build").glob("d2vr_render_probe_arm_v*.dll")) if (mod_home / "build").is_dir() else []
    if len(dlls) != 1:
        problems.append(f"expected exactly one d2vr_render_probe_arm_v*.dll in {mod_home / 'build'} (found {len(dlls)})")
    for name in (OVERRIDE_NAME, AUTOCONTINUE_NAME, KNOBS_NAME):
        if os.path.lexists(mod_home / "build" / name):
            problems.append(f"{mod_home / 'build' / name} exists (an earlier run?); run: d2vr-linux sim --restore (it removes only what the run wrote); otherwise remove it by hand")
    candidate = None
    if getattr(a, "candidate_dll", None) and (mod_home / "build").is_symlink():
        problems.append(f"{mod_home / 'build'} is a symlink; refusing to swap a DLL through it")
    elif getattr(a, "candidate_dll", None) and len(dlls) == 1 and (mod_home / "build" / dlls[0]).is_symlink():
        problems.append(f"{mod_home / 'build' / dlls[0]} is a symlink; refusing to swap it (the restore could not be verified at the link)")
    elif getattr(a, "candidate_dll", None) and len(dlls) == 1:
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
    if a.dry_run:
        return _run_sim(a, home, env)
    inst = steam.locate(home, a.steam_root)
    mod_home = Path(a.mod_home) if a.mod_home else modhome.mod_home_dir(inst.prefix)
    with run_lock(mod_home):
        return _run_sim(a, home, env)


def _run_sim(a, home, env):
    knobs = parse_knobs(getattr(a, "knob", None))
    launch_env = parse_env(getattr(a, "env", None))
    settings = parse_vr_settings(getattr(a, "vr_setting", None))
    probe_mode = getattr(a, "probe_mode", None) or "xr"
    if "temporal_aa" in settings:
        if AA_MODE_KNOB in knobs:
            raise SimError(f"--knob {AA_MODE_KNOB} conflicts with --vr-setting temporal_aa (both write build/{AA_MODE_KNOB})")
        knobs.append(AA_MODE_KNOB)
    inst, mod_home, seconds, script_json, script_cfg, candidate = _plan(a, home, env)
    override = sim_probe_config(seconds, probe_mode=probe_mode, resolution=settings.get("resolution"))
    owned = check_run_files(mod_home, knobs, settings, a)
    if owned:
        raise SimError("; ".join(owned))
    build, art = mod_home / "build", mod_home / "artifacts"
    env.out(f"[sim] scenario {a.script or a.scenario}: {seconds} s session, mod home {mod_home}")
    if a.dry_run:
        env.out("[sim] dry run: nothing written. Override config:")
        env.out("  " + override.strip())
        env.out(f"[sim] script: {script_cfg.splitlines()[0]}")
        env.out(f"[sim] probe mode {probe_mode}")
        for n in knobs:
            env.out(f"[sim] knob build/{n}" + (f" = {settings['temporal_aa']}" if n == AA_MODE_KNOB and "temporal_aa" in settings else " (empty)"))
        for k, v in launch_env.items():
            env.out(f"[sim] env {k}={v}")
        if candidate:
            env.out(f"[sim] candidate DLL {candidate[0]}: sha256 {candidate[1]}; installed {installed_dll(mod_home)} "
                    f"sha256 {candidate[2]} is backed up, replaced for the run and restored afterwards")
        return 0

    stamp = _timestamp()
    backup = backup_saves(inst.prefix, a.backup_root, stamp)
    env.out(f"[sim] saves backed up to {backup}")
    guard, notes, launched, rc, reason, placed, user_knobs = FileGuard(), [], False, 1, "", False, []
    run_dir = Path(a.out_root) / stamp
    started = None
    try:
        if candidate:
            target = installed_dll(mod_home)
            cand_bytes = candidate[0].read_bytes()  # read once: these exact bytes are hashed, recorded and placed
            cand_sha = hashlib.sha256(cand_bytes).hexdigest()
            if not is_pe_dll(cand_bytes):
                raise SimError(f"{candidate[0]} is not a PE DLL")
            before = backup_dll(backup, target, cand_sha)
            if before != candidate[2]:
                raise SimError("the installed DLL changed while planning")
            placed = True
            place_atomic(cand_bytes, target, cand_sha)
            if steam.sha256_file(target) != cand_sha:
                raise SimError("candidate DLL was not placed correctly")
            env.out(f"[sim] candidate DLL placed (installed {before[:12]} backed up to {backup}; restored afterwards)")
        if a.test_save:
            swap_in_test_save(inst.prefix, a.test_save)
            env.out(f"[sim] test save {a.test_save} swapped in")
        guard.remember(build / "dxgi-probe.cfg")
        as_bytes = lambda x: x if isinstance(x, bytes) else x.encode("utf-8")  # noqa: E731
        planned = {build / OVERRIDE_NAME: as_bytes(override), build / SCRIPT_CFG: as_bytes(script_cfg), art / SCRIPT_JSON: as_bytes(script_json)}
        if a.autocontinue:
            planned[build / AUTOCONTINUE_NAME] = (str(a.autocontinue_delay) + "\n").encode()
        if a.settle is not None:
            planned[build / SETTLE_NAME] = (str(a.settle) + "\n").encode()
        for n in knobs:
            planned[build / n] = (f"{settings['temporal_aa']}\n" if n == AA_MODE_KNOB and "temporal_aa" in settings else "").encode()
        # the record first (a hard kill skips the guard): path -> sha256 of the bytes about to be written
        record = {str(p.relative_to(mod_home)): hashlib.sha256(d).hexdigest() for p, d in planned.items()}
        guard.write(build / KNOBS_NAME, json.dumps({"version": 1, "files": record}, indent=1) + "\n")
        for p, d in planned.items():
            guard.write(p, d)
        rig_names = {p.name for p in planned} | {KNOBS_NAME}
        user_knobs = user_knob_files(build, rig_names)
        rendering = [n for n in user_knobs if RENDER_KNOB_RE.match(n)]
        if rendering:
            env.out("[sim] warning: build/ already holds knobs that change rendering: " + ", ".join(rendering) +
                    ". They apply to this run (the probe reads them unconditionally); the report lists them per run")
        env.launch_env = launch_env
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
        with defer_sigint(env.out):
            try:
                gone = close_game(env, launched, notes)
            except Exception as e:  # noqa: BLE001 - the restore below must still be decided
                env.out(f"[sim] closing the game failed ({type(e).__name__}: {e})")
                gone = False
                with contextlib.suppress(Exception):
                    gone = not game_processes(env.proc_root)
            try:
                snapshot(mod_home, run_dir, {"scenario": a.script or a.scenario, "seconds": seconds, "override": override,
                                             "return_code": rc, "reason": reason, "notes": notes, "backup": str(backup),
                                             "probe_mode": probe_mode, "user_knobs": user_knobs, "rig_knobs": knobs},
                         since_ns=started)
                env.out(f"[sim] artifacts snapshot: {run_dir}")
            except Exception as e:  # noqa: BLE001
                env.out(f"[sim] snapshot failed ({type(e).__name__}): {e}")
            try:
                failed = guard.restore()
            except Exception as e:  # noqa: BLE001
                failed = [f"{type(e).__name__}: {e}"]
            if failed:
                env.out("[sim] could not reset dev files: " + "; ".join(failed))
            if gone:
                try:
                    ok, rnotes = restore_saves(inst.prefix, backup)
                except Exception as e:  # noqa: BLE001
                    ok, rnotes = False, [f"restore raised {type(e).__name__}: {e}"]
                for n in rnotes:
                    env.out(f"[sim]   {n}")
                env.out("[sim] saves and installed DLL restored and verified" if ok else f"[sim] RESTORE INCOMPLETE: backup kept at {backup}; fix and run: d2vr-linux sim --restore. {clear_hint(backup)}")
                if not ok:
                    rc = 2
            else:
                dll_note = ""
                if candidate:
                    rec, problem = read_dll_record(backup)
                    if rec:
                        src, dest = backup / rec["backup"], Path(rec["path"])
                        if _sha(src) == rec["original_sha256"]:
                            dll_note = (f" The candidate DLL is STILL INSTALLED at {dest}. After the game has exited, `d2vr-linux sim --restore` "
                                        f"restores it; by hand, only this hash-guarded command: "
                                        f"{manual_restore_command(src, dest, rec['original_sha256'], rec['candidate_sha256'])}")
                        else:
                            dll_note = (f" The candidate DLL is STILL INSTALLED at {dest} and its backup {src} fails its hash check: "
                                        "do NOT copy it; reinstall the mod after the game has exited.")
                    elif problem:
                        dll_note = f" {problem}"
                env.out(f"[sim] the game is still running: saves NOT restored.{dll_note} Close it, then run: d2vr-linux sim --restore ({backup})")
                rc = 2
    env.out(f"[sim] result: {'OK' if rc == 0 else 'FAILED'}{(' (' + reason + ')') if reason else ''}")
    status_file = run_dir / "artifacts" / "xr-live-status.json"
    if not status_file.is_file() and (run_dir / "artifacts" / "dxgi-probe-result.json").is_file():
        status_file = run_dir / "artifacts" / "dxgi-probe-result.json"  # a flat probe mode writes only the result file
    env.out(f"[sim] status fields ({status_file.name}):")
    for line in summarize(status_file, a.status_field or DEFAULT_FIELDS):
        env.out(line)
    return rc
