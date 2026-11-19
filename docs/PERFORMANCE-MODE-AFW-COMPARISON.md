# Performance mode vs Dying Light VR's Alternate Frame Warp (2026-11-19)

Plan item 3.2 of `docs/PERF-PLAN-90HZ-2026-11-19.md`. Read-only research: nothing was built, launched or run on a GPU. It compares our
rebuilt eye (performance mode: one eye rendered per frame, the other rebuilt from its kept image) with Dying Light VR 1.04's Alternate
Frame Warp (AFW), and lists what to change in ours.

Sources:
- Ours: `src/afw2.hpp` (the rebuild, AFW v2, default since v1216), `src/stale_eye_warp.hpp` (layers, the older gather warp, the
  dispatch into AFW v2), `src/rig_mask.hpp`, `src/rig_target.hpp`, `src/eye_motion_pass.hpp`, `src/object_motion.hpp`,
  `src/alternate_eye.hpp`, `src/game_xr.hpp`; `docs/AER-REFERENCE-RESEARCH-2026-11-11.md` (Metro Exodus VR's AER+, which AFW v2 follows);
  sim runs `artifacts/perf-night/r1-baseline` and `r5-performance-mode` (local, v1393, 2246x2419, FSR 3 at 0.77).
- Dying Light VR 1.04 source (GPL-3.0). Paths below are relative to its `src/`: `bootstrap/dlssflat.cpp` (the rebuild, namespace
  `afwrb`, and the upscaler motion vectors), `ovr2xr/camhook.cpp` (the dependency list with each switch's status and measurements),
  `ovr2xr/ovr2xr.cpp` (settings and defaults).

**Licence: Dying Light VR is GPL-3.0. Nothing from it is copied here or should be copied into this repository. Techniques are described
in our own words; any implementation must be written from these descriptions, not from their code.**

## 1. Summary

- Both rebuild the held eye at the fresh eye's moment for the held eye's own camera there (fresh pose + eye separation), and both
  draw the HUD after the rebuild. The designs differ: Dying Light rasterises a depth-tested grid mesh per source (one point every 2
  depth texels, into half-resolution seed maps), then refines each output pixel by fixed-point steps and picks from **two colour
  sources** (held first, fresh second). Ours is Metro's compute **forward splat** of the kept eye at render resolution with
  per-texel 3D motion confirmed by the fresh eye, a jump-flood fill and a two-stage resolve, and shows **the kept eye's own colour**
  almost everywhere.
- What Dying Light has that we lack or only partly have: **exact current-pose motion for the held weapon** (and the body and NPCs)
  through a "held-pose map" redrawn during the fresh render; an **edge ring** that stops the hand's old anti-aliased outline from
  being carried as world; a **stale test** for every held texel at any depth; a **lens-mask marker** the rebuild ignores.
- What we have that Dying Light lacks: per-texel object motion with fresh-eye confirmation and consensus repair (moving NPCs without
  a redraw budget), the exact rig silhouette in the target camera (rig target), camera-cut handling, and exact per-eye upscaler
  vectors (eye-previous matrices, rows and palettes instead of "object vector x2").
- **Cost: ours 2.04 ms p50 (mean 2.09, p95 2.38) at 2246x2419 on an RTX 5070; theirs 1.01-1.06 ms at 2688x2880 (DLAA) on an
  unstated GPU.** Per output pixel ours is ~2.8x theirs (~1.4x if their GPU is about twice a 5070). In the R1/R5 sim pair the
  warp eats most of what the one view saves on the GPU (net ~0.3 ms per frame, section 3).
- On the user's PC the frame is CPU-bound and performance mode does not shorten it (10.37 vs 10.42 ms, `docs/PERF-NIGHT-2026-11-19.md`
  section 2). Its value is on GPU-bound PCs and high headset resolutions, so **the warp's GPU cost comes first**, then the held-item
  artefacts the history keeps fighting (section 4).

## 2. Technique by technique

Status is ours against theirs: **present** (same effect, possibly by other means), **partial**, **missing**, **n/a** (does not apply to
our design).

| # | Technique | Dying Light AFW 1.04 | Ours | Status |
|---|---|---|---|---|
| 1 | Target view | Held eye's camera now = the fresh matrix shifted by the eye separation along view-right; held eye then declared with the fresh orientation (`dlssflat.cpp:13192-13203`, `camhook.cpp:665`) | The other eye's view at the fresh eye's Setup (companion view); both layers carry one head pose (`alternate_eye.hpp:53-74`, `stale_eye_warp.hpp:239-272`, `game_xr.hpp:3072-3077`) | present |
| 2 | Inputs and adjacency gate | Held image + depth and fresh image + depth, proven adjacent by tonemap serials, else no rebuild (`dlssflat.cpp:13175-13181`) | Kept/fresh image + depth, both motion textures, both rig masks, rig target, each checked for this Present (`game_xr.hpp:3084-3117`, `stale_eye_warp.hpp:1064-1116`) | present |
| 3 | Reprojection core | Per source a grid mesh, one point every 2 depth texels, depth-tested into a half-resolution seed map (`dlssflat.cpp:11899-11931`, `:13349-13360`); per output pixel 3 fixed-point refine steps per source (`:11943-11956`); bilinear colour read (`:11957`) | Per render texel a forward splat, `InterlockedMax` on 1/z (`afw2.hpp:580-624`), seams by Crack (`:639-663`), holes by a 10-pass jump flood (`:668-713`, `:1129`), a per-texel decision (`:828-993`) and a display-resolution resolve with 5-tap Catmull-Rom, detail restore and 2x2 clamp (`:735-749`, `:995-1014`) | present (different design, see section 3 for cost) |
| 4 | Two colour sources | Ladder: held own world unless fresh is clearly nearer (1.5 %); else fresh; else held fallback; else near miss; else row fill (`dlssflat.cpp:12024-12133`) | The kept eye's own colour (Metro). Fresh colour only for a moving hand no kept texel explains (`afw2.hpp:716-732`, `:832-841`), the rig target's fallbacks (`:865-907`), holes beyond the kept view or where the rig left (`:917-937`) | partial, by design (v805: fresh-eye fill was 10-25 % off in shading) |
| 5 | Stale-surface test | Held point projected at its depth into the fresh view; nearest fresh depth over 5x3 texels farther by 1.5 % + 1 cm: it moved, use fresh (`dlssflat.cpp:11979-12001`, constants `:12336-12337`, `:13240-13241`); any depth; the fresh eye's body/weapon never counts as evidence | Chain refutation only for texels that carry motion (`afw2.hpp:265-295`, `:408-423`); a still world texel is dropped only nearer than 1.5 m, refuted, with a fresh mover within 8 texels and no rig target (`:420-422`) | partial |
| 6 | Far / sky | Comparisons clamp both depths at 250 m (sky is a flat plane ~97 km out facing each camera: flicker on turns) (`dlssflat.cpp:11963`, `:12333-12336`) | Sky is depth 0, a direction-only class (`afw2.hpp:404`, `:596`, `:966`); tolerance 1 cm + 3 % (`:232`) | present (no far plane to flicker) |
| 7 | Edge rescue (stretched seed, slid off an edge) | Re-refine from the nearest of 4 neighbouring seeds (`dlssflat.cpp:12034-12051`; stretched-sheet flag `:11926-11931`) | No seed refine; slanted-surface corners (`afw2.hpp:568-579`) and Crack cover the splat's equivalents | n/a |
| 8 | Edge hold (held/fresh flip at outlines) | Keep the held pixel where the held image has the nearer surface within 2 texels (`dlssflat.cpp:12063-12082`, radius 2 `camhook.cpp:707`) | One colour source, nothing flips | n/a |
| 9 | Gap fill from the background side | Along the row, per side the nearest covered seed; extend the farther side; never body/weapon (`dlssflat.cpp:12099-12133`) | 2D jump flood: the nearest seed behind the foreground, bounded by the camera step and the largest motion (`afw2.hpp:668-713`); rig seeds excluded only while the rig target is on (`:686-690`) | present (rig exclusion partial) |
| 10 | Player body/weapon mask | LOD-0 draws of the player and both held items redrawn depth-only into own D32; foreground where equal to scene depth (`camhook.cpp:729`, `dlssflat.cpp:11816-11826`) | Rig/held draws replayed depth-EQUAL into a UAV key mask: 255 rig with own motion, 254 rig, 253 held, 251 instanced held part (`rig_mask.hpp:1-35`, `:53-75`); plus the rig target, the rig and held items drawn into the target camera at the fresh tick (`rig_target.hpp:1-10`, `afw2.hpp:842-909`) | present (ours adds the exact target silhouette) |
| 11 | Body moved to its current pose | Held-pose map: during the fresh render the body is redrawn in the held image's pose and camera, each texel carrying its current position; every held body texel is carried from there (`dlssflat.cpp:11843-11866`, `camhook.cpp:689`, default on) | Arm texels with this eye's previous palette (255) move by their own motion, confirmed by chain and by outline votes on the rig target; the rest by the palm or head rigid layer (`afw2.hpp:424-462`, `:486-548`, `:551-560`) | partial |
| 12 | **Held weapon moved to its current pose** | The weapon's per-draw rows saved on the GPU; next image redraws it with the saved rows through the held camera and the current rows as the target: the rigid form of the map (`camhook.cpp:691`, default on) | Held texels (253/251) carry **no own motion** (`eye_motion_pass.hpp:112-113` keeps motion only for world and 255); they move with their palm's rigid layer (`afw2.hpp:246-256`, `:296-300`); outline votes on the rig target decide whether the whole held group stands down to fresh-eye samples (`:448-457`, `:551-560`, `:590`) | **missing** (the root class of v1320-v1327) |
| 13 | NPC motion | NPC draws join the mask and the map (redraw budget 256 per image) (`dlssflat.cpp:11256-11262`) | Entity vectors with this eye's own previous rows and palettes (`object_motion.hpp:81-109`, `skin_motion.hpp:2-6`) -> motion texture -> Scatter, Chain, Repair (`afw2.hpp:301-340`, `:408-423`, `:486-534`) | present (Metro-style; scaled by frame-time shares, not exact under hitches) |
| 14 | Edge ring (old anti-aliased outline of a moved hand) | A held texel that is not body but touches a body texel the map moved > 1.5 depth texels is no candidate; fresh or fill supplies it (`dlssflat.cpp:11867-11895`, default on) | `Clean`/`apart` keep colour to pixels on the same surface by render-resolution depth (`afw2.hpp:754-784`, `:946-984`); the upscaled fringe beyond the depth edge is carried as world | partial |
| 15 | HUD after the rebuild | HUD pass redirected into a per-eye layer, laid on after the rebuild (`dlssflat.cpp:7423-7449`) | Overlays drawn after the warp (`game_xr.hpp:3174-3190`) | present |
| 16 | Lens mask in the rebuild | Hidden area: near plane + stencil during the render, rewritten to a far **marker** (2^-100) before the depth copy; marked texels are never seeds, hits or stale evidence (`dlssflat.cpp:4493-4511`, `:11833-11838`; lens mask off by default) | The stamp needs the job's eye (`dxgi_probe.cpp:3297-3306`, `visibility_stamp.hpp:139`); a one-view render has none, so performance mode never stamps (R5: `visibility_stamp.skipped.no_eye` 4789, `stamps` 0) | missing (no saving; also no hazard today) |
| 17 | Camera cut | Rebuild refused when the depth copies are not adjacent; the held eye is shown as is (`dlssflat.cpp:13180`) | A cut (step > 0.5 m, or > 0.1 m with > 20 deg) rebuilds from the fresh eye alone (`stale_eye_warp.hpp:209-246`, `:1099`) | present |
| 18 | Per-eye upscaler history | History labelled by alternation, anchored to the layer's eye stamp (`dlssflat.cpp:14302-14340`) | Per logical eye (`alternate_eye.hpp:172-189`, `alternate_taa_state.hpp`) | present |
| 19 | Vectors over each eye's two-tick history | Engine buffer is object-only; camera term from depth and the eye's own previous VP (two ticks), object vector x2, default 2.0 (`dlssflat.cpp:3188-3210`, `:13859-13882`, `:16513`; `camhook.cpp:701`) | The view's previous matrices set to this eye's own (`alternate_native_taa.hpp:100-110`), per-draw own previous rows (`object_motion.hpp:81-109`), own previous palettes (`skin_motion.hpp`): exact, no constant-velocity factor | present (more exact) |
| 20 | Jitter | Matrices stripped of jitter; Halton phases doubled so each eye walks all of them (`dlssflat.cpp:3198-3203`, `:2936-2937`) | Per-eye phase, eye 1 offset by half the sequence (v1202, `alternate_taa_state.hpp:102`) | present |
| 21 | Diagnostics | Rule tint, rule counter, capture, GPU timer (`dlssflat.cpp:12136-12170`, `:13284-13316`) | Source codes (`afw2.hpp:787-797`), `rig_world_share`, votes, stage timer, replay tools (`tools/stale_warp_replay.cpp`, `tools/stale_warp_defects.py`) | present |

## 3. GPU cost

| | Dying Light AFW 1.04 | Ours (R5 sim, v1393) |
|---|---|---|
| Rebuilt eye output | 2688x2880 (DLAA), 7.74 Mpx | 2246x2419, 5.43 Mpx |
| Depth / work grid | depth 2688x2880; seed grid 1344x1440 cells, 2 x 11.6 M indexed vertices, seed maps at half output | render grid 1729x1863 (FSR 3 at 0.77), 3.22 M texels, ~10 compute stages + 10 fill passes + display-resolution resolve |
| Measured | 1.009-1.060 ms per rebuild (one per frame), after indexing the seed mesh (was 1.221-1.318; seed rows 0.70 -> 0.50 ms) (`camhook.cpp:697`) | `stale_warp` p50 2.04 ms, mean 2.09, p95 2.38, max 3.37 (`perf_gpu_ms.stale_warp`, 3799 passes) |
| What the number includes | the whole call: a copy of the side-by-side eye image, both seed meshes, the compose, the copy back (`dlssflat.cpp:13284-13319`) | the AFW v2 pass only; not the rebuilt eye's uncant pass (`game_xr.hpp:3132-3146`, outside every timing scope), not the rig mask / rig target replays (inside engine lists) |
| GPU | not stated in the source | RTX 5070 |
| Per output Mpx | ~0.13-0.14 ms | ~0.38 ms (~2.8x; ~1.4x if their GPU is about twice a 5070) |
| Per depth texel | ~0.13 ms/M | ~0.63 ms/M (~4.8x) |

Note: the brief's "1344x1440" is Dying Light's seed grid (half of its 2688x2880 DLAA depth), not the output size.

Where our time goes is not known. The in-game stage split (`afw2.hpp:1084-1156`) puts 1.10 ms in "crack+copies" and 0.84 ms in
"resolve" while 8 of the 10 stages read ~0 (p95 0): the timestamps do not separate the dispatches (the two `CopyResource` calls and the
draw act as sync points that absorb earlier work). The only stage clocks we have, from `tools/stale_warp_replay.cpp`, run on WARP
(`:215`, `:435`).

What the warp does to the frame's GPU budget (R1 stereo vs R5 performance mode, same save, script, size and AA; single runs that
overlapped helper compiles, so treat as indicative):

