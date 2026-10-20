"""Mod home layout, release zip and OpenXR loader verification, game-dir proxy install/uninstall."""
import hashlib
import json
import os
import re
import sys
import time
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

from .steam import GAME_SHA256, Install, SteamError, check_game_exe, sha256_file

MOD_HOME_WIN = r"C:\users\steamuser\AppData\Local\DishonoredVR"
MOD_HOME_REL = Path("drive_c/users/steamuser/AppData/Local/DishonoredVR")
INI_NAME = "d2vr-linux.ini"
PROXY_NAME = "dinput8.dll"
PROXY_MARKER = b"D2VR-LINUX-PROXY-1"
START_TRIGGER = "linux-start"
STOP_REQUEST = "linux-stop"
DLL_RE = r"^d2vr_render_probe_arm_v[0-9]+\.dll$"
MANIFEST_RE = r"^repair-v[0-9]+-test-candidate\.json$"
REPO_ROOT = Path(__file__).resolve().parents[2]
OPENXR_SOURCE = REPO_ROOT / "third_party/openxr/bin/source.json"
DEFAULT_FEATURES = REPO_ROOT / "tools/default_features.json"
FEATURES_STATE = "launcher-features.json"
DEFAULT_PROXY = REPO_ROOT / "build/linux-proxy/dinput8.dll"

MAX_DOWNLOAD = 64 * 1024 * 1024
_LOADER_REL = "third_party/openxr/bin/openxr_loader.dll"


class InstallError(Exception):
    pass


@dataclass
class ModRelease:
    dll_name: str
    dll: bytes
    manifest_name: str
    manifest: bytes
    notices: "bytes | None"


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _basename(member: str) -> str:
    return re.split(r"[/\\]", member)[-1]


def read_release_zip(zip_path: Path, game_sha256: str = GAME_SHA256) -> ModRelease:
    """Validate a published d2vr-v*-dll.zip (members matched by basename; names may use backslashes)."""
    try:
        zf = zipfile.ZipFile(zip_path)
    except (OSError, zipfile.BadZipFile) as e:
        raise InstallError(f"cannot read release zip {zip_path}: {e}")
    with zf:
        by_base = {}
        for info in zf.infolist():
            base = _basename(info.filename)
            if base and not info.is_dir():
                by_base.setdefault(base, info)
        manifests = [b for b in by_base if re.match(MANIFEST_RE, b)]
        if len(manifests) != 1:
            raise InstallError(
                f"{zip_path.name} must contain exactly one repair-v*-test-candidate.json manifest, found {len(manifests)}")
        manifest_name = manifests[0]
        manifest_bytes = zf.read(by_base[manifest_name])
        try:
            m = json.loads(manifest_bytes)
            if not isinstance(m, dict):
                raise ValueError("not an object")
        except ValueError as e:
            raise InstallError(f"{manifest_name} is not valid JSON: {e}")
        if m.get("launch_blocked") is True:
            raise InstallError(m.get("launch_blocked_reason") or "This release is launch-blocked.")
        dll_name = m.get("dll_name")
        if not isinstance(dll_name, str) or not re.match(DLL_RE, dll_name):
            raise InstallError("Invalid candidate filename in the manifest.")
        if dll_name not in by_base:
            raise InstallError(f"{dll_name} is not in {zip_path.name}")
        dll = zf.read(by_base[dll_name])
        if _sha(dll) != m.get("sha256"):
            raise InstallError(f"{dll_name} does not match its manifest sha256; refusing to install a changed DLL.")
        if m.get("game_sha256") != game_sha256:
            raise InstallError("This release was validated for a different game build.")
        notices = zf.read(by_base["THIRD_PARTY_NOTICES.md"]) if "THIRD_PARTY_NOTICES.md" in by_base else None
    return ModRelease(dll_name, dll, manifest_name, manifest_bytes, notices)


def read_openxr_loader(source: dict, zip_path: Path) -> bytes:
    try:
        zip_bytes = Path(zip_path).read_bytes()
    except OSError as e:
        raise InstallError(f"cannot read OpenXR loader zip {zip_path}: {e}")
    if _sha(zip_bytes) != source["zip_sha256"]:
        raise InstallError(f"OpenXR loader zip {Path(zip_path).name} does not match the pinned sha256")
    member = source["zip_member"]
    try:
        with zipfile.ZipFile(Path(zip_path)) as zf:
            dll = zf.read(member)
    except (KeyError, zipfile.BadZipFile):
        raise InstallError(f"{member} not found in OpenXR loader zip {Path(zip_path).name}")
    if _sha(dll) != source["dll_sha256"]:
        raise InstallError(f"{member} does not match the pinned sha256")
    return dll


