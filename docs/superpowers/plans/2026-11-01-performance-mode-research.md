# Performance mode (alternate eye) image quality: research plan

> **For agentic workers:** this is a research plan, not an implementation plan. Work task by task. Each task ends with a written
> result (numbers, images, a hypothesis ruled in or out) appended under **Results** at the end of this file. A fix gets its own
> implementation plan once its root cause is known.

**Goal:** make performance mode (`build/alternate-eye`, one engine view per Present) look as clean as normal rendering, with FSR
and DLSS working in it. First the visible defect, the doubled left eye. Then the artifacts that are inherent to alternate-eye
rendering.

**Approach:** the user's report narrows the defect to the left eye's own image. Reproduce it in the sim with a static view. Split
it with three cheap discriminators: AA Off, an eye-parity flip and a per-pass eye trace. Fix the root cause. Only then work on
motion artifacts, in an offline lab built from captured frames.

**Spec:** the user, 2026-11-01:
- «сейчас он работает, но картинка двоится, рябит и довольно нечетко. Задача сделать это рабочим.»
- After the first draft of this plan: «я использую стим VR и это должно работать с FSR и DLSS так что это очень большой прирост.
  в предыдущих тестах двоился только левый глаз. Правый был в порядке. Глаз двоился всегда независимо от движения»

Background: [plan F](2026-10-21-alternate-eye-f.md), [VR-PERF-LAPTOP.md, Performance mode](../../VR-PERF-LAPTOP.md),
[alternate-native-taa.md](../../alternate-native-taa.md), [VR-v637-TEST-CHECKLIST.md](../../VR-v637-TEST-CHECKLIST.md).

## What is known

1. **The user's evidence rules out most of the AER theory.** Only the left eye doubles, and it doubles with or without motion.
   Three things it rules out as the cause:
   - the interocular one-frame offset of AER: that is binocular and needs motion;
   - runtime reprojection errors: they need head motion;
   - frame pacing: it shows as judder on motion.

   What remains is that the left eye's own image holds two views of the scene, in one of four ways:
   - (L1) its temporal history holds the right eye's images;
   - (L2) its swapchain gets images from two viewpoints;
   - (L3) some state tied to the left eye or native slot 0;
   - (L4) the runtime treats the left layer differently.
2. **Mechanism.** The engine renders the eye given by the display-frame parity. The other eye is resubmitted from its kept copy,
   with the pose it was rendered from (`src/alternate_eye.hpp`, `GameXr::keep_alternate_eye_`). The engine pipeline is one frame
   deep: a Setup made at Present F is shown at F+2. `tools/alt_pose_trace_check.py` showed 0 pose/image mismatches in the sim
   (6,801 to 11,781 one-eye frames at 105-138 fps), never at headset rates. It does not record which eye the temporal passes ran as.
3. **Code audit (2026-11-01, reading only).** No defect specific to the left eye was found:
   - The TAA/FSR/DLSS pass of the single view takes the logical eye from the alternate receipt (`src/native_taa_history.hpp:145-149`).
   - The post-tonemap upscale pass resolves the eye the same way (`src/dxgi_probe.cpp:12773`).
   - Per-eye jitter and FOV slots use the logical eye (`src/dxgi_probe.cpp:5704`).
   - FSR's dt is per eye (`src/fsr_taa.hpp:832`).
   - In AER the fresh image of either eye comes from the backbuffer (`src/dxgi_probe.cpp:9311`).

   Two places are asymmetric:
   - native TAA: the left eye uses the engine's own history pair with its own role flip, the right eye uses twin images
     (`plan_pass`, lines 174-187);
   - the single view reuses native slot 0, which is the left eye's slot in paired mode.
4. **Must keep.** FSR (3 and 4) and DLSS must work in performance mode; that is the large gain. A spatial-only fallback is not an
   acceptable end state. DLSS needs an NVIDIA GPU: the user's PC has an RX 9070 XT, and the Linux laptop rig has an RTX 3070.
5. **Runtime and rates.** The runtime is SteamVR. The earlier draft's Virtual Desktop/VDXR notes do not apply. Today's paired
   session at 90 Hz with FSR4 Quality: pace p50 13.57 ms and 21.9% missed slots. AER saves about 23% in the sim, so it would be
   about 10.4 ms p50 (an estimate), still near the 11.1 ms budget.
