# AFW v2 (stale-eye warp) hand-over, 2026-11-12

Performance mode renders one eye per tick. The other ("stale") eye is rebuilt from its own **kept** frame (colour, depth, motion, rig
mask, one or two ticks old) plus the **fresh** eye's frame of this tick. AFW v2 is a forward-splat rebuild, modelled on Metro Exodus
VR AER+ (docs/AER-REFERENCE-RESEARCH-2026-11-11.md). It lives in `src/afw2.hpp` (class `Afw2Pass`). It runs from
`StaleEyeWarp::apply` (`src/stale_eye_warp.hpp`) when `layers->afw2 && depth_read && fresh_read && !layers->cut`. Otherwise the old warp
(same file) runs. The old warp is the default in game; AFW v2 is opt-in.

## 1. Pipeline (`Afw2Pass::run`, in dispatch order)

Grids: **kept grid = target grid = render size** (1922x2070, `grid`). **Fresh grid** = fresh depth rectangle (`freshGrid`).
**Display** = colour size (2496x2688, `display`). Depth is reverse-Z: `ahead = m11/(d+m10)` (`KeptAhead`/`FreshAhead`).
The camera maps are rigid transforms built on the CPU by `afw2_rigid`/`afw2_mul`/`afw2_inverse`:
- TK: target camera to kept camera; TF: target to fresh; KT, FT are their inverses; KF: kept to fresh.
- GC/GL/GR: the layer maps for the carried item and the left/right hands.
HLSL uses `Apply(M,p)`, a macro over rows. fxc miscompiled cbuffer arrays passed as function arguments.

Per-texel buffers (all structured, `buffer(count,stride)`):
- motion_[2]: uint2, packed (xyz as f16 metres + 16-bit flags); `MotionIn(at)` / `PutMotion(at,m)`.
- keys_[2]: uint key = asuint(1/z) with the low 3 bits as flags (kKeyRig=1, kKeyMoving=2); kSkyKey=8 marks sky.
- landed_[2]: uint, packed source texel (x | y<<15) | kFilled(bit 31) | kSeed(bit 30).
- origins_[2]: uint, the index of the kept texel whose key won the target texel.
- near_[2]: uint2 (nearest seed distance, signed zfore; negative = the foreground there is rig).
- fresh_keys_, decisions_ (uint2), tiles_ (append buffer), fill_args_ (indirect dispatch arguments).
- holes_: [0] holes left after the fill, [1+tile] tile has a hole.
- votes_: 60 uints. [2..7] look votes per owner; [8] max displacement; [10+o*2] outline votes; [18+o*2] held outline votes;
  [26]/[27] target-validation votes; [28..43] target-outline votes per motion source; [44..59] fresh-outline votes per motion source.
- stats_: 4 uints, read back as `rig_stats`.

Motion flags (motion w): kConfirmed=1, kRig=2, kMoving=4, kDrop=8, kSky=16, owner bits 5-6 (1 head's/carried, 2 left, 3 right),
kOutside=128, kHeld=256, kOwn=512 (moved by its own rig motion).

1. **Scatter / ScatterWrite** (fresh grid). A fresh mover (motion w >= mover_px) maps back to the kept texel it came from. The winner
   (InterlockedMax on keys_[1]) writes its motion into motion_[1] for that texel and the same-surface texels within 2.
2. **Motion** (kept grid, writes motion_[0], votes).
   - World texels: their own vector, confirmed by `Chain` (3 Looks into fresh, 3 % + 1 cm, class-aware, Metro's chain); else the
     scattered motion; else still.
   - Rig texels: (a) own rig motion (mask 255) via Chain, else (b) the hand layer `LayerStep`.
   - Also runs `TargetVote` (every 4th texel), the outline votes, and the per-source outline votes (v1206).
3. **Repair** (motion_[0] to motion_[1]).
   - Unconfirmed texels adopt the nearest confirmed same-surface motion within 4 texels (ring search).
   - Confirmed moving texels that are outvoted in their 5x5 neighbourhood take the majority motion.
   - v1206: each hand's arm texels switch to the winner of `LayerWins(owner)` (target outline votes when `TargetOn()`, else fresh-outline
     votes; 16 votes minimum; the layer needs a 5 % higher on-outline share to win).
4. **Splat** (keys_[0]). `SplatOf` projects each texel's square (four corners, each at `CornerAhead` depth) into the target grid;
   InterlockedMax the key into every covered texel. `Lands`: with the target, a rig key lands only where the target has rig.
5. **Origins**: for moving/rig texels, the winner's index goes into origins_[0].
6. **FreshSplat** (fresh_keys_): the fresh eye's rig (only for a group that stood down and no usable target), and near fresh movers.
7. **Crack** (to keys_[1], landed_[0]+[1], origins_[1], near_[1]).
   - Seals 1-texel seams between two keys of one surface and marks seeds (reached texels beside a hole).
   - Appends hole tiles to tiles_; then `CopyStructureCount` into fill_args_.
   - CopyResource keys and origins [1] to [0]: the crack reads their neighbours, so they cannot be written to both directly.
8. **Fill**: jump flood, step 512 down to 1 (10 passes), `DispatchIndirect` over hole tiles only, ping-pong buffers.
   - Takes the nearest seed **behind** the foreground around the hole (zfore includes rig; rig keys are never sources when TargetOn).
   - Bound: `2*nearest + 2 + camera term + max displacement`.
   - Invariant: tiles without holes hold identical data in both buffers.
9. **Decide** (CS, kept grid, writes decisions_ uint2 = q as unorm16 x2, plus how/code/ahead f16 bits; see the comment above
   `Rec`). All resolve rules run here at render size:
   - fresh near-mover keys (code 10);
   - rig-target rules 1-3 (kept rig first, codes 1-5, then "world shows through");
   - turn-only for key==0 (code 9);
   - holes outside the kept view, or vacated by the rig, take the fresh eye's world on the ray (code 8; v1209: any hole whose
     foreground is rig, or where the kept frame had rig on the ray);
   - still frame raw (`kRaw`);
   - else Smooth or Clean at q, with the "apart" test (0.75 px).
   Also fills rig_stats (every 16th texel of a used target: count, count resolved to the world, target state).
