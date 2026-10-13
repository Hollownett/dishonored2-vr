# Live entity list (Dishonored2.exe steam17576867, image 0x41cb000)

Tool: `python tools/entities.py --pid P [--kind npc|item|all] [--near X Y Z --radius R] [--json] [--limit N] [--health]` (read-only).
No Ghidra decompilation was done; everything below was found by live read-only probing of a running gameplay session.

## Verified (live)
- `game = *(base+0x39a5290)`, RTTI `idGameLocal`. `idGameLocal::entities[]` is an array of `idEntity*` starting at
  `game+0x3130b0` (slot 0 = `arkPlayer` "player1", slots 1,2 are other arkPlayer objects). Populated up to about
  `game+0x337128` (~4970 non-null slots in the test level; the tool scans to `game+0x340000`).
  The player pointer used by existing tools (`game+0x3130b0`) is just slot 0.
- Entity class = RTTI via vtable (`rtti_name`). Seen: idEntity, idDynamicEntity, idLight, arkMovable, arkPickup,
  arkNPC (116), arkUsable, arkDoor, arkTrigger, arkSpawner, arkWaypoint, arkWeapon, arkBreakable, ...
- Entity name: `char*` at `entity+0x20` (points to inline buffer at +0x30 for short names). e.g. `player1`,
  `pickups_coin_dunwall_lowvalue_4`, `models/characters/def/generic/aristo_female_01.def_4636`.
- Components: native list (data ptr, count/cap packed as in `rig_fields.native_list`) at `entity+0x268`; each element is a component
  pointer with RTTI `arkCpnt*`.
- World position: the entity's `arkCpntReferenceFrame` (NPC/items/etc.) or `arkCpntReferenceFrame_Player` (player) component,
  float3 at `component+0x30` (a 3x3 rotation follows at +0x3c). Player check: frame pos (68.92,-78.70,-15.16) vs
  `vr_selftest.player_position` camera origin (68.92,-78.64,-13.49): XY identical, camera is eye height (+1.67) above.
- `entity+0x78` (float3) is NOT reliable: for the player it was stale (-91.4,-92.2,3.6, the spawn point), for idle NPCs it equals the
  frame position. The tool uses the frame component and falls back to +0x78 only when no frame component exists (`pos_src` field).
- Items: `arkPickup` entities (components `arkCpntLoot` + `arkCpntPickable_Simple` + `arkCpntRigidBody`) cover food (apple, pear,
  dark_bread, rat_skewer), coins (`coin_*_lowvalue/midvalue/highvalue`), potions, ammo, notes/newspapers, pouches. Throwable props
  (bottles, glasses) are `arkMovable` with `arkCpntPickable_Simple` only. Tool item filter = Pickable_Simple or Loot component, class
  arkPickup, or name matching pickup|loot|food|coin|bottle|bread|apple|fruit|book|note|potion|ammo (excluding NPCs; NPCs also carry Loot).
- Item type: `*(entity+0x2e8)` = idDeclEntityDef; `*(decl+0x48)` is a `char*` model/def path, e.g.
  `models/interactive/pickups_entities/coin_dunwall_lowvalue.def` (the tool prints its tail).
- arkNPC has ~70 components incl. `arkCpntHealthEvolved`, `arkCpntAssassination`, `arkCpntInventory`, `arkCpntAIKnowledge`,
  `arkCpntAIAwarenessHUDDisplay`, `arkCpntAISensor_*`, `arkCpntCorpsePossession`.

## Inferred / not decoded
- Guard vs civilian: name contains "guard" (verified by names). `entity+0xd0` flags differ: 0x1e8008 guards, 0x1e9008 civilians,
  0x1e0008 story NPC (meagan_foster); meaning of bits unverified (printed as `flags` in --json).
- Dead hint: 73 of 116 NPCs are named `models/characters/base/human_dead_N` (pre-placed corpses); `--kind npc` tags `dead` from that name.
  The real health/dead state is decoded below ("NPC health", `--health`); the name hint is kept for the default output.
- The whole level appears streamed in (NPCs/items far away listed, d up to hundreds); filter with --radius.

## Example output (live, player at 68.92,-78.70,-15.16)
```
arkNPC  nwall_guard_male_small_01.def_4733 (39.38, -81.58, -5.29) d=31.28 guard
arkNPC  ls/characters/base/human_dead_4732 (54.57, -74.28, -3.38) d=19.08 DEAD civ
arkNPC  lower_class_male_small_01.def_4667 (12.50, -74.18, 4.55)  (--near 12 -74 4.5) civ
arkPickup pickups_coin_empress_highvalue_1 (68.00,-109.47,-6.19) d=32.06 .../coin_empress_highvalue.def
arkPickup pickups_dark_bread_01_2 (50.63,-98.97,4.79) d=33.81 .../dark_bread_01
arkPickup pickups_rat_skewer_01 (52.30,-98.98,4.86) d=32.99
arkMovable env_bottle_04_21 (bottle prop, Pickable_Simple)
```
Full scan takes ~1.4 s.

