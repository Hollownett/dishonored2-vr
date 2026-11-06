# The weapon's physical body

User requests (2026-11-05): *"add to guns a real physical model, same as to hands, so guns can collide with other physical objects in
game"*, then *"can do same for sword?"*.

## What the hands have, and what a held weapon did not

The hands are solid in three layers:

| layer | where | what it does |
| --- | --- | --- |
| world sweep | `hand_clamp.hpp` + `weapon_collision.hpp` (the weapon's segment) | stops the drawn hand, and the drawn weapon's segment, at walls, props and the ground |
| contact model | `hand_contact.hpp` + `native_hand_contact.hpp` | *pushes* loose dynamic props — bottles, chairs, corpse limbs — by writing the prop's velocity, from sphere proxies on the palm and fingers |
| engine body (optional) | `hand_body*.hpp`, knob `build/hand-body` | a real hknp sphere the physics engine owns |

A hand holding a weapon was only in the first layer: `dxgi_probe.cpp` published the contact model for **empty open hands only**
(`can_push` excludes any hand with a weapon), and the weapon's own segment sweep moves the *hand*, never the prop. So a pistol's
barrel or a sword's blade driven into a bottle passed through it, or stopped against nothing visible. The sword had one extra path —
a new blade contact writes a single impact impulse (`native_grab.poke`, `event->speed*.9` capped at 6 m/s) — but nothing
continuous, and nothing at walking speed.

## The change

1. **`src/weapon_body.hpp`** (new, pure, checked). `weapon_body(grip, up, back, length, radius, reversed)` builds the kind of
   sphere proxy set the hands use for the drawn weapon: balls of its half thickness along the barrel or blade, spaced 0.7 r, so the
   union's surface is the weapon's box. The numbers are the weapon sweep's own (`gun_shape` / `sword_blade_length` in
   `dxgi_probe.cpp`, from the native equip name, `build/gun-shape` overriding), so what blocks a weapon at a wall and what pushes a
   prop with it are one shape. The count follows the half thickness: a 3.5 cm barrel gets 16 beads, a 2 cm blade the whole proxy
   array (24, the hands' own `HandProxies` size — asserted in the header, because a bead past it would write over the body's
   fields). A reverse grip is honoured (`reversed`, as `sword_blade_axis` is). Breech and tip end the set (its reach points); the
   unused shells are parked at 1e6 with radius 0, which no reach test can admit.
2. **`native_hand_contact.hpp`**: a per-side weapon lane beside the hand lane. `publish_weapon()` on the render thread poses it and
   samples its motion (the hands' own `HandMotion` filter, at 60% of the way out — the grip's velocity under-drives a swing or a
   stab); the physics-side `service()` runs the same `hand_contact()` solve on it, so a prop takes the weapon's velocity exactly as
   it takes a hand's. Candidate distance, the corpse-limb choice and the scan rate now measure to the nearest part of a side's
   bodies: a palm, a barrel's breech or muzzle, a blade's tip.
3. **`dxgi_probe.cpp`**: the lane is published every frame from the pose the weapon is *drawn* at (after the hand clamp and the
   weapon sweep), for an armed hand in gameplay with no physical hold — the gun's `gun_shape`, the sword's `.65 m` blade (the same
   constant its sweep uses) with `reverse_sword`. Knob `build/no-weapon-body` turns it off.
4. **Status / log**: `"weapon_body":{"enabled","publishes","touches","pushes"}`; `native-hand-contact.csv` gains a `weapon` column
   as its last field.

A weapon carries none of the hand's cast bookkeeping: its world sweep already keeps it out of walls, so its proxies are simply
where it is drawn — no lag term, no held entity, no `meet` (the drawn hand is never moved for it). The sword keeps its impact poke
as well: the body is the shove (walking, pressing, sweeping something off a table), the poke is the strike.

## v1030 — why the gun did not collide, and what it costs

The first build (v1026–v1029) shoved with the sword and never with the gun. The session of 2026-11-05 20:18 says it in its own files:
44 rows with the `weapon` column set, every one of them on the blade's hand, none on the gun's.

The cause is the world sweep. It stops a weapon's **segment** by its own radius (`blade_radius`: a pistol's .035, a crossbow's .06), so
beads of exactly that radius come to rest **on** the prop's surface with no overlap — and the contact model pushes only on an overlap.
The sword's blade is swept as a line (radius 0), so its 2 cm beads were already 2 cm inside the prop: that is why only the sword worked.
`WeaponBody` is now built with `radius = sweep radius + weapon_body_skin` (.015 m), so the body reaches past where the sweep holds the
hand. `tests\weapon_body_check.cpp` encodes both halves: a body held off exactly by its radius touches but never drives, and the skinned
one drives.

