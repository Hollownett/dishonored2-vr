# VR v973 (2026-11-03)

## SMAA + SSAA 125% (the deferred-engine MSAA-equivalent)

The request was "a good SMAA and MSAA like other VR games". Hardware MSAA is not available in Dishonored 2's deferred
Void engine: the renderer has no per-sample G-buffer and forcing a multisampled target breaks the deferred lighting and
the FSR/DLSS path. Meta's own guidance ties MSAA to the forward renderer, and the CESCG deferred-renderer paper states
the built-in MSAA cannot be used there. See [VR-ANTIALIASING.md](VR-ANTIALIASING.md) for the sources.

The deferred equivalent is supersampling. v973 adds a one-click mode:

- **Anti-aliasing -> "SMAA + SSAA 125%"** (in-game native VR settings and the launcher): temporal 0 + Edge AA on +
  Resolution 125%, i.e. 1.25x per axis and 56% more samples, compositor downsample, then two-pass SMAA.
- The launcher help and the research note say it is **not hardware MSAA**; it is the closest clear-image option this
  engine permits.
- Plain **SMAA (2-pass)** and manual **Resolution 150%** remain for users who want to separate the two.
- The preset is UI mode 10; the version-10 file stores it as temporal 0, Edge AA on, resolution 1.25, so older files and
  the game menu round-trip.

## SMAA quality

v972's two-pass 0.03 SMAA is retained: changed pixels on a real 2688x2880 eye capture 2.8% -> 13.4%, flat surfaces
preserved; `eye_antialiasing_check` passes.

## Build

- `build/d2vr_render_probe_arm_v973.dll`, SHA-256
  `5c38822ac5b1be756d92ac68a8bc16b7c83096c5901035a545a89898ae2b1944` (14,071,808 bytes).
- Launcher `DishonoredVRLauncher-v973.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check`, `eye_antialiasing_check` and `glyph_swap_async_check` pass.
- No headset run: the preset changes resolution and needs the user's frame-time test.