def fetch_openxr_zip(source: dict, cache_dir: Path, urlopen=urllib.request.urlopen) -> Path:
    """Return the verified Khronos loader zip, downloading it into cache_dir when it is not cached."""
    url = source["url"]
    cache_dir = Path(cache_dir)
    dest = cache_dir / url.rsplit("/", 1)[-1]
    if dest.is_file() and sha256_file(dest) == source["zip_sha256"]:
        return dest
    cache_dir.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".tmp")
    h = hashlib.sha256()
    size = 0
    try:
        with urlopen(url, timeout=30) as resp, open(tmp, "wb") as out:
            while True:
                chunk = resp.read(1 << 16)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_DOWNLOAD:
                    raise InstallError(f"download from {url} exceeds {MAX_DOWNLOAD >> 20} MB; aborted")
                h.update(chunk)
                out.write(chunk)
        if h.hexdigest() != source["zip_sha256"]:
            raise InstallError(f"downloaded OpenXR zip from {url} does not match the pinned sha256")
        os.replace(tmp, dest)
    except (OSError, ValueError) as e:  # URLError and timeouts are OSErrors
        raise InstallError(f"could not download {url}: {e}. Download it yourself and pass --openxr-zip")
    finally:
        if tmp.exists():
            tmp.unlink()
    return dest


def is_our_proxy(path: Path) -> bool:
    try:
        return PROXY_MARKER in Path(path).read_bytes()
    except OSError:
        return False


def mod_home_dir(prefix: Path) -> Path:
    return Path(prefix) / MOD_HOME_REL


def _write(path: Path, data: bytes) -> bool:
    """Atomically write data unless identical bytes are already there. True if written."""
    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)
    return True


def _load_source(openxr_source):
    if isinstance(openxr_source, dict):
        return openxr_source, json.dumps(openxr_source, indent=1).encode() + b"\n", OPENXR_SOURCE.parent
    p = Path(openxr_source)
    raw = p.read_bytes()
    return json.loads(raw), raw, p.parent


class FeatureError(Exception):
    """The feature list or state cannot be used (I/O or invalid list). apply_default_features touches nothing when it raises."""


_NAME_RE = re.compile(r"^[a-z0-9-]+$")
_SAVED = ".off-by-launcher"


def load_features(list_path: "Path | None" = None) -> list:
    """Parse and validate the whole list (same rules as launcher/Features.cs Load); any bad row rejects it."""
    list_path = DEFAULT_FEATURES if list_path is None else list_path
    try:
        rows = json.loads(Path(list_path).read_text(encoding="utf-8"))["features"]
        if not isinstance(rows, list):
            raise ValueError("features is not a list")
        out, seen = [], set()
        for r in rows:
            if not isinstance(r, dict):
                raise ValueError("row is not an object")
            for k in ("id", "file", "kind", "label", "help"):
                if not isinstance(r.get(k), str):
                    raise ValueError(f"missing {k}")
            for k in ("id", "file"):
                if not _NAME_RE.match(r[k]):
                    raise ValueError(f"bad {k}: {r[k]!r}")
            if r["kind"] not in ("enable", "disable"):
                raise ValueError("bad kind")
            if not isinstance(r.get("default"), bool):
                raise ValueError("default must be a boolean")
            req = r.get("requires_dll_knob")
            if "requires_dll_knob" in r and not (isinstance(req, str) and _NAME_RE.match(req)):
                raise ValueError("bad requires_dll_knob")
            if r["id"] in seen:
                raise ValueError("duplicate id")
            seen.add(r["id"])
            out.append(r)
        return out
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise FeatureError(f"feature list {list_path}: {e}")


def scan_knobs(dll: bytes) -> set:
    """UTF-16 strings of [a-z0-9-] (3+ chars) at either byte parity, as launcher/Features.cs ScanKnobs."""
    found = set()
    for start in (0, 1):
        run = []
        for i in range(start, len(dll) - 1, 2):
            c = dll[i] | dll[i + 1] << 8
            if 0x61 <= c <= 0x7a or 0x30 <= c <= 0x39 or c == 0x2d:
                run.append(chr(c))
                continue
            if len(run) >= 3:
                found.add("".join(run))
            run = []
        if len(run) >= 3:
            found.add("".join(run))
    return found


def _str_list(v) -> set:
    if not isinstance(v, list) or not all(isinstance(x, str) for x in v):
        raise ValueError("not a list of strings")
    return set(v)


