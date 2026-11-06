# VR v1037 (test candidate, 2026-11-05)

Follow-up to v1036. On a big object the hand no longer spreads, and the thumb stays next to the fingers at every grip size.

## What was wrong

Two things compounded on a large prop:

1. v1024's grip ball was capped at a **4 cm radius (an 8 cm ball)**, so a crate, a head or a body half only ever got the fingers *open* around it: mean
   flexion 50 deg, and the hand read as stretched ("palm stretches too much").
2. For a ball at or above 3.5 cm the v646 "thumb on a ball" search put the thumb's pad **on the ball's surface** - with an 8 cm ball that is the far side of
   it, so the thumb was carried away from the index and middle ("because of it thumb is placed too far from the other fingers").

## The fix

- `sphere_grip_ball_max = .03` (`src/finger_sphere.hpp`): a **6 cm ball at most**. Measured mean flexion of the four long fingers on the huge-object cases
  the check carries (a 15 cm skull, a 50 cm plank): **50 deg at the 4 cm cap -> 59 deg at 3 cm** (against 19.6 deg for the v642 whole-box ball). The hand
  closes on the object instead of spreading over it.
- `thumb_ball_min_radius = .045` (`src/finger_ik.hpp`): above every grip ball this build can produce, so the **ball-wrap thumb search no longer runs at
  all**. The thumb is posed by the ordinary solve at every size - out of the finger bodies and next to them (measured 3.6 mm clearance to the fingers, the
  tip on the palm skin, the same pose from a 1.5 cm to a 4 cm ball instead of reaching away for the ball).
- No other change: the grip ball's *sizing rule* (the thing's thin side, 1.5 cm floor), the flat grip for books, the held-object DEBRIS rule and the
  thumb's hinge geometry are untouched.

## Evidence

- `finger_sphere_check` (updated): the thumb keeps >= 0 clearance to the fingers and a tip above the palm skin at 1.5, 2, 2.5, 3, 3.5 and 4 cm balls, and
  the finger-flexion table prints the tighter huge-object grip. It passes, as do `finger_ik_check` and `held_body_collision_check`.
- Hand lab: `artifacts/hand-lab/big-close-v1037/skull_ball-views.png` (fist around the ball, thumb beside the index) against
  `artifacts/hand-lab/thumb-outside-v1035/skull_ball-views.png` (4 cm cap); index.html has every case.
- DLL: `build\compile-v1037.cmd` (MSVC 2022 x64 `/W4`), dll_exit=0, no new warnings; 14,929,408 bytes, sha256
  `d3085c30a1fdbd9a46552041a27b050788a1ba74ce3db282c67a4f7d7aeadc78`.

## Limits

- **Not headset-tested.**
- A huge flat prop still uses the flat grip (no ball): unchanged by this build.
- The thumb still cannot reach the index's distal phalanx (planar hinges); it now rests beside the fingers rather than on them.
- Carries the worktree's parallel-session changes; no full `tools\build-msvc.cmd` pass (pre-existing `/WX` stops).

Checklist: grip a crate, a head or a body half (the huge-object grip: the hand must close, the thumb beside the index), then a bottle/candlestick and a book
(unchanged).