| GPU per frame, p50 | R1 stereo | R5 performance mode |
|---|---|---|
| engine lists (`frame_split.split.gpu_phase_ms_p50`) | 4.24 + 2.91 = 7.16 ms | 5.52 ms |
| mod GPU sum (`gpu_passes.mod_gpu_ms_p50.sum`) | 2.25 ms (FSR 0.99 + 0.99) | 3.55 ms (warp 2.04, one FSR ~1.0, overlay 0.27) |
| total | 9.41 ms | 9.07 ms |

The one view saves 1.64 ms of engine GPU, the mod adds 1.30 ms: net ~0.34 ms. Two things eat the saving: the warp, and the one view
costing 1.28 ms more than stereo's eye 0 (sun-cascade time slicing is forced off in performance mode since v1160,
`dxgi_probe.cpp:6562-6566`, and the rig mask / rig target replays run in its lists). At Dying Light's cost per pixel the net would be
~1.4 ms.

## 4. What the CPU-bound result means for priority

- On the user's PC (Ryzen 5 5600X, RTX 5070) the frame is limited by the game's main thread; one view still costs it ~8.3 ms, so
  performance mode leaves the frame time unchanged (10.37 vs 10.42 ms). Nothing below changes that; Phase 2 CPU work comes first for
  this PC.
