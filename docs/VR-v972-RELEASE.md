# VR v972 (2026-11-03)

## SMAA Ultra 2-pass

The v1 SMAA used the upstream Ultra preset unchanged: one pass, colour-edge threshold 0.05, tuned for a 1080p flat
screen. At 2688x2880 per eye an edge is thinner and a 0.05 colour delta misses most low-contrast geometry (railings,
window frames, stone joints), which is why it looked close to Off.

v972 changes the SMAA method itself:

- **threshold 0.03** instead of 0.05 (the Ultra preset's line is replaced in the compiled source; the local-contrast
  adaptation still rejects texture edges);
- **two coverage/blend iterations**: after the first edge+weight+blend pass the result is fed back through the detector
  and blender, so edges the first coverage reveals are smoothed too;
- the input is still the sharpened eye image with SMAA as the last spatial step (v963 order).

Measured on a real 2688x2880 eye capture, changed pixels went from 2.8% to 13.4% and the output visibly smooths the
door frame, pipes and stone edges without the flat surfaces changing. The lab `eye_antialiasing_check` still passes
(localized coverage, flat colours, independent eyes, resize, state restore; changed=335 on its synthetic edge).

Cost: six full-screen passes per eye instead of three (still tiny against the scene pass).

## What VR games usually use

- **MSAA 2x/4x + supersampling** is the classic PCVR standard (Oculus/Meta guidance, forward-rendered titles): it
  anti-aliases geometric edges without blurring textures and works with stereo. Dishonored 2 is deferred, so the mod
  cannot use MSAA from the game.
- **Supersampling (1.2-1.5x render scale)** is the most common quality lever in VR, with or without MSAA.
- **TAA / DLSS / FSR** are increasingly common in modern PCVR and UEVR titles; they handle subpixel/motion aliasing but
  bring ghosting, stereo history and shimmer, which is why this mod keeps them as separate modes.
- **FXAA/SMAA** are the cheap spatial fallbacks, mostly on standalone or deferred engines. They cannot remove temporal
  aliasing or subpixel geometry; SMAA 1x only blends 1-2 pixels along detected edges.

For this mod the biggest remaining lever is the Resolution slider (try 1.25-1.5x with SMAA); a deferred engine has no
MSAA path. If two-pass 0.03 is still not enough, the next real step is a depth-predicated SMAA (needs the native depth
buffer) or FXAA cleanup, not another threshold change.

## Build

- `build/d2vr_render_probe_arm_v972.dll`, SHA-256
  `a3c34857c576f257c5bb48fe50a5a04a58fe391def545788db5524f55e4a55fc` (14,070,784 bytes).
- Launcher `DishonoredVRLauncher-v972.exe` in Downloads; installed as the only DLL.
- `eye_antialiasing_check`, `settings` tests and `glyph_swap_async_check` pass. No headset run.
