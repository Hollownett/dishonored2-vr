# Frame generation in other VR mods and runtimes: what we can take for our spacewarp

Date: 2026-11-20. Research only; no code changed. Our design: `docs/SPACEWARP-PLAN-2026-11-19.md` sections 10-21.
Already covered elsewhere and not repeated: `docs/AER-REFERENCE-RESEARCH-2026-11-11.md` (Luke Ross AER),
`docs/PERFORMANCE-MODE-AFW-COMPARISON.md` (PureDark AFW).
Raw notes by the helper agents (outside the repo): `...\side-session-notes\local_2f1ed273-...\research-vrmods.md`,
`research-runtimes.md`, `research-flat-papers.md`.

## 0. How far to trust this

- **VERIFIED** = read on the cited page. **INFERRED** = my deduction, a secondary source or a search snippet.
- Caveat: the pages were read through a fetch tool that summarises them, so quotes are close paraphrases, not byte-exact. Quote
  fragments below are short and should be rechecked before they go anywhere formal.
- Not read at all: the ExtraNet and ExtraSS PDFs (too large for the tool), Pico docs, WMR/OpenXR Toolkit docs (404), the VDXR
  source, Valve's own motion-smoothing pages, Luke Ross's Patreon (403). Nothing found on AFMF, "NeuralASW", or any 2024-26
  extrapolation paper. Those are gaps, not "nothing exists".
- Headline: **no VR mod we found does temporal extrapolation with engine motion vectors and depth.** Our spacewarp is closest to Meta's
  Application SpaceWarp, and in several parts (hands, UI, exact pose) is already ahead of the mods.

## 1. Comparison table

| System | Kind | Inputs | Disocclusion | Hands / HUD | Notes |
|---|---|---|---|---|---|
| **Our spacewarp** | Extrapolation, 45 to 90 | colour, full-res depth, engine MV, exact pose | far-biased fill, 8-dir blend + blur | hand/weapon/body capsules; overlays redrawn | star pattern, close cracks, fast movers |
| **Meta AppSW** (`XR_FB_space_warp`) | Extrapolation | app MV (3D, RGBA16F) + depth, both low-res (~1/4) | not documented (VERIFIED gap) | UI on compositor layers at full rate | one MV per pixel: transparency breaks |
| **Meta ASW 1.0 / 2.0** (PC) | Extrapolation | 1.0: image history; 2.0: + app depth for PTW | not documented | n/a | PTW does head motion, ASW the rest |
| **Virtual Desktop SSW** | Extrapolation, on headset | last 2 frames + poses (INFERRED: no game MV/depth) | not documented | not documented | HW motion engine on the headset |
| **SteamVR Motion Smoothing** | Extrapolation | last 2 frames to GPU video encoder flow | "repeating pattern" artifacts acknowledged | none | no app depth |
| **PureDark AFW** (REFramework / UEVR forks) | Cross-eye reprojection (other eye from N-2) | DLSS/FSR depth + MV | depth/stencil/velocity gates (gist) | arms/guns are its weak spot; reticle moved to layer | closed source |
| **UEVR** | none (AFR, sync sequential) | n/a | n/a | n/a | tells users to turn FG and ASW off |
| **BetterVR, VRto3D, Flat2VR** | none of its own | n/a | n/a | n/a | VRto3D only exposes SteamVR async |
| **DLSS-G, FSR3, XeSS-FG** | Interpolation | HUDless colour, UI+alpha, depth, dilated MV (FSR3 + optical flow) | FSR3: hole mip fill + inpaint pyramid | UI recomposed after: `UI + (1-a) * scene` | adds a frame of latency; not our case |
| **Lossless Scaling** | Interpolation, final image only | final image | n/a | UI jitters (secondary) | not transferable |
| **ExtraNet / ExtraSS** | Neural extrapolation | G-buffers + warped shading | inpainting network | n/a | NOT READ; INFERRED too heavy for D3D11 compute |

## 2. Findings per source

**Meta Application SpaceWarp.** (developers.meta.com/horizon/documentation/native/android/os-app-spacewarp; Khronos
`XrCompositionLayerSpaceWarpInfoFB`) VERIFIED:
- App renders at half rate and gives a motion-vector buffer and a depth buffer; the runtime makes the other frames. MV is "current NDC
  minus previous NDC", 3D, RGBA16F.
