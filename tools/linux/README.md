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
    python3 tools/linux/run_build_cmd.py --list | --dry-run | -v | --timeout N
Exit 0 only if nothing FAILED/BLOCKED; `--strict` also exits 3 if anything is skipped.
Checks that print a skip notice and exit 0 are reported SKIPPED. Logs: `build/linux-harness/*.log`. Output: `build/`.
Final summary: passed / failed / skipped; every SKIPPED line has a reason (`SKIPS` in the script).
Without `--keep-going` it stops at the first failure like `goto build_failed`.

## Known differences from cl
- clang-cl-only flags (`CLANG_ONLY_FLAGS`): `-Wno-` for missing-braces, missing-field-initializers,
  microsoft-cast; `-Wshadow -Wshadow-field -Wshadow-field-in-constructor` (added: cl /W4 raises C4456-C4459, clang-cl /W4
  does not, so under /WX they are errors here too; `-Wshadow-uncaptured-local` stays off); `/clang:-ffp-contract=off` (clang folds `a*b-c` as fused). `/EHsc` is kept, except
  `/EHa` for three checks that fault inside `__try` (clang needs it; cl does not).
- Warnings cl also raises (unused-*, sign-compare, return-type) are NOT muted: any not on the exact
  `TRACKED_WARNING_ALLOW` list (13 entries: unused/set-but-unused globals in dxgi_probe.cpp and 10
  tests, one SEH function ending, one signed compare in native_door_request_check) FAILs the step.
- The real sources compile unmodified. Keep them that way: clang-cl rejects three constructs cl
  accepts. (1) A nested struct's default member initializers needed before the enclosing class is
  complete: an in-class static member initializer (`inline static std::array<Nested,N> x_{};`) or a
  default argument (`f(Nested n={})`). Define such a static after the class
  (`inline std::array<C::Nested,N> C::x_{};`), or hoist the struct to namespace scope behind a
  `using Nested=...;` alias. (2) `static alignas(N) T x;`: write `alignas(N) static T x;`.
  (3) A `__try` nested inside a `__finally`, which crashes clang's SEH codegen: move the inner
  `__try/__finally` into its own function.
- Missing prebuilt FidelityFX `.lib`s (gitignored): the link drops them with `/force:unresolved`
  (only `ffx*` symbols allowed); such outputs are not runnable.
- Checks needing a D3D11 GPU run under Wine's wined3d; those that need real GPU output or D3D11
  bind-flag validation, and anything needing the game, game data or art, PowerShell/cmd.exe or
  Windows-only Python APIs, are SKIPPED.
- Wine's low address space is full, so trampoline/unwind-table checks that `VirtualAlloc` below
  the trampoline (directly or through `NativeInteractionAim::install`) are SKIPPED.
- `cl` for python checks is a shim (`tools/linux/cl`) onto clang-cl.
