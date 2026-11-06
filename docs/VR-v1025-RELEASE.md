# VR v1025 (test candidate, 2026-11-05) — the gun is a physical object

User request: *"add to guns a real physical model, same as to hands, so guns can collide with other physical objects in game."*

This note covers only that change. v1024 (the grip ball) is a separate candidate; nothing here alters it. The sword got the same body in
[v1025's successor](VR-v1026-RELEASE.md) � this build is guns only.

## What was missing

A hand holding a ranged weapon was solid to **walls** only. The hands are solid in three layers — the world sweep
(`hand_clamp.hpp` / `weapon_collision.hpp`), the contact model that pushes loose props (`hand_contact.hpp` →
`native_hand_contact.hpp`), and the optional engine body (`build/hand-body`) — and an armed hand was never published to the
middle layer: `dxgi_probe.cpp`'s `can_push` excludes any hand with a weapon, and the weapon's own segment sweep moves the
*hand*, never the prop. So a pistol or crossbow driven into a bottle or a corpse limb went through it, or stopped against
nothing visible.

## The change

1. **`src/weapon_body.hpp`** (new, pure, checked): the drawn weapon's body as the kind of sphere proxy set the hands use —
   balls of its half thickness along the barrel, spaced 0.7 r, so the union's surface is the weapon's box within 2.5 mm. The
   numbers are the weapon sweep's own (`gun_shape` in `dxgi_probe.cpp`: up, back, length, radius from the native equip name,
   `build/gun-shape` overriding), so what blocks the gun and what pushes with it are one shape. Breech and muzzle end the set
   (its reach points); the remaining shells are parked at 1e6 with radius 0, which no reach test can admit.
2. **`native_hand_contact.hpp`**: a weapon lane beside each hand lane. `publish_weapon()` on the render thread poses it and
   samples the barrel's motion (the hands' own `HandMotion` filter, at 60% of the way to the muzzle — the grip's velocity
   under-drives a swing); the physics-side `service()` runs the same `hand_contact()` solve on it, so a prop takes the gun's
   velocity exactly as it takes a hand's. Candidate distance, the guard-limb choice and the scan rate now measure to the
   nearest part of a side's bodies: a palm, or a gun's breech or muzzle.
3. **`dxgi_probe.cpp`**: the lane is published every frame from the pose the gun is *drawn* at (after the hand clamp and the
   weapon sweep), for an armed non-sword hand in gameplay with no physical hold. Knob `build/no-weapon-body` turns it off.
4. **Status / log**: `"weapon_body":{"enabled","publishes","touches","pushes"}` in the status; `native-hand-contact.csv` gains
   a `weapon` column as its last field.

The gun carries none of the hand's cast bookkeeping: its world sweep already keeps it out of walls, so its proxies are simply
where it is drawn — no lag term, no held entity, no `meet` (the drawn hand is never moved for it). One shape serves both hands
(the left hand's equip name), exactly as the weapon sweep always did.

## Checks

- `tests\weapon_body_check.cpp` (also added to `tools\build-msvc.cmd`) — the barrel's world geometry and motion sample, sphere
  coverage within 2.5 mm, a crossbow's own numbers, bad shapes refused, a bottle driven into being pushed along the barrel, a
  bottle out of reach untouched, a resting gun overlapping without driving, a 20 kg prop refused by the hands' mass rule.
  Run: `build\weapon_body_check.exe` → exit 0.
- `build\d2vr_render_probe_arm_v1025.dll` compiled and linked (14,917,632 bytes, 2026-11-05 19:32).

## Not verified

- **No headset run and no game session for this change.** Whether a muzzle shove, a swipe and a lean on a bottle read right,
  and whether the level's bottles, chairs, barrels and corpse limbs react, is unobserved.
- The worktree does not build `/WX`-clean today from code that predates this change (`dxgi_probe.cpp` C4459 x3 / C4456 x4 /
  C4244 x1, C4267 through `hold_coupling.hpp`, `tests\finger_sphere_check.cpp` C4456 — v1024's note records the same). This
  DLL was built from the same `cl` line with exactly those four warning classes downgraded (`/wd4456 /wd4459 /wd4267 /wd4244`)
  and nothing else; every other /W4 warning still fails the build, and none came from the files changed here.
- Dual wield with two ranged weapons still shares one shape (a `build/gun-shape` override applies to both).
- The engine-side alternative (a second hknp body per gun, like `build/hand-body`) was considered and not built: the R1 recipe
  makes spheres only (radius < 0.2), and several bodies per side would need its ownership, drive and destroy paths extended.
