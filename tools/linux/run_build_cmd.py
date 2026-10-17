#!/usr/bin/env python3
"""Interpret tools/build-msvc.cmd on Linux (clang-cl + xwin + wine).

Verifies the Windows build and checks from Linux. It is not a replacement for a
real Windows (cl.exe) run before release. See tools/linux/README.md.

Run tools/linux/setup-toolchain.sh first.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CMD_FILE = ROOT / "tools" / "build-msvc.cmd"
TOOLCHAIN = Path(os.environ.get("D2VR_TOOLCHAIN", Path.home() / ".local/share/d2vr-toolchain"))
LLVM_BIN = TOOLCHAIN / "llvm" / "bin"
XWIN = TOOLCHAIN / "xwin"
WINEPREFIX = TOOLCHAIN / "wineprefix"
WORK = ROOT / "build" / "linux-harness"       # logs, cl shim, compat copy
COMPAT = WORK / "compat"

# clang-cl-only flags. Each silences a diagnostic that clang-cl emits at
# /W4 /WX but cl does not (cl is clean on the same sources), or restores cl
# semantics. Keep in sync with the README.
CLANG_ONLY_FLAGS = [
    "-ferror-limit=0",                    # see every error (compat gate needs them all)
    "-fno-crash-diagnostics",             # do not litter /tmp with reproducers
    "-Wno-unused-command-line-argument",  # e.g. /Fd: combined with /Zi /LD
    "-Wno-missing-braces",                # RUNTIME_FUNCTION{...} brace elision
    "-Wno-missing-field-initializers",    # partial aggregate initialisers
    "-Wno-microsoft-cast",                # function<->object pointer cast, accepted by cl
    "/clang:-ffp-contract=off",           # cl does not contract; clang folds a*b-c as fused
]

# clang diagnostics that cl ALSO raises at /W4 /WX (C4100, C4189/C4101, C4505,
# C4018/C4389, C4715) are NOT muted. They are downgraded from errors to warnings
# only so that tracked_warning_problems() can compare them against the exact
# allowlist below; anything not on the list fails the step. A new unused
# variable / sign-compare / missing return in src/ therefore FAILs here.
TRACKED_GROUPS = ("unused-parameter", "unused-function", "unused-variable", "unused-const-variable",
                  "unused-but-set-variable", "unused-but-set-global", "sign-compare", "return-type")
CLANG_ONLY_FLAGS += ["-Wno-error=" + g for g in TRACKED_GROUPS]
# (file regex, message regex): warnings clang raises today that cl does not
# (statics/globals written but never read, SEH function ending, constant
# comparisons) in exactly these places.
TRACKED_WARNING_ALLOW = [
    (r"src/dxgi_probe\.cpp", r"unused variable '(wheel_cursor|wheel_dial)'"),
    (r"src/dxgi_probe\.cpp", r"variable 'world_ui_installed' set but not used"),
    (r"tests/full_body_contact_arm_check\.cpp", r"unused function 'rigid'"),
    (r"tests/glyph_swap_async_check\.cpp", r"variable 'creates' set but not used"),
    (r"tests/hand_contact_check\.cpp", r"variable 'stop' set but not used"),
    (r"tests/native_choke_distance_scope_check\.cpp", r"variable '(disables|enables|entry_calls)' set but not used"),
    (r"tests/native_door_empty_gate_check\.cpp", r"non-void function does not return a value"),
    (r"tests/native_door_request_check\.cpp", r"comparison of integers of different signs: 'const std::uint64_t'"),
    (r"tests/native_hud_census_check\.cpp", r"unused variable 'second_member'"),
    (r"tests/native_interaction_stop_check\.cpp", r"variable 'native_calls' set but not used"),
    (r"tests/native_kill_range_scope_check\.cpp", r"variable 'reads' set but not used"),
    (r"tests/native_manual_equip_hooks_check\.cpp", r"variable 'after_calls' set but not used"),
    (r"tests/native_slash_speed_check\.cpp", r"variable '(primary_calls|secondary_calls)' set but not used"),
]
_TRACKED_RE = re.compile(r"^(\S+?)\(\d+,\d+\): warning: (.*) \[-W(%s)\]" % "|".join(TRACKED_GROUPS), re.M)


def tracked_warning_problems(text):
    """Tracked-group warnings in compiler output that are not on the allowlist."""
    bad = []
    for m in _TRACKED_RE.finditer(text):
        path = m.group(1).replace("tests/../", "")
        if not any(re.fullmatch(f, path) and re.search(msg, m.group(2)) for f, msg in TRACKED_WARNING_ALLOW):
            bad.append(m.group(0))
    return sorted(set(bad))


# Steps that cannot work on Linux. Matched with re.search against the step's
# command line (as written in build-msvc.cmd, with / separators) or its name.
# scope "run": still compile/link, skip only execution. scope "all": skip the
# whole step. Every skip is printed with its reason and counted; nothing passes
# silently.
SKIPS = [
    # (regex, scope, reason)
    (r"vcvars64\.bat", "all",
     "needs Visual Studio vcvars; the clang-cl + xwin environment replaces it"),
    # -- missing local-only inputs (gitignored game data / RE artifacts)
    (r"tests/body_ik_check\.cpp", "all",
     "needs tests/player_clip_fixture.hpp (decoded game data, gitignored, absent)"),
    (r"tests/(native_interaction_range_abi|native_weapon_attachment_abi|native_weapon_source_frame)_check\.py", "all",
     "needs artifacts/re-toolchain/.../Dishonored2-steam17576867.exe (the game binary)"),
    (r"tests/native_menu_patch_check\.py", "all",
     "needs the game's menu movies (tools/gen_menu_patches.py reads local game art)"),
    (r"native_art_movie_patch_check", "run",
     "needs the game's menu movie files (the test asserts its input file opens)"),
    # -- Windows-only tooling used inside the checks
    (r"tests/(render_perf_callback|render_perf_maintenance|repair_private_menu)_check\.py", "all",
     "uses subprocess.CREATE_NO_WINDOW / vcvars (Windows-only Python API)"),
    (r"tests/live_session_tools_check\.py", "all",
     "imports tools/sample_threads.py, which needs ctypes.WinDLL (Windows only)"),
    (r"tests/render_perf_readiness_check\.py", "all",
     "invokes cl /analyze (MSVC static analysis; clang-cl has no equivalent)"),
    (r"tests/full_hand_launch_check\.py", "all", "needs PowerShell to run the real launcher"),
    (r"tests/build_exit_status_check\.py", "all", "needs a real Windows cmd.exe"),
    (r"tools/embed_smaa\.py", "all",
     "rewrites the tracked third_party/smaa/shader_source.hpp (CRLF on Windows, LF here); "
     "the committed file is used as is"),
    # -- Wine runtime limitations
    (r"tests/(sword_combat_routing|native_sword_contact_routing)_check\.py", "all",
     "the check compiles with cl and then executes the .exe directly (no Windows loader "
     "on Linux; the harness only runs executables through wine)"),
    (r"(native_art_bounds|native_hud_fallback|eye_sharpen)_check", "run",
     "Wine d3d11 (wined3d) has no usable GPU/GL device here: shader compile or "
     "GPU-computed pixels differ (needs a real or DXVK device)"),
    (r"fsr4_eye_upscaler_check", "run",
     "needs the AMD-signed FSR4 provider DLL and Authenticode verification (absent / "
     "unsupported under Wine)"),
    (r"native_(choke_distance_scope|slash_speed|manual_equip_hooks|offered_choke)_check", "run",
     "Wine: needs VirtualAlloc below the trampoline within 2 GB plus RtlAddFunctionTable; "
     "Wine's low address space is fully occupied, so the allocation fails with "
     "ERROR_INVALID_ADDRESS"),
]

# cl catches hardware faults (bad reads) in __try/__except under plain /EHsc; clang
# only does so with async exceptions (/EHa), which also lets catch(...) swallow SEH
# exceptions, unlike cl. So /EHsc stays the default and these checks, which
# deliberately fault inside __try, are built with /EHa.
ASYNC_EH_CHECKS = re.compile(r"tests/(native_initial_contact|native_regular_contact|native_hud_census)_check\.cpp")

# Prebuilt FidelityFX import libraries are gitignored (third_party/**/*.lib) and
# are not in the repo. When they are missing the link drops them and uses
# /force:unresolved, but only if every unresolved symbol is an ffx* symbol.
# Such outputs are linked but never executed.
# Checks that print a skip notice and still exit 0 must not count as passes.
SELF_SKIP = re.compile(r"(?im)^[^\n]{0,60}\bskipped\s*$|^no d3d11 device\b.*$")

FFX_LIB_RE = re.compile(r"third_party/fidelityfx/libs/\S+\.lib$")

# clang-cl failures that cl does not have; see clang_compat.py. A compile that
# fails with one of these is retried on a rewritten copy of the sources.
COMPAT_SIGNATURES = re.compile(
    r"default member initializer for '\w+' needed within definition of enclosing class|"
    r"GenerateSEHFinallyFunction|'alignas' attribute cannot be applied to types")


class Step:
    def __init__(self, kind, line, argv, env, name):
        self.kind = kind        # cl | run | python | mkdir | vcvars
        self.line = line        # original (expanded) command text, / separators
        self.argv = argv        # tokens after the command word
        self.env = env          # snapshot of cmd 'set' variables
        self.name = name
        self.group = None       # compile+run group id
        self.exe = None         # run: exe path; cl: output exe/dll
        self.objs_in = []       # build/*.obj consumed
        self.objs_out = []      # build/*.obj produced


def to_posix(tok):
    return tok.replace("\\", "/")


def expand(text, env, loopvars, args):
    for k, v in loopvars.items():
        text = text.replace("%%" + k, v)
    text = re.sub(r"%~(\d)", lambda m: args[int(m.group(1)) - 1] if int(m.group(1)) <= len(args) else "", text)
    text = re.sub(r"%([A-Za-z_][A-Za-z0-9_]*)%", lambda m: env.get(m.group(1), ""), text)
    return text


def parse(lines, args):
    """Turn the .cmd text into a flat list of Steps (loops expanded)."""
    steps, env = [], {}
    out_lines = []  # (text, loopvars)
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        i += 1
        m = re.match(r"for\s+%%(\w)\s+in\s+\((.*)\)\s+do\s+\($", s)
        if m:
            var, items = m.group(1), m.group(2).split()
            body = []
            while lines[i].strip() != ")":
                body.append(lines[i].strip())
                i += 1
            i += 1
            for item in items:
                out_lines.extend((b, {var: item}) for b in body)
            continue
        out_lines.append((s, {}))

    for s, loopvars in out_lines:
        if not s or s.startswith("@echo") or s.startswith("::") or s.startswith("rem "):
            continue
        if s == ":build_failed":
            break
        if not s.startswith("if "):
            s = expand(s, env, loopvars, args)
        m = re.match(r'if (not )?"(.*?)"=="(.*?)"\s+(.*)$', s)
        if m:
            neg, a, b, rest = m.groups()
            if a == "%errorlevel%":
                continue  # per-step status is checked by the harness
            a, b = expand(a, env, loopvars, args), expand(b, env, loopvars, args)
            if (a == b) == (not neg):
                s = expand(rest, env, loopvars, args)
            else:
                continue
        if re.match(r"if (not )?errorlevel", s) or s.startswith("exit /b") or s.startswith("goto "):
            continue
        m = re.match(r"if not exist (\S+) mkdir (\S+)$", s)
        if m:
            steps.append(Step("mkdir", s, [to_posix(m.group(2))], dict(env), "mkdir " + to_posix(m.group(2))))
            continue
        m = re.match(r"set (\w+)=(.*)$", s)
        if m:
            env[m.group(1)] = m.group(2)
            continue
        tok = s.split()
        word = tok[0].lower()
        if word == "call":
            steps.append(Step("vcvars", to_posix(s), tok[1:], dict(env), "vcvars64"))
        elif word == "cl":
            steps.append(make_cl(s, tok[1:], env))
        elif word == "python":
            a = [to_posix(t) for t in tok[1:]]
            steps.append(Step("python", " ".join(["python"] + a), a, dict(env), "py " + " ".join(a)))
        elif word.startswith("build\\"):
            exe = to_posix(tok[0])
            st = Step("run", to_posix(s), [to_posix(t) for t in tok[1:]], dict(env),
                      "run " + Path(exe).stem + "".join(" " + t for t in tok[1:]))
            st.exe = exe
            steps.append(st)
        else:
            raise SystemExit("run_build_cmd: unhandled build-msvc.cmd line: " + s)
    assign_groups(steps)
    return steps


def make_cl(line, toks, env):
    argv = [to_posix(t) for t in toks]
    st = Step("cl", to_posix(line), argv, dict(env), "")
    inputs = [t for t in argv if not t.startswith("/") and not t.endswith(".lib")]
    fo = next((t[4:] for t in argv if t.startswith("/Fo:")), None)
    fe = next((t[4:] for t in argv if t.startswith("/Fe:")), None)
    st.objs_in = [t for t in inputs if t.endswith(".obj")]
    if "/c" in argv:
        st.objs_out = [fo + Path(t).stem + ".obj" for t in inputs
                       if t.endswith((".c", ".cpp")) and fo and fo.endswith("/")]
        st.name = "cc " + " ".join(Path(t).stem for t in inputs)
    else:
        st.exe = fe
        st.name = "build " + (Path(fe).name if fe else Path(inputs[0]).stem)
    return st


def assign_groups(steps):
    last_build, gid = {}, 0
    for st in steps:
        if st.kind == "run":
            st.group = last_build.get(st.exe)
            continue
        gid += 1
        st.group = gid
        if st.kind == "cl" and st.exe:
            last_build[st.exe] = gid


def cl_to_clang(argv_in, async_eh=False):
    """Translate cl arguments (already / separated) into a clang-cl argv.

    Returns (argv, degraded) where degraded means missing FidelityFX libs were
    dropped from the link line.
    """
    out = [str(LLVM_BIN / "clang-cl"), "--target=x86_64-pc-windows-msvc", "-fuse-ld=lld"]
    for d in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared",
              "sdk/include/winrt", "sdk/include/cppwinrt"):
        out += ["/imsvc", str(XWIN / d)]
    link, in_link, degraded = [], False, False
    for t in argv_in:
        if t == "/link":
            in_link = True
        elif in_link:
            if FFX_LIB_RE.search(t) and not (ROOT / t).exists():
                degraded = True
            else:
                link.append(t)
        elif t == "/EHsc" and async_eh:
            out.append("/EHa")
        else:
            out.append(t)
    if degraded:
        link.append("/force:unresolved")
    out += CLANG_ONLY_FLAGS + os.environ.get("D2VR_CLANG_EXTRA_FLAGS", "").split()
    out += ["/link"] + link + ["/libpath:" + str(XWIN / d)
                               for d in ("crt/lib/x86_64", "sdk/lib/um/x86_64", "sdk/lib/ucrt/x86_64")]
    return out, degraded


def skip_reason(st, scope):
    for rx, sc, why in SKIPS:
        if sc == scope and (re.search(rx, st.line) or re.search(rx, st.name)):
            return why
    return None


def wine_env():
    env = dict(os.environ)
    env.update({"WINEPREFIX": str(WINEPREFIX), "WINEDEBUG": "-all",
                "WINEDLLOVERRIDES": "mscoree,mshtml="})
    return env


def run_proc(argv, env, log, timeout, cwd=ROOT):
    # Output goes to a file, not a pipe: wine daemons spawned by the first run
    # inherit pipe descriptors and would keep a pipe open forever.
    with open(log, "wb") as f:
        try:
            rc = subprocess.run(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=f,
                                stderr=subprocess.STDOUT, timeout=timeout).returncode
            note = ""
        except subprocess.TimeoutExpired:
            rc, note = 124, "\n[timeout after %ss]\n" % timeout
        f.write(note.encode())
    return rc, log.read_bytes().decode("utf-8", "replace")


# Every error of the original compile must be one of the known clang-only
# signatures, or a cascade they cause, before the compat retry may stand in for it.
COMPAT_ERROR_OK = re.compile(
    COMPAT_SIGNATURES.pattern + r"|no viable overloaded '='")  # cascade of the NSDMI error (invalid nested struct)


def unexplained_errors(text):
    """Error lines of a failed compile that are not explained by the clang-only limitations."""
    lines = [l for l in text.splitlines() if re.search(r"\b(fatal )?error: ", l)]
    if "GenerateSEHFinallyFunction" in text:
        lines = [l for l in lines if "unable to execute command" not in l and "frontend command failed" not in l]
    return [l for l in lines if not COMPAT_ERROR_OK.search(l)]


def build_compat_tree():
    """Rewritten copy of src/tests/tools C++ sources for the compat retry."""
    sys.path.insert(0, str(HERE))
    import clang_compat
    if COMPAT.exists():
        shutil.rmtree(COMPAT)
    for top in ("src", "tests", "tools"):
        for f in sorted((ROOT / top).rglob("*")):
            if f.suffix not in (".cpp", ".hpp", ".h", ".c") or not f.is_file():
                continue
            dst = COMPAT / f.relative_to(ROOT)
            dst.parent.mkdir(parents=True, exist_ok=True)
            text = f.read_bytes().decode("utf-8", "surrogateescape")
            dst.write_bytes(clang_compat.transform(text)[0].encode("utf-8", "surrogateescape"))
    for name in ("third_party", "build"):
        link = COMPAT / name
        if not link.exists():
            link.symlink_to(ROOT / name)


def select_steps(steps, only):
    selected = set(range(len(steps)))
    if only:
        rx = re.compile(only)
        groups = {st.group for st in steps
                  if st.kind in ("cl", "run", "python") and (rx.search(st.line) or rx.search(st.name))}
        selected = {i for i, st in enumerate(steps) if st.kind == "mkdir" or st.group in groups}
        changed = True
        while changed:        # pull in producers of consumed objects (e.g. the minhook compile)
            changed = False
            need = {o for i in selected for o in steps[i].objs_in}
            for i, st in enumerate(steps):
                if i not in selected and need & set(st.objs_out):
                    selected.add(i)
                    changed = True
    return selected


def prepare_wine():
    WINEPREFIX.mkdir(parents=True, exist_ok=True)
    quiet = dict(env=wine_env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not (WINEPREFIX / "system.reg").exists():
        print("Initialising wine prefix ...", flush=True)
        subprocess.run(["wineboot", "-u"], timeout=600, **quiet)
    marker = WINEPREFIX / ".d2vr-no-debugger"
    if not marker.exists():
        # A crashing test must exit, not wait in winedbg.
        for name, val in (("Debugger", "false"), ("Auto", "1")):
            subprocess.run(["wine", "reg", "add", r"HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug",
                            "/v", name, "/t", "REG_SZ", "/d", val, "/f"], timeout=120, **quiet)
        marker.touch()
    subprocess.run(["wineserver", "-p"], **quiet)   # persistent server: fast test start-up


def install_cl_shim():
    """`cl` on PATH for python checks that shell out to the compiler."""
    shim_dir = WORK / "bin"
    shim_dir.mkdir(exist_ok=True)
    shim = shim_dir / "cl"
    if shim.is_symlink() or shim.exists():
        shim.unlink()
    shim.symlink_to(HERE / "cl")


def main():
    try:
        return run()
    finally:
        if shutil.which("wineserver") and WINEPREFIX.exists():
            subprocess.run(["wineserver", "-k"], env=wine_env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode_args", nargs="*", help="arguments for build-msvc.cmd (%%1 %%2), e.g. candidate")
    ap.add_argument("--only", metavar="REGEX", help="only steps whose command line matches (a matched compile+run pair stays together)")
    ap.add_argument("--list", action="store_true", help="list the steps and exit")
    ap.add_argument("--keep-going", action="store_true", help="do not stop at the first failure")
    ap.add_argument("--dry-run", action="store_true", help="print the translated commands without running them")
    ap.add_argument("--no-compat", action="store_true", help="do not retry clang-only compile failures on the rewritten copy")
    ap.add_argument("--timeout", type=int, default=300, help="per-step timeout in seconds")
    ap.add_argument("--strict", action="store_true", help="exit 3 if anything was compile-only or skipped")
    ap.add_argument("-v", "--verbose", action="store_true", help="show output of every step")
    opts = ap.parse_args()

    steps = parse(CMD_FILE.read_text(encoding="utf-8", errors="replace").splitlines(), opts.mode_args)
    selected = select_steps(steps, opts.only)

    if opts.list:
        for i, st in enumerate(steps):
            if st.kind == "mkdir":
                continue
            why = skip_reason(st, "all") or (skip_reason(st, "run") if st.kind == "run" else None)
            print("%s %-62s %s" % ("*" if i in selected else " ", st.name, "SKIP: " + why if why else ""))
        return 0

    WORK.mkdir(parents=True, exist_ok=True)
    if not opts.dry_run:
        if not (LLVM_BIN / "clang-cl").exists() or not (XWIN / "crt").exists():
            print("Toolchain missing: run tools/linux/setup-toolchain.sh (D2VR_TOOLCHAIN=%s)" % TOOLCHAIN, file=sys.stderr)
            return 2
        prepare_wine()
        install_cl_shim()

    passed, compat_passed, failed, skipped, blocked = [], [], [], [], []
    exe_state = {}          # output exe -> (kind, reason), consulted by its run steps
    compat_ready = False
    n = 0
    t0 = time.time()
    for i, st in enumerate(steps):
        if i not in selected:
            continue
        if st.kind == "mkdir":
            if not opts.dry_run:
                (ROOT / st.argv[0]).mkdir(parents=True, exist_ok=True)
            continue
        n += 1
        label = "%03d %s" % (n, st.name)
        why = skip_reason(st, "all")
        if st.kind == "run":
            state = exe_state.get(st.exe)
            if state and state[0] == "failed":
                blocked.append(st.name)
                print("BLOCKED %-60s build step failed" % st.name)
                continue
            why = why or (state[1] if state else None) or skip_reason(st, "run")
        if why:
            skipped.append((st.name, why))
            if st.kind == "cl" and st.exe:
                exe_state[st.exe] = ("skipped", "build step skipped: " + why)
            print("SKIPPED %-60s %s" % (st.name, why))
            continue

        degraded = False
        if st.kind == "cl":
            argv, degraded = cl_to_clang(st.argv, bool(ASYNC_EH_CHECKS.search(st.line)))
            if st.objs_in:
                # clang-cl rejects a single /Fo file name once .obj inputs count
                # as several inputs; the object stem equals the source stem, so a
                # directory-style /Fo produces the same file.
                argv = [("/Fo:" + t[4:].rsplit("/", 1)[0] + "/") if t.startswith("/Fo:") and t.endswith(".obj") else t
                        for t in argv]
            env = dict(os.environ, **st.env)
        elif st.kind == "run":
            argv, env = ["wine", st.exe] + st.argv, wine_env()
        else:
            argv = ["python3", "-u"] + st.argv
            env = dict(os.environ, **st.env)
            env["PATH"] = str(WORK / "bin") + os.pathsep + env.get("PATH", "")
            env["PYTHONDONTWRITEBYTECODE"] = "1"
        if opts.dry_run:
            print(" ".join(argv))
            continue

        log = WORK / (re.sub(r"[^A-Za-z0-9_.-]+", "_", label)[:150] + ".log")
        ts = time.time()
        rc, text = run_proc(argv, env, log, opts.timeout)
        compat_note = False
        if st.kind == "cl" and rc != 0 and not opts.no_compat and COMPAT_SIGNATURES.search(text):
            if not compat_ready:
                print("        (building rewritten source copy for clang-only failures: tools/linux/clang_compat.py)", flush=True)
                build_compat_tree()
                compat_ready = True
            unmatched = unexplained_errors(text)
            if unmatched:
                text += "\n[harness] not retried: errors beyond the known clang-only signatures:\n" + "\n".join(unmatched) + "\n"
            else:
                rc2, text2 = run_proc(argv, env, log.with_suffix(".compat.log"), opts.timeout, cwd=COMPAT)
                if rc2 == 0:
                    rc, text, compat_note = 0, text2, True
                else:
                    text += "\n[harness] compat retry also failed:\n" + text2
        dt = time.time() - ts

        note = ""
        if st.kind == "cl" and rc == 0:
            problems = tracked_warning_problems(text)
            if problems:
                rc, text = 1, text + "\n[harness] warnings cl also raises at /W4 /WX (not on the allowlist):\n" + "\n".join(problems) + "\n"
        if st.kind in ("run", "python") and rc == 0 and SELF_SKIP.search(text):
            skipped.append((st.name, "check skipped itself at run time: " + SELF_SKIP.search(text).group(0).strip()))
            print("SKIPPED %-60s %s" % (st.name, skipped[-1][1]))
            continue
        if st.kind == "cl" and rc == 0 and degraded:
            unresolved = set(re.findall(r"undefined symbol: (\S+)", text))
            bad = sorted(u for u in unresolved if "ffx" not in u.lower())
            if bad:
                rc, text = 1, text + "\n[harness] unresolved non-FidelityFX symbols: %s\n" % bad[:10]
            else:
                exe_state[st.exe] = ("degraded", "linked without the prebuilt FidelityFX libs (absent from the repo); cannot run")
                note = "link degraded: FidelityFX libs absent, %d ffx symbols unresolved" % len(unresolved)
        if rc == 0 and compat_note and st.exe:
            exe_state[st.exe] = ("compat", "built from the rewritten compat copy (clang_compat.py); not executed")

        if rc == 0:
            (compat_passed if compat_note else passed).append(st.name)
            print("%-7s %-60s %5.1fs%s%s" % ("PASS*" if compat_note else "PASS", st.name, dt,
                                             "  [compat copy]" if compat_note else "",
                                             "  [" + note + "]" if note else ""), flush=True)
            if opts.verbose and text.strip():
                print(text.rstrip())
        else:
            failed.append(st.name)
            if st.kind == "cl" and st.exe:
                exe_state[st.exe] = ("failed", "")
            print("FAIL    %-60s rc=%d  log: %s" % (st.name, rc, log.relative_to(ROOT)), flush=True)
            print("\n".join("        " + l for l in text.rstrip().splitlines()[-25:]))
            if not opts.keep_going:
                break

    if opts.dry_run:
        return 0
    print("\n==== summary: %d passed / %d compile-only / %d failed / %d skipped%s (%.0fs) ====" % (
        len(passed), len(compat_passed), len(failed), len(skipped),
        " / %d blocked" % len(blocked) if blocked else "", time.time() - t0))
    for name, why in skipped:
        print("  SKIPPED %s -- %s" % (name, why))
    for name in compat_passed:
        print("  PASS*   %s -- compiled from the rewritten copy only" % name)
    for name in blocked:
        print("  BLOCKED %s" % name)
    for name in failed:
        print("  FAILED  %s" % name)
    if n == 0:
        print("no steps selected", file=sys.stderr)
        return 1
    if failed or blocked:
        return 1
    return 3 if opts.strict and (compat_passed or skipped) else 0


if __name__ == "__main__":
    sys.exit(main())
