# AER reference research: Metro Exodus VR "PERFORMANCE (AER+)" and the Indurhra / Cosmic Tangerine D2 mod (2026-11-11)

Read-only research. Nothing was launched, injected or run on a GPU. Sources:

- Metro: `Downloads/MetroExodusVR-0.9.0 (1)/MetroExodusVR/versions/0.9.0/MetroExodusVR.dll` (the only copy on this machine; Metro
  Exodus itself is not installed, the zip packages hold the same files: exe, launcher, stub, dll, DLSS, bHaptics, licences,
  `version.json`). No `.hlsl/.fx/.cso/.ini/.md/.json` beyond `version.json`. Strings dumped with a Python script (ASCII + UTF-16,
  length >= 8, 22 883 strings); the AER shaders are **embedded as full HLSL source** (compiled at runtime, "Microsoft (R) HLSL Shader
  Compiler 10.1"), so most of part A rests on shader source, not on log strings. Offsets below are file offsets in the DLL.
  The project is `https://github.com/RippeR37/MetroExodusVR` (string at W 0x5193d0); that repository holds only README/header image
  (checked with `gh api .../git/trees/master`), so `docs/AER_PLAN.md` is **not public**.
- D2: `Indurhra/dishonored-2-vr` clone at commit 1fde823 (scratchpad `ind/repo`). File:line references are to that clone.

Notation: **[E]** = directly in the evidence (quoted); **[I]** = my inference.

---

## A. Metro Exodus VR: the AER / AER+ pipeline

### A.0 What the menu and logs say

- Menu: `RENDER MODE` = `mevr_alternate_eye`, value `PERFORMANCE (AER+)` (0x49ef88); help text (0x4a0c50): "PERFORMANCE renders one eye
  per frame, in turn, and rebuilds the other: much faster at a very slight cost of quality in fast motion." There is **no separate
  "AER" (without +) menu value** in the strings. [E]
- Plan counters: `[aer] plans alternate={} off={} no_previous={} stale_eye={} one_eye_submits={} kept_eye_rejections={} this_frame={}`
  (0x514730); timing scopes `kept_eye_source`, `kept_eye_warp`, `motion_chain`, `aer_resync`, `first_person_mask`, `xr_depth`,
  `frame span, one eye` / `both eyes`, `kept-eye work` (0x4b9ee8-0x4b9f90). [E]
- Rejection reasons (consecutive strings at 0x4b49f0-0x4b4a38): `no vectors`, `no camera`, `frame gap`, `magnified`,
  `no previous render`. [E] [I] These are the reasons a kept eye is not re-projected: no motion vectors, no camera data, a frame gap
  (hitch / cut), scope magnification, first frame.
- Capture JSON: `"kept_eye":{"reprojection":{},"motion":{},"chain":{},"pose_correction":{}}` (0x4ba550). Frame-trace view names:
  kept eye `not re-projected` / `as rendered` / `pose-corrected` / `re-projected with motion`; motion vectors `as the engine writes
  them` / `chained over the eye's history` (0x4c4178-0x4c4268). [E]
- **[I] "AER+"** most likely means plain AER (pose correction / depth re-projection of the kept image) **plus** the motion layer
  (per-texel object motion, first-person classes, eye-previous transforms and bones, eye cadence). The four switches in the JSON
  (reprojection, motion, chain, pose_correction) are the layers. Not provable from strings: the plan doc is private.

### A.1 Data and classes

- **Depth source pass** ("kept_eye_source", shader at 0x4ab4bc) [E]: from the engine depth (reversed-Z: `d>=1` -> empty, `d<=0` ->
  infinity) and the first-person mask it writes `float2(z_linear_metres, kind)`; class-1 depth above `foreground_floor` is re-expanded
  (`d = (d - foreground_floor) / (1 - foreground_floor)`) because the viewmodel uses a compressed depth range (also logged:
  "[xr-depth] the viewmodel's depth range {}..{} differs from the eye's").
- **First-person mask without stencil** (shaders `before_precise`, `after_precise`, `after_world`, `after_player`, 0x4ab192;
  PSO names `mask_before_precise` ... `mask_after_player`) [E]:
  ```
  before_precise: copy depth
  after_precise : d != snapshot && d < 1 ? d : -1      // pixels the "precise" (viewmodel) pass wrote
  after_world   : model >= 0 && d == model ? -1 : d    // still the model's -> -1, else the world's depth
  after_player  : before < 0 ? 1 : (d != before ? 2 : 0)
  ```
  So kind 0 = world, **kind 1 = the first-person model drawn in the precise pass and not overdrawn**, **kind 2 = any pixel whose depth
  changed while the player's draws that are recorded after the world ran**. Log: "[kept-eye] first-person draws bracketed: {} player
  entries recorded after the world (pass {}, flag {}, recorder {})" (0x4b4a50). [I] The bracket is depth snapshots around a known
  group of draws, so anything the player draws there (arms, held weapon, held items) is masked with no per-mesh identification.
- Two transform modes per source point [E] (`kWorld = 0`, `kAttached = 1`): world points move by `world` (kept camera -> target
  camera), first-person classes by `attached` (**the play space's move**, "whose move between any two renders is known exactly (each
  frame's world_from_tracking)"). Key bit 1 = moving (only then is an origin written).

### A.2 Constants [E]

| name | value | meaning (their comment) |
|---|---|---|
| `kDepthTolerance`, `kDepthToleranceFloor` | 0.03, 0.01 m | "The same surface: depths within 3% and 1 cm" |
| `kMovingPixels` | 0.25 px | below this an object's motion is 16-bit-float precision, not motion |
| `kStandingPixels` | 0.5 px | a vector landing where standing still would is a standing point |
| `kSeedRadius` | 2 texels | how far a world texel looks for scattered motion |
| `kOwnVectorApart` | 2 texels | standing vs carried-with-camera must differ this much to tell |
| `kRepairRadius`, `kRepairVoters`, `kAgreePixels` | 4, 3, 1.0 px (`kSurelyAgreePixels` 0.999) | consensus repair |
| Chain | 3 lookups | "Three lookups from the motion d; true when the last found the same surface" |
| tiles | 8x8 groups; indirect args capped at 256 groups per row | |

### A.3 Order of passes for the rebuilt ("kept") eye

Pass names in creation order (0x4b4d40-0x4b4de0: `warp_clear`, `warp_scatter`, `warp_arguments`, `warp_scatter_motion`,
`warp_motion`, `warp_repair`, `warp_tiles`, `warp_splat_origins`, `warp_fill`, `warp_resolve`) plus the mask, source and chain passes.
The mapping of names to shader bodies below is [I] from the bindings; the bodies are [E].

1. **Mask** (during the frame): the four depth-snapshot passes above -> class texture.
2. **Source** (`kept_eye_source`): kept and fresh eye depth -> `float2(z, kind)`.
3. **Motion chain** (`kept eye motion chain`, PS, cbuffer `Chain` at 0x4b3be5), when eye-previous is not in force: rewrites the
   engine's one-frame velocity so it spans the eye's own two-frame history. For a texel it finds where the point was a frame ago
   (`was`), looks that point up in the **other eye's** depth/velocity of the frame before, and if the class and depth agree takes the
   other eye's step to extend the vector (`before = was - Rotate(fresh_previous_from_kept_previous, kstill - kwas)`); `span` scales a
   plain extension otherwise. Comment: "Every class's vectors are against the previous camera (the first-person model's too, 9.9.4
   Run F)". Log: "vectors chained over the history {}, extended {}" and "[kept-eye] no temporal pass listener; vectors are not
   chained and kept eyes move without their own motion". This feeds the per-eye TAA/upscaler as well as the warp. [E]
4. **warp_clear** (no body found; [I] clears keys, counters, tiles).
5. **warp_scatter** (CS, 0x4b17b9): for every **fresh-eye world texel** (kind 0) whose own motion over its interval exceeds 0.25 px:
   reconstruct where it was at the kept eye's moment, `k = source_from_fresh_previous(was) + travel * (1 - fresh_share)`, mark the
   8x8 motion tiles around `k` (+-2 texels), and if the **kept** depth at `k` is the same surface, append `{kept texel, 1/z key,
   travel * fresh_share}` and `InterlockedMax(scatter_keys, key)` (nearest wins). [E] So moving objects seen by the fresh eye hand
   their motion to the matching kept-eye texels.
6. **warp_arguments** (CS): builds indirect dispatch args from the counters. [E]
7. **warp_scatter_motion** (CS, 0x4b21a1): writes each scattered motion whose key won. [E]
8. **warp_motion** (`Move`, 0x4af137): per kept texel, a 3D displacement `moved` (metres) from the kept moment to the target moment,
   with `w` = mode (+2 = unconfirmed). [E]
   - sky / hidden area (`z == 0` or `>= far`): stands.
   - Without eye-interval vectors: a world texel the kept eye's own vector shows **carried with the camera** (riding a train) moves
     with the camera (`world_back(p) - p`). Otherwise, for first-person texels or texels in a motion tile: scattered motion if the same
     surface; else one `Look` into the fresh eye: if the fresh eye shows the same surface with < 0.25 px motion it **stands**; else
     seed from the nearest scattered neighbour (2 texels) and `Chain`. Only a confirmed chain gives motion; otherwise motion 0.
   - With eye-interval vectors: `OwnMotion` = the kept eye's own vector over its own interval times `kept_share` (first-person classes
     decoded in the play space). If own motion > 0.25 px, or first-person, or in a motion tile: start from own (or scattered) motion and
     `Chain`; confirmed -> that motion; not confirmed but own-moving -> own motion flagged `kUnconfirmed`.
   - `Look(p, d)`: project `p + d` into the fresh eye (world or attached transform); require the same class and depth within 3 %+1 cm
     (the sky and hidden area never confirm); read the fresh velocity there, rebuild the earlier position at the depth from
     `EarlierDepth` (the eye's **own previous depth**, 3x3, same class, nearest the standing-to-now span; "a sample of another surface (the
     background at a silhouette, run 10: static edges thrown 10-15 cm) is refused"), and return `moved = Rotate(source_from_fresh_previous,
     -offset) * fresh_share`. `Chain` = 3 Looks, each refining `d`.
9. **warp_repair** (CS, 0x4afdc2, 16x16 tile with a 4-texel apron in groupshared) [E]:
   - confirmed texel, and the tile's motion box wider than ~1 px: **5x5 consensus** - neighbours of the same class and surface vote;
     with >= 3 voters and fewer than half agreeing (within 1 px) with the texel, it takes the motion most voters agree with.
   - unconfirmed texel: takes the **nearest confirmed neighbour of the same class and surface** within 4 texels (80 offsets ordered
     by distance); none -> keeps its own unconfirmed motion.
   - then **forward splat**: `SplatTarget` maps the source point plus motion into the target camera (`world` or `attached`), key =
     `asuint(1/z) & ~3 | mode | moving`, `InterlockedMax(keys[target])` (= **depth test, nearest wins**); tiles with a moving texel are
     listed.
10. **warp_tiles** (CS, 0x4b29c7): `target_depth = TargetDepth(key)` (reversed-Z for the compositor), lists 8x8 tiles that contain
    holes (`key == 0`). [E]
11. **warp_splat_origins** (CS, 0x4b2677): for moving texels (all texels with stage flag 8), where the source's key won the target texel,
    store the source texel id (`origins`), so the resolve can read that texel's motion. [E]
12. **warp_fill** (CS, 0x4b2d21, run several times with stage bits first/skip/last and a `step`): for each empty target texel in a hole
    tile, look at the 3x3 neighbours **`step` texels apart** and take the one with the **smallest 1/z, i.e. the farthest surface**
    (`(other.x & ~3) < (here.x & ~3)`), carrying that neighbour's key and texel position; tiles without holes skip; the last stage writes
    the target depth and counts what stayed empty. [E] [I] A jump-flood-style fill with decreasing steps: **disocclusions are filled by
    stretching the background, never the foreground**.
13. **warp_resolve** (PS, 0x4b35b1): per output pixel, the own key or the filled one; z from the key; the ray position of the texel the
    key came from (`landed`: own texel, or the neighbour the fill copied); `s = Back(mode, ray * z) - moved(origin)`; **bilinear sample of
    the kept eye's own previous colour** at `s`. Empty -> black (debug: cyan holes, green = filled, blue = moving, magenta = moving
    without origin). [E] **The fresh eye's colour is never sampled in the warp**: the fresh eye supplies only depth, class and motion
    (confirmation). [E, by the bindings of all warp shaders]

### A.4 "Fresh eye confirms", and what happens when it does not

[E] A kept texel's motion is accepted only when the point, moved by its motion and projected into the fresh eye, lands on the same
class and the same surface (3 % + 1 cm) on the third chained lookup. Failure:
- one-update mode: world texels get no motion (camera re-projection only; first-person texels move with the play space);
- eye-interval mode: if the kept eye's own vector says it moves, it keeps that motion flagged unconfirmed, and in repair adopts the
  nearest confirmed neighbour's motion on the same surface (4 texels), else keeps its own;
- confirmed texels can still be outvoted by the 5x5 consensus.
Points the fresh eye cannot see (occluded, outside its view) therefore keep their own previous motion: "Where the fresh eye does not
show it, it goes on moving as it did." [E]

### A.5 First-person hands and weapon [E]

- Classes 1/2 (A.1). They move with `attached` = the exact play-space move from the kept eye's moment to the target ("the play space's
  move from the kept eye's moment on exact"); only their motion **within** the play space (the hands) is taken from vectors, decoded
  with `*_carried` matrices ("a point fixed in the play space") and scaled at a steady pace (`fresh_share`, `kept_share`). Reason given:
  "In the world frame a share of the two frames' locomotion moved them: off by half the difference of the two steps, which uneven frames
  (a hitch, starting, stopping, turning around) make 0.5-10 cm."
- `model_motion == 0` disables fresh-eye motion for class 1: log "[kept-eye] the viewmodel's screen terms ({}...) differ from the eye's
  (...); the first-person model moves with the play space only".
- Confirmation is class-aware (`uint(there.y) != kind` refuses), so a hand is never confirmed against the wall behind it.
- Hands are taken from the kept image like everything else (no fresh-colour override for hands). [E by the resolve shader]

### A.6 Moving objects [E]

Three sources, combined: the kept eye's own vectors (eye-interval mode), motion scattered from the fresh eye (A.3 step 5), and
neighbour seeding; all confirmed by the fresh-eye chain, cleaned by consensus, then forward-splatted with a depth test. Motion tiles
restrict the expensive work to 8x8 tiles near movers.

### A.7 Per-eye temporal state: eye-previous, eye-cadence, per-eye resources

- **eye-previous** [E]: "[eye-previous] installed (6 previous-matrix builders)"; stats "renders: {} with the eye's own previous, {} with the
  engine's, {} stereo (skipped) | transforms replaced={} new={} refused={} | bones replaced={} new={} | builder calls={} (off thread {},
  in shadow passes {}), {:.3f} ms per render"; "[eye-previous] no shadow pass listener; shadow passes get the eye's previous transforms
  too"; "unexpected prologue at 0x{:X}; previous transforms stay the engine's"; scope `aer.previous_transforms`. So: six engine
  functions that build previous-frame matrices (camera, object transforms, bone palettes) are hooked; in alternate mode each render is
  given **that eye's own previous** (its render two frames back), for object transforms **and skinning bones**. [I] The shadow-pass
  message implies shadow passes are normally **excluded** (they are rendered once, not per eye, so they keep the engine's previous); the
  earlier research doc said "including shadow passes": that was wrong.
- Hair: "[hair] passes with the eye's own previous view={}", fur simulated once per frame. [E]
- **eye-cadence** [E]: "alternate-eye cadence: fog history, motion blur, temporal noise" and "fog history swapped={} kept={}; motion
  blur scaled={}; noise on the eye's index={} on the frame index={}; elevation strips replayed for the eye={}", keys `fog_first`,
  `fog_second`, `fog_only`, `blur_first`, `blur_second`, `blur_only`, `noise_calls`, "(noise publisher not matched: noise keeps the frame's
  phase)". [I] Volumetric fog history is swapped so each eye's fog reprojects from its own last render; motion blur strength is scaled for
  the two-frame interval; temporal noise (dither / stochastic sampling) is indexed by the eye's own frame index so each eye walks the whole
  noise sequence (same idea as per-eye jitter phase); "elevation strips" = some per-frame strip update replayed per eye (unknown what).
- **Per-eye resources** [E]: "[per-eye] following the engine's render-target creation (RT create 0x{:X}, plain create 0x{:X}, device
  texture creation)"; "\"{}\" {}: the second eye has its own copy"; census lines "is per-view state (both views write it, and each reads
  what the other wrote) but the second eye shares it; classify it in per_eye_policy.cpp"; "cross-view temporal leaks (read-before-write
  in one view, written by the other)"; "[stereo] history clone ..."; "suppress duplicate history rollover". They hook **render-target
  creation**, keep a per-eye copy of every temporal target, and have a **runtime census that finds any resource read before it is written
  in one view and written by the other** (a cross-eye temporal leak detector).
- Frame-state once per frame (not per view): flare fade, cloud scroll, post effects, bullet-mark fade, visual settle timer; ripples
  and footprints step once per frame (0x4c2e10; help texts from 0x4aa1f8). [E]
- Pause: "The engine clears the motion vectors while paused ... On, the headset keeps its motion vectors and the pause uses the scripted
  slide's depth of field" (`fix_pause_menu_blur`). [E]
- Own TAA (periphery / fallback) cbuffer `Taa`: `g_color, g_velocity, g_depth, g_history, g_jitter, g_jitter_delta, g_motion_scale,
  g_current_weight` (0x4bdb53). Config `flip_jitter_x/y`, `flip_motion_x/y`; dev toggle "no jitter (DLSS sees a still camera sample)".
  [E] Upscaler per eye: a developer toggle "per-eye TAA history" exists (0x4aa6e0). [E]
- Depth to runtime: "Gives the VR runtime the depth of each frame, so a late frame is shown steadier" (`submit_depth`). [E]

### A.8 Camera cuts / teleports

[E] only indirect: rejection reason `frame gap`; "[vr] runtime supplied no valid continuity pose; temporal history and calibration must
reset before immersive rendering resumes"; `aer_resync` scope; `stale_eye` counter. No cut thresholds in the strings. [I] A cut or gap
rejects the kept eye (the rebuilt eye is not shown re-projected) and `aer_resync` re-establishes the alternation.

### A.9 "Tile glitch probe" [E] (shader at 0x4c3257, logs 0x4c3990-0x4c3b90)

A diagnostic, not a correction. Each eye image (rendered or re-projected) is reduced 4x4 into a ring of slices; per 32x32-px tile it
stores mean linear luminance and the share of black pixels. CPU: "[aer-glitch] frame {}: the {} eye's {} image leaves its neighbours in
a cluster of {} tiles (peak {:.2f} stops); dumped to {}" and on a key "the last {} images of both eyes ... dumped". [I] A tile cluster
whose brightness jumps by more than a threshold in stops relative to the neighbouring frames (and the other eye) flags a pop / black
hole automatically and dumps the ring for offline inspection.

---

## B. Dishonored 2: Indurhra / Cosmic Tangerine

Context: their default stereo is *synchronised* (both eyes at one game instant, the simulation held for the second eye); the old
alternating mode is `Stereo3D = 2`; the AFW mode alternates the fresh eye and lets PureDark's plugin rebuild the other
(docs/STATUS.md:340-344, docs/DLSS-AFW-IMPLEMENTATION.md:473-477).

### B.1 Per-eye motion vectors: engine fields and timing

- Fields (src/Src/StereoTaa.cpp:35-43, docs/ENGINE.md:231-262): view object `+0x3A84` previous vieworg (12 B), `+0x7410` previous
  projection, `+0x74D0` previous view, `+0x7590` previous VP, `+0x7858` previous jitter, `+0x7850` current jitter, `+0x77B4` frame number
  of the copy, `+0xBCFC` cut byte ("the late path that makes previous = current"), `+0x7390` projection (jitter in p02 `+0x7398`, p12
  `+0x73A8`), `+0x73D0` inverse projection. The view setup copies current -> previous once per engine frame when the frame number
  changes, then the projection builder exe+0x3742B0 "also moves the jitter +0x7850 to +0x7858 first"; exe+0x3586A0 uploads
  `cb_previous*matrix` and `cb_jitterrelativetopreviousframe` (StereoTaa.cpp:36-40).
- When: in their `CreateViewMatrix` hook (`OnMainView`, StereoTaa.cpp:593-664), i.e. after the engine's copy and before the upload. It
  stores the just-copied "previous" set for the eye that rendered the last frame (`ReadPrev`, :640) and, unless it is a cut (`reset`,
  :642), the same eye twice (:652) or the eye's set is older than `kMaxAge` = 6 frames (:44, :653), **writes that eye's own last set
  back** (`WritePrev`, :655): org, proj, view, VP **and the 8 jitter bytes at +0x7858** (:186-195). So `cb_jitterrelativetopreviousframe`
  becomes this eye's own jitter step.
- Jitter (StereoTaa.cpp:126-150, :612-636): the engine's 8-entry jitter repeats every 8 frames; with alternating eyes each eye got 4
  phases. They learn the period from observed frames and give each eye **sample k of the whole sequence at its own k-th frame**,
  rewriting `p02 += 2*(want - cur)`, `p12` likewise, `+0x7850`, and rebuilding the inverse projection at `+0x73D0` (:626-631).
- Entities need a separate fix: yes. "The engine's own 'previous' stays in every per-object previous MVP it CACHED from the last frame
  (entities: the arms, the weapons, doors, props -- only the static world is drawn from the view's matrices)" (:656-658); measured "~190
  px of false motion with the head still" (GpuTrace.cpp:3458-3461). Two rules:
  - `C = VP(this eye, its last frame) * VP(engine previous)^-1`, applied to `cb_previousmvpmatrix` rows per draw, but only when it brings
    the draw closer to the static world's rule `prevVP * model` (StereoTaa.cpp:660, :708-777);
  - default `aaprevrule 1` (:781-790, :943-983): every draw's current rows and original previous rows are kept per frame, keyed by the
    current rows' z,w (jitter-free); a draw whose engine previous equals some draw's current rows of frame N-1 gets **that draw's
    original previous** = VP(this eye, its last frame) * M(its frame). Fallback C. Residual "about 0.3 px (peaks 2-4 px)" on moving
    entities (docs/STATUS.md:280-281).
  - The rewrite runs per draw in `BeginSixDof` -> `EyePrevFix` (GpuTrace.cpp:3463-3480, :3501): the cb2 shadow copy is patched and
    swapped into VS (and PS when the same buffer) slot b2, restored after the draw (:3575-3584).
- TAA history per eye (StereoTaa.cpp:1232-1276): the TAA CS is recognised by its reflected names (`ro_taahistory_read`,
  `ro_motionvectors`, `ro_viewcolormap`, `rw_taahistory_write`, `rw_taaresult`, :91-96); the eye is the view matrix in the dispatch's
  PerViewCB matched against built views (:340-381); its own history SRV is bound (:1254-1260) and the result copied out after the pass
  (:1271). Safety net: 3 bad 120-present windows -> `r_skipTemporalAA 1` (:289-323, :1319-1331).

### B.2 Temporal effects with history they identified

- **TAA**: the only one handled per eye. History = ping-pong of two R11G11B10; reprojection uses only MVs (longest in 3x3), Catmull-Rom
  history, 3x3 variance clamp; t3 = auto-exposure (docs/ENGINE.md:342-357). "cb_taaamount=0 does NOT disable history blending"
  (DLSS-AFW-IMPLEMENTATION.md:89-91).
- **Skinning palettes**: `ro_previousframeskinningmatrices` is renewed only on simulated frames (ENGINE.md:355-357), which they rely on for
  synchronised stereo (ARCHITECTURE.md:358-360). [I] In alternating modes a palette's previous is one tick back, not the eye's own two
  ticks: no per-eye palette fix exists in their code for NPCs.
- Auto-exposure, SSAO (`r_SsaoType` 1 = DWAO), fog blur, SSR, motion blur: only listed as cvars (docs/cvars-1.77.9.0.txt:3491-4348:
  `r_skipAutoExposureTemporalAdapatation`, `r_forceInstantAutoExposureConvergence`, `r_skipSSAO`, `r_skipFogBlur`, `r_skipOceanSSR`); no
  per-eye handling and no finding about their history was found in their docs or code. Exposure for DLSS: NGX AutoExposure flag in DLAA
  (DlssLive.cpp:138).
- Lifecycle: `TemporalFrame::HistoryUsable` (src/Src/TemporalFrame.h:20-25) accepts a history only for the same eye, epoch and size and
  `serial - previous <= 2`; epoch changes "on cuts, resize, recenter or renderer changes".

### B.3 Arms and weapon: previous matrix / previous palette rewrite at draw time

- Weapon / rigid first-person draws (GpuTrace.cpp:227-249, :1883-1909): offsets of `cb_mvpmatrix*`, `cb_previousmvpmatrix*`,
  `cb_modelmatrix*` from D3DReflect of each VS (:299-321); the MVP is factored into pose + projection (ENGINE.md:295-306) and rebuilt with
  the hand delta; **the previous rows are factored and rebuilt with the previous frame's delta `dvPrev`** ("motion vectors: the same
  treatment with the previous frame's delta, or the weapon smears under TAA", :1897-1901).
- Skinned arms (GpuTrace.cpp:3153-3189, :3268-3399, :3557-3562): each arm draw binds `ro_matrices` and `ro_previousframeskinningmatrices`
  (slots from reflection, :301-302). The current palette is rewritten per bone (IK) and **stored keyed by the hash of the game's palette
  content** (`PrevStore`, :3363). For the previous-palette slot, `PrevFind` looks up the rewrite **made for that same content in an earlier
  view** (:3175-3189), i.e. exactly what this mesh was drawn with last frame ("A palette is rewritten for the frame it belongs to, never
  with another frame's deltas", :3157-3159). First frame of a mesh: solve it in its previous view with this frame's targets (:3371-3381).
  The patched palette is uploaded to a pooled buffer and bound for that draw only (:3389-3396).

### B.4 AFW inputs and findings

- Plugin: PureDark `PDAFWPlugin.dll` (D3D12 only, bridged from D3D11 with shared textures/fences), `Mode = CombinedWarping`,
  `MotionVectorsType = Normal`, `InMotionScale = (-width, -height)`, `ClearBeforeWarping` on epoch or serial discontinuity
  (AfwPlugin.cpp:87; AfwLive.cpp:49, :57).
- Camera: four matrix pairs (matrix + inverse): target view, target projection (the source projection re-fitted to the **target eye's
  FOV tangents**), source view, source projection (AfwLive.cpp:50-56).
- Jitter: colour comes **unjittered from DLSS**, so the jitter is removed from the source projection (`p[2] -= 2*jx`, `p[6] -= 2*jy`) and
  depth/MV are resampled at the raster jitter offset `(-jx*W, +jy*H)` with point sampling (AfwLive.cpp:46-48; AfwInputs.cpp:26-42;
  DLSS-AFW-IMPLEMENTATION.md:486-489). Depth is raw reversed-Z device depth (R24 from `ro_downsampling_depthbuffer`), MV normalised UV.
- Findings: "PureDark CombinedWarping fills holes with temporal history. The initial fresh-only mode had obvious gaps and was rejected"
  (:479-480). Their own (non-live) prototype `AfwShader.h:70-83`: a held point in front of the newly visible fresh surface is stale;
  "Moving foreground comes from the fresh eye. Never retain a stale held weapon"; explicit holes. Replay with moving head and hands:
  4.19 % uncovered, of which foreground 25.2 %, world 0.29 % (DLSS-AFW-IMPLEMENTATION.md:99-103); the stencil value 1 isolated arms,
  sword and crossbow in those captures (:65-67). Full history reset on camera cuts "is not established" (:550).
- Measured (simulator, RTX 5090): native stereo 57.9, AFW+DLAA 92.0 completed pairs/s (:508-515).

### B.5 Cvars forced for VR

`r_skipCameraMotionBlur 1`, `r_skipDOF 1`, `r_skipLensFlares 1`, `r_skipEmissiveGlare 1`, `r_skipBloom 1` (options, Settings.cpp:262-267);
reasserted at 10 Hz: `r_skipCameraMotionBlur 1`, `view_damageBlur 0`, `view_doubleVision 0`, `r_skipDOF 1` (Settings.cpp:307-316,
PERFORMANCE-REFERENCE.md:83); hooks that suppress `arkPlayerFXMotionBlur`, combat-focus and heavy-hit screen effects (Settings.cpp:318+);
dynamic resolution held off `rs_enable 0` (RenderRes.cpp:285-290); TAA forced on for DLSS (`r_AntiAliasingPreset 3`,
`r_skipTemporalAA 0`, DlssLive.cpp:48-61).

---

## C. What our mod most likely lacks (ranked)

Compared with `src/stale_eye_warp.hpp` (v1176/v1177) and the two handovers of 2026-11-11.

1. **Forward splat of the kept eye with a depth test, kept colour only, background-only hole fill** (A.3 steps 9-13). Our warp
   pulls each target pixel with a 5-step fixed-point `Solve`, which "oscillates or lands on the far surface" at near depth edges
   under camera translation (handover section 3), and it fills from the fresh eye's colour (shading 10-20 % off, our far rule exists
   because of that). Metro scatters every kept texel with its 3D motion into the target, `InterlockedMax` on 1/z makes the nearest
   surface win at every edge by construction, and disocclusions are filled with the **farthest** neighbour's key (background
   stretch), so the rebuilt eye always shows its own shading. This removes both of our top defects (edge solve, mixed-eye shading)
   in one architecture. Cost: ~6 compute passes, mostly tile-limited.
2. **Per-texel motion field confirmed by the fresh eye, then repaired by consensus** (A.3 steps 5, 8, 9; A.4). Our movers M1-M3 are
   per-pixel binary choices that give jagged silhouettes (open defect). Metro decides motion per kept texel: own vector -> 3 chained
   lookups in the fresh eye (class and 3 %+1 cm must match) -> 5x5 vote; unconfirmed texels borrow the nearest confirmed motion on the
   same surface. Motion is then applied by the splat, so silhouettes come from the kept geometry, not from the fresh texel grid. The
   `EarlierDepth` refusal of background samples at silhouettes ("static edges thrown 10-15 cm") is directly reusable in our motion pass.
3. **First-person classes moved by the exact play-space transform, their own hand motion decoded "carried"** (A.5). We move hands by
   recorded grip layers (over-move ~2x, handover v1173) or take them from the fresh eye. Metro: class pixels move by the play-space
   change (known exactly from tracking), plus only their motion relative to the play space from vectors, confirmed in the fresh eye
   (class-aware, so never against the wall behind). Their comment quantifies the error of the world-frame decode we use: 0.5-10 cm on
   uneven frames.
4. **Mask by depth snapshots around the player's draw bracket** (A.1). Our rig mask needs a replay of skinned rig draws and does not
   cover held weapons or props (engine handover c-iii). Metro snapshots depth before/after the group of player draws and classifies by
   "depth changed during that bracket", which covers anything drawn there. Requires finding where D2 records the first-person draws
   (player-model flag +0x18bc & 0x10 is already known); [I] D2's arms/weapons are not a separate pass, so the bracket may need to be
   per draw (depth snapshot is too expensive per draw; a stencil-free alternative is our existing replay extended to rigid
   first-person draws).
5. **Eye-previous for bones at the engine's builders, and eye-interval vectors** (A.7). Our rig motion slots never became valid
   (engine handover c-i: foreign palette buffers). Metro hooks the six previous-matrix builders (transforms and bones) so every
   skinned mesh, not only the player rig, gets its own eye's previous; Indurhra's content-hash lookup (`PrevFind`, B.3) is a second
   proven way to key a previous palette that survives multiple palette rings: key by **palette content**, not by model or buffer.
6. **Cross-eye temporal leak census and per-eye clones** (A.7). Our engine handover (c-v) lists fog/volumetric history, SSAO, SSR,
   exposure, motion blur and the ocean as "not examined". Metro's approach finds them all mechanically: per frame, record per view the
   resources read before written and written; any resource read-before-write in one view and written by the other is shared temporal
   state; give it a per-eye copy by hooking its creation. Then the eye-cadence fixes: per-eye fog history, motion blur scaled to the
   two-frame interval (we force camera motion blur off; `arkPlayerFXMotionBlur` and damage blur are separate, see B.5), temporal noise
   on the eye's own frame index.
7. **Motion-vector chaining for draws we cannot fix** (A.3 step 3). Draws that keep the other eye's rows (oversize cb2, unmatched
   shadows) are zeroed by our other-eye net. Metro instead extends such vectors with the other eye's step of the previous frame, keeping
   real object motion instead of dropping it.
8. **Entity jitter step: probably already done, verify.** Indurhra writes `+0x7858` per eye (B.1); our `alternate_native_taa.hpp:100-126`
   seeds the current set (jitter included) so the engine's copy puts this eye's previous jitter at `+0x7858`, and checks it at `finish`
   (:124-126). The engine handover's open item (ii) says the view constant still carries the other eye's jitter. Read `+0x7858` at the
   motion-vector prepass against this eye's last `+0x7850` live before any more row-side corrections. Per-eye jitter phase: ours already
   walks the whole sequence per eye (`alternate_taa_state.hpp:100`), as Indurhra's remap does.
9. **Automatic glitch probe** (A.9): tile luminance per eye image in a ring, flag clusters that jump by N stops, dump the ring. A cheap
   in-game detector to find pops before the user does, complementing the F/B4 replay scores.
10. **Depth submission to the runtime** (`XR_KHR_composition_layer_depth`, offered by VDXR per our v1105 probe): Metro uses it so late
    frames reproject steadier; with first-person depth range handled.

## D. Could not be determined

- `docs/AER_PLAN.md` (sections 9.9.4, 9.12.5, 9.13 are cited) is private; the CPU-side order, the exact meaning of `pose_correction`,
  `aer_resync`, `stale_eye`, the values of `fresh_share`/`kept_share`, the fill's `step` sequence and the cut thresholds are not in the
  strings.
- The six hooked previous-matrix builders and how bones are replaced; how fog history is swapped; what "elevation strips" are.
- Whether plain AER differs from AER+ as a separate mode (only one menu string).
- Whether the glitch probe compares against previous frames, the other eye, or spatial neighbours ("leaves its neighbours" is
  ambiguous).
- Indurhra: no per-eye handling or analysis of SSAO, fog, SSR or eye adaptation exists in the repository; PureDark's warp is a closed
  binary, so its disocclusion handling is known only as "fills holes with temporal history".