- MV and depth are deliberately low resolution (Quest 2: 368x400 for a 1440x1584 eye). Higher gives "no quality benefit"; full-res MV
  costs ~10x GPU. The sample project lists "motion vector buffer resolution limits at discontinuities" as the cause of artifacts on thin
  geometry (railings). This is the same class as our thin cracks.
- Transparency: "one pixel can need two motion directions"; avoid MV on blended surfaces; alpha-clip surfaces write MV by clip logic.
  Fast near-field transparents (controllers) are the worst case.
- UI/HUD go on compositor layers that bypass warping, world UI via a "poke-a-hole" layer.
- Known artifacts: missing MV = stutter/ripple/stretch; fast rotation gives wrong-direction MVs; simple clean backgrounds show
  distortion most. `appSpaceDeltaPose` tells the runtime about teleports/scripted camera so it can fill background MV and disable
  extrapolation on cuts. Debug overlay `debug.oculus.MVOverlay` (zero motion grey).
- INFERRED (search snippet only): advice to slow or drop MV on very fast objects.
- ASW 2.0 (PC developer guide) VERIFIED: PTW (positional timewarp from app depth) corrects head translation, ASW the remaining content
  motion; they must not both correct the same motion. Edge corrections come "mostly due to disocclusion and view-dependent shading".

**Virtual Desktop SSW / SteamVR Motion Smoothing.** SSW: headset-side motion engine from last two frames plus poses (Qualcomm blog
snippet, UploadVR, Godin on Reddit; so mostly INFERRED). Motion Smoothing (roadtovr, Alex Vlachos quote): last two frames go to the
GPU video encoder for vectors, which are filtered and applied to the newest frame; Valve mentions "different approaches to reduce
repeating pattern artifacts" (VERIFIED, secondary host). Neither uses app depth or engine MV, so they are the weaker design for us; they
confirm that a repeating/star pattern is a known hole-fill/flow artifact class.

**PureDark AFW** (github.com/PureDark/REFramework releases, github.com/PureDark/UEVR releases, a community gist by elliotttate,
scubasteve2365/Pragmata-VR) VERIFIED unless noted:
- The other eye is "reconstructed by warping it with depth and motion vectors" from the upscaler's depth/MV. Closed-source plugin.
- Its history is N-2 frames old while engine MV covers one frame; the gist writes velocity as same-eye camera over two frames plus the
  object residual doubled. (We solve the same problem with `motion_share` and the per-eye motion pass, plan section 19.)
- A debug view tints moving objects green (a mover mask); depth/stencil/velocity gates mark disocclusion.
- Arms and guns double or outline; reticle and subtitles had to be moved to separate layers; full-screen menus fall back to native
  because they trail; grain around movers/hair/hands; edge artifacts at high world scale; "white specks" and "close environment
  objects" were fixed in later betas.

**UEVR, BetterVR, VRto3D, Flat2VR, REFramework base.** UEVR (docs.uevr.io): AFR and synchronized sequential, no reprojection or FG of
its own; sequential gives TAA ghosting; users are told to disable DLSS-FG and runtime ASW (VERIFIED). BetterVR: true stereo and
foveated shading, nothing else (VERIFIED). VRto3D: only exposes SteamVR async reprojection (VERIFIED). Flat2VR: promotes AFW, nothing
of its own. Luke Ross: later builds reportedly add an "optical flow estimator" for AER v2 (search snippet only, INFERRED); nothing
primary beyond the existing doc.

