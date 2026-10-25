# Native Use line of sight: `0x3b7b90` (hands plan Task 4, ruling H15)

Exe: `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180` (image base `0x140000000`).
Source: headless Ghidra 12.1.4 decompile of the 2026-10-19 snapshot project (`~/.local/share/d2vr-re/snapshots/dishonored2-ghidra-2026-10-19`,
`tools/D2MarkerDecomp.java` on `1403b7b90 1403b8430 140878c70 140877530 14061e1a0 14018cd00 140c85470`), `objdump` of the two call
sites, and the runtime trace in `native-loot-los.csv` (knob `build/loot-reach-trace`). Read-only; nothing here changes game behaviour.

## Signature

`bool __fastcall 0x3b7b90(Sensor* s, idEntity* target, const float origin[3], const float axis[3][3], const float point[3], unsigned range, u8* out)`

The low byte of the return is **blocked** (1) or clear (0). `*out` is written 0 first; it is scan continuation, not clearance
(`docs/v221-native-contract-repairs.md`).

## Inputs at the stage-0 callers (`0x3b8430`)

| caller (return RVA) | path | `origin` | `point` |
|---|---|---|---|
| `0x3b87b5` | the direct-ray shortcut (`local_1f0` target of `0x6c6760`) | `rbp-0x80`: the camera eye `camera+0x2527c` | `rsp+0x40`: the direct ray's end at the hit distance |
| `0x3b8b05` | the loop over the sorted hand-volume result set (`0x92630`, sorted by `0x3a37f0`) | `rbp-0x80`: the camera eye `camera+0x2527c` | `rsp+0x40`: a copy of the result record's hit position (record `+0x00`) |

`axis` is `camera+0x25288` (3x3). Both callers pass range 0. The mod's range-origin hook (`0x417820` called from `0x3b81d8`) changes only
the 3b80a0 range check; **the LOS origin stays the camera eye**, read directly from the camera, not through a getter.

## Body

1. `range < 5`, else return 0 (clear) with `*out = 0`.
2. Filter: `arkCollisionQueryFilter` (vtable `0x1ad4b68`), layer mask `0xc43e`, required 0 (`kWorldOccluderLayers`).
3. Shape: `*(s + 0x48 + range*8)`. Runtime: range 0 is an `hknpSphereShape` (vtable `0x1ec1e78`), radius `+0x28` = **0.005 m**.
4. Request: origin, delta = `point - origin`, inverse delta, sign mask (the `InteractionRay` block of `docs/native-world-trace.md`).
5. Cast `0xc89bc0(world, request, axis, collector, 0)` into an on-stack `hknpAllHitsCollector` (vtable `0x1ad2640`, 10 inline 0x70-byte
   records, `+0x0c` any-hit, `+0x30` records, `+0x38` count), then `0xc85470(collector)` sorts the records (tail call into `0x3a37f0` when
   count > 1).
6. Walk the sorted hits in order. For each: entity = `*(*( *(world+0x28) + (body&0xffffff)*0xb0 + 0x90 ) + 8) + 0x10` (`0x18cd00`), body at
   record `+0x48`.
   - entity == `target`, or its parent `*( *(entity+0x358) + 0x3c8 )` (`0x877530`) == `target`: **stop** (clear unless already blocked);
   - else, unless LOS-transparent (`0x878c70`: `*(int*)(entity+0x288) == 4`, or `(entity+0xd1 & 0x28)` and a character type and
     `(entity+0x368 & 0x20)`): **blocked = 1**; then `0x61e1a0(record)` (the hit body's shape carries a `0x1439cb320` component) sets
     `*out = 1` and continues, else `*out = 0` and stop.
7. Free the collector and return `blocked`.

So the first non-transparent hit before the target's first hit decides. A hit the sweep meets *after* the target (the floor under a
coin) never blocks; with no hit at all the result is clear.

## The trace (Task 4)

`native-loot-los.csv`: one row per stage-0 call on a takeable owner of the cycle's set. Columns: tick, side, owner, caller RVA, raw result
byte, raw out byte, origin, point, shape vtable RVA and radius, the collector's any-hit word and hit count, the indices of the first target
hit and of the blocking hit, then the blocking hit and hits 0-3 (`frac, x, y, z, nx, ny, nz, entity, spawn, vt, body, motion, target,
transparent`; `motion` is the body record's `+0x44` byte). The hits are copied by a detour on `0xc85470` that is installed only while the
knob is on, calls the original once with its own argument and copies only when the return address is inside `0x3b7b90` and the call was
armed by the stage-0 hook; the sort's first 16 bytes are verified before hooking and after `disable()`. `0xc89bc0` itself is not hooked:
`NativeWorldTrace::verify` checks its bytes lazily.

## Findings (run `/mnt/steamssd/d2vr-sim-runs/20261025T100910`, `loot_reach`)

| case | calls | blocked | blocking hit | hit point to the loot's bounds |
|---|---|---|---|---|
| table coin `pickups_coin1_lowvalue_33` | 1156 | 0 | none: one hit, the coin (fraction ~0.994) | - |
| floor coin `pickups_coin3_highvalue_14` | 3173 | 3165 | `idWorldspawn` (vtable `0x1c89868`, static, motion byte 1), one plane, normal (0.70, -0.70, -0.15) | min 0.016, median 0.125, max 0.311 m; 6 of 3165 within 2 cm |
| drawer eels (closed `rich_commode_01`) | 1350 | 1203 | `arkUsable` (vtable `0x1c5ec38`, the commode's door, dynamic, motion byte 6) on 1158, `idWorldspawn` on 45 | min 0.296 m |

- Floor: the walk is `worldspawn (side-facing) -> coin -> worldspawn (floor, normal up)` on 2799 calls and `worldspawn (side) -> coin` on 357.
  The floor the coin rests on is met only after the coin, so it never blocks. The blocker is a near-vertical static surface 4 cm in front of
  the coin's centre (the coin's centre is 0.041 m behind its plane, on the far side from the eye), reaching up to 0.30 m above the floor.
  The right-eye capture at that time (`floor-coin-under-cot-t72-right.png` in the run folder) shows the coin in the shadow under the edge of
  an iron cot: the cot's collision hull covers the gap between its rail and the floor. The 8 clear calls aim at the coin's near (+x, -y)
  corner, which pokes out in front of that plane.
- Lifting the end point to the coin's bounds top-centre + 1 cm (ruling H15 fix C1) would not clear it: that point is behind the same plane.
- Drawer: the commode's door usable blocks, as a closed drawer should.