- Performance mode pays on PCs where the GPU is the limit: weaker GPUs, high headset resolutions, DLAA / native. There, its gain is
  the one view's GPU saving minus the warp, which at R5's numbers is small. **So the warp's cost is the first item**: until it is near
  1 ms, recommending the mode (or switching to it automatically, plan 3.1) buys little frame time for the image cost.
- Plan 3.1's automatic switch must key on GPU time (`gpu_busy`), not on missed frames: on a CPU-bound PC a miss-driven switch would
  trade image quality for nothing.
- The held-item artefacts (v1320-v1327) are the most reported; they come second because they matter only once the mode is worth
  using. Both lists are useless without a GPU-bound measurement: run the stereo / performance A/B in a GPU-bound setup on this PC
  (DLAA, or resolution 1.3x or more, until `gpu_busy` >= 0.9) before and after item 1.

## 5. Changes for ours, in priority order

Every change gets its own build knob (opt-in, default after a headset check), a lab or replay check, and a sim A/B. Simulated and
replayed checks do not establish headset image quality or comfort.

### 1. Cut the rebuild's GPU cost to about 1 ms

- **What.** (a) Time each stage for real: add a hardware-device mode to `tools/stale_warp_replay.cpp` (WARP today, `:215`) that runs
  each AFW v2 stage in its own timed bracket with a GPU sync between, on the RTX 5070, over the existing sim dumps. (b) Motion tiles
  (Metro, AER doc A.3 step 9): build the list of 8x8 tiles that hold a mover or a rig texel in `Motion` (`afw2.hpp:399-465`) and run
  Repair's searches, `Origins` and the rig rules of `DecideAt` only there, by `DispatchIndirect` as `Fill` already does (`:1124`,
  `:1134`). (c) Bound the fill: it always runs 10 jump-flood passes, 512 down to 1 (`:1129`); start at the power of two above the
  largest hole (the largest motion in texels is already in `handVotes[8]`, `:463`). (d) Fold the rebuilt eye's uncant into
  `ResolvePS`: compute the canted target position of each straight-view pixel and look up the decision there, removing one
  display-resolution Catmull-Rom pass and one resample per frame (`game_xr.hpp:3132-3146`). (e) Only if (a) shows `Splat`/`Decide`
  dominating: splat a still, flat 2x2 block of the static world as one quad (Dying Light's every-2-texels grid), full resolution at
  edges, movers and the rig.
- **Effect.** Estimate ~0.8-1.0 ms off at 2246x2419 (not measured); the net GPU saving of the mode would rise from ~0.3 to ~1.2 ms at
  R5's setup, more at higher resolutions. (d) also sharpens the rebuilt eye slightly (one resample less).
- **Risk.** Low for (a), (c), (d); medium for (b) (tile borders) and (e) (seams under magnification).
- **Verify.** Replay on hardware: stage times before/after; byte-identical outputs for (b) and (c); `stale_warp_defects.py` F and B4
  within noise for (d) and (e); sim R5 with `native-gpu-timing`: `stale_warp` p50/p95 and the frame's mod GPU sum; the R1/R5 accounting
  above repeated in a GPU-bound setup.

### 2. Exact forward motion for held items (Dying Light's held-pose map, in our terms)

- **What.** Today held texels move by their palm's rigid layer and stand or fall by outline votes (table rows 11-12). A part the
  engine moves relative to the weapon (bolts, rings, v1304) is wrong under one rigid layer, and a failed vote hands the whole held
  group to fresh-eye samples (v1322 dumps: held pixels 30 % kept, 53 % fresh, 11 % fresh-neighbour search: torn thin parts, ghosts).
  In the **fresh** render, replay each placed held draw once more through the existing UAV replay (`rig_mask.hpp:17-31`) with
  `rig_target.hpp`'s retarget: rasterise it in the **kept** camera with the rows that draw had in the kept render (the placed-history
  receipt, `object_motion.hpp:143-157`, v1218; in alternate mode the engine's own raw previous rows are that render's,
  `:83-84`), and write per texel its **current** position in the target camera (the current rows retargeted), nearest wins, kept only
  where its depth equals the kept depth (as the mask copy does). `Motion` then takes a held texel's displacement from this map, with
  the palm layer as the fallback. Cheaper first step for rigid parts: a per-draw id in the kept mask and a small table of each part's
  rigid delta (current model matrix times the inverse of the kept one). Later the same map for arm draws (previous palette from
  `skin_motion.hpp`) can replace own motion plus votes.
