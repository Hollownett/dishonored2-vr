# VR v1027 (test candidate, 2026-11-05)

A diagnostic cost the whole XR session. v1024's 19:34 startup run lost VR four seconds after the native-eye capture compared two differently
sized eye images, and the game then closed to the desktop with no message. v1027 stops a diagnostic from ever recovering a session.

## What the v1024 run actually did (evidence)

- `%LOCALAPPDATA%\DishonoredVR\artifacts\xr-recoveries.log`, 2026-11-05: the session started 19:34:36 (frame 1, `session_state=2`), reached
  gameplay at frame 2670 (19:35:11, `reason=no_eye_sources action=black_quad`), then logged the bare line
  `42814937 Native and processed eye dimensions differ` at 19:35:14.937, created a new HUD window surface at 19:35:18.036 and started a
  **new** session (frame 1).
- `%LOCALAPPDATA%\DishonoredVR\artifacts\native-exception-context.txt` (opened at install, 19:34:29) stayed **0 bytes**: no first-chance access
  violation, illegal instruction or in-page error anywhere in the process.
- No `Application Error`/`Application Hang`/WER report for `Dishonored2.exe` in the Windows event log or `ReportArchive` for that window
  (only the launcher's own 18:22/18:23 crashes, which are a separate .NET problem).
- `xr-live-status.json` at the end: `xr_eye_size [2668,2930]`, `game_render_size [2496,2688]`, `game_source_size [0,0]`, `empty_frames 1`,
  `left_empty 1`; Steam's `console_log.txt`: process 22132 added 19:34:26, removed 19:35:21.
- So: not a memory fault. The process ended cleanly right after the mod tore down and recreated its XR session.

## The cause

`capture_xr_transfer` (`src/dxgi_probe.cpp`) is the `native-eye-capture` readback: it copies the game's and the XR eye images, compares them
and throws when they differ (`"Native and processed eye dimensions differ"`, `"XR base transfer pixels differ before overlay composition"`).
It is installed as `game_xr->observe_transfer` and called from the render callback — and the render callback's
`catch(const std::exception&) { recover_xr_session(error.what()); }` treats **any** exception as a VR fault. A diagnostic mismatch therefore
ran the full recovery: `game_xr` retired, arms/grab/interaction state cleared, an XR session retry scheduled (1-10 s), a new session at a
new recommended eye size (2668x2930 against the game's 2496x2688) — and the game, whose swapchain and device the recovery had just been
through, never came back to gameplay and closed.

## The fix

- `capture_xr_transfer` is now `noexcept` and wraps the diagnostic body (`capture_xr_transfer_unsafe`, the old function, unchanged) in
  `try/catch`. A failure skips that capture, counts once in `xr_transfer_capture_skipped` and writes one recoverable-fault line
  (`Diagnostic eye capture skipped (not a session fault): ...`). It cannot reach `recover_xr_session`.
- `xr-live-status.json` gains `image_transfer_capture_skipped`, so a session that skipped captures says so.
- Everything else is untouched: the present/eye paths still throw where a real fault is, and `recover_xr_session` remains the only
  response to one.

## Checks

- `build\compile-v1027.cmd`: MSVC 2022 x64 `/W4`, dll_exit=0, no new warnings in the changed lines; 14,918,656 bytes, sha256
  `759e4d99f90b1f3b849b277d5b8bdf81baa63da63fcee4ec1da3f2b1c1d7d9bc`.
- `tests/eye_capture_routing_check.py` (new, added to `tools\build-msvc.cmd`): the wrapper is noexcept, contains both old throws,
  counts and notes once, the observer assignment and the recovery catch still exist, and the skip counter is in the status.
- `finger_sphere_check` still passes (the v1024 grip ball is unchanged in this build).
- Pre-existing failures, not from this change: `alternate_eye_routing_check.py` asserts the pre-v997 `frame_split.feed(...)` signature and
  `finger_touch_routing_check.py` a pre-v697 string; both fail at HEAD too. No full `tools\build-msvc.cmd` pass (the worktree still stops on
  pre-existing `/WX` warnings, as v1022-v1026).

## Limits

- **Not headset-tested.** The crash is explained from the artifacts above, not reproduced: nobody watched the game close.
- The build also carries the worktree's other in-flight changes (v1025/v1026 weapon-body work from a parallel session); they are compiled in
  and untested here, as v1023+ already did.
- If the game closes again with the same signature, `image_transfer_capture_skipped` should be non-zero and there should be **no** session
  recovery in `xr-recoveries.log`; if it still closes, the fault is not this one and the next step is a minidump-enabled run.

Checklist: launch, reach gameplay, grab a prop (the v1024 grip ball), open the pause/settings pages, and report whether
`xr-recoveries.log` shows a `xr-event` session restart after the start, plus `image_transfer_capture_skipped` and `grab_focus[i].sphere_mm`.
