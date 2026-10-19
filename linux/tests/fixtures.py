"""Builds temporary Steam / Proton prefix / game trees from the fixture files."""
import hashlib
from pathlib import Path
from types import SimpleNamespace

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def read_fixture(name: str) -> str:
    return (FIXTURES / name).read_text(encoding="utf-8")


def make_steam(tmp: Path, *, exe_bytes=b"MZfake", with_prefix=True, library_name="steam ssd"):
    tmp = Path(tmp)
    home = tmp / "home"
    steam_root = home / ".local/share/Steam"
    library = tmp / library_name / "SteamLibrary"
    (steam_root / "steamapps").mkdir(parents=True)
    vdf = (
        '"libraryfolders"\n{\n'
        f'\t"0"\n\t{{\n\t\t"path"\t\t"{steam_root}"\n\t}}\n'
        f'\t"1"\n\t{{\n\t\t"path"\t\t"{library}"\n\t}}\n}}\n'
    )
    (steam_root / "steamapps/libraryfolders.vdf").write_text(vdf, encoding="utf-8")
    apps = library / "steamapps"
    game_dir = apps / "common/Dishonored2"
    game_dir.mkdir(parents=True)
    (apps / "appmanifest_403640.acf").write_text(read_fixture("appmanifest_403640.acf"), encoding="utf-8")
    (game_dir / "Dishonored2.exe").write_bytes(exe_bytes)
    prefix = apps / "compatdata/403640/pfx"
    if with_prefix:
        (prefix / "drive_c/windows/system32").mkdir(parents=True)
        (prefix / "system.reg").write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
    return SimpleNamespace(
        home=home, steam_root=steam_root, library=library, game_dir=game_dir,
        prefix=prefix, exe_sha256=hashlib.sha256(exe_bytes).hexdigest(),
    )