## NPC health (arkCpntHealthEvolved)
Component = the `arkCpntHealthEvolved` entry of the entity component list (+0x268); vtable `base+0x1c51588`, 32 virtual slots.
Tools: `python tools/entities.py --pid P --kind npc --health` (per-NPC `hp=cur/max DEAD UNCON? aux`) and
`python tools/npc_watch.py --pid P [--near X Y Z --radius R] [--hz 60] [--seconds N] [--out file.csv]` (CSV row on every
health/dead/unconscious change: `perf_s` = `time.perf_counter()` = QPC seconds, `tick_ms` = GetTickCount64, `prev_poll_s` bounds the change
time, plus NPC and player positions; `--selftest` runs a synthetic check). Measured poll rate 60 Hz sustained, ~1800 Hz max for 23 NPCs.
Status tags: VERIFIED = live read plausible AND code seen; LIVE = live read only; CODE = disassembly only; INFERRED = guess.

| Field | Offset | Status | Evidence |
|---|---|---|---|
| current health (float) | `comp+0x20` | VERIFIED | Single writer `SetHealth` (`base+0x7185b0`, args this/xmm1=new): clamps to [0, `vtable[0xd8]` max] and stores `comp+0x20`, posts a change event only when the integer-rounded value changes. Constructor-like init at `0x718b70` stores 1.0f. Live (pid 48096, 116 NPCs): all 23 live NPCs show cur == max; all 20 dead guards show cur 0 with max 30/40. |
| max health (float) | `*(comp+0x08)+0x68` (decl) | VERIFIED | Getter `0x717db0` returns `[[this+8]+0x68]` (base health); `vtable[0xd8]` (`0x7197f0`) is the full max (stage modified; alternate `decl+0xb4` when entity flag 0x20 set). Live decl+0x68 per archetype: civilians 10, `*_guard_male_small` 30 (lower_guard) / 40 (unwall_guard, hatter), story NPC meagan_foster 1; corpse defs `human_dead_*` 0. Equal within every archetype. |
| dead flag | `entity+0xd1` bit `0x08` (= dword `entity+0xd0` bit 11) | VERIFIED (live) / CODE | Live: bit set on exactly the 93 NPCs with cur == 0 (73 corpses + 20 dead guards) and on none of the 23 living ones (0x80 guard, 0x90 civilian). Code: damage handler `0x717950` (vtable-called, event in rdx, amounts in r8) skips processing when set, `SetHealth`/heal `0x719fe0`/`0x71a7c0` bail out when set. The setter was not located (not a literal `or [ent+0xd1],8` in `.text`); existing tools/health_trial_fields.py calls this bit "damage exclusion", live data says dead. |
| unconscious / downed flag | `entity+0xd1` bit `0x20` | INFERRED | Set by `0x879410` (only caller `0x718110`, a HealthEvolved method) and only when bit 0x08 is clear; `OnDeath` branches on it and max health switches to `decl+0xb4`. No NPC in the sampled scene had it set, so it has never been observed live. Fits "knocked out"; could also be a generic "removed/despawning" mark. |
| health aux/modifier | `comp+0x70` (ptr) | CODE | `GetHealth` (`0x719a70`) returns `comp+0x20` plus `0x745f60(aux)` when a feature cvar is on and the pointer is non-null. Live: 0 on all 116, so `comp+0x20` is the exact health today. Tools report `aux` and you should distrust `cur` if it is true. |
| damage entry point | `0x717950` = vtable slot via the event system; applies through `vtable[0xd0]` (`0x718fe0`), then `cur < 1.0 -> SetHealth(0)` and `vtable[0xe8]` (slot 29, `0x71a9e0`, OnDeath) | CODE | `0x7177e0` is the simple `SetHealth(cur - amount)` helper; `0x719fe0` is AddHealth(delta, clamped). |
| kill damage-type mask | `comp+0x130` (qword) | INFERRED | `OnDeath` does `comp+0x130 |= event+0x10`; live value 0 on all living NPCs. Not read live on a freshly killed NPC. |
| damage type mask override | `comp+0x48` (mask) / `comp+0x50` (double, expiry game time or -1) | INFERRED | Handler `0x717950` expires it (`now > comp+0x50` resets to `decl+0x70`) and tests the damage event mask `event+0x10` against it. |
| last-attacker / last-damage-time | none found | n/a | Not in the component; the attacker lives in the 0x90-byte damage event passed to `0x717950`. `comp+0x108`/`+0x10c` look like a hit counter and reset value (CODE `0x719cb0`), `+0x110`/`+0x114..` a health-snapshot ring (`0x71b750`); untested live. |

Notes for latency work: the health write happens on the game thread inside the damage event; a damaged-but-alive NPC changes `cur` only
(dead flag stays 0), a lethal hit changes `cur` to exactly 0 (clamped) and later sets the dead flag, possibly one poll later, so
`npc_watch` logs both rows. All values above were read with a read-only handle (PROCESS_VM_READ), no injection and no writes.
Not yet observed: any non-lethal damage or an unconscious NPC (the sampled session had only full-health and pre-dead NPCs), so the
partial-health value path and flag 0x20 remain unconfirmed by a live transition.
