# Lab facts for the arm/weapon collision and holster sims (2026-11-26)

Measured in simulated sessions on the Linux lab (plan Task 1), baseline builds DOTO v145785 / D2 v1483. Runs under `~/d2vr-test-builds/sim-runs/`.

## Start positions (`d2vr guard list`)

| game | save | player feet | runs |
|---|---|---|---|
| DOTO | chapter 2, Karnaca (lab save) | (-26.42, -52.02, 40.60) | 20261126T164430 |
| D2 | Corvo, Blink only (lab save) | (66.44, -6.56, 32.95) | 20261126T165042 |

## Walls (`lab_spot_scan`: 12 headings, `d2vr trace 3` at eye height)

DOTO: two walls meeting in a corner, plus an object at eye height on wall 1.
- wall 1: normal (0.774, -0.633, 0), `n.p = 11.06`; 1.43 m from the start eye; something protrudes ~0.37 m from it at eye height near (-27.34, -51.49) (slanted top, normal (0.69,-0.40,0.60)): avoid.
- wall 2: normal (0.633, 0.774, 0), `n.p = -59.70`; 2.71 m from the start eye; two clean hits (-28.92,-53.48), (-27.80,-54.40).
- Floor at z 40.60 under the start.

D2: a flat wall straight ahead of the start.
- wall: normal (-0.980, 0.200, 0), `n.p = -67.03`; 0.61 m from the start eye; hits from y -7.32 to -5.11 (it continues at least 1.5 m along +y); a second surface 0.17 m proud of it at (66.49, -8.51) (a pillar or frame) and walls at (66.45, -3.85) facing -y and (65.13, -4.39) facing +x (a room ~2 m deep).
- Floor at z 32.95.

## Spots (setviewpos EYE x y z yaw; eye = feet + 1.65)

| name | game | eye | yaw | what |
|---|---|---|---|---|
| WALL_RIGHT | DOTO | (-27.894, -53.824, 42.25) | -39.3 | wall 2 on the right, 0.38 m from the eye (right shoulder ~0.20 m) |
| WALL_FRONT | DOTO | (-27.755, -53.654, 42.25) | -129.3 | facing wall 2 at 0.60 m |
| WALL_RIGHT | D2 | (66.666, -6.606, 34.60) | 78.5 | the wall on the right, 0.38 m from the eye, facing +y along it |
| WALL_FRONT | D2 | (66.440, -6.560, 34.60) | -11.5 | facing the wall at 0.61 m (the start itself) |

The trace's own direction stayed level (z 0) with the head pitched -15 degrees: `d2vr trace` casts along the level camera heading.
The spots are confirmed by the eye captures of the arm scenarios (Task 9), not by separate runs (ruling R7).

## E1: do the mod's world casts see a living guard? (`npc_contact_probe`, DOTO, run 20261126T164736)

Setup: `god`, `d2vr guard behind 0.7` (m_bodyguard_01, unaware), the open right hand pushed 0.35 m into his upper back at 0.1 m/s, then the
sword drawn at the shoulder and its blade held in him and pushed in again (knobs no-stealth-contact, no-contact-damage).

- Blade cast hit kinds (weapon_collision.contact.hits, right side, [world, npc, dynamic, self, held]): **[0, 225, 0, 0, 0]**: the blade's
  cast reports the living guard (an NPC hit, 225 answers). `npc_blade_contacts` 6. The blade's drawn tip deviated up to 368 mm from the
  controller's while held in him (contact.max_dev_mm), turn <= 10 deg: the blade stops on his body as on a wall.