Cost (user: *"physics should be cheap"*): the registry scan — the expensive half of the contact service — is now gated on motion. A palm
or weapon breech that has not moved 2 cm since the last scan scans every 8th frame instead of every frame; a moved one scans every frame
and a fast swing still runs its four passes. The per-kind counters (`weapon_body.gun`, `weapon_body.blade`, each `[touches,pushes]`) make
the next run answer "does the gun push?" directly.

Session numbers this was judged on (`xr-live-status.json`, 2026-11-05 20:18, v1027): 31,466 contact services, 13 over 100 µs (peaks
173–203 µs, body reads dominant), `discover` ~18 µs mean / 203 µs peak, mod CPU 0.978 ms mean and 4.81 ms p95 with `finger_ik` at 136 µs
mean / 2.65 ms peak. The weapon lane is not the largest CPU item in that frame; the gate removes its steady-state cost.

## v1034 — where a gun actually is

A pistol is **not** held along the hand's forward axis. The mod's own measured hold (`held_copy.hpp`'s grip constant, taken from the native
left-hand pistol and crossbow and used to place every dual-wield copy) puts the barrel about **52° below** the hand's forward axis, with the
grip near the palm. The sweep and the contact body were both built along the hand's forward axis, so:

- the hand was stopped at walls by a line the player cannot see, while the barrel he does see slid into the wall — asked which passes
  through, the user answered *"the hand stops, only the gun slides in"*; and
- the beads shoved props the model was nowhere near, while the visible barrel went through bottles and corpses.

v1034 builds both in the gun's own drawn frame (`authored_gun_frame`): a shown mod-owned copy's published frame when there is one
(`NativeWeaponHands::held_copy_pose`), else `held_copy::place(hand, grip_constant(kind), mirrored)`. The sweep's segment starts at the gun's
grip along its barrel, and a deflected barrel turns the hand by the same world rotation (`rotate_hand_to`) instead of aligning the hand's
forward axis. The sword keeps its hand-relative blade line, which the user accepted.

`weapon_body(frame, 0, 0, length, radius + skin)` — the barrel length is still the sweep's number (.36 m pistol, .28 m crossbow) from the gun's
grip, so a real muzzle a few cm shorter means the hand stops a little early rather than late.

## Candidates

- `build\d2vr_render_probe_arm_v1030.dll` — the current one: this body (skin fix, per-kind counters, scan gate) in the same DLL as the
  parallel session's optical-density (FPS) fix (`docs\VR-v1030-RELEASE.md`, `artifacts\repair-v1030-test-candidate.json`), and the launcher
  published from it (`%USERPROFILE%\Downloads\DishonoredVRLauncher-v1030.exe`).
- v1025 (guns only) and v1026 (guns and sword, before the skin fix) are superseded; v1024 is the grip ball, a separate candidate.

## What the pure check establishes

`tests/weapon_body_check.cpp` (`tools\build-msvc.cmd` runs it):
- the barrel's and blade's world geometry, the motion sample, the parked shells;
- bead coverage (the pistol within 2.5 mm of its box, the blade's beads overlapping), and the full proxy-array edge case;
- the crossbow's own numbers, the reverse-grip direction, and refusal of nonsense shapes (radius, length, NaN, non-frame basis);
- a bottle the barrel is driven into is `touching` and **pushed** by the hands' solve; a bottle the blade is shoved into is driven
  along the shove; a bottle out of reach is untouched; a weapon at rest inside a prop overlaps it without driving it; a 20 kg prop
  is refused by the hands' mass rule.

## What is not verified

- Headset feel and live reactions in a level for both weapons — no game session was run for either change. Watch
  `weapon_body.pushes` in `xr-live-status.json` while shoving props with a pistol, a crossbow and a sword.
- Living guards cannot be pushed (the engine refuses an awake character's rigid body); dead and unconscious ones are shoved like
  any ragdoll limb.
- Dual wield with two weapons shares one gun shape (the left hand's equip name) exactly as the weapon sweep always did; the sword's
  own numbers are fixed.
- The engine-side alternative (a second hknp body per weapon, like `build/hand-body`) was considered and **not** built: the R1
  recipe only makes spheres (radius < 0.2), and several bodies per side would need its ownership, drive and destroy paths extended.
  The contact model gives the same visible collision with the proven ABI.