6. **Inherent AER artifacts, for Phase 3.** These are real, but they are not what the user reports:
   - moving things and stick motion are one frame apart between the eyes (R.E.A.L. VR GTA V README; UEVR docs: AFR "causes eye
     desyncs");
   - snap turns: the planner refreshes paired only on a pose-epoch change (`src/dxgi_probe.cpp:7882`);
   - prediction horizon: each Setup locates for `predictedDisplayTime + period` (`src/game_xr.hpp:2051`), but its image shows two
     periods later;
   - no depth is submitted to the runtime;
   - moving-object MVs under the two-frame per-eye history have never been verified.

## Global constraints

- AGENTS.md:
  - keep the accepted locomotion and turning;
  - launch the headset with `-SkipPreflightWait` and run no headset-availability probes;
  - a loaded DLL is not proof of presentation.
- Never launch the game unless Steam ActiveUser is the user's own account (the id is kept out of the repo). Check the desktop first, because the user may be playing. Keep
  game takeovers short, and let only one agent control the game.
- Lab or sim first: every visual change shows images and metrics before the headset, and feedback requests carry pictures.
- Every new behaviour sits behind a `build/<knob>`, and defaults stay unchanged until the headset verdict. Each build gets a new
  DLL version, and nothing is rebuilt over a loaded DLL.
- No fix may disable FSR or DLSS in performance mode.
- Headset-agnostic (from `xrLocateViews` and `xrWaitFrame`). Fix general mechanisms only, never one object or action.
- Sim and lab results do not establish headset quality or comfort.

## Definition of done

1. **Left-eye defect:** with performance mode on and a static view, neither eye shows a double image in these modes: FSR3 Native,
   FSR3 Quality, FSR4 Native, FSR4 Quality, Game TAA (sim and headset), and DLSS Quality and DLAA (NVIDIA sim). The ghost metric
   of each eye in AER is within the paired margin.
2. **Whole mode** (after Phase 3), in the headset at the refresh rate chosen in Task 8:
   - no double image on head turns, walking past a guard, snap turns or sword swings;
   - static edges crawl no more than in paired mode;
   - as sharp as paired mode;
   - at least 20% more fps;
   - no more than 2% missed slots.

## Phase 1: the doubled left eye (the visible defect)

### Task 1: Reproduce in the sim (Windows host)

- [ ] Run sim scenario "still" (head and stick neutral) with knob `alternate-eye`, in these modes: AA Off, Game TAA, FSR3 Native,
      FSR4 Native and FSR4 Quality. Add a paired FSR4 Native control.
- [ ] Use the existing `xr-t<sec>-left/right.rgba` eye captures.
- [ ] Ghost metric per eye: in a central band, measure the left-right disparity d (best horizontal alignment, as in the
      2026-10-22 swapped-eye check). Then compare the secondary autocorrelation peak of each eye's high-pass image at lag d with
      the paired control.
- Output: whether the sim reproduces it, in which modes and in which eye, with montage PNGs.

### Task 2: Eye trace of the temporal passes (one diagnostic build)

- [ ] Extend `build/alt-pose-trace`. For each TAA/FSR/DLSS and post-tonemap pass of the single view, write:
  - the logical eye used;
  - the receipt's native counter;
  - the view stamp at pass time;
  - the receipt's display frame;
  - the FSR/DLSS context index dispatched.
- [ ] Extend `tools/alt_pose_trace_check.py` with two counts:
  - passes whose eye differs from the eye whose swapchain shows that image;
  - Presents whose two eyes' history contexts were dispatched for the same eye.
- [ ] Add a test that feeds the checker a mislabeled-pass fixture.

### Task 3: Discriminators (sim, same build)

- [ ] **AA Off against FSR.**
  - The double disappears with AA Off: temporal history (L1).
  - It remains: the image path (L2) or state (L3).
- [ ] **Parity flip, knob `build/alt-parity-flip`** (the right eye renders on even frames).
  - The double moves to the right eye: it follows the frame parity or timing.
  - It stays on the left: state tied to the left eye or slot 0 (L3).
- [ ] **Consecutive left frames.** For a burst of consecutive Presents, store the left swapchain image (Task 6 capture ring, or a
      minimal left-only ring if Task 6 is not built yet). With a static view, consecutive left images must match. If they
      alternate between two viewpoints, the cause is L2.

### Task 4: Headset confirmation (only if the sim does not reproduce it; about 5 minutes for the user)

- [ ] Run the Task 2 build with the trace on, performance mode on, FSR4 Native and a static view. Ask:
  - Is the left eye doubled? With one eye closed?
  - With AA Off?
  - With `alt-parity-flip`, which eye is doubled?
- [ ] Ask for a screenshot of the SteamVR settings: refresh, render resolution and Motion Smoothing.
- [ ] Analyse the trace with `alt_pose_trace_check.py`.
- [ ] If only the headset shows it: check SteamVR's handling of the left layer (L4) with a paired probe. Submit the left eye from
      the previous Present's image and pose (`build/probe-stale-left`) and see whether a static view doubles.

### Task 5: Fix and verify

- [ ] Fix the mechanism the evidence names, in the shared path for both eyes.
- [ ] Verify Definition of done item 1:
  - sim (Windows): FSR3/FSR4 Native and Quality, and Game TAA;
  - Linux laptop rig sim (NVIDIA): DLSS Quality and DLAA;
  - then a headset static-view check by the user.
- [ ] Add a regression check: the trace checker counts from Task 2 must be 0 in a sim run.

## Phase 2: the AER lab (for the motion artifacts)

### Task 6: Frame-ring capture

- [ ] Add knob `build/aer-capture` and command `aercap N` (in `artifacts/vr-command.txt`). For N consecutive gameplay Presents it
      stores:
  - both submitted eye images (centre crop or half resolution);
  - each layer's pose and FOV;
  - `displayTime` and the poses located at that time;
  - the fresh eye's depth and MVs;
  - each Setup's game anchor transform;
  - the fresh eye and the kept frames.
- [ ] Read back through an async staging ring and write after the burst. Measure that no frame stalls. It must work in the sim and
      the headset.

### Task 7: The offline lab, `tools/aer_lab.py`

- [ ] It computes:
  - the compositor model: each layer reprojected by rotation from its pose to the display pose;
  - stereo consistency: fresh-eye pixels unprojected with depth and reprojected into the other eye's seen image, giving an error
    map and the p95 misalignment in pixels;
  - the ghost metric (Task 1);
  - static shimmer: frame-to-frame high-pass energy;
  - MTF against AA-off paired;
  - eye age per refresh.
- [ ] It outputs montage PNGs per scenario.
- [ ] Validate it first: paired static gives near-zero error, and one known one-frame-stale case gives one frame of motion.

## Phase 3: the inherent AER artifacts (after Phase 1, in this order)

### Task 8: Pacing

- [ ] AER at 90, 80 and 72 Hz in SteamVR, with Motion Smoothing off and on. Record missed slots, period changes and eye-age
      histograms, plus the user's verdict on judder.
- [ ] If 90 Hz misses slots, hold it with the render scale (the governor on scaled FSR/DLSS modes) or choose 80 Hz.
- [ ] Control: paired at half rate with SteamVR Motion Smoothing. AER must look better to be worth keeping.

### Task 9: Temporal history on moving objects

- [ ] MV audit from captures. Expected results:
  - static: MV is about 0 after dejitter;
  - head yaw only: MV equals the same-eye two-frame camera motion;
  - a moving guard: about half the same-eye displacement, if previous object transforms are one frame old.
- [ ] Jitter-phase coverage per eye over 1000 frames.
- [ ] Candidate fix at the MV seam (`src/taa_mv_dejitter.hpp`, and the FSR/DLSS MV input): `MV' = 2*MV - MV_cam2`, where
      `MV_cam2` is the camera-only MV from depth and the same-eye previous matrices. Alternative: a reactive mask on object motion.
      Judge in the lab by trails, shimmer and MTF.

### Task 10: Cheap pose fixes

- [ ] Paired refresh after game-side heading or position jumps above a threshold: snap turn, blink, teleport, mantle.
- [ ] Stale-eye anchor compensation: `pose' = inverse(A_fresh) * A_stale * pose`.
- [ ] Locate each Setup for its own image's display time.
- Lab: stereo error under snap turns and walking, before and after.

### Task 11: Engine content that changes every frame

- [ ] Take a census (`native-temporal-census`), then A/B `r_skipInsideBodyDithering`, particles and
      `r_skipAutoExposureTemporalAdapatation` through `cvar` commands.
- [ ] Measure the static interocular difference for each.

### Task 12: Removing the time offset on motion (only if the headset still shows motion doubling)

- [ ] Find out whether SteamVR's OpenXR runtime uses `XR_KHR_composition_layer_depth` for reprojection. If yes, submit per-eye
      depth.
- [ ] Lab prototypes:
  - (a) stale-eye reprojection with depth, the camera delta and MVs, with holes filled from the fresh eye;
  - (b) synchronized AER: hold the world every other frame (UEVR Synchronized Sequential; the rival mod's held RunFrame). This
    needs the game tick / time-manager ABI.
- [ ] Port the winner.

## Phase 4: decision

- [ ] Fill the decision table: hypothesis, evidence, fix, measured gain. Choose the default from the headset verdict.
- **Kill criterion:** if Phase 1 cannot make the left eye clean with FSR/DLSS:
  - keep performance mode off and write down why;
  - move the effort to paired performance: the render-scale governor, the refresh rate, canted eyes.

## Review Focus

These failure modes go into every test:

1. Both eyes and every temporal mode. A fix that cleans the left eye must not move the defect to the right eye or to another
   mode (Quality vs Native, FSR3 vs FSR4 vs DLSS).
2. AER/paired transitions (menus, loading, pause, cutscenes, takedowns) and the first frames after them: history ownership and
   resets.
3. Rates the sim never reached: 45-90 fps with missed slots. The pass-to-image eye pairing must hold there too.
4. The mod-driven hands and held weapons (replaced palettes have no engine MVs) under temporal AA in AER.
5. Snap turn, blink and teleport: one-frame pose jumps and the paired refresh after them.

Parallel work: Task 1 and Tasks 2-3 share one game session; only one agent drives the game. Tasks 6-7 (lab) and the SteamVR
reading in Task 12 are desk work that can run alongside.

## Results

(append per task: date, build, numbers, images, verdict)

### 2026-11-01, v790 headset run 1 (12:54-12:59, SteamVR, RX 9070 XT, 90 Hz)

**Correction from the user after the run:** the doubled eye is the RIGHT eye, not the left. The left eye was normal. Read every
"left eye" in this plan's hypotheses as "right eye". The right-eye asymmetries are:
- native TAA: twin history for eye 1;
- the right eye's upscaler start;
- possibly the runtime's handling of the right layer.

This matches the v636 report.

- **Pairing is correct at headset rates.** `alt_pose_trace_check.py` passed on 8705 one-eye Presents and 3311 paired Presents with 0
  mismatches. All 25,995 temporal passes (post-tonemap and FSR4) ran as the eye whose image was shown, split evenly between the eyes.
  So L1 (history fed through mislabeled passes) and L2 (image routing) are ruled out at the mod level.
- **Band bursts.** Each eye's stale image repeats exactly (match 1.00). The 2D autocorrelation finds no distinct ghost copy in either
  eye's submitted image (bursts 1-2). The defect is then probably added after submission (runtime), or it is too faint for the metric.
  This is still open.
- **Pacing.**

  | Mode | AA | Present interval | Missed slots | SteamVR period |
  |---|---|---|---|---|
  | AER | FSR4 Quality | p50 13.4 ms | 20% | alternates between 11.1 and 22.2 ms |
  | AER | FSR4 Native | p50 11.1 ms | 0.7-1.4% | 90 Hz held |
  | paired | FSR4 Native | p50 16.3-20.2 ms | 23-32% | 22.2 ms (45 Hz) |

  AER saves about a third of the frame here. SteamVR's half rate and Motion Smoothing during missed slots are a prime suspect for a
  doubled eye: the 45 Hz pairing of AER frames gives one eye a constant role.
- **The mode switched itself off after 2.5 minutes** (`alternate-eye: the engine view count disagreed with the plan for 30 frames`).
  Root cause: the user opened the weapon wheel (logic mask 256, `gameplay` false).
  - `alternate_after_present` then plans a paired frame, but it switches the descriptor only `if(gameplay)`.
  - The engine keeps rendering the single view, so every Setup is a mismatch and is dropped (31 unsubmitted Presents).
  - After 30 frames the session falls back for good.

  Every wheel or menu opened during AER therefore ends performance mode. This is a definite bug, separate from the doubling. The v636
  "first minutes" fallback is most likely the same cause.
- Data: `artifacts/claude/perf-mode/v790-run1/` (trace, bands, frame timing, status).

### 2026-11-01, v791 headset run 2 (13:07-13:19)

v791 = v790 + `alternate_install_descriptor`: a paired plan installs the paired descriptor outside gameplay too.

- **The wheel fix holds in the headset.** The weapon wheel was opened several times (151 logic-256 menu rows) and there were 0
  fallbacks. After each wheel or menu, AER resumed after 2-3 paired frames.
- **Parity flip (`altparity flip` at 13:10:18): the user still saw the RIGHT eye doubled.** The doubling is tied to the right eye, not
  to the frame parity.
- **The mod output is clean again.**
  - 12,299 one-eye Presents with 0 pose/image mismatches.
  - All 36,755 temporal passes (native TAA, FSR4, post-tonemap) ran as the eye shown.
  - The left and right band autocorrelation profiles match within 0.03 in all 3 bursts, before and after the flip.
  - Full-resolution bands show no doubled edges in the right eye.
- **The OpenXR runtime is Virtual Desktop (VDXR)**: HKLM ActiveRuntime = `virtualdesktop-openxr.json`. SteamVR processes ran alongside
  it, but they are not the game's runtime.
  - VDXR's source does not derive the right eye from the left. Each `RenderPose` comes from its own submitted view.
  - `overrideIpd` acts only when VD's `world_scale` is not 100. It moves both positions symmetrically and never changes orientation.
  - So a right-eye-only defect would come from the closed parts: the VD streamer, the headset's reprojection, or SSW. A pipeline that
    reprojects both eyes from one pose (the left's or the head's) would misplace only the right eye whenever the two eyes were rendered
    at different times. That fits every observation: right only, independent of parity, AER only, and absent from the submitted images.
- Pacing:
  - AER with FSR4 Quality 0.77 ran at p50 11.1-11.6 ms with 0.2-7% missed slots.
  - The period dropped to 22.2 or 33.3 ms in heavy moments.
  - The status fraction flips between 0.77 and 1.0 when menus open (menus render at full resolution).
- From 13:12:10 to the exit the status froze: no Presents, probably the headset taken off.
- Data: `artifacts/claude/perf-mode/v791-run2/`.

### 2026-11-01, user description and the v792 fix

- **The user's runtime is SteamVR with Steam Link, not Virtual Desktop.** The system ActiveRuntime still names VDXR, which is why a
  plain steam:// launch stayed flat with -35. Virtual Desktop must keep working too.
- **The user's description:**
  - standing still, the image is normal;
  - when the camera moves, there is strong doubling in the right eye;
  - "the frame lags, catches up, lags again", like astigmatism.
- **Mechanism, consistent with every observation:** the runtime reprojects both eyes from one pose, view 0's.
  - The left eye is always reprojected from its own pose, so it is always right.
  - The right image, rendered at a different time, is placed as if rendered at the left pose plus the IPD: +delta on one frame,
    -delta on the next. That is a double image only while the head moves, independent of the parity.
- **Fix v792** (`src/stale_eye_warp.hpp`; knob `build/no-stale-eye-warp`):
  - The Setup of a one-view frame also records the other eye's view at that Setup (`PendingViews::record_companion`): the same
    head pose, that eye's own render FOV and cant.
  - At the Present that shows the image, the stale eye's kept image is warped to that view: rotation homography, Catmull-Rom with
    nine bilinear taps, sRGB views.
  - Its layer carries that view, so both layers have one head pose on any runtime.
  - The reticle is moved with the inverse map.
- **Lab:** `tests/stale_eye_warp_check.cpp`.
  - Math: identity, yaw, pitch, relative rotation, round trip, behind the camera, invalid inputs.
  - WARP GPU: an identity warp copies the image within 1 LSB. A turned view moves a line to the CPU-predicted column 148 on both
    read paths, at full brightness.
- **Checks:** `alt_pose_trace_check` accepts a stale layer only if it equals the companion or the kept view. The routing checks pin
  the wiring.
- **Status:** `alternate_eye.stale_warp`, `stale_warps`, `stale_warp_failures`.
- Not tested in the headset yet. Launcher: `Downloads\DishonoredVRLauncher-v792.exe`.

### 2026-11-01, v793 headset run 3 (15:18-15:34) and v794

v793 = the shared tree at 14:26 (dual-wield v781/v782, weapons v783) + v790-v792, with dxgi_probe.cpp merged hunk by hunk.

- **The warp works.** The trace showed 0 mismatches over 69,854 one-eye Presents. Every stale layer carried its companion view (34,926 /
  34,928 warped), there were 0 warp failures and 0 fallbacks.
- **The user's verdict:** "much better, the doubling is practically gone". A slight delay and doubling with mild motion sickness
  remain, and switching the FSR modes made no visible difference.
- **FSR/DLSS were in fact off from 15:21:04** (`aa 0 (req 5)`): "Alternate temporal ownership failed".
  - Cause: an authored scene began. The Setup that had planned its eye's temporal state returned early (its frame is dropped).
  - The glue (`view_setup_hook`) then called `fail()`, which latches spatial AA for the session, although nothing had been written to
    the view (`seed()` never ran).
- **v794 fixes:**
  - The plan is only cancelled (`taa_plan_cancels`). Real failures name their site: `alternate_native_taa.hpp` sites 1-14, glue 21,
    `taa_failure_site`, and the recovery log.
  - Views and hands are located for their image's display, two periods ahead instead of one (`locate_periods`; knob
    `build/legacy-locate-horizon`).
- Launcher: `Downloads\DishonoredVRLauncher-v794.exe`.
- **Next if motion sickness remains:** depth-based translation (parallax) in the stale-eye warp, covering stick locomotion and head
  translation. Rotation alone leaves the stale eye 2-5 cm behind while walking.

### 2026-11-01, v794 headset run 4 (15:46-15:53) and the merge

- **FSR4 Native stayed on for the whole session** (`aa 5 (req 5)`), and temporal ownership did not fail (`taa_failure_site 0`). The
  situation that used to end it happened 13 times (`taa_plan_cancels 13`); each time the plan was cancelled.
- **Trace clean.**
  - 23,947 one-eye Presents with 0 mismatches.
  - All 71,698 temporal passes (FSR4 upscaler and post-tonemap, both eyes) ran as the eye shown.
  - Every stale layer was warped (23,954 warps, 0 failures).
  - 0 fallbacks.
- **Pacing:** mostly 90 Hz (p50 11.1-11.4 ms). In the last minute it ran at p50 24.5 ms with 22% missed (a heavy stretch before the
  exit).
- `locate_periods` 2 is active. The user's verdict on v794 comfort is pending.
- **Merge (15:48):** the v790-v794 perf changes are now in the shared src/tests/tools, with the weapons session's v784 edits kept.
  - v795 = a snapshot of the shared tree at 15:48:59.
  - It builds, and 16 suites pass.
  - Earlier launchers built from the shared tree (weapons v784) lacked the perf fixes.
- Data: `artifacts/claude/perf-mode/v794-run4/`.

### 2026-11-01, user verdict on v794 and the v796 parallax warp

- **User (v794, higher resolution, ~22 ms a frame):** looking around is fine. While walking, the right eye visibly lags and doubles.
- **Cause:** the rotation warp ignores translation. The stale eye's camera is one frame behind: at 4 m/s and 22 ms that is 9 cm. A wall
  2 m away then shifts 2.5 degrees, about 50 px. Smooth turning (an anchor turn) was not warped either: the rotation came from tracking
  space only.
- **Depth evidence:** `artifacts/fsr-depth-left.r32` (an FSR depth dump, 2496x2688).
  - World pixels: device depth 0.0005-0.01. With near = 0.01 m (`m11`), that is 1 m to 20 m.
  - Hands: 0.10-0.20, in the engine's own near range.
  - Nothing falls between 0.01 and 0.03, so the FSR reactive threshold of 0.02 separates hands/body from world.
- **v796 (`src/stale_eye_warp.hpp`):**
  - The probe records each view's game camera (position, basis, projection `m10`/`m11`) and its companion's, using the same
    `to_game_pose` as the view written to the engine. It skips authored cameras.
  - At the Present, GameXr gets the kept image's camera, the stale eye's target camera and that eye's FSR/DLSS depth copy
    (`FsrTaa::eye_depth`).
  - The pass first maps each target pixel by the tracking-space rotation. If the kept depth there is above 0.02 (hands, body), it keeps
    that result. Otherwise it starts from the game-space rotation (smooth turns included) and runs 4 fixed-point steps: read the kept
    depth, then put the point at that distance on the target ray.
  - The sky (depth 0) stays at infinity.
  - The reticle moves at its own distance (`EyeReticle::depth`).
  - Without FSR/DLSS there is no depth copy, so the warp stays rotation-only (counted as `stale_parallax_misses`).
  - Knob `build/no-stale-eye-parallax`. Status: `stale_parallax_warps`, `stale_parallax_misses`.
- **Lab (`tests/stale_eye_warp_check.cpp`, WARP):**
  - A 4 cm step moves the 1 m line 5 px and the 10 m line half a pixel, as computed.
  - With hand depth the line stays put.
  - A smooth turn turns the sky but not the hands.
  - A floor 1.6 m below while sprinting 30 cm in one frame: the line lands on row 117 against an analytic 117.3.
  - A non-R32 depth is refused.
- **Known limits:**
  - Disocclusions at object edges keep the last estimate, a few pixels of smear while walking.
  - World behind a hand keeps the infinity estimate.
  - Moving NPCs are not extrapolated.
  - A world surface closer than 0.5 m (device depth above 0.02) warps like a hand.
- **Build and merge:**
  - v796 = the shared tree at 16:24 (weapons v785/v786 included) plus these hunks. 27 suites pass (22 C++, 5 Python), with BUILD OK.
  - Launcher: `Downloads\DishonoredVRLauncher-v796.exe`.
  - Merged into the shared src/tests at ~16:30 (whole files, plus the probe rebased hunk by hunk).
- **v796 headset run 5 (16:30-16:36):**
  - Parallax on 23,252 of 23,409 stale-eye warps. The 157 misses came in the load burst (147), then one after each switch to paired
    frames.
  - 0 warp failures, 0 fallbacks, FSR4 on all session.
  - Pace mostly 90 Hz (p50 11.1-12.9 ms). Heavier stretches ran at p50 14.5 ms, with SteamVR at 45 Hz.
  - Trace: 23,664 one-eye Presents with 0 mismatches. 70,847 temporal passes, all on the eye shown.
  - Data: `artifacts/claude/perf-mode/v796-run5/`.
  - **User verdict:** "walking works much better, very well overall". What remains: the hands double, and so does everything in them
    (weapons, a guard's body being carried).

### 2026-11-01, v797: per-hand layers

- **Cause:** the launcher's "Hand and prop depth repair" (`world-hand-depth`, on by default) sets `r_playerDepthHack` 0. Hands, held
  weapons and carried props then draw at world depth.
  - The 0.10-0.20 hand range in the Oct 12 dump was the depth hack at 5.
  - So v796 warped every hand, weapon or body part beyond 0.5 m as static scenery, and while walking it moved them wrongly.
  - Nothing ever followed the hand's own motion between the two frames.
- **v797 (`src/stale_eye_warp.hpp` StaleLayers):** each kept pixel belongs to whatever moves it.
  - **Hand:** two capsules around the drawn grip.
    - Hand and forearm: from 0.28 m behind the grip to 0.06 m ahead, 0.11 m round.
    - Weapon: from the collision shape. The sword is 0.65 m along the blade axis; the pistol and crossbow use their gun shapes.
    - A grab or held object: 0.45 m round the grip.
    - The hand moves with its drawn grip (kept Setup to target Setup), solved like the scene from the target camera carried back by
      that motion.
  - **Player:** anything nearer than 0.5 m, or in the body capsule under the head (radius 0.25 m, from the neck to above the floor).
    It is warped in tracking space (full 6DOF).
  - **Scene:** everything else, as in v796.
  - **Per pixel:** the nearest layer whose solution lands on its own pixels wins. Hands are solved only inside their target-space
    rectangles. Uncovered pixels stretch the nearest scenery (searched out to 3.6% of the image).
- **Data:** the probe resets `warp_hands` every displayed frame. It fills it just before `arm_tracking.publish(hands)` and records it
  with both cameras. Status: `stale_hand_layers`.
- **Lab:**
  - A hand moving 3 cm moves its line 106 to 112 (computed 112), and the scenery line stays.
  - Walking 20 cm with the hand carried keeps the hand line, while the scenery line moves 192 to 199 (computed 199).
  - All v796 cases still pass.

### 2026-11-01, v797 headset run 6 (17:07-17:14) and v798: near content from the fresh eye

- **Counters:** layers on 27,168 of 27,217 warps, both hand layers every warp, 0 failures, 0 fallbacks, FSR on.
- **User:** "the hands and everything in them still double, and the pixels around them look a bit wrong".
- **Band bursts (`tools`-style analysis, scratchpad band_steps.py), each warped frame against its eye's fresh frames before and
  after:**
  - The wall in a fast turn moved -29..-45 px per Present, split evenly between the two steps (-34|-41, -44|-29): the scene layer
    works.
  - A carried guard's body held right in front of the eye moved 20-45+ px between fresh frames. The warped frame moved it 0-10 px,
    a two-frame staircase.
  - So it was warped as the player's own (near zone), while it swings on its physics tow.
  - What moves near the eye cannot be told from the kept image and known poses.
- **v798:** near content (nearer than `near_range` = 2 m, knob `build/stale-near-range`; 0 = v796) comes from the fresh eye's image of
  this same Present, moved to this eye by the fresh FSR/DLSS depth. Hands, weapons, towed bodies and NPCs are then current by
  construction.
  - GameXr processes the fresh eye first; its kept image is the source.
  - Per pixel: probe the ray in the fresh image at 0.4 m, 0.9 m and infinity, and solve (fixed point, 5 steps) only from probes that
    land near; the front-most converged one wins.
  - The kept image fills what only this eye sees beside near objects, and gives its far scenery where it held something near.
  - The far scene stays the v796 parallax.
  - The v797 hand capsules and player layer are removed.
- **GPU cost, real RX 9070 XT, 2709x2898:** rotation 0.16 ms, scene layer 0.24-0.27 ms, scene + fresh 0.51 ms. Unprobed (three solves
  every pixel), it was 0.9-1.3 ms.
- **Lab:** two eyes 6.4 cm apart, the hand 5 cm further left in the kept frame.
  - The output hand line is at 119 (computed 119.3); the kept image had it at 106.5.
  - Where the hand was, and beside it, shows wall.
- **Diagnostic (alt-pose-trace sessions):** up to 8 `stale-warp-N-*` dumps (crops of kept, warped and fresh images, both depths,
  layers JSON).

### 2026-11-01, v798 headset run 7 (17:45-) and v799: the near search

- **Counters:** the fresh layer was on for every layered warp (31,256 by 17:54), 0 refusals, 0 failures, 0 fallbacks.
- **Dumps** (`artifacts/claude/perf-mode/v798-run7`; replayed offline through the current shader on WARP by
  `build/v798src/out/bench/replay.cpp`):
  - A hand gripping a sword hilt ~0.3 m from the eye came out of v798 torn into vertical stripes.
  - Cause: it is nearer than the nearest fixed-point start (0.4 m), and on slanted fingers the fixed point converges too slowly for a
    1 px test (contraction about 0.7 per step). Stripe by stripe it fell back to the kept image.
- **v799:** the near search marches the ray in the fresh image in inverse distance, ten even steps from 0.2 m to 1.25 near ranges,
  and bisects the first crossing five times.
  - The crossing counts only when the surface is there on both sides of it: |ray - surface| < 2 cm + 3%, and just in front of it too.
  - Without the front check, a background ray passing just behind a near edge was taken, and that duplicated the hand beside itself.
  - Where the kept image held something near and the fresh eye now sees far along the ray, the fresh eye's far view is used (it moved
    away); the far-scenery stretch is dropped.
- **Lab:** a tilted hand 0.28 m away, 4 cm further left a frame ago, lands whole (44 of 44 columns, line at 113 vs 112.9).
- **Replay of dump 2 (two open hands):** the left hand, fully inside the crop for both eyes, comes out clean, where v798 broke its
  fingers.
- The v798 diagnostic's fresh crop was the target's box, so near content's match (up to ~300 px aside) fell outside it. v799 saves the
  fresh image and depth ±400 px wider (`fresh_box`).

### 2026-11-01, v800 headset run 8 (18:22-18:28) and v801: no frame-old hands in the rebuilt eye

- **Counters:** 24,922 of 25,031 warps with the fresh layer, 0 failures, 90 Hz (p50 11.6 ms), FSR4 on.
- **User:** "double silhouettes of the hands, very noticeable, and of what I hold, in the left eye and in the right eye"; "the same with
  the upper body"; guards' edges shimmer slightly; the distance is a little soft (acceptable).
- **Each eye alternates** between its own render and a frame rebuilt from the other eye, so anything the rebuilt frame gets wrong
  flickers at 45 Hz in both eyes.
- **Replay with the pixel source coloured** (`artifacts/claude/perf-mode/lab-v801/`, README there; WARP, the shared shader):
  - It matches the in-game output to under 1 level on average.
  - Where the fresh eye has no near hit but the kept image held something near, v799 fell back to the kept image (8-11% of a hand
    crop).
  - In dump 7 (walking, the left hand ~0.4-0.6 m away), that fallback showed slivers of the frame-old fingers between and beside the
    fingers. The kept hand was wrong twice over: a frame old, and moved ~50 px by the walk's scene parallax, which treats it as
    scenery.
- **v801 (`src/stale_eye_warp.hpp`):** the kept image's near point on the ray is projected into the fresh image.
  - Same depth there: the fresh pixel (the same surface, now).
  - The fresh eye sees past it (it moved away): the fresh far solve, else the scenery a few pixels in front of the near edge the ray
    passes behind (`NearHit` now returns 1 / 0 / -1 and that edge pixel).
  - Hidden behind something nearer: the kept image, as before.
- **Lab:** the "moved hand" case (`tests/stale_eye_warp_check.cpp`; the hand 6 cm further right a frame ago, so its old place lies
  where only this eye sees) shows the old hand in 14 of 14 columns on v799 and in 0 on v801. All earlier cases pass.
- **Still open:**
  - The kept strip beside a near object keeps its frame-old shading (a moved hand's shadow): a faint tone edge in dump 2.
  - Fast fingertips are already translucent in the fresh FSR render (dump 3). Each eye's history is 22 ms old in this mode.
  - The v800 band bursts are unusable: with the right eye fresh, eye 1 is captured first, the seq desyncs, and half the left files
    are overwritten.
- **Cutscenes (user: "in performance mode the cutscenes all render in theater mode"):** `cutscene_paired_path()` refused
  `mono_view_state` 1. Alternate-eye already plans every frame outside free gameplay as a pair, so v801 lets it through.
  - `tests/cutscene_routing_check.py` was updated for this.
  - That check already failed at a later, unrelated line, because of another session's `game_xr.hpp` edit.
  - Needs a headset cutscene to confirm.

### 2026-11-01, v801 headset run 9 (19:15-19:18) and v802: this eye's own frame first

- **Run 9 (`artifacts/claude/perf-mode/v801-run9`):** 13,585 one-eye frames, 0 fallbacks, 0 warp failures, 90 Hz.
  - Cutscene fix confirmed: 847 immersive frames, 0 theater.
- **User:** "nothing is fixed, exactly the same double hand silhouettes; the guards' and the distant edges are blurry, the picture is not
  sharp".
- **Dump 6 (still hands):** the rebuilt frame draws a thin finger outline on the floor beside each finger, which the eye's own frame
  does not have.
  - It is the seam where the strip filled from the kept image meets the floor taken from the other eye.
  - The other eye sees the hand from 6 cm aside and shades surfaces a little differently. Its pixels next to its own hand silhouette
    carry the hand's anti-aliased colour.
  - With each eye alternating between its own frame and the rebuilt one, any such difference flickers at 45 Hz as a second outline.
  - Own and rebuilt frames do not differ in overall brightness (band means within 1-2%).
- **v802:** the rebuilt frame is the eye's own previous frame (v796 parallax) wherever it can still be right:
  - everything beyond reach (1.5 m);
  - within reach too while the drawn hands held still (`stale_hands_still`: 4 mm and 2 degrees between the kept and target Setups,
    from the drawn grips now recorded with each Setup's camera).
  - Only moving hands, what they hold or carry, and what their old place uncovers come from the fresh eye (the v801 logic, limited to
    within reach).
- **Tried in the lab replay and rejected:**
  - A per-pixel colour gate: patchwork and translucent half-blends inside hands.
  - A rotation-only own-frame candidate: doubled fingers when the hand also moved.
  - A depth-shape check: it fires on every static silhouette.
- **Lab:** a still hand with the other eye 30 levels brighter is this eye's own frame in 256 of 256 columns. The moving case still
  takes the hand from the fresh eye. All earlier cases pass.
- **Replay of the v801 dumps with still hands:** mean difference from the eye's own frame 1.44 levels, against 3.16 in game.
- **Trade-off:** moving NPCs lag one frame in the rebuilt eye again, as in v796.
- **Diagnostic:** dumps now also save the eye's own next frame (`-next.rgba`) and `near_from_kept`, so replays can be scored against
  what the eye really showed.

### 2026-11-01, v802 headset run 10 (19:54-19:59) and v803: sharp rebuilt frames, thin objects found

- **Run 10:** 21,431 one-eye frames, 0 fallbacks, 90 Hz. 8 dumps, each with the eye's own next frame
  (`artifacts/claude/perf-mode/v802-run10`).
- **User:** "better, sharper when still, but it still doubles, strongly in motion; far away it is much less sharp than on the
  monitor; moving guards in the distance are blurry; the sword turns a little when I turn my head".
- **Scoring against the real neighbours** (flicker F = (|R-K| + |R-X| - |X-K|)/2, where K and X are the eye's own frames before
  and after; mean over the 8 dumps):
  - v802 in game: 2.54;
  - v801 logic on the same inputs: 2.96;
  - own frame only: 2.51;
  - still hands (dumps 2 and 6): about half of v801.
- **Band bursts:** every rebuilt frame had 11-16% less fine detail (mean Laplacian) than the eye's own frames around it. The
  monitor shows only own frames.
  - v803: the warp's sub-pixel sampling adds back 1.5 times what the cubic gained over a bilinear tap, clamped to the 2x2
    pixels around the sample.
  - Unclamped, it rang beside bright thin lines.
  - Replayed v802 dumps: 0.997 of the own frames' detail (was 0.848).
- **Dump 5 (walking, sword):** the blade, seen edge-on, vanished from the rebuilt frame, because the ten-step near search moved
  26 px a step. v803 takes 32 steps from 0.25 m to 1.25 reaches (about 6 px a step), and the blade is there in the replay.
- **Checked, not the cause:** each eye already has its own FSR jitter phase and previous state (alternate_taa_runtime).
- **Still open:**
  - Moving NPCs lag one frame in rebuilt frames (v796 behaviour).
  - The still-hands threshold (4 mm, 2 degrees) is too strict: dump 7 would have scored 0.57 with the own frame instead of
    3.27. Dumps now record `hands_moved_m` and `hands_turned_deg` to calibrate it.
  - The sword turning with the head.

### 2026-11-01, v803 headset run 11 (20:29-) and v804: walking hands, the sword, far content now

- **Run 11:** 0 fallbacks, 90 Hz, cutscenes immersive (834 frames). 8 dumps with the eye's next frame
  (`artifacts/claude/perf-mode/v803-run11`).
- **User:** "better; now the hands double only while walking; the sword still moves when I move the camera; guards and people in
  the distance twitch and hop a little; their lines and those of some objects are still unclear; the distance is still blurry".
- **Walking hands (dumps 5 and 6):** the walk carries the head and the hands together (5.7 cm in dump 5). A strip of wall up to
  35 px wide beside each finger's trailing edge was behind that finger in the kept frame. On those rays the kept fixed-point search
  has no solution. It alternates between the wall (1.8 m) and the old finger (0.45 m), and after five steps half the rows end on
  the finger: a second hand, classed "kept, beyond reach" because the last depth read was the wall (lab `solvetrace.py`).
  - v804: where the kept search does not settle, the fresh eye's search answers. Where that eye's hand hides the wall too (most of
    dump 5's strip: both eyes blind there), the scenery beside the near edge fills it. The edge sample moved from about 4 px to
    15 px past the crossing; at 4 px it caught the finger's dark rim on alternate rows (stripes).
  - Ghost pixels (skin in the rebuilt frame, in neither own frame): dump 5 11,217 -> 1,978; dump 6 2,617 -> 783. Static dumps
    replayed in their real still-hands mode: 0 pixels change (3, 4, 7), 762 in dump 8, same score.
  - Lab case "walking hand" (tests/stale_eye_warp_check.cpp): head and hand carried 15 cm together in front of a 1.8 m wall.
    It fails on v803 (6 columns of hand beside the trailing edge) and passes on v804 in both directions.
  - What stays: in the strip neither eye saw, a smear of the wall beside it instead of the wall itself.
- **Sword turning with the head** (helper analysis, `artifacts/claude/sword-camera/`):
  - Head-follow (NativeTurning) turns the native heading by the unfollowed head yaw on physics steps. BodyAnchor takes the same
    turn off the room anchor, so the camera and the controllers keep their pose.
  - The arm upload's root-motion carry (`full_body_driver.hpp` `anchored()`) rotated the solved hands by the whole root turn
    between publish and upload, head-follow included. The drawn sword swung about the player by that step's turn (lab: 0.7
    degrees -> grip 5.7 mm, blade tip 13.5 mm).
  - v804 leaves the head-follow part out of the carry, clamped to the root's own same-sign turn. Stick and snap turns are still
    carried. Check: tests/head_follow_carry_check.cpp (5 of 6 cases fail before the fix).
  - Not observed yet: that the body frame (+0x1860) turns inside the tick. The sword grip trace (build/sword-grip-trace) now logs
    `root_turn_deg` and `follow_deg`.
- **Distant guards hopping:** from the kept frame a walking guard shows a frame old, so each eye showed it at N-1, N-1, N+1, N+1.
  v804: beyond 2 m, the fresh eye's view of now, wherever its search settles.
- **Distance blur, measured:**
  - The rebuilt frames are registered to the fresh eye's view of the target pose within 0.01-0.35 px for static scenery in
    every v802/v803 dump (`regfresh.py`). That includes dumps where they sit 3.5-8.9 px off the midpoint of the own frames
    around them: that is the head's real acceleration, and each layer carries its own pose.
  - Both warp inputs are taken after sharpening.
  - The run renders 2432x2600 per eye with FSR4 native AA, sharpening at Max and mip bias 0.
  - So the remaining softness is the per-eye resolution (Resolution 105%), not the rebuilt frames. The Resolution setting is the
    lever, if the GPU holds 90 Hz.
- **Dumps:** every other dump is now the view centre (guards), the others the lower middle (hands).

### 2026-11-01, v804 headset run 12 (21:1x) and v805: this eye's own shading

- **User:** "better; in motion the pixels are much more stable, less ghosting. But big shadows behind the fingers and the sword, shown
  as white lines or dark hand silhouettes behind the sleeves; door frames and some railings double, which is new".
- **Cause** (path-coloured replays of the 8 run-12 dumps, `artifacts/claude/perf-mode/v804-run12`; replay vs in-game mean 0.2
  levels): the v804 far rule drew 50-76% of every rebuilt frame from the other eye. That eye's image is 10-20% brighter or darker
  than this eye's own frames (linear light, per scene; each eye's own frames agree with each other), and up to 3x off beside
  structures.
  - Beside each hand the other eye's view is blocked by its own hand, so there the kept image stayed: a dark (or light) hand-shaped
    hole in a brighter (or darker) frame (dump 3).
  - Thin far edges came half from each eye: the doorway's edge doubled (dump 4).
  - The walking strips, filled from the other eye in v804, were 7-26% off in brightness: a band behind the fingers.
- **v805:**
  - Beyond reach the rebuilt frame is the kept frame again, hands still or moving. The still-hands unsettled rule (other eye) is
    gone too.
  - What a moving hand uncovers (its old place, the walking strip, a spot gone in the fresh view) comes from `KeptBeside`: the kept
    pixel where the ray's infinity lands, if the kept depth there is beyond reach. Walking carries the hands with the head, so that
    is the scenery beside the hand, in this eye's own shading. Otherwise the fresh eye as before.
  - Run-12 dumps against the eye's real frames: mean flicker 2.05 -> 1.15 (dump 3 2.25 -> 0.68, dump 4 3.59 -> 2.78, dump 8
    2.89 -> 0.39).
  - v803 walking dumps: second-hand pixels as in v804 (1,973 and 785); flicker 5.25 -> 4.97 and 4.85 -> 4.64.
  - A march to the first pixel off the old edge gave streaks; the infinity point gives the wall's own texture.
- **Checks:** "far guard" now requires the rebuilt row to equal this eye's own frame, hands still or moving, with the other eye
  30 levels brighter (v804 fails: 253 of 256 columns). "Walking hand" requires this eye's own wall in the strip, with the other eye
  60 levels brighter.
- **Cost:** walking guards far away lag a frame again (two-frame steps). Fixing that needs per-object motion in this eye's own
  image (the game's motion vectors), not the other eye's colour.
- **Lesson:** every pixel taken from the other eye carries that eye's shading. Within one eye's 45 Hz alternation of own and rebuilt
  frames it shows as a band, a hole or a flicker. Use the other eye only for what this eye's frames cannot have: a moving hand as it
  is now.

### 2026-11-01, why the distance is sharp on the monitor and soft in the headset

The monitor shows the same eye image scaled down; the headset magnifies it. Measured along the chain (run 11/12 logs and configs):

- **Eye image:** 2432x2600 over a symmetric +-54 x +-55 degree frustum (the engine needs centred projections). Quest 3's real eye
  view is 54 out, 40 in, 44 up and 55 down, so about a third of the pixels lie outside it.
  - Centre density: 15.4 px/deg.
  - SteamVR's own target at the user's 126% is 18.2 px/deg (recommended 2316x2476 = panel x sqrt(1.26), for the asymmetric view).
  - The panel shows about 25 px/deg.
  - So the headset scales the image's centre up about 1.6x, while the monitor scales it down.
- **Unused levers in the mod:**
  - `--optical-density` keeps SteamVR's density on the visible part (+24% width, +19% height). It was dropped in October after a
    black image on re-entering VR.
  - `build/cant` (canted eyes, yaw) puts all pixels inside the lens view: 19.8 px/deg at the same pixel count. It has never been
    tried in the headset.
- **Steam Link** (`Steam\config\steamvr.vrsettings`, `driver_vrlink`):
  - `automaticStreamFormatWidth` false and `streamFormatWidth` 1344, while the driver's automatic choice would be 1280
    (`driver_vrlink.txt`).
  - Encoder 2688x2688 HEVC 10-bit for both eyes, `targetBandwidth` 345 Mbit fixed.
  - The driver's own sharpening is off (`useSharpening` false, strength 0.2 in its default.vrsettings).
  - A video stream about 1344 px wide per eye (foveated) is a downscale plus compression the monitor never shows. Users report
    Steam Link softness from it.
- **Not the cause** (measured): the rebuilt frames' registration (0.01-0.35 px) and detail, the sharpening order, the FSR4 history.
- **v806 (user: "Yes, build v806"): canted eyes by default** (`build/no-cant` turns them off). `canted_fov` for Quest 3 turns each eye
  6.65 degrees outward and 5.37 down, with a +-49.8 x +-52.5 degree frustum. Canting forces the optical-density sizing, so each eye is
  sized to SteamVR's density.
  - At Resolution 105%: 2600x2833 (7.4 MP, +17% pixels) for 19.2 x 19.0 px/deg at the centre (was 15.4 x 15.9).
  - At 100%: 2476x2698 (+6%) for 18.3 x 18.1.
  - Check: `fresh_math` maps the fresh eye's point through two canted views exactly. The warp, the companion record and the layer
    submission carry each eye's turned pose. `canted_fov_check` joins the focused tests.
  - Not yet seen in the headset: frame time at +17% pixels, the desktop mirror (the right eye's turned view), the October black image
    on re-entering VR with optical density, and the outer 13 degrees, where the two canted eyes no longer overlap (no fresh near
    content there).

### 2026-11-01, v806 headset run 13 (21:41-21:48) and v807

- **User:**
  - "Clearly sharper; motion stable, everything else fixed. Big progress."
  - "But the eyes felt turned strongly toward the centre: strong visual discomfort."
  - "The hands jump about when I turn with the right stick."
  - "In the last minutes the hands jumped, drew at wrong moments and showed through at the shoulder."
  - "The mod must not be tuned for Quest 3; it must work on any headset."
- **Canting, checked in the run's logs:**
  - Both layers carried the canted +-49.8 x +-52.5 FOV, 13.3 degrees apart.
  - The game camera and the layer pose come from the same turned quaternion (`rendered_orientation`; game camera line 7510).
  - The other-eye layer of the dumps is consistent with the turn.
  - So the mod's side was consistent. The pull toward the centre is what turned images shown unturned would do (crossed disparity),
    but whether the runtime or the stream did that, or the game's screen-space effects differ between turned eyes, is not
    established.
  - v807: off by default again (`build/cant`).
  - Next idea: render canted, then resample into the located asymmetric view before submission, so the runtime sees ordinary layers.
- **Snap turns:** the user snap-turns 45 degrees (about 130 in the run).
  - The eye rebuilt right after a snap had half its view beyond the edge of (or behind) its kept frame. The clamped edge pixels
    smeared across it, and the hands with them. v804's far rule had hidden this by drawing far content from the fresh eye.
  - v807: wherever the kept frame has no pixel for the ray (outside it or behind it), the fresh eye (near search, then its far
    solve). `KeptBeside` also refuses points outside the kept image.
  - Lab `gpu_turn`: a 30 degree snap. Beyond the kept edge 0 of 91 columns are wrong (v806: 63). Inside, 0 of 159 stray from this
    eye's own frame.
  - The 24 run-11/12/13 dumps change in 0 pixels: the crops never reach the image edge.
- **Late hand problems:** not diagnosed. The 8 dumps were all taken in the first minute. The physical hand bodies were created and
  destroyed several times in the last minute (`hand-body-events.csv`, the wheel open in between). v807 takes 12 dumps: 4 early, then
  one every 30 s.

### 2026-11-01, v808: canted render, straight submission

- **User:** "Do it" (render canted for sharpness, submit ordinary layers), and "during the climbing animation the left eye showed a
  strange picture".
- **v808 canting:** on by default again (`build/no-cant` for the enclosing frustum).
  - In `GameXr`'s submit loop, an image rendered turned (its record carries the canted FOV; authored cameras are not turned) is
    resampled by its own `StaleEyeWarp` (`uncant_`) into the eye's located view: the record's orientation times the inverse cant,
    and the runtime's FOV.
  - That image is copied into the swapchain and submitted with that pose and FOV.
  - The kept image stays turned, matching its records for the rebuilt eye. The reticle point is mapped by the inverse homography.
    The desktop mirror shows the straight image.
  - Status: `uncanted_eyes`. Check `uncant_math`: three different eye FOVs resample exactly (1e-6), corners covered.
  - Costs one more resampling pass per eye.
- **Climbs in the run-13 logs:**
  - 11 authored episodes (0.3-7.3 s).
  - The alternate mode switches to pairs for them (both eyes set up and presented every frame, `fresh_eye` -1). At the switch there
    are two empty frames ("no_eye_sources ... resubmit_last_projection").
  - The submitted poses during the climb are consistent: 0.064 m apart, 0 degrees relative, enclosing FOV.
  - In v806 every climb start and end also switched the eyes between canted and enclosing (an 8.5 degree layer-pose step).
  - No image exists: band bursts run every 60 s and none overlapped a climb.
  - v808 saves both eyes' final images of the 3rd and 15th pair of up to 3 climbs (`xr-climb-*`).

### 2026-11-01, v807 headset run 14 (22:15-22:18) and v809: hands near the face

- **User:** "807 is ok. Artifacts with a glass in the hand (the image in the hands warped), hand afterimages in a fall, double edges
  on the health potion."
- **Dump 6** (right eye, walking, sword held up): the rebuilt hand was torn open, the sky showing through holes.
  - The kept depth puts the hand at 0.17-0.25 m (median 0.20); 94% of its pixels are nearer than 0.25 m, where `NearHit`'s march
    started. The hand was never met.
  - The P-test then found its old spot gone in the fresh view (it moved 37 mm) and filled it with the fresh eye's scenery: 97% of the
    "gone" pixels were that hand.
- **v809:** `NearHit` marches 12 coarser steps from 0.1 m to 0.25 m first (anything that near is large in the view), then the 32
  fine steps as before.
  - Dump 6: whole hand, flicker 12.52 -> 3.79. The other 31 recorded frames (runs 11-14) change in 0 pixels (5 in one).
  - Lab case "hand at 18 cm" fails on v808 (17 of 78 columns are holes) and passes on v809.
- **Not captured:** the potion and the glass (probably the same when raised to the face; glass without depth would need its own
  data) and the fall.

### 2026-11-01, v809 headset run 15 and v810: one motion model for the rebuilt eye

- **User:** "809 is sharper, fewer artifacts. Hands jerk on jumps; hands and body jerk on turns (smooth, no afterimages); glass cups
  unstable; the body jerks; hands and held objects stretch near the view edges. Build the complete math, not patches."
- **Run 15** (`artifacts/claude/perf-mode/v809-run15`): 12 dumps. In dumps 11 and 12 the hands moved 10 cm together with the head
  (running, jumping or falling); in dump 7 the head moved 23 cm in one frame. No climb was captured.
- **Why the rules kept failing:** each earlier rule (P-test, far rule, near search, beside rule, outside rule) answered "where was
  this pixel?" for one kind of motion. A pixel that moved another way (the body and arms carried with the head through a jump or a
  stick turn, a hand sweeping across a wall) fell to whichever rule fired first.
- **v810 model:** every kept pixel moved in one of four ways, each a parallax layer whose kept camera is moved by that motion
  (`moved_with`):
  - world: standing still (the old layer);
  - carried: moved with the head (the player's own body and arms in jumps, falls, stick and snap turns): the target camera with the
    kept projection;
  - hand L / hand R: moved rigidly with the drawn hand, from the grip pose recorded with the kept frame to the target's. Each starts
    its search at that hand's distance ahead (0.1-1.5 m), so it does not settle on the wall behind.
- **Per pixel:**
  1. A candidate from each layer (`Candidate`: the layer's `Solve`, inside the kept image).
  2. `Confirm` checks each against the fresh depth (+-1 texel): confirmed, refuted, hidden, or outside the fresh image.
  3. The nearest usable candidate wins. Near-ties (3% + 1 cm) go to the best brightness-normalized texture match against the fresh
     image, with a fixed preference: hands, then carried, then world.
  4. The winner's exact point in the fresh image is compared in brightness. A surface that moved in an untracked way (Void shards,
     a hand off its grip) goes to the fresh eye's `NearHit`.
  5. No usable candidate: the fresh `NearHit`, this eye's own hidden world point, `KeptBeside`, then the fresh `Solve` inside its
     image.
- **Checks:** all lab cases pass. New case "tracked hand": the hand poses differ by 6 cm, and the hand comes from this eye's own
  frame, in its own shading, with no holes and nothing at its old place. 44 recorded frames (runs 11-15): flicker 2.729 -> 2.551,
  better in 21, worse in 3 (806-2 is the crop artifact below; 807-3 and 809-6 have moving Void shards). The largest gains are
  803-4 (1.21 -> 0.51) and 804-2 (1.11 -> 0.31).
- **Replay lesson:** canted dumps kept only +-400 px of the fresh eye, so every case that needed the fresh image beyond it looked like
  a regression (806-2) or could not be checked (the fall, 809-7). v810 dumps keep +-700 px across and +-200 px up and down, and
  record the carried and hand layers and their start distances.
- **Not covered:**
  - Glass without depth: the rebuilt eye cannot place a transparent surface.
  - Game-side jerks (IK, animation) do not show in the dumps. Performance mode off in the launcher separates them: a jerk that stays
    is the game's.
  - The hand layers were never run on headset data (old dumps have no hand poses); v810 dumps record them.

### 2026-11-02, v810 headset run 16 (00:12-00:16) and v811: one owner per pixel, hands measured against the other eye

- **User:** "Sharper. But very many artifacts on the hands and what they hold, very unstable (why, if the world is fine?). Very many
  artifacts from the glass. Find the general solution." Also: "smoother hand animation in jumps and turns, if needed".
- **Run 16** (`artifacts/claude/perf-mode/v810-run16`, 11 dumps; the Heart, a glass, a pistol, the sword):
  - Path-coloured replays (`variant.py`, `edits_pd.py`): on a still hand the chosen candidate changed pixel by pixel between the left
    hand's, the right hand's, the head's and the world's motion. Each samples a slightly different kept pixel, which gives a grain.
  - The glass: the colour check against the fresh eye (brightness off by 60%) sent the glass pixels to the fresh eye, which sees through
    the glass elsewhere. Result: fragments.
  - The tracked hands' depth is right: stereo matching of kept vs fresh agrees within 10% (`stereodepth.py`). The FSR note about a
    first-person depth range no longer holds for tracked arms.
  - On the hands the fresh eye's shading matches this eye's within 3-5%. The 10-20% mismatch of v805 is far world surfaces.
  - In moving dumps (5, 10) both drawn hands moved 10-16 px in the view between frames, while the recorded grips held their place
    relative to the head. The hand layers left the rebuilt hand a frame behind. The drawn hand is not the recorded grip.
- **v811:**
  - **Ownership.** A drawn hand owns the kept pixels within 22 cm of its drawn palm (`drawn_palm_frame`, recorded per camera as
    `warp_palms`). Only its own layer may take them, unconditionally. The world candidate on them is the hand's old place, so it goes to
    the fallbacks. No texture ranking; near-ties go by a fixed order: own hand, world, carried, other hand layers. The photometric override
    is gone.
  - **Hand fit** (compute: `FitSample`, `FitCoarse`, `FitFine`, `FitPick`):
    - 48 x 48 samples per hand over its palm's square, moved by the recorded layer.
    - Shifts of 13 x 13 at 4 px, then 7 x 7 at 1 px.
    - Gains in depth+brightness inliers over all owned samples and in depth inliers over the outline band; both must gain, together at
      least 0.10.
    - The centre of the plateau of near-best shifts, not its nearer end (that bias was a lag).
    - The warp moves the hand candidate by the result. Status: `hand_fit` [fits, corrections].
  - Offline check on the v810 frames: the frames that moved get 5-14 px; still hands and a floor-outline false fit (dump 4) get 0.
- **Lab:**
  - The fresh-layer, near-hand, 18 cm and moved-hand cases now carry a tracked hand whose recorded grip did not move, so the fit must
    place it. Within 2 px on the flat, textureless test square; exact on the near and 18 cm hands.
  - New case: a still hand beside a hand recorded moving keeps 0 of 256 columns off this eye's own frame.
- **Known limits:**
  - An untracked object sliding at its own depth shows its old texture for one rebuilt frame (the colour check that caught it also
    shattered glass).
  - A surface within 22 cm of a palm moves with the hand.
  - The game-side cause of the drawn hands moving against the camera in falls and runs (arm root carry versus camera anchor) is not
    found. The status line's `full_body.root_gap_publishes` stayed 0. Next session: `hand_fit` corrections, and dumps with `palms`.

### 2026-11-02, v812: measured motion of moving objects

- **User:** "Fix moving objects; research the math, it has most likely been solved."
- **Research** (subagent; sources: Bowles et al. 2012 iterative image warping; Meta AppSW and ASW 2.0; FSR 3 frame-interpolation
  shaders; DLSS 3; GFFE; Oculus Stereo Shading Reprojection):
  - Every pixel moves by its own motion field.
  - The inverse warp is a fixed point `x = t - V(x)`; with several solutions, the nearest depth wins.
  - Never average two sources on what moves.
  - Separate object motion from camera motion.
  - The game's motion vectors exist (FSR input), but in alternate mode their "previous frame" may be the other eye's camera.
    Unverified, so they are not used.
- **v812 `TileFit`** (compute, one group per 32 px tile of the kept image, 8 x 8 samples):
  - Each sample is moved as the world (the camera alone) into the fresh eye.
  - The tile's brightness relation between the eyes: least squares `lb = gain*la + offset`, over samples that agree in depth and within
    25% in brightness (a slid texture and the eyes' 10-20% kept apart). With fewer than 8 such samples, depth alone decides.
  - Still sample: depth and modelled brightness agree. A tile with at least 25% moving samples (and at least 8) is searched with the hand
    fit's shift search over its moving samples.
  - The shift is kept only if every moving sample on average fits at least in depth and it beats no shift by a quarter of them. Ties go to
    the smaller shift, so a direction the content cannot tell stays still.
- **Warp:**
  - A kept pixel of a measured tile whose modelled colour the fresh eye no longer sees at its place is gone from there. This holds even at
    the same depth, for a coat sliding over itself.
  - Moved candidates: the distinct motions of the 3 x 3 tiles around the ray, solved as `t - M` against the kept depth. A candidate is
    rejected if its kept point's tile measured a different motion; an unmeasured edge tile is allowed. It must be confirmed by the fresh
    depth.
  - Ranking: own hand, then still world, then moved, then carried. The far rule keeps refuted far pixels only where no motion was
    measured, so small far movers still step a frame behind. `KeptBeside` refuses measured movers.
  - Status: `motion_tiles`.
- **Checks:**
  - New case: a 1 m wide striped guard 3 m away walked 20 cm; the other eye is 10 levels darker. Its line lands at 126 (want 126.1), the
    newly covered strip is in this eye's own shade (170, not 160), and the old place is wall.
  - All earlier cases pass, including additive 30-60 level eye offsets (depth alone there).
  - The v810 frames are identical to v811 except dump 9 (a weapon swap).
- **Not seen yet:** a real walking NPC in a dump (run 16 had none near). Next session: `motion_tiles`, and stale-warp dumps with
  movers.

### 2026-11-02, v812 headset run 17 (07:31-07:33) and v813: tiles removed, no hand where the other eye sees past it

- **User:** "motion fairly stable, but many artifacts in the frames, especially on still objects, and some on the hands".
- **Data:** artifacts/claude/perf-mode/v812-run17 (dumps 1-8, status, alt-pose-trace). Status: hand_fit 10065 fits, 5532 corrected;
  motion_tiles 10070.
- **Lab correction:** the v812 replays had never run the tiles. Dumps keep the forward rows only, and the replay derived the kept-side
  rows for the hand layers but not for the scene layer, so `backW` was zero and no tile sample was valid. "The v810 frames replay
  identically" said nothing about the tiles. Fixed in scratchpad/lab replay_v812.cpp (`invert(layers.world)`); its output now matches the
  in-game warped frames (flicker 1.29/3.88/2.05/7.97/3.11/2.44 vs 1.28/3.87/2.05/7.94/3.03/2.44).
- **Cause 1, still objects:** the tiles moved 76-105 tiles per still frame (tilemap.py). A sample the other eye sees hidden behind
  something nearer (around the hands, behind a chair edge) counted as moving, and so did brightness that differs between the eyes
  (the subtitle line, glasses with the floor's depth under them, fine carpet texture). Moving samples fail at zero shift by definition,
  so any shift where chance matches exceed 25% won. Result: torn subtitles, glasses and chair edges, crack lines beside fingers.
  Without the tiles (= v811) the 7 replayable dumps go 3.79 -> 3.12 (still dump 7: 3.03 -> 1.86). The tiles are removed. A future
  mover method needs positive geometric evidence (a surface gone from where the camera alone puts it, with the hidden ones unknown),
  depth-layer segmentation of the mover, or the engine's motion vectors.
- **Cause 2, hands (dump 3, a 44 degree snap turn):** the recorded grip turned 44 degrees in place (moved 0.8 mm) while the game moved
  the drawn hand out of that part of the view; v811 showed a hand's own pixels wherever its motion put them, without the fresh eye's
  check, so a dark ghost of hand and sleeve stayed for a frame. v813: such a pixel is dropped when the fresh eye sees only what lies
  farther within 6 texels (`GoneWide`). A per-pixel refutation (+-1 texel) was rejected: it outlined moving hands in black and cut thin
  held objects (run-16 dumps 4 and 5).
- **Checks:** new case "hand its motion misplaced" fails on v811 (28 of 28 columns show the hand) and passes on v813 (0); all other
  cases unchanged. Run-16 dumps: 3.45 -> 3.42 (dump 9 9.61 -> 9.24, dumps 4/5 equal). Run-17 dumps: 3.12 (dump 3, the snap turn, cannot
  be replayed: its kept crop misses where the turned view looks).

### 2026-11-02, v814: no flat tutorial text; a performance-mode sim run with guards

- **User:** "I should not see this huge white tutorial notification; there is a VR copy of it". v710 made tutorial and information messages
  VR-only, but `NativeHudChildDraw` suppressed an uncaptured draw only for prompts and the HUD window panel, so a message whose capture
  was refused (slot busy, frame not capturing) went flat across the eye. v814 suppresses it too (test: native_hud_child_draw_check, fails
  before, passes after). The line in the run-17 dumps with the "Swing" glyph is the head-locked VR copy, not the flat one.
- **Sim:** v814 in performance mode on the user's save (artifacts/runs/20261102T072501.000836Z-pid73288-arm-v814-failed): 7584
  alternated frames, 0 fallbacks, FSR 4 depth (parallax 7573, fresh 7573), hand fit 7573/1275. The 7 stale-warp dumps were taken in the
  lead-in and on a wall (flicker 0.00-0.16: still frames rebuild exactly). Guards walked up to the player at 76-96 s; the timed eye pairs
  (one eye rebuilt each) show them whole in both eyes. Not measured: the one-frame lag of moving NPCs.

**Earlier next step, superseded by v792.** Two runtime A/B tests that need no build:
1. Virtual Desktop SSW off.
2. The game on SteamVR's OpenXR runtime instead of VDXR.

If the doubling is VD's single-pose handling, the robust mod-side fix works on any runtime: before submission, warp the stale eye to the
fresh eye's frame pose, so both views carry poses from one head pose.
