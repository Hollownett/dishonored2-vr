"""Toolchain detection and Wine helpers shared by the proxy tests (harness imported read-only)."""
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "linux"))
import run_build_cmd as harness  # noqa: E402


def toolchain_available() -> bool:
    return ((harness.LLVM_BIN / "clang-cl").exists() and (harness.XWIN / "crt").exists()
            and shutil.which("wine") is not None)


def wine_env(extra_overrides: str = "") -> dict:
    env = harness.wine_env()
    if extra_overrides:
        env["WINEDLLOVERRIDES"] = env["WINEDLLOVERRIDES"] + ";" + extra_overrides
    return env


def start_wine() -> None:
    harness.prepare_wine()


def stop_wine() -> None:
    subprocess.run(["wineserver", "-k"], env=wine_env(), stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=60)


def to_wine_path(p: Path) -> str:
    return "Z:" + str(Path(p).resolve()).replace("/", "\\")


def load_proxy_build():
    """Load linux/proxy/build.py by path (a bare `import build` would hit the repo's build/ dir)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("d2vr_proxy_build", REPO / "linux" / "proxy" / "build.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
