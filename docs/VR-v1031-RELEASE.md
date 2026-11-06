# VR v1031 (test candidate, 2026-11-05)

The rule, as asked: **a held object never collides with the hand or the player's body.**

(The number is 1031 because a parallel session published a v1030 of its own, canted-eye/FPS work; that DLL was replaced by this compile, so its candidate
manifest now fails its hash check and it must be rebuilt. This build contains the current worktree, so it also carries that session's v1030 changes.)

## Why it could

A motor-held prop (the physical grab) stays on the engine's RIGID_BODY layer 3, so it keeps colliding with everything on its mask, the player's capsule
included: carrying a crate or a body shoved the player and fought the hand. The mod only stripped the held layer from the **hand body's** mask
(`HandFilterKeeper`), which fixes hand↔prop but not prop↔player. The engine's own hold solves exactly this by putting the item on the **DEBRIS** layer
while it is held (`native_held_object.hpp`: a throwable on RIGID_BODY "collides with it there", so it takes DEBRIS for the hold and gets its word back
afterwards).

## What v1031 does (`src/held_body_collision.hpp`)

- At the bind of a physical hold (game thread, once), the held body's filter word is rewritten with the DEBRIS layer and its own mask kept, through the mod's
  already verified writer (`NativeHeldObject::write_filter` → 0x97600 with the busy pre-read; the same call the native hold's layer swap uses).
- When the hold ends - open fist, throw, timeout, tracking loss, another object taking the hand - the body gets its **exact** previous word back. The restore
  re-reads the record's handle pair and only writes if the record is still that body and still carries the DEBRIS layer it wrote; a respawned or
  engine-rewritten body is never clobbered (counted `skipped`).
- If the engine puts the old layer back mid-hold, the word is reasserted once (`reasserted`).
- Corpse limbs keep their own rule (no prop filter is written for a bone hold).
- `build/legacy-held-collision` restores the old behaviour (a held prop keeps its layer and can shove the player), for an A/B in the headset.
- Cost: one 4-byte read per held hand per frame; writes happen once per hold.
- Status: `held_collision {swapped, restored, refused, skipped, reasserted, legacy}`.

## Evidence (offline; no headset run)

- `tests/held_body_collision_check.cpp` (new, in `tools\build-msvc.cmd`): drives the ledger against a fake pool record with the engine call replaced by a
  recording seam. Proves the DEBRIS word is written exactly once per hold with the mask kept, is not rewritten while held, is reasserted when the engine
  reverts it, the exact original word comes back once and only once, a changed/respawned record is skipped rather than clobbered, a second object taking the
  hand restores the first before its own swap, and an unreadable game pointer is refused without any write.
- `tests/held_body_collision_routing_check.py` (new): the include, the instance, the per-frame loop for both hands (including the release path that runs when
  nothing is held), the knob and the status counters.
- `build\compile-v1031.cmd`: MSVC 2022 x64 `/W4`, dll_exit=0, no new warnings in the changed lines; 14,925,824 bytes, sha256
  `14a2cbb04fb62353732416a075bf8bfaf2f0bbe760b63dfb0e75b5df22ff5b56`. `finger_sphere_check`, `finger_sphere_routing_check`,
  `eye_capture_routing_check` and `held_body_collision_check` pass.

## The thumb (not fixed here)

The thumb does not reach the grip ball: at 1.5-4 cm balls its tip stays 62-80 mm off the ball's surface in the finger solver, so it hangs beside the index
instead of resting on the grip. `finger_ik.hpp` already has a "thumb on a ball" search (v646) but its cost balance prefers a pose that leaves the ball; a
weight experiment (surface pull 10 → 40, index-pad pull 1 → 0.1) moved the 4 cm case further out (44 → 58 mm), so the search itself needs restructuring
(target the ball's near surface from the index fingertip, finer grid) rather than a weight change. It is not in this build.

## Limits

- **Not headset-tested.** The filter write is the engine's own call on an engine body: the code path is verified (`write_filter` is the same call the native
  hold uses) and the ledger is unit-checked, but only a headset session shows a carried crate no longer shoving the player.
- The held prop now collides like a native held item (DEBRIS): it can also pass through NPC bodies while held. That is the engine's own hold behaviour.
- The build carries the worktree's parallel-session changes; no full `tools\build-msvc.cmd` pass (pre-existing `/WX` stops).

Checklist: carry a crate/bottle into a wall and walk into it (the player must not be pushed), grab and release repeatedly (the prop must land and rest
normally, `held_collision.restored` equal to `swapped`), let go by losing tracking, and report `held_collision`.
