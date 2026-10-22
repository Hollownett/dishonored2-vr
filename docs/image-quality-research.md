# Image quality in native stereo: TAA, post effects and options

Later implementation: [alternate-eye native Game TAA candidate, 2026-10-22](alternate-native-taa.md). This document preserves the original paired-view research below.

Status: research only, 2026-10-09. No game process was touched and nothing was built or injected.
Static evidence comes from the pinned Steam executable
(SHA-256 `b671f824…6180`, local copy `artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe`).
It was read with a byte scanner and a read-only Ghidra copy of the existing project, using
`tools/D2StaticResearch.java`. All addresses below are RVAs for that build.
Tags: **[static]** = read in this build's code or data; **[trace]** = from an earlier local GPU trace;
**[inferred]** = implied by the evidence but not observed in the game; **[public]** = third-party source.
Nothing here is evidence about headset visual quality.

## 1. Answer: is TAA correct with two views per frame?

**Probably not. The left eye most likely reprojects the right eye's history (hypothesis b).**

1. **The TAA pass and its gate [static].** `0x327270` is a post-process pass callback. Its view pointer is
   `*(ctx+0x70)`. It runs TAA only when `(view+0x3a34 & 1)==0` and `(view+0x3a37 & 4)==0`.
   `idRenderView::Setup` (`0x374e40`) copies `r_skipTemporalAA`
   (int cache `0x397bbf8`) into that bit of each view on every Setup. The mod's existing
   `--skip-temporal-aa` therefore switches off the TAA pass itself. It is not known to switch off the jitter
   (see 3).
2. **One ping-pong pair, chosen by the global frame counter [static].** The pass looks up its two history
   images in the pass resource table at `ctx+0x80`. Entries are 16 bytes, `{hash, ptr}`, and the two hashes
   are `0x3337e277` and `0x0d35680e`. It then swaps read and write when
   `FUN_14035eb00(&DAT_1432fc580) & 1` is set. That function returns `*(0x32fc580+0xcc)`, and the same
   counter is used once per frame by Setup to roll the previous matrices (it is called from `0x374e40`,
   `0x351780` and `0x3742b0`). The history textures are created once per render-target-set object by `0x36d0c0`
   (`_TemporalAAHistory0_%p` / `_TemporalAAHistory1_%p` at set `+0x2d8`/`+0x2e0`). The per-view
   renderer `0x351780` binds them as the global parameters `temporalAAHistory0/1` (`0x3579eb`, `0x357a23`).
   Other names nearby are `taaHistory_read`, `taaHistory_write`, `taaAmount` and `taaDitherAndViewportSize`, plus the
   materials `PostFX/TemporalAA_R11G11B10` and `PostFX/TemporalAA_R16G16B16` (`r_hdrBufferFormat`).
3. **Both eyes share the scene targets [trace].** In `artifacts/dxgi-gpu-trace.csv`, the stereo phase drew both
   eyes into the same 2496x2688 HDR, depth and G-buffer texture objects. Normal mode uses one set per frame.
   That trace logs draws only, not compute dispatches, so the TAA dispatch bindings themselves are **not yet
   observed**.
4. **What follows [inferred]:** with one pair and per-frame parity, frame N reads slot R and writes slot W
   for both eyes. The left eye writes W, then the right eye reads R (its own frame N-1 output) and overwrites
   W. In frame N+1 the left eye reads W, which holds the right eye's previous output. **The right eye gets
   correct TAA, but the left eye's history is offset by the full stereo disparity.** The variance clamp mostly
   rejects that history. The left eye is then left with raw jittered samples, which crawl and shimmer, and
   with ghost fringes wherever the clamp lets mismatched history through.
   EternalVR measured the same failure in DOOM Eternal: a ghost coefficient of 0.206 with shared history and
   0.029 per eye ([stereo-temporal.md §7][evr-taa]).
   **Cheap confirmation:** the user closes each eye in turn. The prediction is that the left eye shows clearly
   more shimmer and fringing than the right.
