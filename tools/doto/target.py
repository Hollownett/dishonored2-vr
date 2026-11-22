"""Port targets: one JSON per game build the Dishonored 2 mod is translated to (tools/doto/targets/<name>.json).

The base is the pinned Dishonored 2 build (targets/base.json). A target names its executable (found in any Steam library),
its SHA-256 and SizeOfImage, its C++ define (D2VR_GAME_<NAME>, profile traits in src/game_profile.hpp), its reviewed
rules file and its output folders. Adding a game or a new game build = a new target JSON, a profile block in
src/game_profile.hpp, then the pipeline (docs/DOTO-VR.md, "Adding a target").
"""
import json
import os
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TARGETS = HERE / "targets"


def steam_libraries():
    roots = []
    try:
        import winreg
        for hive, key, value in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                                 (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    roots.append(Path(winreg.QueryValueEx(k, value)[0]))
            except OSError:
                pass
    except ImportError:
        pass
    roots.append(Path(r"C:\Program Files (x86)\Steam"))
    libs = []
    for r in roots:
        vdf = r / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            for m in re.finditer(r'"path"\s+"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace")):
                libs.append(Path(m.group(1).replace("\\\\", "\\")))
        libs.append(r)
    seen, out = set(), []
    for lib in libs:
        k = str(lib).lower()
        if k not in seen:
            seen.add(k)
            out.append(lib)
    return out


def find_exe(folder, exe):
    env = os.environ.get("D2VR_EXE_" + re.sub(r"\W", "_", exe).upper())
    if env and Path(env).is_file():
        return Path(env)
    for lib in steam_libraries():
        p = lib / "steamapps" / "common" / folder / exe
        if p.is_file():
            return p
    raise FileNotFoundError(f"{exe} not found in any Steam library (steamapps/common/{folder})")


class Target:
    def __init__(self, name):
        self.name = name
        data = json.loads((TARGETS / f"{name}.json").read_text(encoding="utf-8"))
        self.data = data
        self.display = data["display"]
        self.exe_name = data["exe"]
        self.folder = data["steam_folder"]
        self.sha256 = data.get("sha256")
        self.size_of_image = int(data["size_of_image"], 0) if data.get("size_of_image") else None
        self.define = data.get("define")
        self.rules = ROOT / data["rules"] if data.get("rules") else None
        self.lab = ROOT / data.get("lab", f"artifacts/{name}-lab")
        self.src_out = ROOT / data.get("src_out", f"build/{name}-src")
        self.dll_out = ROOT / data.get("dll_out", f"build/{name}")

    @property
    def exe(self):
        return find_exe(self.folder, self.exe_name)


def base():
    return Target("base")


def load(name="doto"):
    return Target(name)


if __name__ == "__main__":
    # tools\doto\build-doto.cmd reads the target's folders: "python tools\doto\target.py folders" prints
    # SRC=<src_out> and OUT=<dll_out> for D2VR_PORT_TARGET (default doto).
    import sys
    t = load(os.environ.get("D2VR_PORT_TARGET", "doto"))
    if sys.argv[1:] == ["folders"]:
        print(f"SRC={t.src_out}")
        print(f"OUT={t.dll_out}")
    else:
        print(f"{t.name}: {t.display} ({t.exe_name}) -> {t.src_out}, {t.dll_out}")
