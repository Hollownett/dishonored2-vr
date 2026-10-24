"""Find the user's OWN nvngx_dlss.dll in their installed Steam games and record it in build/dlss-dll-path (a Wine Z:\\ path).
Nothing is copied, bundled or downloaded; the DLL (src/dlss_mode.hpp) reads the knob. Mirrors launcher/DlssFinder.cs."""
import mmap
import os
import struct
from pathlib import Path

from . import steam

DLL_NAME = "nvngx_dlss.dll"
KNOB = "dlss-dll-path"


def libraries(home: Path, steam_root: Path = None) -> list:
    roots = [steam_root] if steam_root else steam.steam_roots(home)
    return steam._dedup([lib for r in roots for lib in steam.library_paths(Path(r))])


def candidates(libs, max_depth: int = 5, max_files: int = 50) -> list:
    """steamapps/common/**/nvngx_dlss.dll, at most max_depth folders below common, no symlinks, unreadable folders skipped."""
    found = []

    def walk(d: Path, depth: int):
        if len(found) >= max_files:
            return
        try:
            entries = sorted(os.scandir(d), key=lambda e: e.name)
        except OSError:
            return
        for e in entries:
            if e.name.lower() == DLL_NAME and e.is_file(follow_symlinks=False):
                if len(found) < max_files and Path(e.path) not in found:
                    found.append(Path(e.path))
        if depth >= max_depth:
            return
        for e in entries:
            try:
                if e.is_dir(follow_symlinks=False):
                    walk(Path(e.path), depth + 1)
            except OSError:
                pass

    for lib in libs:
        common = Path(lib) / "steamapps/common"
        if common.is_dir():
            walk(common, 0)
    return found


def file_version(path: Path) -> tuple:
    """(a, b, c, d) from the PE VS_FIXEDFILEINFO block, (0, 0, 0, 0) when unreadable."""
    try:
        with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
            sig = struct.pack("<II", 0xFEEF04BD, 0x10000)  # signature + dwStrucVersion, so a stray 4-byte match is skipped
            i = m.find(sig)
            if i < 0 or i + 16 > len(m):
                return (0, 0, 0, 0)
            ms, ls = struct.unpack("<II", m[i + 8:i + 16])
            return (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)
    except (OSError, ValueError):
        return (0, 0, 0, 0)


def _mtime(p: Path) -> float:
    try:
        return p.stat().st_mtime
    except OSError:
        return 0.0


def best(cands):
    """Highest PE FileVersion when readable, else newest mtime; ties go to the shortest path."""
    cands = list(cands)
    return min(cands, key=lambda p: (tuple(-x for x in file_version(p)), -_mtime(p), len(str(p)), str(p)), default=None)


def game_name(dll: Path) -> str:
    for d in dll.parents:
        if d.parent.name == "common" and d.parent.parent.name == "steamapps":
            return d.name
    return dll.parent.name


def wine_path(p: Path) -> str:
    return "Z:" + str(p).replace("/", "\\")


def find_and_configure(mod_home: Path, libs):
    """Returns ("exists", None) when the mod home already has a provider or knob, (dll, game) after writing the knob, else (None, None)."""
    mod_home = Path(mod_home)
    knob = mod_home / "build" / KNOB
    if (mod_home / DLL_NAME).exists() or knob.exists():
        return "exists", None
    dll = best(candidates(libs))
    if dll is None:
        return None, None
    knob.parent.mkdir(parents=True, exist_ok=True)
    tmp = knob.with_name(KNOB + ".tmp")
    tmp.write_text(wine_path(dll) + "\n", encoding="utf-8")
    os.replace(tmp, knob)
    return dll, game_name(dll)