5. **Motion vectors and previous matrices look per eye [static + inferred].** Setup rolls the previous
   matrices (`view+0x7390..0x75cc` -> `+0x7410..`) once per frame for each view object, keyed by
   `view+0x77b4`. The mod's `NativeEyeSources` rejects a frame in which both eyes share one view address, and
   eye captures succeed, so the two eyes use distinct view objects. Each eye's previous matrices should
   therefore be its own. This still needs a cbuffer check (see the diagnostics in §4).
6. **Jitter [public + inferred].** Luma shows that D2's per-view constants carry the jitter in
   `cb_projectionmatrix(2,0)/(2,1)` and in `cb_jittervectors` (current xy, previous zw), and that the jitter
   is removed after TAA ([Luma main.cpp][luma]). The jitter source in this build was not located. The mod
   overwrites the projection in the Setup hook, so it is **unverified** whether the engine's jitter survives
   that override. Without jitter, TAA only blurs. With jitter but no usable history, the image crawls
   ("AA not working").
   The frame counter is per frame, so both eyes should get the same jitter phase. EternalVR's alternate-phase
   problem should not apply here [inferred].
7. **Other temporal state [inferred].** Auto exposure (`r_skipAutoExposureTemporalAdapatation`) probably
   adapts once per view, so it steps twice per frame. Camera motion blur uses previous matrices. SSAO is
   `r_SsaoType 1` (DWAO), and whether it accumulates temporally is unknown. No other `%p` history images
   were found in the render-target-set allocator: it holds the backbuffer, depth, decompressed depth, HDR1/2,
   the TAA histories and the mask buffer.

## 2. Cvars in this build

Each cvar object has the int cache at `+0x28` and the float cache at `+0x2c`. The float layout was checked
against the mod's own `0x397bfec` and `0x397c06c` writes, which are rs_forceFractionX/Y. Defaults are the
registration strings. Descriptions are from the 1.77.9.0 [cvar gist][gist]. **Bold** marks cvars that are
copied into the view by Setup on every view, so a raw cache write takes effect from the next Setup onward.
For the others, it is unverified whether a raw write is honoured.

