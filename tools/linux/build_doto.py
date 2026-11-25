#!/usr/bin/env python3
"""Build a port-target DLL (default: Death of the Outsider) on Linux: tools/doto/build-doto.cmd with clang-cl + xwin.

    python3 tools/linux/build_doto.py <version> [--python PY] [--skip-port]
    -> build/doto/d2vr_render_probe_arm_v<version>.dll (+ .pdb, .map)
    python3 tools/linux/build_doto.py <version> --game d2
    -> build/d2/d2vr_render_probe_arm_v<version>.dll: the Dishonored 2 DLL from src/ with build-msvc.cmd's own DLL line
       (its flags minus /WX: clang-cl raises /W4 diagnostics cl does not; the harness's dxgi_probe step stays the /WX gate)

Same steps as build-doto.cmd: regenerate the target tree with tools/doto/port.py, then compile the cl line written in
build-doto.cmd (its flags are the single source of truth) through run_build_cmd.cl_to_clang. The MinHook and ImGui
objects come from the harness's own build-msvc.cmd steps when missing. Another target: D2VR_PORT_TARGET=<name>.
port.py needs pefile, capstone and numpy: pass --python (default: $D2VR_RE_PYTHON, else
~/.local/share/d2vr-re-venv/bin/python when it exists, else this interpreter).
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import run_build_cmd as harness  # noqa: E402

CMD = ROOT / "tools" / "doto" / "build-doto.cmd"
NEEDED_OBJS = [f"build/md/{n}.obj" for n in ("buffer", "hook", "trampoline", "hde64")] + \
              [f"build/imgui/{n}.obj" for n in ("imgui", "imgui_draw", "imgui_tables", "imgui_widgets", "imgui_impl_dx11")] + \
              ["build/vsh/afw_warp.obj"]   # VR Stereo Hub AFW (third_party/vsh_afw), build-msvc.cmd's own step


def default_python():
    env = os.environ.get("D2VR_RE_PYTHON")
    if env:
        return env
    venv = Path.home() / ".local/share/d2vr-re-venv/bin/python"
    return str(venv) if venv.exists() else sys.executable


def cl_line():
    lines = [ln.strip() for ln in CMD.read_text(encoding="utf-8").splitlines() if ln.strip().startswith("cl /nologo")]
    if len(lines) != 1:
        raise SystemExit(f"build_doto: expected one cl line in {CMD}, found {len(lines)}")
    return lines[0]


def d2_line(version, out):
    """build-msvc.cmd's mod DLL line (the one writing %D2VR_DIAGNOSTIC_OUTPUT%), to out/, without /WX."""
    msvc = ROOT / "tools" / "build-msvc.cmd"
    lines = [ln.strip() for ln in msvc.read_text(encoding="utf-8").splitlines()
             if ln.strip().startswith("cl /nologo") and "/Fe:%D2VR_DIAGNOSTIC_OUTPUT%" in ln]
    if len(lines) != 1:
        raise SystemExit(f"build_doto: expected one mod DLL line in {msvc}, found {len(lines)}")
    rel = out.relative_to(ROOT).as_posix()
    s = (lines[0].replace("/Fe:%D2VR_DIAGNOSTIC_OUTPUT%", f"/Fe:{rel}/d2vr_render_probe_arm_v{version}.dll")
         .replace("/Fd:build\\dxgi_probe.pdb", f"/Fd:{rel}/dxgi_probe_v{version}.pdb")
         .replace("/Fo:build\\dxgi_probe.obj", f"/Fo:{rel}/dxgi_probe.obj"))
    if "%" in s:
        raise SystemExit("build_doto: unexpanded variable in the D2 line: " + s)
    return [t.replace("\\", "/") for t in s.split()[1:] if t != "/WX"]


