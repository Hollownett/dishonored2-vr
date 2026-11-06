# VR v1017 (2026-11-05)

## Crash right after a save loads (v1016)

v1016 exited as soon as gameplay began after loading a save. `artifacts/native-exception-context.txt`
(2026-11-05 15:10:26, pid 39536) records a first-chance access violation in the mod:

```
code=0xc0000005 module=d2vr_render_probe_arm_v1016.dll rva=0x85c3
rdx=0x64657463656a6572 ("rejected")  access_address=0xffffffffffffffff
```

`tools/symbolize_crash.py` resolves the chain to
`present_hook -> arm_flicker_json -> weapon_draw_json -> ObjectMotion::weapon_status_json -> ostream<</strlen`.

Cause: v1016 added `weapon_draw::Stage::unbound`, so `stages` became 10, but `weapon_status_json` still had a
9-entry `stage_names[]`. The list is indexed by stage and stage 9 (`placed_pass`) is only non-zero once a hand
actually places the held weapon - i.e. the first seconds of gameplay after a save load. `stage_names[9]` then read
the ASCII bytes of the adjacent `"rejected"` literal as a pointer and the status writer faulted. The same off-by-one
put `placed_pass` counts into the `weapon_eye0` counter.

## Fix

- `src/object_motion.hpp`: `stage_names` gained `"unbound"` and a `static_assert` ties its size to
  `weapon_draw::stages`; `Counter` gained `weapon_stage9` and a `static_assert` ties the stage counters to the same
  enum; `weapon_unbound` was added to the published counter names.
- `src/native_weapon_hands.hpp`: the constant `living_pull_enabled` guard is `if constexpr`, so the check build no
  longer stops on C4127 under `/WX` (behaviour unchanged; the pull stays compiled out).
- `tests/finger_sphere_check.cpp`: renamed a shadowed local (C4456 under `/WX`).

## Build and evidence

- `build/d2vr_render_probe_arm_v1017.dll`, 14,895,616 bytes, SHA-256
  `1c7bb5ff950e8ddf83b67372555aa1075f58fdbbabcc2374322556cd2c2aa71f`, built by `build/compile-v1017.cmd`
  (MSVC 2022 `/W4`, the same recipe as v1016).
- `object_motion_check` and `weapon_draw_place_check` PASS (both `/W4 /WX`); the compile itself proves the two
  arrays and the counter span match the enum.
- A headset-free simulated session (v1017) ran in gameplay and wrote well-formed
  `artifacts/object-motion.json -> weapon_status` (`skip:[{},{}]`, pass table populated). That session had no placed
  weapon draw (`placed:[0,0]`, `matched:0`), so the `placed_pass` branch itself has not yet run live.
- `tools/build-msvc.cmd` still does not complete on this worktree: it stops on pre-existing `/WX` warnings in
  `src/dxgi_probe.cpp` (C4459/C4456) and on `tests/finger_sphere_check.cpp`, and its DLL step passes
  `/FIsrc\dlss_eye_upscaler.hpp`, which MSVC resolves relative to `src\`. The per-version compile scripts use the
  absolute `/FI` path and are the working recipe.
- Installed as the only DLL in `%LOCALAPPDATA%\DishonoredVR\build`; the old v1016 file is kept in
  `%LOCALAPPDATA%\DishonoredVR\artifacts\installed-backup`. Launcher rebuilt with v1017 embedded
  (`launcher/dist/DishonoredVRLauncher-v1017.exe`). No headset run yet: the crash fix still needs the user's
  headset confirmation that a save loads and gameplay continues.