10. **ResolvePS** (display). Reads its texel's decision; q = uv + (decision q - texel centre); kWhole/kLoad use q as is. Then
    `Smooth` (5-tap Catmull-Rom + 2x2 clamp), `Clean` (no colour across depth edges), or `FreshRigSample` (masked bilinear, falling back
    to the texel centre's sample).

Constants and where they come from:
- **From Metro:** Tol = 1 cm + 3 % (kDepthTolerance); scatter seed radius 2.
- **Ours, measured:** afw2_moving_px = 0.25 px (v1178: still props p99 0.05 px); "apart" 0.75 px; the fill bound; vacated-hole rules
  (v1190/v1209 swing); TargetOn >= half on and >= 32 votes (v1197); LayerWins 16 votes and +5 %.
- **Edge band:** `StaleLayers::border` = {0,0} since v1191. Band 0 beat the default on every border replay; the v1183+ scenes have no band.

Profile: `stage_ms` (10 stages, `stage_names`) and `rig_stats`, both from timestamp queries read back a few passes late.
Engine publishes them as perf_gpu_ms.afw2_stages and alternate_eye.afw2.rig_target_state / rig_world_share.

## 2. Inputs (StaleLayers and views)

- kept/fresh colour: _SRGB views at display size (t0/t2). Depth: R32 render size (t1/t3). Motion: rgba16f (t4/t5, xyz world metres
  per tick, w object-motion px). Rig masks: R8 (t6/t7).
- Rig mask values as the shaders read them:
  - >0: rig (`KeptRigAt` / `FreshRigAt`);
  - >254.5/255: own motion valid (`KeptRigMoves`);
  - <253.5/255: held item (kHeld; 253 = held; 252 would also read as held, nothing treats it specially);
  - 254: rig without valid motion.
- Rig target at **t15** (engine binds it for CS and PS; `run(...,rig_target=true)`): R32G32F render size, x = reverse-Z depth, y = class
  (255 rig, 253 held), 0 = none.
  - Engine side now draws two slices (one per eye hypothesis) and hands over the right one. I have not seen that code.
  - Validated per pass: `TargetVote` reprojects every 4th target texel into fresh and votes on/off against fresh rig at that depth.
    `TargetOn()` is false when there are >= 32 votes and fewer than half are on. Then the whole pass runs as if there were no target.
    Lab W guards this; the engine target was 290 texels off in v1197.
- StaleLayers fields used: world, fresh, carried, hand[2], palms, kept_axes, kept_fov / target_fov / fresh_fov, fresh_part,
  kept_part, motion_rules, rig_motion_rules, rig_from_fresh, hand_moving[2], border, cut, afw2, rig_target.
  - `still` (TK identity) gives a byte-exact copy (lab C).

## 3. Lab (`tests/stale_eye_warp_check.cpp`, `afw2` argument)

Cases, each from a real defect:
- **gpu_afw2_full**: full-size dry run at 2496x2688 / 1922x2070, every input including t15, 4 passes; no throw, no fallback, no black.
  It caught the v1196 shader compile failure.
- **A**: near object before a wall, camera 2/5/15 cm sideways and forward (strip, foreground, holes).
- **B**: thin pole.
- **C**: still frame byte-exact.
- **D**: moving hand, grip right / over-moved; ghost test accepts fresh-shaded wall since v1209.
- **F**: anti-aliased leaning edge (stair-steps).
- **G**: plate seen edge-on.
- **H**: hand outside the fresh view.
- **I**: slow guard (0.25 px threshold).
- **R**: rig target, both orders, grip right / over-moved / still, post in front.
- **S**: no held-stick colour in the place the hand left.
- **P**: phantom stick in the target shows the wall.
- **W / W'**: wrong-camera target is ignored; right target is used (rig_stats).
- **Q1 / Q2**: per-hand motion-source choice.
- **V**: area a 9 cm hand left shows the wall, with and without target.
- **E / E'**: walking guard, with and without kept vectors (scatter).

Run:
- `build\kmarch\lab.cmd afw2` builds from `src` and runs at low priority after `build\d3d-guard.cmd`.
- Guard refuses while Dishonored2.exe runs or `VR-mods-amd-ags\artifacts\agent-locks\game` exists: wait and retry, never work around.
- `build/kmarch/labs.sh <root>...` runs the lab of scratch roots (`<root>-lab.cmd`) with guard retry.
- `AFW2_VERBOSE=1`: extra prints in some cases.
- Debug colours (`rig_debug=1`) are written through the _SRGB view. Decode with `((v/255+0.055)/1.055)^2.4*255/20`.

## 4. Replay and scoring

`tools/stale_warp_replay.cpp`, built and run by `build\kmarch\replay.cmd <root> <out> <dump dir> N...` (guarded, low priority).
- `-replay-on.rgba` = AFW v2, `-replay-off.rgba` = old warp (with STALE_REPLAY_AFW2=1). Prints per-stage ms (WARP: relative only),
  holes / tiles, votes, rig stats.
- Env knobs:
  - STALE_REPLAY_AFW2, STALE_REPLAY_NO_SHEETS, STALE_REPLAY_RIG_ORDER;
  - STALE_REPLAY_RIGSRC (writes `-rigsrc.rgba` code map);
  - STALE_REPLAY_RIGTARGET=0, STALE_REPLAY_BORDER=auto|0, STALE_REPLAY_RIGMOTION=0 (rigid layers only), STALE_REPLAY_MOVING, STALE_REPLAY_AB.
- In bash, write output paths as `"$K"'\o\'"$s"`. `"...\\$s"` loses the variable (the tool unescapes `\\`).

Truth dumps (sim-pose-hold, meta `truth_next`): `-next.rgba` is the stale eye rendered at exactly the warp's target pose.
- Skip warm-up dump 1, and any dump without next.rgba.
- Drop dumps whose truth drifted: `next_hands_moved_m` / `next_stage_mm.drawn` > ~1 mm for hand regions, `next_camera_step_m` for the frame.
- Still dumps after warm-up differ from truth by 0.3-0.9 levels (the noise floor).

Scoring tools:
- `tools/stale_warp_truth.py <dumps> name=dir[:suffix] ... --dumps N --sheets dir`: per-region mean |R-T| plus a ghost metric.
  - Regions (first wins): border = right/bottom 48 px; held = target 253; rig = target 255; vacated = kept rig minus target;
    edge = within 6 px of a kept depth step > 15 %; near < 1.5 m; far.
  - `game` = the dumped in-game `-warped.rgba`.
- `tools/stale_warp_handcheck.py`: whole frame and next-rig pixels vs the unwarped kept frame; non-zero exit on failure.
- `build/kmarch/handsrc.py`: next-rig error per AFW source code, against the old warp.
- `stale_warp_rigcheck.py`: target vs fresh/next masks. `stale_warp_rigscore.py`, `stale_warp_sheet.py`, `stale_warp_score.py`: older tools.
- `build/kmarch/hlsl_extract.py` + fxc (Windows Kits 10.0.22621 x64): offline shader compile and instruction counts, no device.

Scratch rule:
- Experiment in a copy root (`build/kmarch/wip*/src`, `tests`, `tools`; `<root>-lab.cmd`). Replay with `replay.cmd <root>`.
- Copy to `src/afw2.hpp` and `tests/` only after the full lab is green there. Then run `lab.cmd afw2` on src.
- To show a new lab case fails on the committed pass: `headq` root = committed src + new test.
- Keep outputs small: delete `build/kmarch/o*` replay dirs after scoring; the disk was full.

## 5. State (v1209 truth sets; my replays, old warp = replay-off on the same frames)

Columns: border, held, rig, vacated, edge, near, far, then all.

| set | AFW v2 (now) | old warp |
|---|---|---|
| room | 1.20, -, 1.56, 3.10, 3.38, 0.82, 1.21 (all 1.32) | 1.89, -, 1.70, 4.72, 3.75, 1.07, 1.21 (all 1.50) |
| swing | 0.37, 4.16, 0.61, 2.24, 0.66, 0.15, 0.30 (0.30) | 0.78, 5.27, 0.90, 2.33, 0.90, 0.17, 0.30 (0.38) |
| street | 8.40, -, 16.82, 27.31, 8.68, 6.44, 1.50 (3.94) | 7.77, -, 18.35, 35.11, 7.29, 4.96, 1.18 (3.54) |

Hands (next-rig, v1202/v1197 room 13-16, v1206 street 14-16): AFW v2 at or below the old warp except:
- v1197 room 14: 0.46 vs 0.41;
- v1206 street 14 and 15: 12.9 vs 12.5 and 5.74 vs 5.45.
Never worse than unwarped.

In-game cost (v1209, room/street, mean ms): repair 0.34/0.35, splat 0.31, origins 0.12, crack+copies 0.36, fill 0.18-0.21,
decide 0.31, resolve 0.64-0.72, total 2.4-2.6. Target: 1.5.
- The v1209-plus commits are not measured yet: 5-tap filter, own offset, 8-byte motion, single near write.

Proven vs inferred:
- **Proven by lab and replay:** the items above.
- **Replay fidelity:** shown by matching in-game numbers on v1197 (room 4, street 7).
- **Inferred only:**
  - street dump 12 rig 22.5 = a moving NPC shadow on the arm (thumbnail only);
  - v1202 street 15/16 frame error = an NPC sword swing;
  - WARP stage timings do not predict GPU timings.

## 6. Open work, ranked

**(a) Street loses to the old warp:** edge band 8.40/7.77, depth edges 8.68/7.29, near 6.44/4.96, far 1.50/1.18.
- Not yet investigated in detail. Start with `stale_warp_truth.py --sheets` on v1209-streettruth camera dumps 4-13, comparing
  `o/s-...:replay-off` with replay-on, plus RIGSRC codes on the worst tiles.
- Hypotheses:
  1. Near: hands/props nearer 1.5 m outside the rig mask splat at render size and resolve with Clean, which picks one surface per pixel
     (stair-steps). Look at the "apart" test and Clean.
  2. Far/border: the old warp's continuous homography resamples once. AFW splat + resolve at render size loses sub-pixel detail on fine
     far texture (5-tap filter, offset quantisation unorm16 = 0.04 px). Try Smooth with the 9-tap filter only on far texels, and check
     `kRaw`-like exact paths when TK is near identity.
  3. Edge: the fill picks the wrong seed (code 7) where camera steps uncover background. Compare code 7 vs 8 per tile.
  4. NPC movers: street has walking guards; check that the scatter and chain confirm them (E/E' lab cases cover only rigid walking).

**(b) Cost:**
- Repair: tile-limit, e.g. a tile list of unconfirmed / moving / rig texels from Motion. Watch the in-place race; ScatterWrite already
  races on shared seeds (nondeterministic, minor).
- Splat: every texel is needed. Cheaper `CornerAhead`? Early-out for sky?
- Crack: drop the keys/origins copies by making Fill read neighbours from [1] when the tile has no hole.
- Display pass (0.64-0.72 before the 5-tap change): one decision read + 5 taps + 1 bilinear + 4 clamp loads. The coordinator suggests
  folding it into the engine pass that writes the eye output.

**(c) Fragile:**
- fxc: an unrolled loop with `return` inside a compute shader found nothing (lab R failed). Use `[loop]` with a `const int n=nn` copy
  (Decide's inpaint searches).
- fxc: cbuffer-array function arguments and indexed local arrays miscompile (`Apply3`, `CornerAhead`).
- fxc: a `//` comment can swallow declarations on the same line (the v1196 failure). Use `/* */`.
- Raw strings are split at `)HLSL" R"HLSL(` (MSVC string size limit).
- HLSL for-loop variables leak into the enclosing scope (X3078 warnings).
- Two UAV declarations share registers (u0, u4, u6 across entries). Fine while no entry point uses both.
- Shader flags: STRICTNESS | OPTIMIZATION_LEVEL3.
- PS output RTV is _SRGB.
- Stale-folder trap: always regenerate the src baseline in the same batch you compare against.