| Cvar | Object RVA | Default | Meaning / VR note |
|---|---|---|---|
| `r_AntiAliasingPreset` | 0x3982ef0 | 3 | 0 off, 1 FXAA low, 2 FXAA high, 3 TAA. The menu builder `0x50c200` reads it, and the Sharpness slider is only active at 3. Prefer the menu or config over a raw write. |
| **`r_AAMode`** | 0x397bb50 | -1 | Per-view override: -1 uses the view's setting, 0 off, 1 FXAA Orbis, 2 FXAA 3.11 HQ. Setup writes it to `view+0x3a38` bits 0-1. |
| **`r_skipTemporalAA`** | 0x397bbd0 | 0 | Skips the TAA pass (`view+0x3a37` bit 2). Already used by the mod. |
| `r_TAAAmount` | 0x32f88c0 | 10 | "Sharpness of TXAA". This is the menu Sharpness slider; the menu passes 0x14 (=20), probably its maximum. The TAA pass reads it at `0x327695`. Lower it to avoid edge halos. |
| `r_jitter` / `r_jitterXFactor` / `r_jitterYFactor` | 0x39375e0 / … | 0 / 1.0 / 1.0 | Random jitter for HQ screenshots, not the TAA jitter [inferred]. |
| `r_skipCameraMotionBlur` | 0x39a4220 | **0 (on)** | Set to 1 for VR. The motion-blur/distortion material `PostFX/motionBlurDisto` is registered next to it. |
| `view_damageBlur` | 0x39a0510 | 0 | Damage blur; keep at 0. |
| `r_skipDOF` | 0x32f3d50 | 0 | Set to 1 for VR. |
| `r_DofUseFringing` / `r_DofFringChroma` | 0x32f4dd0 / 0x32f4ad0 | 1 / 0.6 | Bokeh chromatic fringing inside the DOF pass. Set to 0 if DOF stays on. |
| `r_DofUseVignette`, `r_DofUseNoise`, `r_DofUseDisto` | 0x32f5050, 0x32f50d0, 0x32f4f50 | 0, 0, 0 | DOF vignette, noise and barrel distortion are already off. |
| `r_skipLensFlares` / `r_useCheapLensFlareDistortion` | 0x32f7fc0 / 0x32f8840 | 0 / 0 | Lens flares are screen-space, so they look wrong per eye. Set to 1 if they look bad. |
| `r_skipBloom` | 0x32f7f40 | 0 | Keep bloom on; it is part of the art direction. |
| **`r_skipEmissiveGlare`**, **`r_skipGodRays`**, **`r_skipLightScattering`** | 0x397afd0, 0x397ad50, 0x397acd0 | 0 | Screen-space effects. Use only for A/B tests of edge artifacts. |
| **`r_skipSSAO`**, `r_SsaoType`, `r_hbao` | 0x397b4d0, 0x32fb6a0, 0x32ef770 | 0, 1, 0 | A/B test for noise or halos. |
| **`r_skipOceanSSR`**, **`r_skipDistortionSurfaces`** | 0x397aed0, 0x397a8d0 | 0 | A/B only. |
| `r_skipAutoExposure`, `r_skipAutoExposureTemporalAdapatation`, `r_forceInstantAutoExposureConvergence` | 0x32f7d40, 0x32f7dc0, 0x32f7e40 | 0 | Exposure history; see §1.7. Do not freeze it by default. |
| `rs_enable` | 0x397bec0 | **3 (DRS on)** | Set to 0. The mod pins `rs_forceFractionX/Y` (0x397bfc0, 0x397c040) to 1.0. `rs_minimumResolutionScale` is 0.85. Helix also says to avoid DRS. |
| **`rs_skipUpsample`** | 0x397c740 | 0 | Read by Setup at `0x375280`. |
| `image_anisotropy` / `image_lodbias` | 0x32ea3f0 / 0x32ea470 | 1 / 0 | Raise anisotropy to 16 if the menu does not. A negative LOD bias adds shimmer without TAA. |
| `view_skipShakes`, `pm_noBob`, `ark_playerBobAmount` | 0x39a41a0, 0x39a3490, 0x398e200 | 0, 0, 1.0 | Comfort settings. These are camera-only; ask before changing locomotion feel. |
| `stereoRender_separation` / `screenSeparation` / `guiOffset` / `swapEyes` | 0x2be6920… | 1.5 / 0.25 / 0 / 0 | Native stereo settings, already managed by the mod. |

Not cvar-controlled: the crouch vignette, the "dirty screen" and the lens distortion while running or crouched.
These come from `arkPlayerFX*` declarations and shaders. Luma exposes `VignetteStrength`, `LensDirtStrength`,
`LensFlareStrength` and a `DISABLE_LENS_DISTORTION` define. Helix-mod's F6 and F7 toggle the vignette and the
dirty screen.
No film-grain or chromatic-aberration cvar exists apart from the DOF fringe settings.
`r_AntialiasingTSAAMipBias`, which is mentioned in Luma, is a CryEngine (Prey) cvar and does not exist in this
executable.

## 3. How comparable mods handle temporal effects

- **EternalVR** (DOOM Eternal, sequential "Route S") [public, [stereo-temporal.md][evr-taa]]. Every TAA, exposure
  and anti-ghosting image was picked by backend-frame parity, which cross-fed the eyes. Their fix:
  - allocate a second accumulation pair for the right eye through the engine's own slot builder;
  - detour the two selector leaf functions;
  - give each eye its own reset and jitter phase, and fix the exposure index per eye;
  - create a DLSS "twin" NGX feature for the right eye;
  - force the still-shared temporal effects off through the engine's cvar setter;
  - fail closed to a no-TAA cvar set.

  Measured ghosting went from 0.206 to 0.029 per eye; the cost was about 0.09 ms per eye. This is the closest
  model for D2. Their earlier light-tile finding ([stereo-bin-tiles.md][evr-tiles]) is already used here.
