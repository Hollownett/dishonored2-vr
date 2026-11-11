# Stale-eye warp handover (state of e31693c, v1176)

Performance mode renders one eye per tick. `src/stale_eye_warp.hpp` rebuilds the other eye ("target") from that eye's own previous
render ("kept", one render old) plus the eye just rendered ("fresh", the other eye, this tick). Everything below refers to HEAD e31693c.

## 1. Map of src/stale_eye_warp.hpp

### C++ side
- `WarpCamera`: game camera (world metres, basis rows forward/left/up), reverse-Z terms `m10,m11` (ahead = m11/(depth+m10)), drawn hand
  poses (`hands`, `hand_bits`), drawn palms.
- `StaleParallax`: one layer = homography `world` (target uv -> source homogeneous uv) + `shift` (target camera centre in source image,
  metres), the inverse (`back`, `back_shift`, used by the hand fit), `m10/m11`.
- `stale_eye_parallax(from,to)`: builds a layer; refuses a step >= 1 m.
- `stale_eye_layers(kept,target,fresh)` -> `StaleLayers`: `world` (kept->target), `fresh` (fresh->target), `carried` (kept projection at
  the target's pose: what moves with the head), `hand[2]` (`moved_with` the recorded grip motion), `start[3]` (search start distances),
  `palms[2]` (kept-camera metres + owned radius), `kept_axes` (world->kept camera), `cut`, `camera_moving`, `hand_moving[2]`.
  Caller-set fields: `fresh_part/kept_part` (frame-time shares, 0.5 default), `motion_rules`, `rig_from_fresh`, `border`,
  `rig_motion_rules`/`rig_motion` (inert), `near_from_kept`/`hands_motion` (diagnostic).
- Constants: `near_range_default` 2 m (fresh layer on); `hand_own_radius` 0.22 m (v811 palm sphere, mask-less ownership);
  `hand_rig_radius` 0.45 m (rig ownership reach); `mover_px` 3 (object motion, render px over two ticks); `cut_step` 0.5 m,
  `cut_turn_step` 0.1 m, `cut_turn_deg` 20 (cut); `camera_still_m` 3 mm, `camera_still_deg` 0.25 (near_fresh trigger);
  shader `reach` 1.5 m.
- `StaleEyeWarp::apply(source,h,layers,depth,fresh,fresh_depth,kept_motion,fresh_motion,kept_rig,fresh_rig)`. Missing inputs degrade:
  no layers/depth -> rotation `h` only; no fresh -> scene layer only; motion/rig only if both eyes' textures pass format/size checks
  (counters `*_refusals`).
- SRVs: t0 kept colour (sRGB view), t1 kept depth R32, t2 fresh colour, t3 fresh depth, t4 hand-fit result, t5/t6 kept/fresh motion
  RGBA16F (xyz world m, w object px), t7/t8 kept/fresh rig R8 (0/255). Depth, motion and rig are render-sized (1922x2070) under a
  display-sized image (2496x2688); every lookup is in uv, so sizes may differ.
- Constant buffer b0 (192 floats, 768 bytes), packed in this order by `put`:
  `row0-2` (= `h`), `size` (w,h,1/w,1/h); `scene[4]` (= world layer, `[3].w`=1 on); `fresh[4]`; `depthMap` (kept m10, m11, near_range,
  depth on); `freshMap` (fresh m10, m11, -, near_from_kept); `depthSize` (kept w,h, fresh w,h); `carried[4]`, `handL[4]`, `handR[4]`
  (`[3].w` = search start, 0 = off); `keptFov` (tan l,r,u,d); `palmL/R`; `backL/R[4]`; `fitGrid` (48,48,4); `motionSize`;
  `motionMap` (fresh_part, kept_part, on, mover_px); `keptAxes[3]`; `rigSize`; `rigMap` (on, hand_rig_radius, rig-motion on (always 0
  now), rig_mover_px slot); `validRect` (kept u,v max, fresh u,v max = 1-border); `frameMap` (x cut, y left hand moving, z right hand
  moving (y,z only with masks and rig_from_fresh), w near_fresh = masks && camera_moving).
- Hand fit compute (`FitSample`, `FitCoarse` 13x13 at 4 px, `FitFine` 7x7 at 1 px, `FitPick`): shifts of each hand's kept samples moved
  by its recorded layer, scored against fresh depth+brightness and outline. Off on cuts. Read back for the trace (`hand_fits`).

### Pixel shader `WarpPS`, ordered exits (numbering of the replay `-exits` map, HEAD)
1. no depth, ray behind the camera: black. 2. no depth: kept by rotation `h`.
3. `frameMap.x` cut: `FreshOnly` (fresh NearHit, else fresh fixed point, else nearest valid fresh pixel).
(t is the target uv clamped into `validRect`: the edge band is rebuilt from its inner edge.)
4. no fresh layer: scene-layer kept pixel (black if no solve).
Motion block (`frameMap.y|z|w`), one `NearHit` on the fresh eye:
5. near_fresh: fresh near surface within reach, not rig -> fresh colour.
6. fresh rig surface owned (`MovingHandAt`) by a moving hand -> fresh colour.
7. kept pixel is a moving hand's (or its 2 px fringe) and the fresh eye sees scenery there -> fresh surface.
8. moving hand's kept rig pixel outside the fresh view (Confirm == 2) -> kept in place.
Candidates: world (`Solve` from infinity), carried, handL, handR (`Candidate`), each `Confirm`ed; ownership (`Owner`) restricts which
layer may move which kept pixel; movers M2 marks the world candidate of a kept mover refuted.
9. far rule: the world is beyond reach and nothing nearer: kept pixel as is.
10. mover colour (M1 or M3) if it is front-most and outranks.
11. best usable candidate (rank: own hand 0, world 1, carried 2, other hand 3; near-ties 3 %+1 cm): its kept pixel.
Fallbacks (nothing confirmed):
12. `NearHit` near surface (a hand untracked / near object) -> fresh colour.
13. world hidden from the fresh eye or outside it -> kept pixel (own scenery).
14. `KeptBeside` (kept pixel at the ray's infinity point, beyond reach, not a mover or fringe) -> kept.
15. fresh fixed point (or NearHit's edge point) inside the fresh image -> fresh colour.
16. kept pixel if inside the kept image. 17. fresh fixed point. 18. nearest valid fresh pixel to the ray's far point (never black).

### Helpers
- `Ahead(depth,extent,m,uv)`: metres ahead from reverse-Z; 0 = sky. Clamps uv to the edge texel (never use for out-of-image points).
- `AheadSeen`: the same, 0 outside the fresh valid rectangle (v1162; stops "hits" on the clamped edge).
- `Solve(layer,...)`: 5 fixed-point steps on target ray t against that image's depth; `ok` = last step < 1 px. Fails (oscillates or
  converges to the far surface) at depth discontinuities when parallax across the edge exceeds the edge width; nothing then guarantees
  front-most.
- `Candidate`: Solve under a motion layer from its start distance, inside the kept image.
- `Confirm(t,k)`: fresh depth at the point (3x3 texels, `AheadNearest`): 1 same (tol 2 cm+3 %), -1 farther, 0 nearer, 2 outside.
  The tolerance (11 cm at 3 m) cannot separate a sole from the floor beneath it.
- `GoneWide`: fresh sees only farther within +-6 texels (a hand placed by a wrong motion); with masks only rig texels count.
- `FreshEdge`: fresh point on a surface edge (texel beside clearly farther).
- `NearHit`: dense march on the fresh epipolar line 0.1 m..1.875 m (44 steps + 5 bisections); 1 hit, 0 none, -1 passes behind a near
  edge (returns `edge`, a fresh point 15 px in front: a smear source, see open defects). `FarHit`: the same beyond 1.875 m (15 steps)
  then a fixed point from 30 m.
- `KeptBeside`: kept pixel at the ray's infinity point if beyond reach (v805: own scenery beside a moved near edge).
- `Owner(q)`: mask-less: palm sphere 22 cm; masks: not rig -> 0; rig -> nearer palm if within 45 cm and nearer to the palm than to
  the eye, else 1 (carried). `KeptRig`/`FreshRig`: mask texel > 0. `MovingHandAt`, `KeptMovingHand` (2 px fringe).
- Movers: `MotionAt`, `Mover` (w >= mover_px), `KeptMoverAt` (mover or its 2 px fringe on a farther surface), `KeptStep`,
  `KeptImage/KeptFrom` (kept camera metres <-> homogeneous image), `KeptHas` (kept depth agrees, 3x3), `MoverSeen` (M1),
  `MoverHidden` (M3: ring seeds 0.03/0.08 tan, two fixed-point steps, motion consistency).
- `InKept/InFresh`: inside `validRect`. `Smooth`: Catmull-Rom from 9 bilinear taps + clamped 1.5x detail restore, sRGB-linear.

## 2. Rules and why
- Far rule (v805): beyond reach the kept pixel stands even if the fresh eye disagrees: the other eye's shading differs 10-20 % on far
  surfaces; filling from it banded (headset v804).
- Layers + one owner per pixel (v810/v811): every motion competing per pixel produced grain on still hands (headset v810); ownership
  by palm sphere 22 cm. Hand fit (v811): drawn hands are not the recorded grips.
- GoneWide (v813): snap-turn ghost hands. A per-pixel +-1 texel refutation was REJECTED (black outlines, cut thin props).
- Masks (v1161): the sphere swallowed tables/door handles/walls near a palm; rig mask from a depth-equal replay of rig draws (v1163).
  45 cm reach so forearms/held items follow the hand; nearer-to-eye-than-palm -> carried (v1164: hood 8 cm from the eye shimmered).
- rig_from_fresh (v1173): recorded grip over-moves the drawn hand ~2x (dump: 50 px recorded vs 10-28 px drawn, sleeve 0); moving hands
  come from the fresh eye, their old place from the fresh surface, refuted hand-layer candidates dropped. Hand outside the fresh view:
  kept in place (v1174). Still hands keep today's own-frame path.
- near_fresh (v1175, narrowed v1176): near non-rig scenery from the fresh eye only while the camera moves (3 mm / 0.25 deg); on hand
  motion alone it made static near scenery worse (door 2: 1.83 -> 2.62; camera-only 1.65).
- Border band (v1167/v1174): eye images carry a bright band right/bottom (8-11 px, sometimes 36-46 px; colour only, not depth; in the
  engine's tonemapped colour, not FSR). Sources in it are invalid; target pixels in it are rebuilt from the clamped edge ray (a frozen
  kept strip flickered in turns).
- Cut (v1171): step > 0.5 m or (> 0.1 m and turn > 20 deg) -> fresh eye alone. Teleport dumps: 0.3 m + 75 deg. Snap turns and
  sprints are not cuts.
- AheadSeen (v1162): rays leaving the fresh image "hit" the clamped edge (white corner block, sleeve stamped over floor).
- Movers M1-M3 (v1150-v1157): engine own-eye motion textures; M1 fresh geometry + own shading one tick back; M2 old place (+2 px
  fringe) cleared; M3 kept mover carried by kept motion where only this eye sees it (accepted where confirmed only if its own texel
  hides it or is a mover: boot-sole band). mover_px 3 because entity vectors carry a jitter step; the other-eye net lives engine side.
- REJECTED, do not bring back: photometric/colour overrides against the other eye (v810; glass fragments); image-tile motion (v812;
  false motion on still frames); per-pixel +-1 texel refutation of far or hand pixels (v813); filling large areas from the other eye
  (v805); sleeve-hole fill by the hand layer's motion (v1166: painted floor where the over-moved sleeve still was); M1 edge-carry /
  graze search (made B4 worse, no visual gain).

## 3. Open defects
- Jagged mover silhouettes: per-pixel binary choice between M1 and fallbacks follows fresh texels shifted by fractional disparity;
  edge-carry attempts failed. Needs coverage-aware compositing.
- Uncovered strip (disocclusion behind movers/hands) is the fresh eye's colour (~2 % brightness off on real dumps); no own source.
- Rigid hand layer vs drawn arm: still the fallback where the fresh eye cannot see; over-moves. Engine rig motion (Step 2) is the
  intended replacement.
- World `Solve` (5 steps from infinity) at near depth edges under camera translation: oscillates or lands on the far surface; `ok`
  false then the pixel is not "confirmed" and falls to exits 12-18 (fresh colour, or kept pixel at the unconverged q). Exit 9/13/16
  return the kept pixel at the world-solve q: reprojected by the camera layer, but at a possibly wrong depth near edges; exit 13 in the
  v1174 table sat a tick behind (7.5 levels vs next, 1.0 vs kept).
- Teleport residue: thin streak in the bottom band and a seam near the right edge on the cut path (nearest-valid fresh clamp).
- Camera-cut black (v1165 hold 2) reached exit 18 now (no longer black); quality there is the fresh eye only.
- Hold 11 (v1165): border inset changed the hand fit's samples; rig region scores worse by F/B4 but closer to next.

## 4. Tools
- Guard: `build\d3d-guard.cmd` (no `Dishonored2.exe`, no `VR-mods-amd-ags\artifacts\agent-locks\game`); called first by the lab and
  replay scripts. Another session runs sims at unpredictable times.
- Lab: `build\check-stale-warp.cmd` builds `tests\stale_eye_warp_check.cpp` (real HLSL on WARP). Cases are functions (`gpu_fresh`,
  `gpu_turn`, `gpu_movers`, `gpu_rig`, `gpu_edges`, ...) with small analytic renderers producing colour, R32 depth, RGBA16F motion
  and R8 rig textures from planes; each case prints a line and `CHECK`s. Lab images are band-less: a `lab_layers` macro wrapper sets
  `border {0,0}` (r9 sets its own band). `build\build-stale-warp-only.cmd` compiles without running D3D.
- Byte-identity harness (not in the repo; rebuild it when needed): copy HEAD's test to a scratch dir, replace `warp.apply(` with a
  `hashed_apply` that reads back each output, writes FNV hashes (and raw `.bin`) tagged `case#index`, build it against the old and the
  new header (the other headers from `src`), diff hashes, then locate changed pixels from the `.bin` files.
- Replay: `build\replay-stale-warp.cmd [N...]` builds `tools\stale_warp_replay.cpp`, places dump crops in full-size textures (matrices
  unchanged), derives what dumps lack (hand-layer back = inverse(world); kept_axes = probe yaw + pitch fitted to motion; cut from the
  world layer). Env: `STALE_DUMPS`, `STALE_OUT`; `STALE_REPLAY_AB` = `rig` (off sphere / on masks) or `rigmotion`; default off/on =
  motion rules off/on; `STALE_REPLAY_EXITS=1` (`-exits.rgba`, red/10 = exit number above); `STALE_REPLAY_REASONS=1` (MoverSeen/
  MoverHidden early-return codes); paths map (`-paths.png`, when movers exist; hooks are text patches of the shipped source);
  `STALE_REPLAY_FITS=1` (hand fit results); `STALE_REPLAY_MOVER_PX=v` (scales w); `STALE_REPLAY_NO_CUT=1`;
  `STALE_REPLAY_MOVING=0|1|2` (hand/camera moving flags; 2 = as the game). Output table: match1/match4 = fidelity vs the dumped
  `-warped.rgba` (~1.0 when the shader matches the dumping build; GPU vs WARP bilinear precision costs a few %).
  `build\replay-v1165-all.cmd` runs the v1165 hold/door/chop sets.
- Scoring: `tools\stale_warp_regions.py` (whole, near < 1.5 m, 64 px border), `stale_warp_defects.py` (regions border/edge_in/
  edge_out/rig/near_palm/rest, worst 64 px tiles with their exits, white count), `stale_warp_crops.py` (per-path scores, enlarged
  crops), `stale_warp_rig_ab.py` (rig/near_palm/rest A/B). F = (|R-K|+|R-X|-|X-K|)/2 rewards staying between kept K and next X and
  scores R=K as 0 (blind to "a tick behind"); B4 = max over K,X of R's best match within +-4 px: also favours the stale frame and
  breaks under camera motion > 4 px. Use error vs next and vs kept per exit (section 3 tables) to spot "left behind".
- Dump sets (`VR-mods-amd-ags\artifacts\claude\`): perf-motion-v1152 (crops, a walking guard: dump 5); v1160-hands (full frame, no
  real rig mask); v1163 (masks, nearly still); v1165-hold/door, v1164-chop (masks, moving hands, cut in hold 2); v1166-hold (rig
  motion, dumps 4/6 invalid); v1170-hold (teleport dump 3); v1172-hold (hands 30-40 mm). Files per dump: kept/fresh/next/warped .rgba,
  kept/fresh depth .r32, motion .rgba16f, rig .r8, meta.json (boxes, layers, palms, parts, flags). `artifacts\dxgi-camera-probe.csv`
  (per session, yaw only) is needed for kept_axes.

## 5. Parked Step 2 (rig pixels from their own motion)
`build\step2-rig-motion.patch` (diff against `build\step1\` = the Step 1 state before the hole fill was removed; rebase onto HEAD).
Content: rig pixels become movers when masks live and both renders' rig motion is valid (`rigMap.z`), thresholds `MoverK/MoverF`
with `rig_mover_px` 1.5 (still rig p90 0.4-0.9 px), extra M1 gate points 0.35/0.25 m, M1 own-shading only where the kept pixel is rig
at 1 cm+1 % depth (finger self-occlusion), rigid layers and fit off, a hole fill from the pixel's own image motion; lab m1-m5.
r3 broke with it while every shader change was gated on rig motion (off in r3); the same test passed against the Step 1 header, so the
cause is in the patch's ungated parts (candidates: the C++ `rig_motion_on`/`fit_on` change, `MoverK/MoverF` replacing `Mover` in
`KeptMoverAt`, `Owner`'s new early return). Bisect those three first. Engine side: rig motion was opt-in and not yet valid.

## 6. Recommendation
Make same-eye reprojection the backbone. Replace the 5-step fixed-point world `Solve` with an epipolar march against the kept depth
(inverse-distance steps along the target ray's kept epipolar line, first crossing bisected, front-most surface, explicit "passes
behind an edge" result), for every static pixel near and far. Then the kept image is reprojected correctly at depth edges under camera
translation, the rebuilt eye keeps its own shading and registration everywhere it saw the surface, and the other eye is used only for
true disocclusions (the strip a kept edge uncovered), movers (M1/M3) and hands (rig_from_fresh). That removes the need for v1176's
switch of the whole near field to the fresh eye under camera motion (a mix of two eyes' shading in one view), lets the far rule and
near rules merge into one, and gives a principled "hidden" signal for M3 and the hole fill. Cost: one dense march per pixel on the
kept depth (~50 taps), affordable if the fresh NearHit is then run only where the kept march reports a disocclusion. Prove it in the
lab with a near depth edge under 5-30 cm camera steps, then on v1172/v1165 replays with error vs next per exit.
