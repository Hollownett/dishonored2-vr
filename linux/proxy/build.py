#!/usr/bin/env python3
"""Build the Linux/Proton proxy and its Wine-hosted checks with the harness's clang-cl toolchain."""
import subprocess
import sys
from pathlib import Path
from typing import Iterable

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "linux"))
import run_build_cmd as harness  # noqa: E402  (read-only import)

OUT = REPO / "build" / "linux-proxy"
_FLAGS = ["/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W4", "/WX", "/O2", "/MT"]
_COMMON = _FLAGS + ["/DUNICODE", "/D_UNICODE", "/Ilinux/proxy", "/Fo:build/linux-proxy/"]

TARGETS: dict = {
    "core_check": {
        "args": _FLAGS + ["/Ilinux/proxy", "linux/tests/proxy_check/proxy_core_check.cpp",
                          "/Fo:build/linux-proxy/", "/Fe:build/linux-proxy/proxy_core_check.exe"],
        "out": OUT / "proxy_core_check.exe",
    },
    "proxy": {
        "args": _COMMON + ["/LD", "linux/proxy/dinput8_proxy.cpp", "/Fe:build/linux-proxy/dinput8.dll",
                           "/link", "/DEF:linux/proxy/dinput8.def", "kernel32.lib", "user32.lib"],
        "out": OUT / "dinput8.dll",
    },
    "host": {
        "args": _COMMON + ["linux/tests/proxy_check/host.cpp", "/Fe:build/linux-proxy/proxy_host.exe",
                           "/link", "dinput8.lib", "dxguid.lib", "user32.lib"],
        "out": OUT / "proxy_host.exe",
    },
    "fake_mod": {
        "args": _COMMON + ["/LD", "linux/tests/proxy_check/fake_mod.cpp",
                           "/Fe:build/linux-proxy/d2vr_render_probe_arm_v999.dll"],
        "out": OUT / "d2vr_render_probe_arm_v999.dll",
    },
    "fake_mod_lowcaps": {
        "args": _COMMON + ["/DFAKE_CAPS=1", "/LD", "linux/tests/proxy_check/fake_mod.cpp",
                           "/Fe:build/linux-proxy/lowcaps/d2vr_render_probe_arm_v999.dll"],
        "out": OUT / "lowcaps" / "d2vr_render_probe_arm_v999.dll",
    },
}


def cl_args(target: str) -> list:
    return list(TARGETS[target]["args"])


def build(targets: Iterable) -> dict:
    results = {}
    for name in targets:
        out = TARGETS[name]["out"]
        out.parent.mkdir(parents=True, exist_ok=True)
        argv, _ = harness.cl_to_clang(cl_args(name))
        r = subprocess.run(argv, cwd=REPO, capture_output=True, text=True)
        text = r.stdout + r.stderr
        if r.returncode != 0 or "warning:" in text:
            raise RuntimeError(text)
        results[name] = out
    return results


def main(argv=None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    names = list(TARGETS) if "--tests" in args else [t for t in ("proxy",) if t in TARGETS]
    try:
        for name, path in build(names).items():
            print(name, path)
    except RuntimeError as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
