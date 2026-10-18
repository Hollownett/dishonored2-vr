"""Mod home layout, release zip and OpenXR loader verification, game-dir proxy install/uninstall."""
import hashlib
import json
import os
import re
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
DEFAULT_PROXY = REPO_ROOT / "build/linux-proxy/dinput8.dll"

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
    try:
        with urlopen(url) as resp:
            data = resp.read()
    except (OSError, ValueError) as e:
        raise InstallError(f"could not download {url}: {e}. Download it yourself and pass --openxr-zip")
    try:
        if _sha(data) != source["zip_sha256"]:
            raise InstallError(f"downloaded OpenXR zip from {url} does not match the pinned sha256")
        tmp.write_bytes(data)
        os.replace(tmp, dest)
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
    if openxr_zip is None:
        openxr_zip = fetch_openxr_zip(source, cache_dir, urlopen)
    loader = read_openxr_loader(source, Path(openxr_zip))
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