- **UEVR** [public, local `praydog--UEVR` @4ee5c6b]. Native stereo uses UE's own per-view `FSceneViewState`,
  which holds the TAA history, previous matrices and eye adaptation. Its "ghosting fix" for AFR swaps in the
  other eye's scene state on alternate frames (`FFakeStereoRenderingHook.cpp` ~3180). It forces
  `r.DefaultFeature.MotionBlur 0` under AFR (`VR.cpp` ~1578).
  ([repo](https://github.com/praydog/UEVR))
- **REFramework VR** [public, [VR.cpp][ref]]. `disable_bad_effects` forces TAA to NONE when "force AA
  settings" is on, and it disables lens distortion and lens flares. `fix_temporal_effects` keeps
  `old_view_projection_matrix` per eye under AFR. The post-effect distortion type is switched around the
  post-effect layer "to fix TAA".
- **KHARVOX** (DOOM 2016) [public, local README @ff348bb]. The same-frame renderer (SFS) keeps the game's AA
  choice. The alternate-eye renderer (AER) defaults to SMAA. Every launch disables motion blur,
  glory-kill blur and chromatic aberration. FSR1 is used below 100% render scale.
  ([repo](https://github.com/CactusVRStudios/KHARVOX))
- **Helix Mod D2 (3D Vision)** [public, [post][helix]]. "Don't use Temporal AA", "Disable DOF and Motion
  Blur", avoid dynamic resolution, use water quality LOW. It provides sharpening, vignette and dirty-screen
  toggles.
- **Luma D2** [public, [main.cpp][luma]]. The TAA compute shaders are `06BBC941` (D2), `8EDF67D9` (D2 low
  quality?) and `9F77B624` (DotO). They are recorded on a **deferred context**. Bindings:
  - CS `b1` holds the per-view globals (projection with jitter, previous view-projection, `cb_jittervectors`,
    `cb_framenumber`);
  - SRV `t1` is motion vectors, `t2` is pre-TAA colour, `t3` is the auto-exposure buffer;
  - UAV `u1` is the output.

  The upscale/sharpen pass is PS `1A0CD2AE`, tonemap is `A6F33860` and SSAO is `94445D2D`. Luma injects DLSS
  and FSR at that dispatch by splitting the command list. Several comments are carried over from Prey, so read
  it as guidance only. Custom-MIT licence with attribution and commercial conditions.

## 4. Recommended plan (ranked by quality gain against risk)

**P0. VR-safe settings and the AA path (hours, low risk).**
- Set `r_skipCameraMotionBlur 1`, `r_skipDOF 1`, `r_DofUseFringing 0` and `rs_enable 0`.
- Lower `r_TAAAmount`: test 0, 5 and 10 on the user's machine.
- Set `image_anisotropy 16`. Make `r_skipLensFlares 1` optional.
- Write through the engine's cvar setter, as EternalVR does with `idCVar::SetString`. The D2 setter is **not
  yet located**; find it from a console-command path. Until then, use cache writes with compare/exchange and
  restore, as `--skip-temporal-aa` already does. Raw cache writes are only proven for the Setup-copied cvars
  in bold.

Also tell the user to disable ASW/SSW or Motion Smoothing while judging, because it adds its own edge
artifacts.

**P1. Diagnostics that settle §1 (hours, low risk; desktop-only checks are fine).**
- (a) Extend the existing retained dispatch observer to log `Dispatch` calls on deferred contexts. For CS
  hashes 06BBC941 and 8EDF67D9, record the eye and the resources behind SRV t0–t3 and UAV u1.
  Expected: the same two history textures alternate per frame and both eyes use them.
- (b) Copy each eye's CS `b1` and log `cb_projectionmatrix[2][0..1]`, `cb_jittervectors` and the previous
  view-projection per eye. This shows whether jitter survives the Setup override and whether previous matrices
  are per eye.
- (c) Measure a per-eye temporal-variance or ghost metric over N static frames, like EternalVR's
  `ghost_coeff`. Also do the user's one-eye check.

**P2. Stable fallback now: TAA off with the jitter off (about 1 day, low risk).**
- Select preset 2 (FXAA High) through the menu, or with `r_AAMode 2`, instead of only `r_skipTemporalAA`.
  This should also remove the jitter [inferred]; confirm with P1(b).
- Keep the per-eye SMAA 1x. Add AMD FidelityFX CAS ([MIT](https://github.com/GPUOpen-Effects/FidelityFX-CAS))
  at low strength.
- Spend GPU headroom on a 1.2–1.4x render multiplier; the backend for this already exists.

Expected: no ghosting and no cross-eye crawl, but some residual specular and dither shimmer. Use this as the
shipping default until P3 passes.

**P3. Per-eye TAA history (2–4 days, medium risk; largest gain).**
- Hook the TAA pass `0x327270`. Find the eye from `*(ctx+0x70)`, using the view addresses the mod already
  qualifies.
- For the right eye, replace the `ptr` of the `0x3337e277` and `0x0d35680e` entries in the `ctx+0x80` table
  with a twin pair, then restore it after the original call returns. The parity swap stays the engine's own,
  which gives each eye its own ping-pong.
- Create the twins with the engine allocator path used by `0x36d0c0`: name formatting with `FUN_140004b80`,
  then `FUN_1402ecb20(DAT_1422aba40, name, desc, …)` at `0x36d71a` and `0x36d750`. Tie them to the
  render-target set's lifetime and to resize.
- Before shipping:
  - verify the entry layout and image ABI;
  - find any other reader of the global `temporalAAHistory0/1` parameters;
  - reset an eye's history on its first frame, on resize, after menus and on camera cuts.

If engine image creation is too risky, a D3D11 fallback exists: own twin textures, plus SRV/UAV remapping while
the right eye's TAA dispatch is recorded (P1(a) identifies the textures). Its cost is deferred-context
bookkeeping.
Validate with the P1(c) metric, aiming for left ≈ right ≈ the no-TAA baseline ghosting, plus the user's
headset judgment.

**P4. Optional, high effort: per-eye FSR 3 or native-AA at the TAA dispatch.** Use one context per eye, fed
from Luma's bindings (motion vectors t1, colour t2, depth, jitter from b1). This lets the scene render below
100% scale for headroom on the RX 9070 XT. It reuses Luma's deferred-context split
and EternalVR's twin pattern. Do it only after P3, because it inherits all of P3's eye bookkeeping.

**Monitor:** brightness differences between the eyes (shared exposure adaptation), and dithered materials
that rely on TAA (`r_skipInsideBodyDithering`).

[luma]: https://github.com/Filoppi/Luma-Framework/blob/5d7ee5470d8f09e27dfe529a386af46d8c6f8e33/Source/Games/Dishonored%202/main.cpp
[gist]: https://gist.github.com/fakuivan/8910a208fc88f831f06b047d0955e4fe
[helix]: https://helixmod.blogspot.com/2018/03/dishonored-2-dx11.html
[evr-taa]: https://github.com/ArcadaLabs-Jason/EternalVR/blob/2c6b6389459617dae52b41399172281880bdf824/docs/rig-findings/stereo-temporal.md
[evr-tiles]: https://github.com/ArcadaLabs-Jason/EternalVR/blob/2c6b6389459617dae52b41399172281880bdf824/docs/rig-findings/stereo-bin-tiles.md
[ref]: https://github.com/praydog/REFramework/blob/master/src/mods/VR.cpp
