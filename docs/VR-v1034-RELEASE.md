# VR v1034 (test candidate, 2026-11-05) — the gun's collision is where the gun is

Report on v1030/v1032: *"gun still don't physically collide like sword ... it also goes through surfaces, so it not feels like an physical
object."* Asked which of the two passes through a wall, the answer was decisive: **"the hand stops, only the gun slides in"**.

## The cause

A pistol is not held along the hand's forward axis. The mod's own measured hold — `held_copy.hpp`'s grip constant, taken from the native
left-hand pistol and crossbow and used to place every dual-wield copy — puts the barrel about **52° below** the hand's forward axis, with the
grip near the palm. The world sweep and the gun's contact body were both built along the hand's forward axis:

- the hand was stopped at walls by a line the player cannot see (hence "the hand stops"), while the barrel he does see slid in;
- and the contact beads shoved props the model was nowhere near, while the visible barrel went through bottles and corpses (which is also why
  the counters could look healthy: `weapon_body.gun [64 touches, 24 pushes]` in the 21:43 session, on bodies the barrel was not touching).

## The change

- **`authored_gun_frame(side, hand)`** (`src/dxgi_probe.cpp`): the gun's drawn frame — a shown mod-owned copy's published frame when there is
  one (`NativeWeaponHands::held_copy_pose`, whose model row 0 is the barrel), else `held_copy::place(hand, grip_constant(kind), mirrored)`.
- The gun's **sweep** runs along that frame's barrel axis from the gun's own grip (`barrel_offset = frame.position - hand.position`) instead
  of the hand's forward axis with the up/back offsets; a deflected barrel turns the hand by the same world rotation (`rotate_hand_to`), where
  the sword keeps `align_weapon_blade`.
- The gun's **contact body** is published in the same frame: `weapon_body(frame, 0, 0, length, radius + skin)`. The hand frame is only the
  fallback when no frame could be read that frame (cutscene, authored camera, no sweep).
- The sword is untouched: its blade is hand-relative and the user accepted it.

Kept from v1030/v1032: the **skin** (the body reaches .015 m past the sweep's hold-off — without it a bead of exactly the sweep's radius
rests on a surface with no overlap and never pushes), **rigid contact** for weapons (`ContactInput::rigid`; knob `build/weapon-body-ride`),
the per-kind counters (`weapon_body.gun` / `weapon_body.blade`) and the motion-gated scan.

## Checks

- `tests\weapon_body_check.cpp` (in `tools\build-msvc.cmd`) — exit 0: geometry, bead coverage, the reverse grip, the proxy-array ceiling, the
  hold-off-versus-skin pair, the rigid-versus-ride pair, driven/untouched/resting/heavy cases.
- `build\d2vr_render_probe_arm_v1034.dll`, 14,929,408 bytes, sha256 `974c0a0f…`, `dll_exit=0`; launcher published with it and copied to
  `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1034.exe`.

## Not verified

- **No headset run.** What is established: the sweep and the body are built in the hold the mod measured from the native pistol/crossbow and
  already uses to place copies. Whether the barrel now stops at a wall, shoves a bottle and slides a corpse is unobserved.
- The barrel length is still the sweep's number (.36 m pistol, .28 m crossbow) from the gun's grip; a real muzzle a few cm shorter would stop
  the hand slightly early rather than late.
- For an engine-drawn (native) item the mod does not read the engine's item transform itself; the authored hold is the measured stand-in.
- The worktree still does not build `/WX`-clean from code that predates this work; this DLL uses the canonical `cl` line with only those four
  warning classes downgraded, and no warning came from the files changed here.

## What to check

1. Push the pistol's barrel into a wall: the **barrel stops**, not just the hand.
2. Sweep the crossbow/pistol across a table: bottles move where the barrel touches them.
3. `weapon_body.gun` in `xr-live-status.json` should now rise *while the barrel is on something*, and `weapon_collision.events_side` should
   fire on the gun's hand when the barrel — not the hand — meets the wall.
