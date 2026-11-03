# VR v979 (2026-11-03)

## Implementation review: SMAA is working, but SMAA 1x is conservative by design

The 150% v977 status proved the path:

- `game_render_size = [3744,4032]`, `game_source_size = [3744,4032]`, `xr_eye_size = [3744,4032]`
- `temporal_aa = 0`, `edge_antialiasing_enabled = true` (v978), `rs_enable = 0`, `dynamic_scale_forces = 0`

So the game really rendered 3744x4032, the mod copied it, and SMAA ran at 3744. The effect is still small because:

1. **SMAA 1x only blends 1-2 pixels along detected edges.** It is deliberately conservative to preserve textures.
2. **125 -> 150 is only 1.44x samples** (1.5²/1.25²). On thin window frames that is a minor change, not a
   night-and-day one.
3. SMAA cannot invent a sample for geometry thinner than a pixel. That is exactly what MSAA does, and Dishonored 2's
   deferred G-buffer cannot use hardware MSAA.

This is not a hidden SMAA bug; the measured lab (`eye_antialiasing_check`, changed=287 on its edge fixture) and the
3744x4032 source/target prove the pass runs.

## v979: 200% + SMAA

The only spatial step that can produce a visibly different result at this resolution is more samples. UI mode 11 is now:

**"SSAA 200% + SMAA (max clarity)"** — temporal 0, Edge AA on, Resolution 200%.

- 4992x5376 game render (4x the pixels of 100%, 2.56x the samples of 125%).
- If the runtime maxImageRect accepts it, XR is 4992x5376; if it clamps, `EyeResolve` box-filters down.
- SMAA runs at the game size before submission.
- **This is heavy**: expect a large frame-time cost on the RTX 3070 laptop. It is the quality ceiling, not a
  recommendation for normal play.

SSAA 125% (pure samples) remains for performance. A manual Resolution 150% + Edge AA still exists via the slider but is
no longer a labeled preset because 1.44x is too small to satisfy the request.

## Injection research and the remaining limit

- ReShade/OpenVR-layer post-process AA injects into the eye texture exactly like this mod's SMAA; it has the same
  conservative 1x limitation.
- CMAA2 is a better morphological filter (its Z-shape handling is stronger on thin features), but it needs a compute
  pipeline and LUTs; it is a candidate for a future version, not a drop-in.
- MSAA is the method most VR games use for clear geometric edges, but it requires a forward renderer (Meta states this
  explicitly; the CESCG deferred-renderer paper states hardware MSAA cannot be used there). Dishonored 2 is deferred.
- Temporal methods (game TAA, FSR 4 Native) stabilise subpixel edges but soften; they are the alternative when 200%
  SSAA frame time is unacceptable.

If 200% + SMAA still looks unchanged while `game_source_size` is really 4992x5376 and frame time drops, the remaining
loss is outside the mod (Virtual Desktop stream resolution / headset panel resampling), not the AA math.

## Build

- `build/d2vr_render_probe_arm_v979.dll`, SHA-256
  `c4e01b19d23563cf8f088e994ba898f0e480d22efd5f2fb49e19ccbc67edea7e` (14,080,000 bytes).
- Launcher `DishonoredVRLauncher-v979.exe`; installed as the only DLL.
- `vr_settings_native_check`, `settings_file_check`, `eye_antialiasing_check`, `render_size_check`, `eye_resolve_check`
  pass. No headset run.
