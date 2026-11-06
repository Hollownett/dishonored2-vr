# VR v1026 (test candidate, 2026-11-05) — the sword is a physical object too

User request, after v1025 gave the gun a body: *"can do same for sword?"* Yes — same mechanism, two sword-specific details (a long thin
blade, and a reverse grip points it back down the hand).

This note covers the weapon body for **both** weapons. v1025 was guns only (`docs\VR-v1025-RELEASE.md`); v1024 (the grip ball) is a
separate candidate.

## What was missing

A hand holding a weapon was solid to **walls** only: `dxgi_probe.cpp` publishes the contact model for empty open hands
(`can_push` excludes any hand with a weapon), and the weapon's own segment sweep moves the *hand*, never the prop. The sword had one
extra path — a new blade contact writes a single impact impulse (`native_grab.poke`, `speed*.9` capped at 6 m/s) — so a fast strike
nudged a bottle, while walking, pressing or sweeping something off a table did nothing, and a slow blade through a chair passed through.

## The change

1. **`src/weapon_body.hpp`** — the drawn weapon's body as the hands' sphere proxy set: beads of its half thickness along the barrel or
   blade, spaced 0.7 r. A 3.5 cm barrel gets 16, a 2 cm blade the whole proxy array (24 — the hands' own `HandProxies` size, now
   `static_assert`ed in the header: one bead past it would write over the body's own fields, which is exactly the bug the sword's first
   build hit and the check now guards). The numbers are the sweep's own (`gun_shape`, `sword_blade_length=.65`, both in
   `dxgi_probe.cpp`), so the shape that blocks a weapon at a wall and the shape that shoves a prop are one shape. `reversed` points a
   reverse-grip blade back down the hand, as `sword_blade_axis` does.
2. **`native_hand_contact.hpp`** — the per-side weapon lane (`publish_weapon`, the same `hand_contact()` solve, motion sampled 60% of the
   way out); candidate distance, corpse-limb choice and scan rate measure to a palm, a breech/muzzle or a blade tip. The sword keeps its
   impact poke: the body is the shove, the poke is the strike.
3. **`dxgi_probe.cpp`** — the lane is published for any armed hand in gameplay with no physical hold: the gun's `gun_shape`, or the
   sword's `.65 m` blade with `reverse_sword`. Knob `build/no-weapon-body`.
4. **Status / log** — `"weapon_body":{"enabled","publishes","touches","pushes"}`; `native-hand-contact.csv` gains a `weapon` column.

## Checks

- `tests\weapon_body_check.cpp` (in `tools\build-msvc.cmd`), exit 0: barrel and blade geometry, motion sample, bead coverage (pistol
  within 2.5 mm of its box; the blade's beads overlapping with no hole), the full array's edge, the crossbow's numbers, the reverse
  grip's direction, bad shapes refused, a bottle driven into pushed along the barrel, a bottle shoved by the blade driven along the
  shove, out of reach untouched, resting overlap not driving, a 20 kg prop refused.
- `build\d2vr_render_probe_arm_v1026.dll` compiled and linked (14,917,632 bytes, sha256 `C5A39774…`, 2026-11-05 19:42).

## Not verified

- **No headset run and no game session** for this change (or for v1025). Whether a blade shoved along a table, a walking sword knocking a
  bottle, and a stab into a corpse limb read right is unobserved. `weapon_body.pushes` in `xr-live-status.json` counts the writes.
- Living guards cannot be pushed (the engine refuses an awake character's rigid body); dead and unconscious ones are shoved like any
  ragdoll limb.
- The worktree still does not build `/WX`-clean from code that predates this work (v1024's note lists the same warnings); this DLL was
  built from the canonical `cl` line with exactly those four warning classes downgraded (`/wd4456 /wd4459 /wd4267 /wd4244`). None of the
  warnings come from the files changed here, and the compiler reports nothing else at /W4 /WX.
- Dual wield with two ranged weapons still shares one gun shape (the left hand's equip name), as the weapon sweep always did.