- **Effect.** Held texels land on the rig target by construction; no group stand-down, so no fresh-sample tearing; bolts and strings
  follow their own motion. Targets the crossbow doubling while walking (v1323/v1324), torn thin parts during hand motion (v1321) and
  doubled held props.
- **Risk.** Medium-high: one more replay per held draw on the render thread (CPU on a CPU-bound PC); class 251 instanced parts cannot
  be retargeted (v1312) and keep the layer; a placed-history miss falls back.
- **Verify.** Lab: extend `tests/rig_target_check.cpp` / `tests/alt_weapon_place_check.cpp` with a part rotating against its palm (map
  vs layer error). Replay of the crossbow-pair sway and hand-rotation dumps (v1321-v1324): held pixels from the kept image, fresh-
  neighbour share, held outline votes, held-region |rebuilt - next| (6.3 in v1321). Sim: `weapon-perf.csv` mod CPU p50/p95 and render
  thread busy (budget ~0.1 ms), `afw2.rig_world_share`.

### 3. Edge ring: no old outline beside a moving hand or weapon

- **What.** In `Motion`, flag kept world texels within one render texel (8 neighbours) of a kept rig/held texel whose displacement
  exceeds about one target texel. They still splat (they hold the background's depth), but `DecideAt`/`Clean` treat them as another
  surface (`afw2.hpp:754-784`, `:946-984`) and the fill does not take them as seeds; their colour comes from the nearest same-surface
  background pixel beyond them, or from the fresh eye where it confirms that background at that depth. Also exclude rig seeds from
  the fill when the rig target is off (`:690` tests only `TargetOn`).
- **Effect.** Removes the faint contour where a hand or weapon was (v1172 "a halo around the hands while they move", v1184 lit sleeve
  outline, stick-turn contours).
- **Risk.** Low-medium: thin background detail right beside a moving hand loses 1-2 px of its own colour.
- **Verify.** `stale_warp_defects.py` region `edge_out` (non-rig within 24 px of rig), F and B4, on the hand-motion dumps; `rig` and
  `rest` regions unchanged; a lab case with a hand moving 20 texels over a textured wall.

### 4. A general stale test

- **What.** For every non-rig kept texel that is still or unconfirmed: project it at its target depth into the fresh camera; if every
  in-view, non-rig texel of a 5x3 fresh neighbourhood is farther by 1.5 % + 1 cm, drop it (`kDrop`, no splat; fill and fresh supply
  it). Replaces the narrow rule at `afw2.hpp:420-422` (nearer than 1.5 m, fresh mover within 8 texels, no rig target). Out of the fresh
  view, or behind something nearer there, is no evidence.
- **Effect.** Objects whose vectors are missing or wrong (props outside the rig mask, v1152: ~300 px off; doors, debris, NPC parts
  without own rows) stop leaving their old image in the rebuilt eye.
- **Risk.** Medium: false drops at thin silhouettes (the 5x3 nearest guards it), and if the lens stamp ever runs without item 5's
  marker (Dying Light's U-59 bug).
- **Verify.** A drop counter beside `votes_`; a still scene must drop ~0; replay of dumps with walking NPCs and doors: `rest` region F
  and B4; lab: a box moved with zero vectors.

### 5. Lens-mask stamp in performance mode, with a marker the rebuild ignores

- **What.** Give the stamp the logical eye of the one-view render (the alternate receipt or planned eye) instead of skipping it
  (`dxgi_probe.cpp:3303-3305`). Before FsrTaa's depth copy (`fsr_taa.hpp:1171-1174`), rewrite stamped texels to a marker that the
  rebuild, the motion pass and the rig mask copy read as "no data": never a splat source, never confirming or refuting, never "the
  fresh eye sees past"; the upscaler sees far plane.
- **Effect.** Performance mode gets the stamp's forward-pass saving that stereo has (E8: 9 % hidden on VDXR; outside-FOV adds more on
  canted images). Without the marker the stamp would put near-plane sheets into both depths and the rebuild would carry them with huge
  parallax.
- **Risk.** Medium (the copy gains a consumer; FSR at the mask edge).
- **Verify.** Lab: a stamped band never reaches visible pixels after a 5 deg turn and a 2 cm step (`tests/stale_eye_warp_check.cpp`);
  sim with the mask fallback: `visibility_stamp.stamps` > 0 in performance mode, engine GPU lists with and without.

### 6. Disocclusions behind moving objects from the fresh eye, gain-matched

- **What.** For a filled texel far from its seed (more than ~4 texels) and not beside the rig: if the fresh eye's surface on the ray
  (the 2-step fixed point at `afw2.hpp:926-934`) has the seed's depth within tolerance, take the fresh colour scaled by the kept/fresh
  ratio measured on that surface where both eyes see it. Dying Light takes fresh colour unscaled; v805 measured 10-25 % shading
  differences, hence the gain.
- **Effect.** Fewer stretch smears behind a walking guard or an opening door.
- **Risk.** Medium (bands if the gain is wrong; view-dependent highlights).
- **Verify.** Replay of dumps with walkers: `rest` region F and B4; share of fill code 7 vs the new code.

### 7. Trim the one view's extra GPU over stereo's eye 0

- **What.** First measure (per-pass table, `build/native-gpu-timing`) what makes the one view 1.28 ms dearer than stereo's eye 0 in
  R1/R5: the forced-off sun-cascade slicing (`dxgi_probe.cpp:6562-6566`, v1160), the rig mask and rig target replays, FSR's copies. If
  it is the cascades: fit the time-sliced far cascades to one view both eyes share (as shadow sharing does in stereo) so slicing can
  stay on; v1160's "shadows out of place" came from slices fitted to whichever eye rendered.
- **Effect.** Up to ~1 ms of GPU in shadow-heavy spots (estimate).
- **Risk.** Medium (v1160's shadow artefact must not return).
- **Verify.** Sim A/B with `build/alt-shadow-slicing`; headset check of shadows during head motion.

## 6. Not adopted

- Dying Light's broad use of the fresh eye's colour (rung 2 of its ladder): our own measurement (v805) found the other eye's shading
  10-25 % off; item 6 takes it only for disocclusions and gain-matched.
- Edge hold and edge rescue: they fix flips and refine failures our single-source splat does not have.
- Object vectors x2: ours are exact over each eye's interval (table row 19).
- Rebuilding the core as a seed mesh plus refine: only if item 1 cannot reach ~1.2 ms. Dying Light also ruled out a bicubic colour read
  (`dlssflat.cpp:11799-11800`); we keep Catmull-Rom because v803 measured an 11-16 % detail loss without it.

## 7. Open questions

- Dying Light's GPU for the 1.0 ms figure (not in the source).
- Where our 2.04 ms goes (item 1a).
- Whether the per-eye FSR dispatch falls inside the engine-list span in performance mode; if so the R1/R5 net saving is larger by up
  to ~1 ms.
- Whether the engine's raw previous rows in the fresh render are exactly the kept render's for every held draw (assumed in item 2 from
  `object_motion.hpp:83-84`); check against live memory before relying on it (AGENTS.md).
