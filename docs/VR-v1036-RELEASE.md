# VR v1036 (test candidate, 2026-11-05)

One fix only: the thumb is no longer bent into the fingers on a grip.

## What was wrong

The v646 "thumb on a ball" search put the thumb's pad **on the grip ball's surface**. That was right for the ball v642 used (a book's 13-14 cm, a
candlestick's 15 cm), but v1024 replaced it with the thing's *thin side* — 1.5-3 cm for a bottle, a candlestick, a spoon — and a 2 cm ball sits right where
the folded index and middle fingers are. The search then bent the thumb **into the finger body** to reach it (user, headset: "thumb bone placed to the
fingers, should be placed out of the fingers ... bent into an unrealistic position").

Measured on the player's rig before the change: the thumb's clearance to the finger bodies went negative (inside them) for every ball under 3.5 cm, and its
tip came out between the index and middle.

## The fix (`src/finger_ik.hpp`)

- `thumb_ball_min_radius = 3.5 cm`: the ball-wrap search only runs for a **real grip ball**. Below it the ordinary thumb solve poses the thumb, which keeps
  it clear of the palm and of the fingers (`ThumbBody::clearance`).
- Measured after: on 1.5-3 cm balls the thumb is **3.6 mm clear of the fingers** and its tip on the palm skin (out of the fingers, alongside them); a 3.5 cm
  ball still wraps (tip 58 mm off the ball), a 4 cm one as before.
- Nothing else changed: the grip ball's radius, the flat grip for books, the held-object DEBRIS rule and the thumb's hinge geometry are untouched.

## Evidence

- `finger_sphere_check` now encodes the rule: for a ball below the threshold the thumb's clearance to the fingers is >= 0 and its tip never sinks into the
  palm; at or above 3.5 cm the tip sits on the ball's surface. It passes, as do `finger_ik_check`, `finger_touch_curl_check` and
  `held_body_collision_check`.
- Hand lab, same rig and solver: `artifacts/hand-lab/thumb-outside-v1035/candle_ball-views.png` (the thumb beside the index instead of over it) against
  `artifacts/hand-lab/flat-v1029/candle_ball-views.png`; index.html has every case.
- DLL: `build\compile-v1036.cmd` (MSVC 2022 x64 `/W4`), dll_exit=0, no new warnings; 14,929,408 bytes, sha256
  `81b7f96f5ebf7a1901f365a58ad8dfe01633f2bb9732c54f88d1525a364c3b14`.

## Limits

- **Not headset-tested.** The thumb's clearance is measured, not seen in a headset.
- The thumb still cannot reach the index's distal phalanx (its planar hinges reach the PIP at best); this build only stops it from bending *into* the fingers.
- The number is 1036 because a parallel session published 1032-1034; this build carries the current worktree, including that session's changes.
- No full `tools\build-msvc.cmd` pass (pre-existing `/WX` stops).

Checklist: grip a candlestick/spoon/bottle (a small grip ball) and look at the thumb - beside the index, not through it; then a crate or a head (a 4 cm ball)
and a book (flat) - both unchanged.
