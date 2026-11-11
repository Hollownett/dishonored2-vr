# Performance mode: moving objects from engine motion (design)

Date: 2026-11-11. Base: v1132 (`94eed5b`). Branch `feat/perf-mode-motion`, worktree `VR-mods-perf-motion`.
Version numbers for this line start at **v1150** (v1141 is taken by another session).

## Why

Performance mode (`build/alternate-eye`) renders one eye per tick and rebuilds the other
(`src/stale_eye_warp.hpp`). The user's verdict after v814: artifacts, "especially around moving objects":
previous-frame afterimages, lagging guards, drifting edges.

The cause is known. The rebuilt eye starts from that eye's own kept frame, one tick old, and treats every
pixel beyond 1.5 m as static world. A moving object is drawn where it was. Every attempt to guess motion
from the images (v810 photometric check, v812 tiles) put false motion on still frames and was removed.
The v813 note says what is needed: "positive geometric evidence or engine motion vectors".

Two references use exactly that:
- Indurhra's D2 mod feeds colour, depth and engine motion vectors to PureDark's closed warp DLL.
- Metro Exodus VR (strings only) reprojects the kept eye "with depth + motion vectors", lets the "fresh eye
  confirm", and renders each eye with "the eye's own previous transforms".

We cannot ship PureDark's DLL (proprietary, D3D12, DLSS-only in their integration). We can do better with
what we own: the warp source, exact per-eye cameras, and the per-draw previous-MVP machinery.

## Goal

A moving object in the rebuilt eye sits where the fresh eye proves it is on this tick, drawn with the
rebuilt eye's own shading, with no afterimage at its old place.

## Anti-goals (these make it a failure even if movers look right)

- Any change to a still frame. With no measured mover the output must be bit-identical to v1132.
- Large areas filled from the other eye's colour (v805 rule: shading differs 10-20 %).
- A per-NPC, per-item or per-headset special case.
- Motion guessed from image content.

## Non-goals

Glass and other surfaces without depth; a rework of the hand layers; per-eye fog/noise cadence (Metro 1b);
paired mode. Hand-owned pixels (22 cm around a drawn palm) keep today's hand layers.

## Facts the design rests on

Verified in code (survey 2026-11-11):
- Engine MV = t1 of the TAA dispatch, R16G16_FLOAT, UV **current minus previous**, render rectangle at the
  top left of a full-size target, **includes the jitter step**, sky texels stay (0,0), not dilated.
- Static-world MVs in alternate mode span **this eye's own last render** (two ticks):
  `alternate_native_taa.hpp` seeds the engine's previous matrices from `alternate_taa::History`.
  `Receipt{current, previous}` holds projection, view, view-projection, position and jitter of both renders.
- `FsrTaa` keeps a per-eye depth copy (`Eye::depth`, R32F) and hands it to the warp. The MV is not kept.
- Per-eye object motion (`object_motion.hpp`, v828/v950) is **switched off in alternate mode**
  (`dxgi_probe.cpp:5964` passes view 0).

Inferred, **to be measured first (Phase 0)**:
- H1: entity MVs in alternate mode use the engine's cached previous MVP of the last rendered view, i.e. the
  **other eye one tick ago**. Then every entity pixel, even of a still prop, carries interocular parallax as
  "motion". That would also explain "moving guards blurry" under FSR/DLSS in this mode.
- H2: skinned meshes carry only the model transform in the MV, no bone motion.

## Design

### 1. One motion meaning for both consumers (Metro 1a)

In alternate mode every draw's previous MVP becomes **this eye's own previous render** (two ticks back),
like the camera already is. The engine's raw previous rows are the other eye's current rows of tick N-1; that
record's raw previous rows are this eye's rows of tick N-2. `PrevMvpTable` already stores rows per
(eye, frame); alternate mode needs the two-hop lookup instead of the paired one-hop. Draws that cannot be
proven fall back to the camera-only previous (static assumption), counted in `object-motion.json`.

Result: the MV buffer is "this eye, two ticks" everywhere. FSR/DLSS get correct mover history. The warp
gets one consistent input.

If Phase 0 refutes H1 this section is adjusted to what is measured; sections 2-4 do not change.

### 2. Motion texture per eye

Next to the depth copy in `FsrTaa::dispatch`, a small compute pass writes `Eye::motion`, RGBA16F, render
rectangle at the top left (same layout as `Eye::depth`):

- `xyz` = world-space displacement in metres of the surface at this pixel between this eye's previous
  render and this render (camera motion removed).
- `w` = object motion in render pixels after removing camera reprojection and the jitter step. 0 for static
  world, sky (depth 0) and anything unknown.

Math, camera-relative for precision: unproject the pixel with the current snapshot and depth -> `P_now`;
previous UV = UV - (MV - jitter step); unproject with the previous snapshot at the same view distance ->
`P_prev`; `xyz = P_now - P_prev`; `w = |previous UV - reproject_prev(P_now)| * render size`.
Snapshots come from the eye's `Receipt`. No receipt, no motion texture (`w` = 0 everywhere).

Accessor `FsrTaa::eye_motion(eye, w, h)` mirrors `eye_depth`. `GameXr` keeps the kept eye's motion texture
with `alt_kept_[eye]` the same way it keeps the kept camera.

