# VR v975 (2026-11-03)

## SSAA 150% for thin distant geometry

SSAA 125% was the best result so far, but 1.25 samples per axis still undersamples thin subpixel geometry such as window
frames at distance. v975 adds **Anti-aliasing -> "SSAA 150% (thin edges)"** (launcher and in-game VR settings):

- temporal 0, Edge AA off, Resolution 150% (2.25x pixels), no post filter;
- still pure supersampling, the deferred engine's MSAA equivalent;
- Off returns Resolution to 100%, so Off/125/150 are distinct states;
- SSAA 125% remains the default recommendation; 150% is the next step for thin edges at higher GPU cost.

There is no spatial post-process that can recover a window frame thinner than a pixel; more samples (150-200%) or a
temporal method (DLAA/FSR) are the only options in a deferred engine. At 150% the XR eye is 3744x4032 on the current
headset, so frame time/VRAM is the limiting factor.

## The one-time crash when first enabling SSAA

`artifacts/native-exception-context.txt` records a first-chance access violation in **VirtualDesktop.LibOVRRT64_1.dll**
(`access_address=0xffffffffffffffff`) during the XR swapchain/overlay recreation that follows a resolution change; the
mod's own frame is only the exception observer. The later session ran at 3120x3360 without a repeat. This is a Virtual
Desktop runtime fault on the resize path, not a graphics bug in the mod.

Practical mitigation: **choose SSAA 125%/150% in the launcher before starting the game**, so the XR session is created at
the final size instead of recreating every swapchain and overlay surface mid-session. The in-game change still works, but
it is the path that faulted once.

## Build

- `build/d2vr_render_probe_arm_v975.dll`, SHA-256
  `21e4ce585aeb52225c2c70bffe4a3cd881508b04a66dfc37a0c366955dd9e8a8` (14,072,320 bytes).
- Launcher `DishonoredVRLauncher-v975.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check` and `eye_antialiasing_check` pass. No headset run.