def _read_state(state_path: Path, changed: list, log):
    """(off, seeded, applied). I/O errors raise FeatureError; parse/type errors archive the file to .bad and start fresh."""
    if state_path.is_dir():
        raise FeatureError(f"{state_path} is a folder")
    if not state_path.exists():
        return set(), set(), {}
    try:
        raw = state_path.read_bytes()
    except OSError as e:
        raise FeatureError(f"cannot read {state_path}: {e}")
    try:
        d = json.loads(raw)
        if not isinstance(d, dict) or d.get("version") not in (1, 2):
            raise ValueError("bad version")
        off, seeded = _str_list(d["off"]), _str_list(d["seeded"])
        applied = {}
        if d["version"] == 2 and "applied" in d:
            if not isinstance(d["applied"], dict):
                raise ValueError("applied")
            for k, v in d["applied"].items():
                if not (isinstance(v, dict) and isinstance(v.get("file"), str) and isinstance(v.get("kind"), str)):
                    raise ValueError("applied row")
                applied[k] = (v["file"], v["kind"])
        return off, seeded, applied
    except (ValueError, KeyError, TypeError):
        try:
            os.replace(state_path, state_path.with_name(state_path.name + ".bad"))
        except OSError as e:
            raise FeatureError(f"cannot archive corrupt {state_path}: {e}")
        changed.append(f"build/{FEATURES_STATE}.bad")
        if log:
            log("feature state was corrupt and was moved to launcher-features.json.bad")
        return set(), set(), {}


def _touch(path: Path, changed: list, build: Path) -> None:
    if not path.exists() and _write(path, b""):
        changed.append(f"build/{path.name}")


def _retire(path: Path, changed: list) -> None:
    """Turn an enable knob off without losing a valued file: empty is deleted, anything else is set aside."""
    if not path.exists():
        return
    if path.stat().st_size == 0:
        path.unlink()
    else:
        os.replace(path, path.with_name(path.name + _SAVED))
    changed.append(f"build/{path.name}")


def _restore(path: Path, changed: list, build: Path) -> None:
    if path.exists():
        return
    saved = path.with_name(path.name + _SAVED)
    if saved.exists():
        os.replace(saved, path)
        changed.append(f"build/{path.name}")
    else:
        _touch(path, changed, build)


def _write_state(state_path: Path, off, seeded, applied, changed: list) -> None:
    data = (json.dumps({"version": 2, "off": sorted(off), "seeded": sorted(seeded),
                        "applied": {k: {"file": v[0], "kind": v[1]} for k, v in sorted(applied.items())}}, indent=2) + "\n").encode()
    if _write(state_path, data):
        changed.append(f"build/{FEATURES_STATE}")


def apply_default_features(home: Path, list_path: "Path | None" = None, dll: "bytes | None" = None, log=None) -> list:
    """Same semantics as launcher/Features.cs Apply. `off` is authoritative (enable: file absent, disable: file present);
    otherwise features not yet seen, or whose file/kind changed, are seeded (default-true rows only) and user-made knobs adopted.
    Old files of changed rows and removed rows are never deleted. Raises FeatureError (touching nothing) on a bad list or an
    unreadable state file. Returns the changed paths (relative)."""
    build = Path(home) / "build"
    features = load_features(list_path)
    known = scan_knobs(dll) if dll is not None else None
    state_path = build / FEATURES_STATE
    changed = []
    off, seeded, applied = _read_state(state_path, changed, log)
    for f in features:
        req = f.get("requires_dll_knob")
        if req and known is not None and req not in known:
            continue
        path = build / f["file"]
        enable = f["kind"] == "enable"
        was_seeded = f["id"] in seeded
        old = applied.get(f["id"])
        row_changed = was_seeded and old is not None and old != (f["file"], f["kind"])
        if row_changed and log:
            log(f"feature {f['id']}: row changed ({old[1]} {old[0]} -> {f['kind']} {f['file']}); the old file is left in place")
        if f["id"] in off:
            if enable:
                _retire(path, changed)
            else:
                _touch(path, changed, build)
        elif not was_seeded or row_changed:
            if path.exists():
                if not enable:
                    off.add(f["id"])
            elif f["default"]:
                if enable:
                    _touch(path, changed, build)
            elif not enable:
                _touch(path, changed, build)
                off.add(f["id"])
        seeded.add(f["id"])
        applied[f["id"]] = (f["file"], f["kind"])
    try:
        _write_state(state_path, off, seeded, applied, changed)
    except OSError as e:
        raise FeatureError(f"cannot write {state_path}: {e}")
    return changed


