# VR v965 hotfix (2026-11-03)

v964 plus the fix for the VR start failure introduced by the v963 SMAA reorder.

## What broke

The v963 change moved sharpening **before** SMAA but left the per-eye working texture at `nullptr`. The new first step
was therefore `sharpen_->apply(nullptr, sharpness)`, which throws `CAS input is missing` on every eye, every frame.
The XR Present never got a finished eye image, so `xr-recoveries.log` showed repeated
`empty-frame ... reason=continuity_unusable` and `CAS input is missing`, `game_layers_submitted` stayed 0, and the
session never reached gameplay; the repeated failure ended the game/driver.

Evidence from the last session (`%LOCALAPPDATA%\DishonoredVR\artifacts`):

- `xr-recoveries.log`: `CAS input is missing` on each empty-frame retry.
- `xr-live-status.json`: `game_layers_submitted: 0`, `tracked_render_pair: false`, `logic_mask: 64`,
  `session_state: 2/4`.
- `linux-proxy.log`: probe started the correct v964 DLL.

## Fix

`src/game_xr.hpp`: the per-eye `processed` texture starts as `sources[eye]` again; sharpening then SMAA then the copy
run on a real eye image. This is a one-line initialization fix; the v963/v964 SMAA order, the selectable SMAA mode,
the controls, sword charge and weapon fixes are unchanged.

## Build

- `build/d2vr_render_probe_arm_v965.dll`, SHA-256
  `21329e64c650e1885892cea484d9b71243d77f62df9df6b40d723d7a08475aa4` (14,066,176 bytes).
- `%LOCALAPPDATA%\DishonoredVR\build` contains only v965; a direct Steam launch uses it.
- The launcher is `DishonoredVRLauncher-v965.exe` in Downloads, embedding v965.

## Verification

- DLL builds; the SMAA/settings/controls labs still pass.
- No headset run here: the headset-free harness reported `Find headset: -35` (OpenXR runtime unavailable), so VR
  presentation could not be exercised. The failure signature above is exact and the null input is removed.
