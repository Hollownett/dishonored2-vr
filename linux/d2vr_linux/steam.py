"""Steam discovery: VDF/ACF parsing, library, game and Proton prefix lookup, exe pin."""
import hashlib
from dataclasses import dataclass
from pathlib import Path

GAME_APPID = "403640"
STEAMVR_APPID = "250820"
GAME_EXE = "Dishonored2.exe"
GAME_BUILD = "17576867"
GAME_SHA256 = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"

_ESCAPES = {"\\": "\\", '"': '"', "n": "\n", "t": "\t"}


class SteamError(Exception):
    pass


def _tokens(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c in "{}":
            yield c, False
            i += 1
        elif c == '"':
            i += 1
            out = []
            while True:
                if i >= n:
                    raise ValueError("unterminated string in VDF")
                c = text[i]
                if c == '"':
                    i += 1
                    break
                if c == "\\" and i + 1 < n and text[i + 1] in _ESCAPES:
                    out.append(_ESCAPES[text[i + 1]])
                    i += 2
                else:
                    out.append(c)
                    i += 1
            yield "".join(out), True
        else:
            raise ValueError(f"unexpected character {c!r} in VDF")


def parse_vdf(text: str) -> dict:
    """Parse Valve KeyValues text into nested dicts of strings (later duplicate keys win)."""
    root = {}
    stack = [root]
    pending = None  # key waiting for its value
    for tok, quoted in _tokens(text):
        cur = stack[-1]
        if quoted:
            if pending is None:
                pending = tok
            else:
                cur[pending] = tok
                pending = None
        elif tok == "{":
            if pending is None:
                raise ValueError("'{' without a key in VDF")
            child = {}
            cur[pending] = child
            stack.append(child)
            pending = None
        else:  # "}"
            if pending is not None or len(stack) == 1:
                raise ValueError("unbalanced '}' in VDF")
            stack.pop()
    if len(stack) != 1 or pending is not None:
        raise ValueError("unbalanced braces in VDF")
    return root


def _read_vdf(path: Path):
    try:
        return parse_vdf(path.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return None


def _dedup(paths):
    seen, out = set(), []
    for p in paths:
        key = p.resolve()
        if key not in seen:
            seen.add(key)
            out.append(p)
    return out


def steam_roots(home: Path) -> list:
    cands = [home / ".local/share/Steam", home / ".steam/steam", home / ".steam/root"]
    return _dedup([c.resolve() for c in cands if c.exists()])


def _posix_abs(s: str) -> bool:
    return s.startswith("/") and "\\" not in s


def library_paths(steam_root: Path) -> list:
    libs = [steam_root]
    data = _read_vdf(steam_root / "steamapps/libraryfolders.vdf")
    if data:
        top = next((v for k, v in data.items() if k.lower() == "libraryfolders"), None)
        if isinstance(top, dict):
            for key, val in top.items():
                if not key.isdigit():
                    continue
                path = val.get("path") if isinstance(val, dict) else val
                if isinstance(path, str) and _posix_abs(path):
                    libs.append(Path(path))
    return _dedup(libs)


def find_game(libraries: list) -> tuple:
    for lib in libraries:
        acf = lib / f"steamapps/appmanifest_{GAME_APPID}.acf"
        if not acf.is_file():
            continue
        data = _read_vdf(acf)
        installdir = data.get("AppState", {}).get("installdir") if data else None
        if not isinstance(installdir, str) or not installdir:
            raise SteamError(f"{acf} has no installdir; ask Steam to verify Dishonored 2")
        game_dir = lib / "steamapps/common" / installdir
        if not (game_dir / GAME_EXE).is_file():
            raise SteamError(f"{game_dir / GAME_EXE} not found; reinstall or verify Dishonored 2 in Steam")
        return lib, game_dir
    raise SteamError("Dishonored 2 (app 403640) is not installed in any Steam library")


def find_prefix(game_library: Path, libraries: list) -> Path:
    for lib in [game_library] + [l for l in libraries if l != game_library]:
        pfx = lib / "steamapps/compatdata" / GAME_APPID / "pfx"
        if (pfx / "drive_c").is_dir():
            return pfx
    raise SteamError(
        "No Proton prefix for Dishonored 2 (compatdata/403640/pfx); run Dishonored 2 once "
        "with Proton so Steam creates its prefix, then try again"
    )


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_game_exe(game_dir: Path, expected: str = GAME_SHA256) -> str:
    got = sha256_file(game_dir / GAME_EXE)
    if got != expected:
        raise SteamError(
            f"{GAME_EXE} hash {got} does not match the supported build {GAME_BUILD} ({expected}). "
            "Steam updated the game; this mod only supports that build"
        )
    return got


def find_steamvr(libraries: list):
    for lib in libraries:
        d = lib / "steamapps/common/SteamVR"
        if (lib / f"steamapps/appmanifest_{STEAMVR_APPID}.acf").is_file() and (d / "steamxr_linux64.json").is_file():
            return d
    return None


@dataclass
class Install:
    steam_root: Path
    libraries: list
    game_library: Path
    game_dir: Path
    prefix: Path


def locate(home: Path, steam_root: Path = None) -> Install:
    roots = [steam_root] if steam_root else steam_roots(home)
    if not roots:
        raise SteamError("Steam not found (looked in ~/.local/share/Steam, ~/.steam/steam, ~/.steam/root)")
    libs = _dedup([l for r in roots for l in library_paths(r)])
    game_library, game_dir = find_game(libs)
    return Install(roots[0], libs, game_library, game_dir, find_prefix(game_library, libs))