**Flat frame generation.** DLSS-G (NVIDIAGameWorks/Streamline `ProgrammingGuideDLSS_G.md`): HUDless colour plus a premultiplied UI
buffer, `Final = UI + (1-a)*Hudless`; dense MVs; Reflex mandatory. FSR3 FI (gpuopen.com FidelityFX frame-interpolation doc): game MVs
are **dilated** and packed with depth priority (nearest wins); an SPD mip chain fills the MV holes ("closest in 2D, farthest in
depth"); optical flow fused and weighted by colour match; two disocclusion masks; an **inpainting pyramid that ignores holes** fills
what remains. XeSS-FG (github.com/intel/xess): same UI formula, high-res MV must be dilated, needs a 40+ fps base. All VERIFIED.
Softmax splatting (arXiv 2003.05534, abstract only): handles many-to-one collisions in forward warping. Crack/hole filling by
density-aware normalised convolution: weak sources only, INFERRED. YORO (uploadvr summary, secondary): pixel shift, mark occluded,
depth-aware inpaint blurring background into holes; fails on very near objects and transparency.

## 3. Ranked improvements

Our pass for reference (`src/spacewarp_pass.hpp`): `Classify` (297), `Edge` (306), `Land`/`SplatDepth`/`SplatIndex` (360-379, one source
texel lands in one render-size cell), `Crack` (383, radius 2), `Fill` (401, 8 directions, farthest seed), `Blend` (475, 8-direction
1/distance blend), `BlurBlend` (484, 7-tap tent), `Resolve` (496, back-projects per output pixel, `Seen` at 426).

### 1. Pyramid inpainting instead of 8-direction blend + tent blur
- **Fixes:** the faint star pattern in uncovered areas. An 8-ray blend is anisotropic by construction; the 7-tap tent only softens the
  seams (plan section 18, "a faint star pattern ... remains").
- **Where:** replace the `Uncovered`/`Blend`/`BlurBlend` path (`spacewarp_pass.hpp` ~460-495) with a coverage-weighted mip pyramid built
  over *drawn, non-filled* cells, weights = coverage x depth bias toward the far surface x same class (keep the class gate and the
  3/4-depth gate), then push down and fill holes only. Keep `Fill` for the seed, depth and class bookkeeping; this replaces only the colour.
- **Evidence:** FSR3 uses exactly this ("inpainting pyramid that ignores holes", "closest in 2D, farthest in depth"), gpuopen.com
  frame-interpolation doc, VERIFIED. That it removes our star specifically is INFERRED; it is isotropic by design. Meta/Valve both
  name repeating/simple-background distortion as the known artifact (VERIFIED, weak link to ours).
- **Effort / risk:** medium (about 4-6 small dispatches; the blend is already at render size, cost probably comparable to the present
  0.2-0.3 ms). Risk: low-frequency colour bleed across classes; mitigated by keeping the class and depth gates. Lab test 14 gives an
  existing A/B (the striped wall).

### 2. Magnification-aware splat footprint, dilation in `Crack`
- **Fixes:** thin cracks on very close magnified surfaces. Forward point splats of a surface that expands (close, approaching) leave
  one-cell gaps that `Crack` (radius 2, 10 % depth tolerance, needs both neighbours as surface) sometimes classes as holes.
- **Where:** `Land`/`SplatDepth`/`SplatIndex` (360-379): when a texel's local expansion is above 1 (compare the landing of the texel
  and its +x/+y neighbours; or derive from depth/ displacement), splat to the covered cells (2x2 or more) rather than one. Or widen
  `Crack` to radius 3-4 only where expansion > 1. Note `Resolve` already back-projects per output pixel with `Seen`, so the goal is to
  keep these cells "surface, not filled", so they take their own source point.
- **Evidence:** Meta names low-res MV at discontinuities as the cause of thin-geometry artifacts (VERIFIED); FSR3 and XeSS dilate MVs
  (VERIFIED); softmax splatting addresses forward-warp collisions (abstract only). Our cracks being magnification gaps is INFERRED from
  plan section 18 (they appear "on very close magnified surfaces", also in the old rule).
- **Effort / risk:** low-medium. Risk: footprint splats on silhouettes can fatten edges; limit to same-surface neighbours (depth within
  5 %, the same test the feather rule uses).

### 3. Confidence-gated mover extrapolation instead of a flat 3 cm cap
- **Fixes:** fast movers lagging. The cap (`SpacewarpParams::mover_cap`) was added because an uncapped guard's raised arm kept moving
  while the next real frame swung it down (plan section 19).
- **Where:** `game_xr.hpp` where `in.motion` and `motion_share` are set, and the cap in the pass. Keep last real frame's motion texture
  per eye; scale the cap up (to, say, 8-10 cm) per texel where the previous frame's mover vector agrees in direction and magnitude
  (steady walk or run) and keep 3 cm where it reverses, jumps or is new. A cheap one-frame history test, in the spirit of FSR3
  fusing flow only where colour confirms it.
- **Evidence:** Meta's mitigation is to slow or ignore MV on fast/bad objects (INFERRED from a snippet; "fast rotation gives wrong-
  direction MVs", VERIFIED); AFW scales the object residual by the history interval (gist, VERIFIED); FSR3 weights flow by colour agreement
  (VERIFIED). The agreement-gating itself is my design.
