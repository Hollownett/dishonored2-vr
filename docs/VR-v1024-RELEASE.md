# VR v1024 (test candidate, 2026-11-05)

The grip ball. A physical prop is wrapped by the fingers on a small ball at the grip place again, sized by the thing's own
thin side, instead of on the ball round its whole box (v642). User request: "when I grab physical object I grab a sphere around
it and it can be very big sphere ... change it to a small sphere at a grip place for physical props. It should provide more
native grip."

## What the ball was

v642 sized the ball as the sphere round the prop's whole box, so every corner of the prop stayed inside it. That fixed a v641
complaint (a finger seemed to pass through the thing) but the radius is the box's diagonal, not the thing's thickness: a
candlestick 15.3 cm, a bottle ~15 cm, a book 13.7 cm, a folder 19.4 cm, a plank the 0.5 m cap. The palm sits on the prop's own
surface (the motor seats the grasp point there, Task 12), so a ball that size stands off the palm and the fingers only open:
holding a book looked like holding a beach ball, and the prop floated inside an invisible sphere.

## What it is now (`src/finger_sphere.hpp`)

- `sphere_grip_ball_radius`: the ball's radius is the thing's **thin half side** — the thing's own cross-section under the
  hand — floored at 1.5 cm and capped at 4 cm (`sphere_grip_ball_max`; past ~37 mm the fingers stop wrapping and only open,
  `docs/research-hand-grip-placement.md` §"SphereGrip").
  Spoon 1.5 cm, candlestick 2.2 cm (its shaft), book 2 cm (its thickness), bottle 3.5 cm (its radius), crate/skull 4 cm.
- The ball is still built in the drawn palm's frame and never re-read, so the v638 stability result stands (0 deg per-frame
  joint change on the ball); the fingers wrap the same shape every frame.
- The ball's position is unchanged and is already the grip place: its near side lies on the palm skin at
  `held_palm_depth`, which is where the motor seats the prop's surface.
- Both ball sites use the rule: the physical grab's ball (`NativeGrab::begin_hold`, decided once at the bind) and the native
  `HoldObject` limb ball (`dxgi_probe.cpp`, `SphereGripLatch`) — a severed body half was a 0.40 m ball before.
- `build/legacy-sphere-grip-whole-object` restores v642's ball round the whole box, for an A/B in the headset.
- Status: `grab_focus[i].sphere_mm` is the ball the fingers are wrapping this hold (0 when nothing is held).

## Evidence (offline; no headset run)

- `tests/finger_sphere_check.cpp` passes: the radius rule (thin side, floor, cap, unreadable box, own axes through a 30°
  tilt), the latch, the ball on the palm skin, the jitter result (0 deg), the thumb clear of the palm at 1.5-4 cm balls, and
  the legacy rule against v646's thumb case.
- Finger closure, same rig and solver, the four long fingers' mean flexion (v642's whole-box ball -> grip ball):
  spoon 27° -> 72°, candlestick 19° -> 65°, book 21° -> 67°, folder 17° -> 72°, skull 20° -> 50°, plank 10° -> 50°.
  `artifacts/build-logs/finger_sphere_check-v1024.log`. The old ball leaves the hand open, the grip ball closes it.
- Hand lab renders, RIGHT hand posed by the current solver, every case's old path, v642 ball and grip ball:
  `artifacts/hand-lab/gripball-v1024/index.html` (e.g. `candle_ball-views.png`, `book_ball-views.png`). The candlestick's fist
  closes around its shaft; the book's fingers close around its 4 cm thickness.
- `finger_sphere_routing_check.py` and `finger_collider_routing_check.py` pass. `finger_touch_routing_check.py` fails on a
  v697 assertion that HEAD also fails (`hand_loadout_bits(...)` vs the asserted string) — not this change.
- DLL: MSVC 2022 x64 `/W4`, `build\compile-v1024.cmd` dll_exit=0, no new warnings in the changed files,
  14,906,880 bytes, sha256 `ceae22139af656e76eca5ee093cc3e250b5f86dad8ecd9bdfd2297dac41b3bc7`. No full
  `tools\build-msvc.cmd` pass: the worktree still stops on pre-existing `/WX` warnings (same as v1022/v1023).

## Limits and what to look for in the headset

- **Not headset-tested.** Nothing here establishes how the grip feels on a real controller.
- Wide flat props get a fist, not a flat hand: the ball is one sphere, so a book or a folder lying on the palm has the fingers
  closing through its thickness instead of lying on its cover (the render shows it). If that reads worse than the old open
  hand, the fix is a slab contact for thin props, not a bigger ball.
- The thumb joins the fingers on 4 cm balls but stays alongside the index at 1.5-3 cm (62-80 mm off the ball): the small balls
  give a closed fist, not a thumb on the object.
- The cap is a constant, not a property of the hand: a very wide prop (a crate face) still opens the hand to an 8 cm ball.
- The ball is decided once at the bind from the box read then; a prop whose box is wrong or unreadable gets the 6 cm unknown
  ball. A prop that changes shape after the bind does not re-decide it.

Checklist: grab a bottle (body and neck), a candlestick, a book, a folder/paper, a spoon, a crate or plank, a severed limb; look
for a closed grip at the hand rather than an open hand around a sphere; the prop must not visibly slide out of the fingers; the
fingers must not twitch while the prop lags behind the hand. Report `grab_focus[i].sphere_mm` per prop.