def folders(py):
    out = subprocess.run([py, str(ROOT / "tools/doto/target.py"), "folders"], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    vals = dict(ln.split("=", 1) for ln in out.splitlines() if "=" in ln)
    return Path(vals["SRC"]), Path(vals["OUT"])


def expand(line, version, out):
    s = line.replace("%V%", version).replace("%OUT%", str(out)).replace("%R%", str(ROOT))
    if "%" in s:
        raise SystemExit("build_doto: unexpanded variable in the cl line: " + s)
    return [t.replace("\\", "/") for t in s.split()[1:]]   # drop the "cl" word


STALE_SOURCES = {"build/vsh/afw_warp.obj": ["third_party/vsh_afw/core/gfx/afw_warp.cpp", "third_party/vsh_afw/core/gfx/afw_warp.h"]}


def ensure_objs():
    def stale(o):   # an object whose own source changed (the AFW library is edited here; MinHook/ImGui are vendored as-is)
        obj = ROOT / o
        return obj.exists() and any((ROOT / src).stat().st_mtime > obj.stat().st_mtime for src in STALE_SOURCES.get(o, []))
    missing = [o for o in NEEDED_OBJS if not (ROOT / o).exists() or stale(o)]
    if not missing:
        return
    print("build_doto: building MinHook/ImGui/AFW objects (missing: %s)" % ", ".join(missing), flush=True)
    subprocess.run([sys.executable, str(HERE / "run_build_cmd.py"), "--only", r"^cc (buffer|imgui|afw_warp)"], cwd=ROOT, check=True)
    still = [o for o in NEEDED_OBJS if not (ROOT / o).exists()]
    if still:
        raise SystemExit("build_doto: objects still missing: " + ", ".join(still))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version", help="DLL version number, e.g. 145752")
    ap.add_argument("--python", default=default_python(), help="interpreter for tools/doto (pefile, capstone, numpy)")
    ap.add_argument("--skip-port", action="store_true", help="compile the existing generated tree")
    ap.add_argument("--game", choices=("doto", "d2"), default="doto", help="d2: the Dishonored 2 DLL from src/ (no port)")
    opts = ap.parse_args()
    if not re.fullmatch(r"\d+", opts.version):
        raise SystemExit("build_doto: version must be digits")
    if opts.game == "d2":
        src, out = ROOT, ROOT / "build" / "d2"
        out.mkdir(parents=True, exist_ok=True)
        ensure_objs()
        argv, degraded = harness.cl_to_clang(d2_line(opts.version, out))
    else:
        if not opts.skip_port:
            subprocess.run([opts.python, str(ROOT / "tools/doto/port.py")], cwd=ROOT, check=True)
        src, out = folders(opts.python)
        out.mkdir(parents=True, exist_ok=True)
        ensure_objs()
        argv, degraded = harness.cl_to_clang(expand(cl_line(), opts.version, out))
    # clang-cl rejects a single /Fo file once .obj inputs count as several inputs: a directory /Fo writes dxgi_probe.obj
    argv = [("/Fo:" + t[4:].rsplit("/", 1)[0] + "/") if t.startswith("/Fo:") and t.endswith(".obj") else t for t in argv]
    if degraded:
        raise SystemExit("build_doto: FidelityFX libraries missing under third_party/fidelityfx/libs (gitignored); "
                         "the DLL would not run")
    log = out / f"build_v{opts.version}.log"
    with open(log, "wb") as f:
        rc = subprocess.run(argv, cwd=src, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT).returncode
    text = log.read_text(encoding="utf-8", errors="replace")
    bad = harness.tracked_warning_problems(text)
    dll = out / f"d2vr_render_probe_arm_v{opts.version}.dll"
    if bad:   # build-doto.cmd compiles without /WX (the D2 candidate step is the /WX gate): report, do not fail
        print("build_doto: warnings outside the harness allowlist:\n" + "\n".join(bad))
    print(f"dll_exit={rc}  log: {log}")
    if rc == 0:
        print(f"built {dll} ({dll.stat().st_size} bytes)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