### 3. Mover rules in the warp

New inputs to `StaleEyeWarp::apply`: `StaleMotion{texture, width, height}` for the kept eye and the fresh
eye (t5, t6), and in `StaleLayers` two fractions from the frame times:
`fresh_part = (tN - tN-1) / (tN - tN-2)` and `kept_part = (tN - tN-1) / (tN-1 - tN-3)`; 0.5 when times are
missing. A missing motion texture means "no movers" and today's path.

`mover(px) := motion.w >= mover_px` (constant, start 0.75 render px; Phase 0 measures the static noise
floor and the constant must sit at least 10x above it).

Per target pixel `t` of the rebuilt eye, hand-owned pixels excluded:

- **M1, mover the fresh eye sees.** Take the fresh pixel `p` on the target ray (existing fresh solve /
  near march). If `mover(p)`: `P_now` from fresh depth, `P_then = P_now - fresh_part * xyz(p)`, project
  `P_then` into the kept camera -> `q`. If `q` is inside the kept image and the kept depth at `q` agrees
  with `P_then` (same tolerance as `Confirm`), colour = kept at `q` (own shading). Otherwise colour = fresh
  at `p` (only the part of the mover the kept frame did not show). Geometry is always the fresh eye's.
  Rank: after the own hand, before world.
- **M2, no afterimage.** A world candidate whose kept pixel is `mover` is not usable as world and the
  far rule ("beyond reach, kept as is") does not apply to it. The existing fallbacks then show what is
  there now.
- **M3, mover only this eye sees.** For a kept mover the fresh eye cannot confirm (hidden from it or
  outside its view): candidate by moving the kept pixel forward by `kept_part * xyz` (two fixed-point
  steps). Usable only where the fresh depth says hidden or outside. If the fresh eye sees farther there, the
  mover is gone from that place: drop it.

Constants live beside the existing ones (`reach`, `hand_own_radius`). Knob `build/no-stale-eye-motion`
turns sections 2-3 off; status `stale_motion:[m1, m3, kept_fallback]` counts from the diagnostic dumps only
(no per-frame readback).

### 4. Evidence

Order per the lab-first rule:

1. **WARP lab** (`tests/stale_eye_warp_check.cpp`, software D3D11, real HLSL). New cases, each must fail on
   the v1132 shader and pass after:
   - striped guard at 3 m walks 20 cm between ticks: at the new place, own shading, wall at the old place;
   - same guard, other eye 30 levels brighter: guard and wall columns keep the kept eye's levels;
   - guard half hidden from the fresh eye by a pillar: the hidden half moves with the visible half (M3);
   - guard that left the view: no afterimage;
   - **still scene with a zero motion texture: byte-identical to no motion texture** (anti-goal 1);
   - mover that overlaps a still hand: hand pixels unchanged.
2. **Dumps and replay.** `stale-warp-N` dumps add `-kept-motion.rgba16f`, `-fresh-motion.rgba16f` and the
   two fractions in `meta.json`. Old dumps (no motion) must replay unchanged.
3. **Sim** (alternate-eye, guards walking, v814 recipe): 0 fallbacks, dumps at the view centre scored by the
   flicker metric against the eye's next own frame; object-motion counters; warp GPU time (budget: +0.3 ms
   over today's 0.51 ms at 2709x2898).
4. **Headset.** Only the user's eyes decide image quality. A sim pass is a candidate, not a result.

## Phases and owners

| Phase | Work | Files | Owner |
|---|---|---|---|
| 0 | Measure H1/H2 and the static noise floor: diagnostic dump of MV + depth + receipts for both eyes on consecutive ticks, offline analysis script | `fsr_taa.hpp`, `dxgi_probe.cpp`, `tools/` | Opus A |
| 1 | Warp mover rules M1-M3 + lab cases, against a synthetic motion texture | `stale_eye_warp.hpp`, `tests/stale_eye_warp_check.cpp` | Opus B (own worktree) |
| 2 | Per-eye previous MVP in alternate mode; motion texture pass; plumbing to `GameXr::submit`; dump fields | `object_motion.hpp`, `fsr_taa.hpp`, `dxgi_probe.cpp`, `game_xr.hpp` | Opus A |
| 3 | Knob/pin checks, replay script, build scripts, docs | `tests/alternate_eye_knob_check.py`, `tools/`, `docs/` | Sonnet |
| 4 | Sim run and scoring; headset candidate | - | coordinator (game access) |

Phases 0 and 1 run in parallel; they share no file. The interface between them is section 2's texture.

## Left to the builders

Shader structure, the exact fresh-ray search for far movers, tolerances within the stated rules, how the
two-hop table lookup is stored. Any rule that cannot be met as written is reported, not worked around.

## Calls made by the coordinator (not by the user)

- Fix the motion vectors at the source (section 1) instead of decoding two meanings in the warp.
- Hands stay on the hand layers in this round.
- World-space displacement in the texture, not pixel vectors, so the warp needs no extra matrices.
- New worktree off v1132; the uncommitted v1141 work in `VR-mods-held-place` is not touched.
