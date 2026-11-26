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
