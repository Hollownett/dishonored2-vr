# Linux harness for the Windows MSVC build and checks

Builds and runs `tools/build-msvc.cmd` on Linux with clang-cl + xwin (Microsoft CRT/SDK) + wine,
so changes to the C++ DLL and its ~300 checks can be verified without Windows. It is **not** a
replacement for a real `tools\build.ps1` run with cl.exe before release.

## Setup (once, no sudo)
    tools/linux/setup-toolchain.sh        # LLVM 23.1.3 (sha256-pinned), xwin 0.10.0, wine prefix
Installs under `$D2VR_TOOLCHAIN` (default `~/.local/share/d2vr-toolchain`, ~14 GB; `xwin-cache/` can
be deleted afterwards). Running it means accepting Microsoft's CRT/SDK license (`xwin --accept-license`).

## Use
    python3 tools/linux/run_build_cmd.py --keep-going     # everything, report all failures
    python3 tools/linux/run_build_cmd.py --only 'door_owner|dxgi_probe\.obj'   # matching steps (+ their run step)
    python3 tools/linux/run_build_cmd.py --list | --dry-run | --no-compat | -v | --timeout N
Exit code 0 only if nothing FAILED or was BLOCKED. Logs: `build/linux-harness/*.log`. Output: `build/`.
Final summary: passed / failed / skipped; every SKIPPED line has a reason (`SKIPS` in the script).
Without `--keep-going` it stops at the first failure like `goto build_failed`.

## Known differences from cl
- clang-cl-only flags (`CLANG_ONLY_FLAGS`): `-Wno-` for missing-braces, missing-field-initializers,
  unused-parameter/function/variable/const-variable/but-set-*, sign-compare, microsoft-cast,
  `-Wno-error=return-type`, `/clang:-ffp-contract=off` (clang folds `a*b-c` as fused). `/EHsc` is
  kept, except `/EHa` for three checks that fault inside `__try` (clang needs it; cl does not).
- `PASS*` = compile/link only, from a rewritten copy of src/tests/tools (`clang_compat.py`) because
  clang-cl rejects code cl accepts: nested-struct default member initializers used in the enclosing
  class, `static alignas(N) T`, nested `__try/__finally` (clang segfaults). The rewrite is unsound
  for running, so those checks are never executed here; the real Windows run covers them.
- Missing prebuilt FidelityFX `.lib`s (gitignored): the link drops them with `/force:unresolved`
  (only `ffx*` symbols allowed); such outputs are not runnable.
- Checks needing a D3D11 GPU run under Wine's wined3d; the three that need real GPU output, and
  anything needing the game, game art, PowerShell/cmd.exe or Windows-only Python APIs, are SKIPPED.
- Wine's low address space is full, so trampoline/unwind-table checks that `VirtualAlloc` below
  the trampoline are SKIPPED. `cl` for python checks is a shim (`tools/linux/cl`) onto clang-cl.