def set_feature(home: Path, feature_id: str, on: bool, list_path: "Path | None" = None) -> list:
    """The user's explicit switch (Python twin of Features.Set)."""
    build = Path(home) / "build"
    f = next((r for r in load_features(list_path) if r["id"] == feature_id), None)
    if f is None:
        raise FeatureError(f"unknown feature {feature_id}")
    changed = []
    off, seeded, applied = _read_state(build / FEATURES_STATE, changed, None)
    path = build / f["file"]
    if f["kind"] == "enable":
        _restore(path, changed, build) if on else _retire(path, changed)
    elif on:
        if path.exists():
            path.unlink()
            changed.append(f"build/{path.name}")
    else:
        _touch(path, changed, build)
    (off.discard if on else off.add)(f["id"])
    seeded.add(f["id"])
    applied[f["id"]] = (f["file"], f["kind"])
    _write_state(build / FEATURES_STATE, off, seeded, applied, changed)
    return changed


def install(inst: Install, zip_path: Path, proxy: Path, openxr_zip: "Path | None", cache_dir: Path,
            game_sha256: str = GAME_SHA256, openxr_source=OPENXR_SOURCE,
            urlopen=urllib.request.urlopen) -> list:
    """Install the mod into the Proton prefix and the proxy into the game dir. All checks precede all writes.
    openxr_source is the path of source.json (a parsed dict is accepted too). Returns the changed paths."""
    # 1-5: checks
    try:
        check_game_exe(inst.game_dir, game_sha256)
    except SteamError as e:
        raise InstallError(str(e))
    release = read_release_zip(Path(zip_path), game_sha256)
    source, source_bytes, source_dir = _load_source(openxr_source)
    license_path = source_dir / "LICENSE"
    if not license_path.is_file():
        raise InstallError(f"{license_path} is missing")
    proxy = Path(proxy)
    if not proxy.is_file() or not is_our_proxy(proxy):
        raise InstallError(f"{proxy} is missing or is not the d2vr Linux proxy; build it: python3 linux/proxy/build.py")
    proxy_bytes = proxy.read_bytes()
    target = inst.game_dir / PROXY_NAME
    if target.exists() and not is_our_proxy(target):
        raise InstallError(
            f"{target} is another mod's dinput8.dll; refusing to replace it. "
            "Move it away if you want to use this mod instead.")
    if openxr_zip is None:
        openxr_zip = fetch_openxr_zip(source, cache_dir, urlopen)
    loader = read_openxr_loader(source, Path(openxr_zip))

    # 6: mod home
    changed = []
    home = mod_home_dir(inst.prefix)
    if not (home / ".git").is_dir():
        (home / ".git").mkdir(parents=True)  # StartProbe returns 3 without <root>/.git
        changed.append(".git")
    files = [
        (f"build/{release.dll_name}", release.dll),
        (f"artifacts/{release.manifest_name}", release.manifest),
        (_LOADER_REL, loader),
        ("third_party/openxr/bin/LICENSE", license_path.read_bytes()),
        ("third_party/openxr/bin/source.json", source_bytes),
        ("build/fsr4.off", b""),
    ]
    if release.notices is not None:
        files.append(("THIRD_PARTY_NOTICES.md", release.notices))
    for rel, data in files:
        if _write(home / rel, data):
            changed.append(rel)
    build = home / "build"
    for old in sorted(build.glob("d2vr_render_probe_arm_v*.dll")):
        if old.name != release.dll_name and re.match(DLL_RE, old.name):
            try:
                old.unlink()
                changed.append(f"build/{old.name}")
            except OSError:
                pass  # still loaded by a running game

    try:  # feature seeding must not fail an otherwise valid install
        changed += apply_default_features(home, dll=release.dll, log=lambda m: print("d2vr: " + m, file=sys.stderr))
    except Exception as e:  # noqa: BLE001
        print(f"d2vr: warning: default features were not applied: {e}", file=sys.stderr)

    # 7: game dir
    if _write(target, proxy_bytes):
        changed.append(PROXY_NAME)
    if _write(inst.game_dir / INI_NAME, (MOD_HOME_WIN + "\r\n").encode("ascii")):
        changed.append(INI_NAME)
    return changed


def uninstall(game_dir: Path) -> list:
    """Remove our proxy and ini from the game dir; a foreign dinput8.dll is left alone."""
    removed = []
    proxy = Path(game_dir) / PROXY_NAME
    if proxy.is_file() and is_our_proxy(proxy):
        proxy.unlink()
        removed.append(PROXY_NAME)
    ini = Path(game_dir) / INI_NAME
    if ini.is_file():
        ini.unlink()
        removed.append(INI_NAME)
    return removed


def write_request(mod_home: Path, name: str) -> Path:
    path = Path(mod_home) / "build" / name
    _write_replace(path, str(int(time.time())).encode("ascii"))
    return path


def _write_replace(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)
