# Mines and spring razors aimed by the left hand (Track T item B)

Mines / spring razors keep the game's placement; only the placement ray and the throw-preview sensor start at the left hand and
point along it (grenades are thrown by hand motion, see native-throw-hand-velocity.md). Knob: `build/no-hand-mine-aim`.
Ideas from the external GPL-3.0 mod's documented facts (CamSwap around the same two sensors); no code shared.

## RVAs verified on our exe (sha256 b671f824...84516180)
Each sensor body embeds its own profile string, so the identity is the exe's own:
- exe+0x3C1E40: raycast call passes the string at exe+0x1B41768 = `arkCpntPlayerSensorPlacement::UpdateQueryPhysics`.
  Called only from the stage-1 job thunk 0x954A0E (the thunk list 0x954960..0x954BA0 walks the sensor family).
- exe+0x3CEC30: string at exe+0x1B42C48 = `arkCpntPlayerSensorThrowHit::UpdateQueryPhysics,aabbQuery`; called only from 0x954B9E.
- Placement call shape (void(sensor)): the ray function 0x95130(world, out, r8 = start float3, r9 = direction float3, dist float,...)
  forms `end = start + direction*dist`. Start = `0x417820([s+0x40])` = `[s+0x40]+0xD8`; direction = `[s+0x48]+0x2C`; length `[s+0x58]`.
- ThrowHit: origin = `[s+0x10]+0x300` (the player entity), direction = x,y of `[s+0x10]+0x30C` normalised with z forced 0 (the
  disassembly at 0x3CECC2..0x3CED2B zeroes z), same start `[s+0x40]+0xD8`.
- Prologues pinned: Placement `40 57 48 83 EC 40 80 79 38 00 48 8B F9 74 65 48`, ThrowHit `48 8B C4 55 48 81 EC 50 01 00 00 80 79 38 00 48`.

## What the hook does
`src/native_gadget_aim.hpp`: MinHook on both. While the left aim pose published by the Present thread is fresh (<= 100 ms), write its
origin / forward into the fields above, run the native sensor, restore every field (`FieldSwap`, `src/gadget_aim.hpp`, tested by
`tests/gadget_aim_check.cpp`). Stale pose, unreadable pointer or prologue mismatch = untouched sensor. The pose is the same
left-hand game aim that powers and the crosshair use (`publish_left_aim` in dxgi_probe.cpp, `aim_for_shots` frames). Status JSON `gadget_aim`
{placement [calls, swapped], throw_hit [calls, swapped], stale, bad_pointer}.

## Gating (found in the sim)
Both sensors run EVERY frame, even with no gadget (sim run 20261025T135531: 9795 calls each). The hook therefore acts only when the
sensor's own enable byte (+0x38) is set AND the left trigger is held (the pose is only published then, fresh for 100 ms). Sim run
20261025T143x: Placement swapped 0 of 9609 idle calls (enable byte clear); ThrowHit's enable byte is set 81% of frames, so the trigger
gate is what keeps it off the idle ones. No mine/grenade was equipped in the sim saves, so the re-aim itself is untested.

## Not verified
What `[s+0x48]` is (a decl-like object whose +0x2C is a direction) and whether the Placement ray start at `[s+0x40]+0xD8` is the eye
rather than a body-relative point were not traced further; headset / sim must show the ghost following the hand (swapped counter > 0).
Placement needs a gadget equipped: check `placement[0]` in the sim status.
