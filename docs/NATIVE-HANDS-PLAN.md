# Native VR hands: gap analysis and plan (2026-11-06)

Goal (user): Bonelab / Half-Life: Alyx hands. Grab anything physical, hit walls, grab bodies and body parts, carry, throw,
full finger IK. All cheap on CPU.

Sources: the read-only audit of the mod (this doc's matrix), [research-bonelab-alyx-hands.md](research-bonelab-alyx-hands.md)
(HIGGS/PLANCK verified at code level; Bonelab and Alyx only secondary), and [research-itr2-hands.md](research-itr2-hands.md)
(static RE of Into the Radius 2, the reference game on the user's PC; BONELAB is not installed).

## What already exists (default on)

| Capability | State | Where |
|---|---|---|
| Finger IK wrapping held shapes, memoised per finger, 70 ms pose spring | default on; headset-tested to v1024, later fixes untested | `finger_ik.hpp`, `finger_sphere.hpp` |
| Visible hand stops at surfaces (sphere sweeps) | default on | `hand_clamp.hpp` |
| Hand pushes props | default on | `hand_contact.hpp`, `native_hand_contact.hpp` |
| Physical hknp hand bodies | on through `build/hand-body`; headset checklist never run | `hand_body*.hpp` |
| Grab any `arkCpntRigidBody` prop: velocity motor, mass-aware force cap, held clamp | headset-accepted v642 | `physical_grab.hpp`, `grab_force_cap.hpp`, `held_clamp.hpp` |
| Throw (60 ms window), thrown props deal `phys_damage` | sim only | `native_grab.hpp` |
| Corpse by any limb, drag, shoulder dock, native carry | drag headset-OK v627; carry fix v1045 sim only | `grip_decision.hpp` |
| Severed parts, bottles (native HoldObject) | default on | `native_held_object.hpp` |
| Sword/pistol full grip, drop, throw, recall | partly headset-tested | `native_weapon_hands.hpp` |
| Living guard: grip + pull = ragdoll yank | crude | `native_weapon_hands.hpp` yank |
| Doors (clip time follows hand), valves (held Use) | door sim, valve headset-OK | `native_door_*`, v732 |

## Gaps vs Bonelab/Alyx, ranked by what the player feels

1. **No contact haptics.** About 30 fixed `pulse()` calls; nothing for touching, hitting, grabbing or holding weight.
   HIGGS: 10 ms pulse scaled by speed and mass^0.6, a grab pulse fading over 0.1 s. Cost ~0.
2. **Weight is not felt.** The force cap exists, but the player does not slow down with a heavy object (HIGGS: 0.675 x mass, max 75%)
   and the hand does not visibly lag under heavy objects.
3. **Hits don't scale with hand speed.** A punch or slap does nothing special: props get a capped push, guards get the yank.
   Want a speed-gated impulse (>2.5 m/s, one event per 0.3 s) on props and a fist hit on NPCs.
4. **No two-handed holds.** Two hands on one body only split its mass. No two-hand sword, no two-hand crate or corpse,
   no off-hand pose solve (HIGGS: main hand drives position, off hand blends orientation 0.5-1.0).
5. **Living guards can't be grabbed by a limb.** They have no ragdoll until knocked out; the pull-and-stagger probe was never run live.
6. **Old corpses can't be dragged.** Ragdolls go static a few minutes after a load.
7. **Finger wrap is re-solved continuously.** HIGGS and Alyx solve once at grab and then hold the result, which is steadier and cheaper.
   Ours is memoised, but held poses still re-solve when the shape key moves; the thumb has needed six fixes.
8. **No physical drawers or levers; no hand climbing.**

## What the reference games do (short)
- **ITR2** (static RE): one physics hand body per side, driven by a PD force/torque controller
  (position gain 500, velocity 20, rotation 3500, angular velocity 500), VRExpansionPlugin grips with a secondary (two-hand) grip,
  and **authored per-object hand poses** (hand sockets + data table) blended from open to grab. No procedural finger fitting.
- **HIGGS** (Skyrim VR, Havok): keyframed hand box, held objects on a position-motor constraint, finger curl solved once at grab.
- So both shipped games author poses per object class. Phase 2 adds a small per-class pose table (sword hilt, pistol grip,
  bottle, book, limb, corpse wrist/ankle) on top of the procedural wrap, used where the wrap looks wrong.

## Plan (each phase: lab first, then sim, then one headset pass)

CPU budget for the whole hand stack: **≤ 0.5 ms per frame on the game thread**, measured with the existing `cost_us` counters
in `xr-live-status.json`. Every new feature reports its own `cost_us` and has a `build/no-<feature>` kill switch.

### Phase 0: headset verification of what exists (no code)
One checklist pass over the untested pile (hand bodies, v1029-v1037 finger fixes, throw, v1045 carry). Fixing a broken
base is cheaper than building on it. Checklist: [VR-bonelab-feel-hand-body-TEST-CHECKLIST.md](VR-bonelab-feel-hand-body-TEST-CHECKLIST.md)
plus a short finger/throw/carry section.

### Phase 1: feel layer (cheap, highest impact)
- `hand_haptics.hpp`: one function from (contact speed, mass, event) to (amplitude, duration), called from the existing
  contact, clamp, grab, release and hit points. Rate-limited per hand (one pulse per 25 ms). Unit check.
- Weight: held mass from `grab_mass.hpp` slows the published move vector (cap 75%) and lowers the motor force cap so heavy
  props visibly lag; two hands share it.
- Hit impact: hand velocity at the clamp/contact hit (already known there) above 2.5 m/s gives one impulse
  (mass-scaled, through the existing velocity writer) with a 0.3 s cooldown per hand, plus a haptic.
  NPC fist hits: needs the user's choice (below).

### Phase 2: grip quality
- Grab-time wrap: solve the fingers once at grab (and when the hand slides along the object), then ease to that pose at a
  capped speed and hold it. Per-frame finger cost of a steady hold drops to ~0.
- Two-handed holds: the second hand on an already held body becomes an off-hand grip. Position stays with the main hand; the
  orientation comes from the hand-to-hand axis (sword, plank, crate), weight split. Sword two-hand pose through `body_ik`.

### Phase 3: bodies
- Re-wake a static corpse ragdoll on grab (START_RAGDOLL 0xb9 on an already dead NPC, measured live first).
- Living guard pull/stagger without ragdoll (needs a test save with an unaware guard in reach; static plan in
  [BONELAB-FEEL-GUARD-PULL-RE.md](BONELAB-FEEL-GUARD-PULL-RE.md)).

### Phase 4 (only if wanted): drawers/levers.

## User decisions (2026-11-06)
- Fist hits on guards: **damage + alert**, like a weak melee hit (through the existing `NativeMelee` TakeDamage path).
- Heavy held objects **slow the player** (cap 75%) and lag behind the hand.
- Hand climbing: **no**. Phase 4 is drawers/levers only.

## Coordination
Another session ("Weapon selection and VR rendering bugs") is editing `src/` in this checkout. No `src` edits or builds from
this work until it has committed, or work happens in a worktree from its commit. Version range for this work: v1050-v1069.

## Phase 1 status and hook points (2026-11-06)

v1050 (branch `feat/native-hands`, built, NOT sim/headset tested). Off switch: `build/no-hand-feel`.
- Contact haptics already existed (`hand_clamp.hpp` surface-hit pulse with cooldown); kept as is.
- Grab pulse now scales with the held mass (`hand_feel_grab_pulses`, waits for the lane's mass), plus a release pulse.
- Walk slowdown from `NativeGrab::held_total_kg()` (one body in both hands counts once) before `controller_bridge.publish`.
- Fist punch: closed empty fist (grip > 0.6, no item, not holding) at >= 2.5 m/s within 8 cm of a living guard's bounds ->
  `NativeWeaponHands` shove request with `phys_damage`, 2-8 hp by speed, haptic, `combat_log("fist_hit")`. 0.3 s per hand.
- Prop pushes stay with the existing `hand_contact` (no separate punch impulse).

Hook points (read-only map, line numbers as of 2026-11-06 20:15; recheck before editing):
- Touch/push haptic: `hand_contact.hpp:267` (`hand_velocity`, `speed`); clamp hit `dxgi_probe.cpp:7961` (`approach_speed`). Render thread.
- Grab/release haptic + mass: `native_grab.hpp:715` `begin_hold`, `:734` `end_capped_hold`, `lane.mass` (`:790-803`).
- Walk slowdown: scale `input.move_x/y` between `dxgi_probe.cpp:13861` and `controller_bridge.publish` at `:13928`, using the sum of both lanes' mass.
- Hits: `dxgi_probe.cpp:8152-8163` contact event (`event->speed`, `native_grab.poke`); fist on NPC through the `shove` request
  (`native_weapon_hands.hpp:137`) with a punch damage decl instead of `shove_damage`, gated by `HandHitGate`.
- `game_xr->pulse(side, amp, s)` (`game_xr.hpp:1550`), render thread only.

## Phase 2 status (v1051, 2026-11-06, built, NOT sim/headset tested)

**Two-handed props.** Before: the second hand could not take a prop the other hand held (focus exclusion, acquisition skip and bind
refusal in `native_grab.hpp`). Now (off switch `build/no-two-hand`):
- The second hand may grab a held prop (props only; corpses keep one hand per bone, two bones as before).
- The earlier hand drives the body. `PhysicalGrab::target()` turns its goal about the main hand by the shortest arc that puts the second
  hand's grasp point (`hand_in_object()`) on the line toward the second hand (HIGGS: main = position, off hand = orientation).
- The later (secondary) hand writes nothing; it stays bound, so it is drawn on the prop and releases normally.
- Releasing one hand while the other holds throws nothing; a secondary left alone `reseat()`s on the current pose (no jump).
- Each hand takes half the mass (existing `share`); the walk slowdown counts the prop once.
- Check: `tests/physical_grab_check.cpp` two-hand block (45 degree turn, <5 cm dead zone, NaN, reseat). Regression: corpse_hold_check
  fails to build and native_grab_compile asserts at line 79 on the base snapshot too (pre-existing); physical_carry_followup 5/5 pass.

**Per-object poses.** `authored_hand_bank.hpp` is an open/closed arm pose pair, not per-object grips; the sword already mirrors the
game's authored grip (`native_finger_grip.hpp`). A per-class pose table is only worth it for props where the procedural wrap still looks
wrong in the headset (books, large crates): decide after the headset pass instead of building it blind.

Not done in phase 2: sword two-hand pose through `body_ik` (the sword is a mod-held copy, not a physics body).

## Sim results (2026-11-06, v1052-v1056, simulated VR on the user's own save)

Test saves no longer work (Steam Cloud); the user's save near Clockwork Mansion has only knocked-out guards nearby, so punches were
aimed at a Clockwork Soldier (`--near npc-front --npc-index 3`). New `punch` scenario in `tools/vr_selftest.py`; counters in
`dxgi-probe-result.json` `hand_feel`.
- Punch: fast closed fists are seen (peak ~8 m/s), the soldier is found in reach, and TakeDamage is applied (`last_shove` 0).
  The soldier lost no HP: clockworks ignore phys_damage in the game too (thrown bottles do not hurt them). Damage on a human guard
  is still unverified. v1052 sent 6 hits for 3 punches (pull-backs counted); v1054 requires the fist to move away from the head
  (>= 1.5 m/s outward): 2 hits for 3 punches, the soldier walks.
- Knocked-out guards are not punch targets (creature_on skips them).
- Weight: a dragged corpse read 0.6 kg (the wrist's motor mass); v1056 counts a corpse as the whole body (75 kg): walk scale 0.514,
  body still dragged 1.91 m (2.03 m on the v1047 baseline).
- Two-handed props: not sim-testable (no scenario places a physics prop under both hands). Headset item.
- Every run (v1047 baseline too) fails "injection: diagnostic thread still running" and the HUD capture checks: pre-existing tooling issue.

## v1057-v1061 (2026-11-06/07 overnight): natural surface grip, hands and weapons push each other

User: "grip natural and active on the object surface ... all physical props, legs, head, boxes"; lab review: "thumb too far
from the other fingers, palm too stretched"; "hands should react to weapons and sword, weapons react to each other".
- Surface grip (v1057-v1059): the held thing's own shape (`finger_sphere.hpp` `surface_grip_shape`: oriented box, a capsule
  only for a round cross-section, corpse bones their capsule), measured in the drawn palm while it settles (0.35 s), then fixed in
  the palm. Replaces the v638 ball / v1028 flat grip (`build/no-surface-grip` restores them). Native held severed limbs/skulls
  use the fitted-shaft seat.
- Thumb opposition on boxes (`finger_ik.hpp` box thumb search); thin boxes aim at the face across from the fingers.
  Lab thumb-index tip: book 144 -> 19 mm, bottle box 81 -> 31, plank 85 -> 48 (palm on edge 71 -> 55), skull 84 -> 52.
  Corpse forearm/calf/head scenes: natural wrap, head = palm on the skull.
- Hands and swords push each other (`hand_weapon_contact.hpp`, v1058): capsules for both hands and held swords, 4 pairs,
  no engine query, pulse on contact, strong pulse on a blade clash (>= 1.5 m/s). Guns: hand capsule only.
  `build/no-hand-weapon-contact`.
- Sim v1060 (corpse drag): no crash, dragged 1.91 m, surface grip 528 frames, finger step 26 us avg.
Open: plank thumb tip ~10 mm short; ragdoll bone long axis unknown (limb lies across the palm; read it live);
gun barrel capsule in hand_weapon_contact; nothing headset-tested.

## v1062-v1064: no penetration, natural seat, power grip (2026-11-07)
User: "book render is bad, fingers should not get through objects ... touch, grab any physical object and use a natural grip";
"palm near thumb zone stretched too much".
- Seat at grab (`native_grab.hpp`): the box's support depth along the palm direction (was the ball round the whole box), then
  `natural_seat` (`finger_sphere.hpp`): a long side turns across the palm (<= 50 deg), the nearest face lies flat on it
  (<= 35 deg), the grasped spot is kept; things narrow along the fingers sit in the palm crease (knuckle shift).
  `build/no-natural-seat`. `held_local_extents_any` solves the 45-degree boxes the exact solve could not.
- `held_standoff` (`finger_ik.hpp`): 200 ms into a hold the frozen surface shape is measured against the palm skin, the knuckle
  pads and (under wide faces only) the thumb root; the THING is moved along the palm normal (`NativeGrab::nudge_seat`), never the
  hand. JSON hand_feel seat_nudges / seat_mm_max / natural_seats.
- Thumb: hard no-penetration in the box/ball searches and a final guard for every shape; clear of the hand so the drawn pose keeps
  it; base kept within 30 deg of rest with a spread cost (it swung to the 45 deg open limit, thumbs-up).
- Fingers: pad samples at 45 degrees to each side against held-size shapes (box edges slipped between the in-plane samples).
- Corpse bones: capsule along the bone body's own long axis, visible radius 0.6 of the padded box.
- Lab (`tools/hand_pose_lab.cpp`): PENETRATION line per scene, "taken" scenes (grabbed tilted / off square), `--no-seat`,
  LAB_SEAT_DEBUG. Worst depth of any finger/palm on surface grips 34 mm -> 5.9 mm (contact tolerance 4), palm 0; thumb base
  <= 28 deg; wrap gap bottle box 124 -> 24 mm, corpse limbs 123 -> 23.
- Sim v1062/v1063 corpse drag: 1.90/1.91 m, no crash; seat move 71 mm (old limb shape) -> 30 mm pull-in.
Open: a book face-on on the palm is a resting hand (fingers flat), not a pinch; thin spoons are a fist; nothing headset-tested.
- v1064: power grip - the thumb root is left out of the seat for things narrow along the fingers (bars, bottles, limbs).
- v1065: a held gun's barrel capsule (authored gun frame, gun shape) joins hand_weapon_contact; never a clash.
- v1066 (user: "objects picked up with game Use, like limbs, also using native VR grips, only visual like bottles"): severed
  limbs/heads held by HoldObject are seated like bottles (seat_held), the ball placement only without the surface grip;
  severed parts and big native holds (> 5 cm radius) are pushed out of the hand visually (native_seat_push); thumb guard
  judges world shapes only, prefers out-of-the-thing over the spread cap, keeps a holding thumb clear of the hand and draws it
  as placed. Lab: severed forearm pen 2.4 mm, head 16 -> 2.9 mm.
- v1067 (user's v1066 headset test: "so far so good"; held heads/legs/props jitter "calculated out of frame", plates/boxes
  sometimes behind the hand, corpse to the shoulder must be easy): held things (grab props, Use holds) are placed at their
  draws like weapons (weapon_draw_place.hpp HeldBind: G = inverse(drawn joint 59) * engine frame, averaged only while the
  hand is still, placed after 0.7 s settle, falls back to the engine frame while off > 15 cm / 34 deg, relearned when off
  while still for 0.5 s; match radius 2 cm so a twin prop is never moved; two-handed holds and corpses stay at the game's
  frame; knob build/no-held-draw-place; status held_draw_placed). natural_seat refuses a face it cannot turn to the palm.
  ShoulderDock: holding a body in a shoulder zone 0.2 s docks it (no let-go needed) and hands it to the game's carry as
  before; a body lifted with both hands docks too; zone radius .22 -> .24.
- v1069 (user's v1067 headset test: "kind of better"; corpse to the shoulder even easier, a screwdriver inside the hand not
  touched by it, a held prop/head/leg and the hand lag behind while walking then resync): held-prop motors get the player's
  velocity as feed-forward (PhysicalGrab::motor carrier, the hand servo's Fix B; only the hand's own motion fades with the
  sample's age); HeldBind learns the hold only while the hand is still in tracking space AND the player stands (v1067 learned
  the first 0.7 s from any motion and walking frames taught the walking lag), places only after 0.15 s of still frames past
  the 0.25 s seat nudge, and widens its off tolerance by 0.15 s of walking speed. Shoulder zone radius .30 with a .16 face
  sphere, dwell 0.12 s.
- v1070: holding Y opens only the game's pause menu (no legacy VR settings panel from a hold in Pause).
- v1071 (user's v1070 test: a carried object still out of sync): the Task 12 prop seat pulled the drawn hand onto the prop's
  physics grasp point (up to 0.35 m), which trails while walking or swinging, so the placement drew the prop on a lagging
  hand. The seat ends once HeldBind has learned the hold (held_bound_entity); the off tolerance also grows with hand speed.
- v1072 (user's v1071 test: "still out of sync and still flickering, and the hand which holds it" while running; the old VR
  settings panel on the first menu open):
  - Match reach of held things 2 cm -> 0.5 m. A draw's model is F times a fixed part offset (weapons: up to 13 cm), so a prop
    whose mesh sits off its frame was never placed at all since v1067. The 9 axes must still agree within 0.004.
  - Place::publish_bound keeps each side's previous read F (same identity, within 0.6 m) as an alternative with its own exact
    delta and previous rows: the engine composes a render's constants before the eye-0 begin where F is read (v901), so when
    the game writes the next tick in between, a moving thing's draws carry the previous F and missed every match (flicker).
    Weapons get it too. Status: arm_flicker.weapon_draw.place.alt, held_bind outcomes, held_skip, prop_seat_frames.
  - The native "VR Settings" menu item never falls back to the drawn (legacy) panel; a failed first open retries once in
    the same drain (status native_options_page[11] = open failures).
  - Sim: `holdrun --near bottle` (take a bottle, hold it still 2 s, run 5 s, stop); `--lead-ms` now also works with
    `--launch`; sims need `build/diagnostics` in the worktree for the full status.
- v1080 (branch feat/prop-grips; user's v1066-v1071 headset tests: "a screwdriver inside the hand, fingers not touching it",
  "a book on an open palm rests on a flat hand", "a thin spoon held in a full fist"): grips from the props' REAL shapes.
  The grip used the render model's world AABB, solved back into the body's axes (lossy, ambiguous at 45 degrees).
  Every physical prop is a Havok hknp body: body record +0x60 is its shape; a convex shape (dispatch byte +0x24 == 2) keeps
  its vertices in the relative array at +0x40 (u16 count, u16 offset) and its convex radius at +0x28 (box factory 0xca15b0,
  native_world_contact_surface.hpp). The game's hulls are coarse (read from the .hkx files: book/plate/box = 8-corner boxes,
  screwdriver 7 vertices, the spoon's hull is its bowl only, the bottle a two-hull compound), so the hull is the KEY and the
  surface is the render mesh: launcher/PropShapes.cs writes %LOCALAPPDATA%\DishonoredVR\prop-shapes.bin (2679 model/scale
  entries, surface-sampled render-mesh points under the FNV-1a hash of the hull's vertices at 0.1 mm; hull and mesh frames
  agree to 0.0 mm). src/prop_hull.hpp builds a PropShape per shape (cached for the session): the whole mesh's own frame
  (exact box at any pose), and up to six convex slabs along a long thing's axis that the fingers meet with an exact
  signed distance (HeldShape/PalmShape.hull). A hull alone (no entry) is used only when it covers the render box
  (2.5 cm); composite shapes keep the render box (status prop_hull_composite + vtable). natural_seat, the flat/ball choice,
  the seat depth, the seat-out and the thumb's face rules read the real shape. Pinch: a bar whose slab at the grasped spot
  is thin (<= 6 mm half thick, <= 16 mm half wide: a spoon/fork handle, a pen, a shaft) is seated at the fingertips
  (8.5 cm along the fingers, 3.5 cm off the palm) instead of in the palm crease. Lab: `sh tools/hand_lab.sh realshapes
  corvo --only real_` (real render meshes + the database lookup by the Havok hull, as the DLL does; data from
  tools/prop_mesh_export, local, never committed); tests/prop_hull_check.cpp (C++ key == launcher key for book_01).
  Not covered: things held with the game's Use (bottles, glasses, severed limbs) keep the cylinder/profile path; compound
  bodies (the bottle) keep the box. Status: prop_hulls, prop_hull_meshes, pinch_seats; new hulls are appended to
  artifacts/prop-hulls.jsonl for lab replay.
- v1081 (user's v1080 headset run: "props still use the old grip"; the live dump showed 35 of 52 nearby props found their
  mesh, but the things the user picks up are held with the game's Use, which v1080 did not touch): a Use-held thing's own
  body (NativeHeldObject::exclusion_body record) finds its render mesh by its hull; the mesh pieces lie where the fitted
  cylinder is seated (seat_held's axes, the model box centre at its middle: where the placement draws it) and the fingers
  meet them; the seat-out pushes a real shape out of the hand (the drawn thing moves the same). Two-hull bottles and severed
  limbs keep the fitted cylinder. Status: finger_real_holds.
