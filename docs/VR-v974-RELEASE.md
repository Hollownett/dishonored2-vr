# VR v974 (2026-11-03)

## Why "SMAA + SSAA 125%" looked worse than Off

The last session's `xr-live-status.json` showed:

- `feedback_render.temporal_aa = 6` (FSR 4 Quality)
- `resolution = 1.25`
- `edge_antialiasing_enabled = true`
- `smaa_eye_passes = 7508`

So the game was running **FSR 4 Quality at 125%**, not SMAA: the SMAA mode was not selected (and edge AA is
intentionally skipped in a temporal mode). v973's UI mode 10 also added **SMAA on top of the 125% supersample**, and the
two-pass SMAA softened the supersampled image; turning AA Off left the 125% resolution but removed SMAA, so Off looked
sharper.

v974 corrects both:

- **UI mode 10 is now SSAA 125% only** (temporal 0, Edge AA **off**, resolution 1.25). It is pure supersampling, the
  deferred engine's MSAA equivalent; there is no SMAA blur on top.
- **SMAA is back to one 0.03-threshold pass.** The two-pass version changed 13.4% of a real eye capture and spread
  softness into distant detail; one-pass 0.03 changes 2.8% (the old rate) but with the lower threshold, so it smooths
  geometry edges without the second-pass blur. SMAA remains a separate mode.
- Selecting **Off** from the SSAA preset returns Resolution to 100%, so Off and SSAA are no longer the same 125% image.
- The launcher materializes both spatial presets correctly: a stale disk temporal mode can no longer survive saving
  mode 9/10.

## The clear-image settings

- **Anti-aliasing → “SSAA 125% (MSAA-equivalent)”**: 1.25x per axis, no post filter, sharp; the best deferred-engine
  equivalent of MSAA. Try Resolution 150% manually if the GPU holds frame time.
- **Anti-aliasing → “SMAA (edge anti-aliasing)”**: native resolution plus one-pass 0.03 edge blending, for users who
  want spatial AA without supersampling.
- **Off**: native resolution, no AA.

Do not combine SMAA with SSAA and expect more clarity; SSAA already supplies the samples, and post-SMAA only softens.

## Build

- `build/d2vr_render_probe_arm_v974.dll`, SHA-256
  `c5f7ecc2b1b46f7f4bb6358e876e15980834932fda9668f8522d2cddc29ccc24` (14,071,296 bytes).
- Launcher `DishonoredVRLauncher-v974.exe`; installed as the only DLL.
- `eye_antialiasing_check` (changed=287), `vr_settings_native_check` and `settings_file_check` pass. No headset run.
