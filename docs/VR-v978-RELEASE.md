# VR v978 (2026-11-03)

## What the 150% session actually ran

The v977 status at 15:10 shows:

- `resolution_multiplier_effective = 1.5`
- `game_render_size = [3744,4032]`
- `game_source_size = [3744,4032]`
- `xr_eye_size = [3744,4032]`
- `feedback_render.temporal_aa = 0`
- `edge_antialiasing_enabled = false`
- `smaa_eye_passes = 0`
- `rs_enable = 0`, `dynamic_scale_forces = 0`

So 150% really rendered 3744x4032 and the frame rate dropped for that reason. But that preset ran **pure SSAA with no
SMAA at all**. The comparison was **125% + SMAA (Edge AA on)** against **150% with no edge filter**: 1.44x more samples
(1.5²/1.25²) versus losing SMAA entirely, which is why they looked almost identical on thin edges.

## v978: 150% includes SMAA

- UI mode 11 is now **"SSAA 150% + SMAA (thin edges)"**: temporal 0, Edge AA **on**, resolution 150%.
- It renders 3744x4032, applies the one-pass 0.03 SMAA at that resolution, then submits 3744x4032.
- The 125% preset remains pure SSAA (no SMAA) for users who want the sharpest unfiltered image.
- The launcher and native settings materialize mode 11 as `temporal 0 + Edge AA on + resolution 1.5`; Off still
  restores 100%.

The latest log also confirms the v976/v977 fixes are active on this machine: the XR eye is 3744 (not clamped to 3120),
the game source is 3744, and the dynamic-resolution guard did not need to fire.

## Build

- `build/d2vr_render_probe_arm_v978.dll`, SHA-256
  `3dccb2bfab12f54d9ceb49a7e7dfc51280b7e196951fbdbdb55f5bdb480c7bd4` (14,080,000 bytes).
- Launcher `DishonoredVRLauncher-v978.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check`, `eye_antialiasing_check`, `eye_resolve_check` pass. No headset run.
