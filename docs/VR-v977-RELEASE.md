# VR v977 (2026-11-03)

## The game's dynamic resolution was the remaining no-op

The engine has its own dynamic resolution (`rs_enable`, cvar 3) and a forced render fraction. In the latest session it
was on (`feedback_render.rs_enable = 1`, render 1922x2070 for a 2496x2688 display). If it re-enables itself in a
temporal-off mode, the game renders below the requested size and upscales; every resolution/SSAA increase then adds no
geometry samples, and the fraction moving frame to frame makes edges unstable.

v977 enforces, every 100 ms in any temporal-off mode (SMAA/SSAA/Off):

- `upscale_fraction = 1` and `apply_upscale(1)`;
- `rs_enable` (cvar 3) forced to 0 if anything set it back to 1;
- counter `dynamic_scale_forces` in `xr-live-status.json` shows how often it had to be forced.

This is the mode-independent "game implementation" part: without it the engine can silently render at 0.77x even in
SSAA.

## What to verify in the log

New fields in `xr-live-status.json`:

- `game_render_size` — the size requested from the game (3744x4032 at 150%).
- `game_source_size` — the eye texture the game actually handed over this Present.
- `xr_eye_size` — the runtime-clamped XR image (3120x3360 on Virtual Desktop High).
- `dynamic_scale_forces` — nonzero means the engine had re-enabled dynamic resolution and the mod forced it off.

If `game_source_size` equals `game_render_size` (3744x4032) and `xr_eye_size` is 3120x3360, SSAA 150 is real. If
`game_source_size` stays 3120x3360, the engine ignored the larger output request and the next step is not another AA
filter but forcing its render-target size; the log now tells us which.

## Build

- `build/d2vr_render_probe_arm_v977.dll`, SHA-256
  `156cfca3e0aad48a20520a1c7c85e7ac57044c2f58d591ce2c249493e63f9c14` (14,080,000 bytes).
- Launcher `DishonoredVRLauncher-v977.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check`, `render_size_check`, `eye_resolve_check` pass. No headset run.