- Hand clamp: contact frames 166 / 145, holds 17 / 16 while the open hand was in him: the clamp's casts see him too.
- Answer: **yes, living guards are surfaces** for the hand clamp and for the blade / barrel casts (`kWorldOccluderLayers` 0xc43e includes
  the characters' bodies). Spec 6.3: nothing to add; no NPC capsules are needed. The arm collision deliberately ignores NPC hits
  (`npc_entity`), as the reference mod's forearms meet the world only: an arm around a guard (a choke, a carry) must not be pushed by him.

## Arm collision in the sims (2026-11-26, DOTO v145788-v145791, D2 v1486-v1489)

- **No false contact in ordinary poses.** `arm_wall` (a wall 0.38 m to the right of the eye) and `arm_corner` (an inside corner): about
  5,000 arm casts a side, 0 hits. The trace (`artifacts/arm-collision.csv`) shows why: the player's capsule keeps the shoulder >= 0.27-0.39 m
  from any wall and the natural elbow (the Boneworks pole points mostly down) stayed >= 0.11 m (wall) / 0.20 m (corner) off; the hand clamp
  held the wrist 4.6 cm off the wall. Nothing to resolve, nothing moved.
- **Real contact: leaning toward the wall** (`arm_lean`, the game's lean from a sideways head offset; the body follows the camera):

| run | natural elbow in the wall (frames, max mm) | drawn elbow in the wall (frames > 1 mm, max mm) | swivel max | step max |
|---|---|---|---|---|
| DOTO, shadow (computed, not applied) | 122, 143.5 | 150, 105.9 | (101 deg computed) | - |
| DOTO, on | 175, 100.8 | **5**, 58.1 (the first frames of a contact) | 96 deg | 14.7 deg/frame |
| D2, on | 121, 121.9 | **14**, 104.7 | 75 deg | 16.6 deg/frame |

  Captures (`sw-capture-00/02-real`, shadow vs on): the forearm and its Voltaic Gun move off the wall plane. Initial-overlap casts (the
  upper arm starting inside the wall while leaning: 203 / 661) are ignored, as designed (the shoulder itself is in the wall: nothing an elbow can fix).

Clean A/B after the shadow fix (v145793, the shadow mode now publishes the drawn elbow as the natural one): shadow 142 frames in the
wall (106 mm); on: natural 209 frames (102 mm), drawn 7 frames (59 mm): 97% of the contact frames kept out.

## Other proofs (2026-11-26, v145790-v145793 / v1488-v1491)

- **Forearm contact** (`forearm_block`, DOTO): the right hand down through the left forearm held across the chest: 305 contact frames,
  the largest correction 77 mm; with `no-forearm-contact` 0 (the hand passes through).
- **Fingers on a wall** (`palm_wall`, DOTO, right hand palm-first, pressed 5 cm past the surface, slides, a strafe along the wall):

| build / knobs | frames over the limit | deepest slow / fast (mm) | thumb (mm) | mean tip step (mm) | flag flips |
|---|---|---|---|---|---|
| v145785 (before the finger work) | 22 | 18.5 / 27.8 | 28.3 | - | (hyper 2) |
| round 1 (round 2 knobs off) | 1 | 3.2 / 16.5 | 26.6 | 1.41 | 4 |
| round 2, put-out off | 51 | 24.2 / 23.1 | 30.9 | 1.88 | 0 |
| round 2 without G8 | 3 | 8.5 / 20.5 | 20.0 | 1.34 | 0 |
| round 2 (default) | 1-2 | 7.0-8.9 / 10.4-12.1 | 23.8-26.1 | 2.21-2.23 | 0 |
| round 2, surface rules off | 1 | 4.0 / 20.6 | 24.3 | 2.53 | 0 |

  The palm never rested on the wall in DOTO (G3 open frames 0: the hand clamp holds the palm off; Billie's wrist gun?); in D2 it did
  (41 frames). The thumb inside the wall is older than this work (v145785: 28 mm).
- **Holstered gun** (`holster_walk`, D2, walking / sprinting / strafing / turning): its placement against the hip of the body drawn in
  the same frame (paired by the expected root): mean 3.4 mm carried vs 10.5 mm legacy (1,635 / 1,761 frames); max 165 mm in both (a turn).
- **Lab note:** `dual-wield-events.jsonl` accumulates across sessions (append mode, never cleared): filter it by the run's tick range;
  `xr-live-status.json` is per run.

## 2026-11-27: the load crash, the bisect, the second wave

- **A save loaded during play crashed the game** (Death of the Outsider `loadgame 33`, 3 of 3 runs, base v145785 identical): an unhandled
  access violation in VCRUNTIME memcpy called from `read_self_raw` (render_player_root on the present thread). clang-cl `/EHsc` does not
  put memcpy-builtin calls under `__try` (a scratch check with the plain pattern died with "Unhandled page fault"; cl catches it).
  Fix: `src/seh_copy.hpp`, 57 sites. Sims after the fix: `loadgame` in both games, no crash.
- **Bisect** (13 builds, `~/d2vr-bisect` + `~/d2vr-test-builds/bisect/variant.sh`): the first fix build crashed 3 s into play in the
  engine (DOTO +0x526047: `[[r13+0x60]+0x38]` null, then `[r15+0x20]`). Not the fingers, not the slot count, not give item; one header,
  `bodyless_motion_guard.hpp`; include-only fine, the guarded call crashes, an integer-only opaque call crashes too: a live `__except` in
  that read is the trigger. It stays as it always was.
- **D2 grenades**: the lab save holds none: `d2vr give item regular_grenade 3` (the game's AddItem 0x6156e0) raises the existing record.
  The held grenade's entity decl is `models/weapons/gadgets/grenade_base.def` (shared): the belt key comes from the slot-1 inventory
  item's decl name (`regular_grenade.inventoryitem`). Put back + drawn 2x, no failure.
- **Back of the hand**: back_wall's right hand did not register (to look at); the rule engaged on the resting left hand 1,003 frames
  (0 with `legacy-finger-surface`).
- **Thumb on a wall** (palm_wall, right hand): 25.7 / 17.5 / 26.4 mm across runs (v145785: 28.3). The solve's least-depth thumb pose is
  not what is drawn (the relaxed/open want points into the wall).

## 2026-11-27 (later): hands on surfaces, thumb, probes

- **The hand clamp is a sphere.** `HandClampConfig::palm_radius` .05 + skin .005 around the grip, any orientation: an empty hand
  stops ~4 cm short of a wall (back_wall: the back-of-hand probe's peak share 0.086 = the back skin ~4 cm off). Fixed by the settle
  step (palm box + drawn phalanges), not by changing the clamp (its sweeps, validation and snapback are tuned on the headset).
- **OpenXR grip axes in the sims:** the open fingers lie along the grip's -Y; -Z runs along the handle (little finger to thumb). A
  scenario pose that takes -Z for the fingers hangs them down.
- **Probe hygiene:** a sphere cast that starts inside geometry reports no real face (`initial_overlap`); the mod's own hand body is
  reported as entity 0 (`hand_body`). Either made a slab through the hand. Both are misses now, for every free-hand cast.
- **Measuring fingers:** `finger_ik.tip_pen[5]` (thumb) is a whole-run maximum; `round2.thumb_pen` is per frame. `steady[1]` is a
  per-frame step in the PALM frame: a 100+ mm step is a finger flipping, not the hand moving; `round2.spikes` lists the last 12 with
  the settle offset and blocked flags.
- **D2 belt_reset** never registers the "empty" pull after the load (all builds); the belt does forget (key "" at the end, no draw).
- **D2 heart_pocket:** the left natural elbow is in something 18 frames, up to ~205 mm, in every build (unchanged).
