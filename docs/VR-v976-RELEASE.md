# VR v976 (2026-11-03)

## Why 150% looked the same as 125%

The runtime's `maxImageRect` clamps the XR eye. Virtual Desktop High reports **3120x3360**, which is exactly **125%** of
the 2496x2688 recommended size. At 150% the old path:

- computed `scaled_eye_size(..., 1.5, max=3120x3360)` -> effective **1.25**, size 3120x3360;
- tied the XR swapchain to the game backbuffer (3744x4032), which exceeds that max;
- so the game either stayed at 3120 or the resize was refused. 150% never rendered more samples. The math lab now
  covers this exact pair: `scaled_eye_size(...,1.5,{3120,3360}).effective == 1.25`, while the unclamped game size is
  3744x4032.

## Fix: internal SSAA above the XR max

v976 separates the two sizes:

- **XR eye**: `scaled_output_size(...)`, still clamped to the runtime maximum (3120x3360 at Virtual Desktop High).
- **Game render**: `game_output_size(...)`, bounded only by D3D11 (3744x4032 at 150%).
- The game's output-size hook now requests the larger game size; `GameXr::resize` keeps the XR swapchain at the clamped
  size.
- `EyeResolve` (new fullscreen pass) box-filters the 3744x4032 game eye down to 3120x3360 before sharpening/SMAA/XR
  submission. Four bilinear taps, sRGB, no history.
- At 125% (game == XR) nothing changes. At 150% the game really renders 44% more pixels than 125% and resolves down.
- Status now prints `game_render_size` and `xr_eye_size` side by side; at 150% expect `[3744,4032]` vs `[3120,3360]`.

This also removes the 3744-wide XR swapchain that the runtime max refused, so the first-enable path no longer asks Virtual
Desktop for a size it cannot create.

## Build

- `build/d2vr_render_probe_arm_v976.dll`, SHA-256
  `83e2fc98523fd5630674d2f5cf2f404c4c917bf0c42fa758a90e6e06d6ac642a` (14,079,488 bytes).
- Launcher `DishonoredVRLauncher-v976.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check`, `eye_antialiasing_check`, `render_size_check` and the new
  `eye_resolve_check` pass. No headset run.

## Limits

125% -> 150% is 44% more samples, not a night-and-day change; thin subpixel frames still need more. The Resolution
slider now works above the XR max through the same internal path: **200%** is 4992x5376 game resolution resolved to
3120x3360, which is a real 4x-pixel SSAA but heavy on an RTX 3070 laptop. Use 150% first, then 200% only if frame time
allows.
