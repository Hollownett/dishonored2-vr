# Physical hands phase 1 — headset checklist

Branch `feat/physical-hands` (6616d5d..8365631). Compiled from the final whole-branch review; ordered by risk. Offline evidence: Linux harness 483 passed / 0 failed / 41 skipped; push lab 183 judged scenarios (G1 clean 1–20 kg, smooth per-frame input). Nothing here has been built with MSVC or run in a headset yet.

Run this after the Windows build passes. Keep the status JSON (`hand_clamp`, `hand_push`, `hand_contact`, `palm_offset`, `carry`) from every run.

**0. Build and boot**
1. `tools\build-msvc.cmd` passes on Windows (C1/C2 shadowing fixed in be60a78), and the DLL loads.
2. In status: `hand_clamp.missed_notes` and `repeated_updates` stay `[0,0]`. `probes` reaches `[24,24]` once the binding is captured, matching the visible hand.

**1. Frame budget and engine assumptions** (these decide whether everything else is trustworthy)
3. With both hands pressed near geometry and on props, `hand_clamp.cast_us` ≤ 500 and `cast_us_max` stays reasonable. Note whether `tip_probe` turns false or `alternate` turns true, and whether they recover after menus or loads. Check `hand_contact` `cost_us`/`cost_max_us` while pushing guards.
4. `unanswered` stays small relative to frames (the render thread does not outrun the game thread).
5. `reacquire_blocked` stays 0 in normal standing play: the chest sweep must not hit the player's own capsule. Crouching under a table may raise it, and the hand must follow the controller there without flicker.
6. Press a bottle into a wall: `exclusion.hits` > 0 and `self_hits` == 0. If walls do not report, create `build/hand-clamp-exclude-info-zero` and compare. `exclusion.unmapped` should stay low while touching bottles.
7. A death reload shows logic 0 or bit 15 and forgets the planes: no hand pulled toward the old position. Pause and quicksave do not forget. `reacquire_holds` rises after menus.

**2. Combat, C9** (user-accepted behaviour must not regress)
8. Aim, the crosshair, shots and powers are unchanged with the new left-aim publish order (published after the hands).
9. Attack and block detection and swing speed are unchanged. Takedowns: `yields` rises and the arms follow the native animation.
10. A gun pressed or jabbed (4–6 m/s) into a thin door: the shot hits the near side. Then a free hand's barrel tip pressed into a 2 cm door (parked "barrel probe"): note whether a shot passes.
11. With the hand stopped at a wall, the pistol crosshair follows the drawn barrel. Confirm this is acceptable.
12. Draw the sword and the pistol after a hand fit: the weapon direction is unchanged (translation only, no tilt). Check that the 3 cm offset of the visible weapon from the controller is acceptable.

**3. G1: the hand stops at surfaces**
13. Press a palm onto a table and onto a wall: the hand rests on the surface while the real hand keeps going.
14. Slide along a bare wall in both directions: no fingers inside, no jitter. Watch for sticky wall ends (0.1–0.2 s hold past the end) and faint judder on a convex crate edge.
15. Swat a bottle and a wall fast. The bottle flies and the hand never shows inside the wall. Accepted: a one-frame pop on a 2 m/s swat, and 4–11 cm jumps at wall-contact onset. Report how visible both are.
16. Rising and descending diagonal reaches into a wall (about 45°), and a −35° light touch: watch for fingers 2–3 cm inside (parked "per-finger first contact").
17. Open a door you were touching: watch for an invisible-door hold up to 6.5 cm when holding still or sliding sideways. Pushing forward should free it within 15 frames.
18. Push into a narrow slot (hand wider than the gap): note partial overlap (parked least-violation).
19. **Corpse against a wall (fixed in c19c83d, 8365631):** a dead guard slumped against a wall. Push through his arm toward the wall, and slide the fingers up the wall beside him: the drawn hand must not enter the wall. Read `exclusion.unmapped`.

**4. G3: snap-back**
20. Reach through a wall and pull back: one clean snap-back, no flicker.
21. Walk and snap-turn with a hand on a wall: no snap-back.
22. A deep circling press on a pinned bottle snaps back at 35 cm, which is the expected G3 behaviour.

**5. C2: guards**
23. An awake guard: the hand stops and nothing else happens (no push, no AI reaction).
24. Push a knocked-out guard and a dead guard on the limbs and the torso: they move, and heavy torsos move slowly. If pushes silently do nothing, read `ragdoll_chooses` and `hand_contact.tested`.
25. While meeting one limb, sweep across another limb or the torso: note pass-through (parked other-limb).
26. Shoulder-carry a body and touch it with the free hand: no push. An NPC dragging a corpse is also never pushed.
27. Push a corpse lying at your feet or against your body: the view and body never move.

**6. G4: weighted pushes**
28. Push a crate (it resists and the hand is held back) and a bottle (it moves with the hand). There is no time scaling in slow motion (C13: time factor 1; status `time_factor_verified:false`, `mass_verified:false`). Read `hand_push.capped`.

**7. G2: the arm follows**
29. The arm reaches the drawn hand with no wrist gap. With the arm fully extended while walking, the palm may sit 1.5–4.6 cm short for a frame (`reach_clamped`). Check for a few-mm residual when the reach barely overshoots.

**8. G5: hand fit**
30. Pause → VR Settings → Calibrate → X opens hand fit. The right hand is active and A switches. The left stick moves the hand: check that stick up = forward and stick left = hand left. The right stick tilts: check that up = fingertips up and left = yaw left. The trigger of each hand confirms that hand. B returns.
31. The hand sits in the controller for both hands. Tilt applies only to empty hands: no tilt flash at session start or after a pause, and a 0.15 s ease when equipping.
32. Restart the session: the fit persists. The status `palm_offset.source` shows `saved` and `profile` names the real interaction profile. Watch for a one-time jump about 1 s after start when the profile arrives (Minor 10).
33. A confirm made in the first second, before the profile is known, while a genuine Touch entry exists, is not stored (accepted). Re-confirm if it was lost.

**9. G6: carry** (diagnostics run)
34. Create `build/carry-trace` before launch. Carry the whale-oil tank 30–60 s standing and 30–60 s walking (turn, crouch, lean into walls). Then repeat with a bottle as a control. Note the clock time of any image break and save the `carry` status at the break and at the end. Keep `native-carry-trace.csv`. Check:
   - `yields` rises on the carrying side;
   - a transient clear shows at most one RIGID row before re-apply (T10 P3);
   - `failed_reads` is about 0 and `rejected_publishes` is noted.
35. Judge each candidate with the task-9-report thresholds:

| Candidate | Confirmed if | Refuted if |
|---|---|---|
| C1 swap lost | `rigid_after_swap` ≥ 3 or `layer_flips` ≥ 1 | both 0 with `swaps` ≥ 1 |
| C2 rebind | `rebinds_hold_max` ≥ 3 | 0 while `anchored` grows |
| C3 torn quaternion | `bad_quat_reads` ≥ 2 | — |
| C4 render scale | `scale_frames` ≥ 5, or `scale_reentries` ≥ 1, or `scale_clear_frames_max` > 10 | — |
| C5 stale residual | `residual_none_frames` > 10% or max age > 300 ms | — |
| C6 master re-base | `entity_err_frames` ≥ 5 while C1–C5 are quiet, and a `master_drift` row > 0.25 m | `max_entity_err_slow` < 0.05 m and `max_master_drift` < 0.15 m |

**10. A/B**
36. Create `build/no-hand-clamp`: the drawn hand reverts to the pre-Task-5 behaviour, while the raw C9 routing stays on. Compare feel and frame time against the clamp.