- **Effort / risk:** medium. Risk: flicker on attack animations; protect with `no-spacewarp-movers` style knob and the existing
  14-triple A/B (flicker 6.9 vs 3.9 is the failure to avoid).

### 4. Nearest-depth dilation of the mover motion texture
- **Fixes:** mover silhouettes (thin limbs, weapon edges of NPCs) that move less than their body because the MV edge is blurred by
  upscaling/TAA; related to `Edge` rim handling.
- **Where:** `Classify` (297) where it reads the motion texture: take, for each texel, the motion of the nearest-depth texel in a 3x3
  (FSR3 priority-by-depth), then let `Edge` operate on it.
- **Evidence:** FSR3 and XeSS both require dilated MVs (VERIFIED). Whether our motion texture needs it is INFERRED; measure with
  `spacewarp altmovers` first.
- **Effort / risk:** low. Risk: dilation at a mover/wall border enlarges the mover by 1 texel; the soft-edge rim pass should absorb it.

### 5. Motion/class debug views and a "no MV" detector
- **Fixes:** finding objects that never write an MV and so stand still in generated frames (stutter at 45 Hz). New gain: diagnosis.
- **Where:** extend `debug_tint_classes` to show motion magnitude (zero = grey) and flag texels in the near field with MV = 0 but
  displaced relative to the previous real frame (the confirmation test we already compute in the previous-frame rules).
- **Evidence:** Meta's `MVOverlay` (VERIFIED), AFW's green mover tint (VERIFIED).
- **Effort / risk:** low, no runtime risk (diagnostics only).

### 6. Optical-flow fallback for no-MV movers (long shot)
- **Fixes:** objects with no engine MV (and perhaps NPC limb accuracy).
- **Where:** a new input to `Classify`, fed from the NVIDIA optical flow engine or a compute flow between the two last real frames
  (the SteamVR and SSW approach, and what Ross seems to add in AER v2).
- **Evidence:** Motion Smoothing and SSW use encoder/hardware flow (VERIFIED/INFERRED); FSR3 fuses flow with game MV (VERIFIED).
- **Effort / risk:** high; risk of noise and extra latency in the 45 Hz pipeline. Only worth it if item 5 shows many objects without MV.

## 4. Already matched (no action)

- UI/HUD out of the warp and redrawn: Meta (separate full-rate layers), DLSS-G/XeSS (UI recomposed), AFW (reticle/subtitles on
  layers). Ours: plan section 21. Keep.
- Head motion from depth plus pose, motion vectors only for residual object motion: Meta ASW 2.0's PTW split. Ours is the exact pose
  and depth warp, plus movers; make sure movers never include the camera part twice (the `rig`/class design already does).
- Cuts/teleports (`appSpaceDeltaPose`): our `cuts` (0.5 m / 25 deg) and `play_mismatch`; same idea.
- Hands and weapons: AFW's weak spot; our bodies are a better design. Meta gives no hand guidance.
- Far-biased hole fill: FSR3 "farthest in depth", our `Fill`.

## 5. What NOT to copy

- **Video-encoder/optical-flow-only extrapolation (SSW, SteamVR Motion Smoothing)** as the primary path: no depth, repeating-pattern
  artifacts, and we have exact pose and engine MV.
- **Low-resolution MV/depth (Meta's 1/4 size):** a Quest bandwidth trade-off; Meta's own sample ties it to thin-geometry artifacts. We
  have full-res depth.
- **Cross-eye reprojection (AFW, AER, UEVR AFR):** exclusive with spacewarp by the user's rule, and doubles hands/weapons and
  fast things.
- **Frame interpolation (DLSS-G, FSR3 FI, XeSS-FG, Lossless Scaling):** holds back a frame (extra latency); LSFG works on the final
  image so UI jitters. Take the techniques (dilation, pyramid fill, UI recomposition) not the pipeline. Also: do not stack them
  (user rule).
- **Neural extrapolation (ExtraNet/ExtraSS):** needs a trained network per game and a heavy compute budget; INFERRED not portable under
  ~0.3 ms in D3D11 compute (and I did not read the papers).
- **One-MV-per-pixel for transparency:** Meta's own limitation. Do not give particles or glass the mover's full vector.
- **A hard cap lifted blindly (uncapped movers):** already tried; see item 3 for a gated version.
- **Unverified claims:** Ross optical-flow details and VD SSW internals come from snippets; do not design around them.
