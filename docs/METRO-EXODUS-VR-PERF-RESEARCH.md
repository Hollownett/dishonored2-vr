# Metro Exodus VR 0.9.0: rendering-performance techniques vs ours (2026-11-08)

Source: static strings of `MetroExodusVR/versions/0.9.0/MetroExodusVR.dll` (10.8 MB) in
`Downloads/MetroExodusVR-0.9.0 (1)`: the option help texts, config validation messages, log formats and the
embedded HLSL comments. Nothing was run or injected. No code is reused, only the techniques they name. Their
own design doc (`docs/AER_PLAN.md`) is referenced in the shader comments, but it is not in the package.

## What the Metro mod does (from its own strings)

| # | Technique | Their option / log text | Notes |
|---|---|---|---|
| 1 | **Alternate-eye rendering (AER)** | "STEREO renders both eyes every frame. PERFORMANCE renders one eye per frame, in turn, and rebuilds the other" - menu `PERFORMANCE (AER+)` | Kept eye reprojected with depth + motion vectors, then checked against the fresh eye ("fresh eye confirms", tile glitch probe, `kept eye mask/warp/motion chain`). |
| 1a | Per-eye previous transforms | `[eye-previous] renders with the eye's own previous ... transforms replaced ... bones replaced` | Each eye's motion vectors span *that eye's* last render (two frames back), not the engine's last frame, including skinned bones and the shadow passes. |
| 1b | Per-eye temporal cadence | `[eye-cadence] alternate-eye cadence: fog history, motion blur, temporal noise` | Fog history swapped per eye, motion blur scaled for the 2-frame interval, noise seeded per eye. |
| 2 | **Hidden-area mask** | `XR_KHR_visibility_mask`, `visibility_mask = off / all_visible / depth_stamp / gbuffer_stamp / late_restore`, `mask_post_passes`, `mask_temporal_pass` | The runtime's hidden triangles are stamped into depth (or the G-buffer) so the lens-hidden pixels are never shaded; post passes and the temporal pass are masked too; `late_restore` puts the area back for the upscaler. Logs the hidden percent per eye. |
| 3 | **Foveated upscaling** | "Upscales only the middle of each eye's view ... Smaller angles are faster" (off, auto, 45 down to 20 deg); periphery "NATIVE (FSR/FSR) is sharper ... TAA is faster" | DLSS/FSR2 runs on a centre sub-rectangle (`DLSS.Input.*.Subrect.*`); the edges get the mod's own TAA. Cuts the upscaler cost, not the scene cost. |
| 4 | **Fixed foveated VRS** | "Renders the edges of your view in less detail ... NVIDIA GPUs only", NVAPI `RSSetShadingRateResourceView`, patterns of rings `[degrees, rate]`, passes `gbuffer, lighting, water` | D3D11 VRS through NVAPI (Turing and newer). |
| 5 | Sun shadows once per frame | `share_sun_shadows`, `share_atlas_region` ("sixteen-region atlas updated in the first eye only") | Same idea as our v1009 shadow share. |
| 6 | **Staggered sun cascades** | `sun_cascade_schedules` (per frame, which cascades 0-3 update), `sun_region_interval`, `sun_cascade_drift_texels` | Far cascades refresh every Nth frame. |
| 7 | Local shadow atlas / lights once per frame | `[local-shadow-share]`, `[shared lights]`, `[light memo]`, `[shadow atlas] dedup` | Same idea as our v1009-v1012 light-link + record-count carry. |
| 8 | Per-eye scene occlusion | `per_eye_scene_occlusion`, `[per-eye occlusion]` | A correctness fix (second eye culling with the first eye's Hi-Z), not a speed-up. |
| 9 | Render scale, FSR2 / DLSS (presets E/J/K/L/M) | standard | We have FSR4 + DLSS. |

## What we already have

| Metro | Ours | Status |
|---|---|---|
| 1 AER | `src/alternate_eye.hpp`, `src/stale_eye_warp.hpp` (perf mode, v790-v813) | Exists. Ours struggled with doubling/ghosting in motion. |
| 1a per-eye previous | `src/object_motion.hpp` per-eye prev MVP (v828, objects only) | **Partial**: objects yes. Not checked for camera, bones, shadow passes in perf mode. |
| 1b per-eye cadence | none found | **Missing.** |
| 2 hidden-area mask | none (`XR_KHR_visibility_mask` is not even probed in `src/game_xr.hpp:1965`) | **Missing.** |
| 3 foveated upscaling | none (`docs/vr-performance-plan.md:38` marked fixed foveation "not feasible" for scene render, not for the upscaler) | **Missing.** |
| 4 VRS | none | Missing, but see below. |
| 5, 7 shadows/lights once | `src/shadow_share.hpp` (v1009-v1012) | Exists. |
| 6 cascade stagger | none | **Missing.** |
| 8 per-eye occlusion | `docs/ENGINE-PERF-PLAN.md:13`: shared culling measured *slower* | Not a lever for us. |

## Can we use them? Ranked for the user's rig (RX 9070 XT, Quest 3 over Virtual Desktop, 90 Hz = 11.1 ms)

Known budget (`docs/ENGINE-PERF-PLAN.md`, Candidate GPU levers): heavy spot, AA off, 13.8 ms native + 0.9 ms mod;
FSR4 native adds about 4.7 ms for both eyes.

1. **Foveated upscaling (3): the biggest pool.** FSR4 is our largest measured cost (about 4.7 ms). Running it on a
   centre rectangle (e.g. 30-35 deg) and a cheap upscale/TAA outside could remove roughly half of that. Works on AMD.
   Open question: whether the FSR4 provider (`src/fsr4_eye_upscaler.hpp`) accepts an input/output offset. FSR 3.1/4
   take a render size but no input origin, so a copy into a centre-sized texture may be needed. DLSS supports subrects
   natively (the laptop). Seam blending at the rectangle edge has to be checked in the headset.
2. **Hidden-area mask (2): cheap, any GPU.** The Quest 3 lens-hidden area is typically about 10-20% of each eye
   image. Stamping it into depth after the game's depth clear (reversed-Z: write the near value) culls it through
   early-Z for G-buffer, lighting and forward passes. Gains scale with the pixel-bound passes, roughly 1-2 ms at the heavy spot
   (estimate, not measured). Steps: probe `XR_KHR_visibility_mask` (VDXR support unknown, so log the extension list),
   find the scene depth clear (the v999 pass map has the passes), and draw the triangles. Risks: FSR4/TAA history at
   the mask edge (Metro's `late_restore` mode exists for this reason), and canting changes the mask per view.
3. **Staggered sun cascades (6): small, builds on shadow_share.** Cascades 2-3 every 2nd/4th frame. Saves part of
   the shadow group that shadow_share already halved. A shadow-pass GPU time from `gpu_passes` decides whether it is
   worth doing. Risk: swim/pop on moving casters in the far cascades.
4. **AER quality fixes (1a, 1b): only if perf mode comes back.** Metro's AER is the same design as ours. Their
   additions address the artifacts we hit: eye-own previous transforms for the camera, bones and shadows, and fog
   history / motion blur / noise per eye. Do this only if the user wants performance mode back. Stereo with levers 1-3
   may close the 90 Hz gap without it.
5. **VRS (4): skip for the desktop.** NVAPI only. D3D11 has no VRS on AMD. Useful only on the RTX 3070 laptop.

## Recommended next step

Read the v999 `gpu_passes` numbers for the FSR4 pool and the pixel-bound pass group at the heavy spot. Then build
in order: (a) log the runtime extension list and the visibility-mask hidden percent (read-only, one build), (b)
prototype foveated FSR4 in the existing FSR4 GPU fixture lab before the sim (lab first), (c) depth-stamp the mask.

## Measured: v1105 probe (headset, VirtualDesktopXR, Quest 3, canted render, 2026-11-08)

- `XR_KHR_visibility_mask` v2 **is offered**: 52 vertices / 52 triangles per eye, mirrored between the eyes.
- Lens-hidden area: **10.9 % of each eye's runtime FOV**.
- Our canted render: **28.2 % of the pixels we render are never seen** (10.9 % lens mask plus about 17 % canted-frustum
  area outside the runtime FOV). FOV tangents: left eye l -1.376 r 0.839 u 0.966 d -1.428 (the mask bounds match exactly).
- So a depth stamp of mask + outside-FOV region can skip up to about 28 % of the pixels in the pixel-bound passes. The real
  ms gain depends on how pixel-bound the heavy spot is (v999 `gpu_passes`).
- Other extensions worth noting: `XR_KHR_composition_layer_depth` (submit depth so the runtime can reproject better),
  `XR_FB_display_refresh_rate` (the app could request 90 Hz itself), `XR_EXT_eye_gaze_interaction` (no eye tracking on
  Quest 3, so no dynamic foveation here). No `XR_FB_foveation` (that one is for Quest-native swapchains).

## Built and tested (2026-11-08)

| Version | Change | Result |
|---|---|---|
| v1105 | Read-only probe: runtime extensions, per-eye hidden area | Numbers above |
| v1106 | Visibility stamp: lens mask + outside-FOV area written at reverse-Z near before each `FORWARD_WORLD` job (`src/visibility_stamp.hpp`) | Headset: stamped in both eyes, about 26 % of each image, image OK. Live A/B inconclusive (frame rate locked at 45 Hz) |
| v1107 | Foveated FSR 4: FSR 3 upscales the whole eye, FSR 4 a 30 deg box at each eye's optical centre (`src/foveated_upscale.hpp`) | GPU lab at headset sizes: FSR 4 2.65 -> 0.65 ms per eye; box matches full inside, nothing written outside |
| v1108 | Same for DLSS through the NGX subrect bases (one feature pair at the box size) | Headset (FSR 4 Quality, RX 9070 XT): 20k box dispatches per eye, 0 failures; pacer work p50 9.95 -> 8.33 ms and no 45 Hz locks (different session spot); user: great performance, no seam. DLSS path untested on RTX |

Switches: `build/no-visibility-stamp`, `build/no-foveated-upscale`, `build/foveated-upscale-deg` (10-60); live `vstamp on|off`, `fovea off|<deg>`
through `artifacts/vr-command.txt`. Labs: `tests/visibility_mask_check.cpp`, `tests/fsr4_fovea_lab.cpp` (GPU, real provider).
