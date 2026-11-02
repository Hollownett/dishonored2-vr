# Dual wielding: weapons that belong to the hand (research, 2026-10-31)

Steam build 17576867. RVAs, image base 0x140000000. Static research only: no code changed, nothing built, the game never ran.
The detail reports, each with file:line and VERIFIED/INFERRED marks, are in `artifacts/claude/dual-wield/`:

| Report | Subject |
|---|---|
| A-mod-coupling.md | about 40 places where the mod assumes right = sword and left = gun |
| B-engine-slots.md | engine slots, EquipChange, fire/melee states |
| C-hand-owned-use.md | firing, throwing and hitting without the slot |
| D-npc-weapons.md | guard weapons: representation, drop, pickup |
| E-wheel-bindings.md | wheel, X/A, wheel at the hand |

## Goal (user, 2026-10-31)

- A weapon is an object a hand picks up. The hand holding it uses it with its own trigger.
- Swap the sword and the grenade between hands. Sword left and pistol/crossbow right. Two pistols. Crossbow right with pistol left.
- Pick up a guard's sword or pistol and use it.
- Weapon wheel per hand, drawn as a ring at that hand:
  - hold X (left controller) opens it for the left hand;
  - hold A (right controller) opens it for the right hand.

## What the engine is

- **The slot belongs to the item, not to the hand or the state.** The equipment decl field `+0x68` (`arkEquipmentSlot_t`) sets it:
  - melee goes to slot 0 (right);
  - pistol, crossbow, grenade, mines, powers and Heart go to slot 1 (left).
  - The equipped items are at `inv+0x68 + slot*8`.
  - EquipChange 53/64 do not check the type; `835e00` / `614b20` put the item where its decl says. 53 with a pistol puts the pistol into the LEFT slot. (B, VERIFIED)
- **Use states hard-code the slot.**
  - About 30 Fire/Reload/Throw/Place/UsePower/Heart sites call `802c20(player, 1, ...)`.
  - Slash/Block/Assassination read `inv[0]` and need melee.
  - The gun scene is authored for `PlayerLeftEquipment`; the sword hit window is keyed RIGHT in `84e0c0`. (B, VERIFIED)
- **Aim is not tied to a hand.**
  - The shot launch `639970` takes an origin and frame. The mod already hooks it (NativeShotAim) with the left hand's pose.
  - The grenade throw and the mine/razor sensors are fed a hand pose by the mod as well. (A/B/C)
- **One inventory record per item decl, one left slot.**
  - Two pistols, or crossbow + pistol at once, cannot exist natively.
  - Powers share the left slot with the guns. (B)
- **All ranged weapons share one launch API** (`arkCpntWeaponRanged`):
  - `63b5f0` creates the projectile from `comp+0x98`;
  - `639970` places and launches it;
  - `639360` handles ammo and magazine;
  - `63b3c0` plays the shot sound.

  Bullets and bolts are real `arkProjectile`s with damage, owner, AI attention and impact FX. Grenade arming: `490600(GetComponent(proj,0x39e28e0),1,0)`. (C, VERIFIED code, never run)
- **Melee without the slot** works: `0x780260` builds the params, then `victim->vtbl[40]` (`0x879270`) applies the hit. The damage decl is found by name (`player_sword.damage`).
  - Lost compared with a native Slash: NPC parry (no ValidateHit), combos, blood FX (a `wpn.sword_flesh` play is needed) and the native takedown. (C)
- **Guard weapons are separate entities.**
  - They are NPC inventory items (`TYPE_NPC_WEAPONRY`, `.def` models bound to `right_hand_wpn` and similar).
  - `ARK_MSG_DROP_EQUIPMENT 0xb6` drops them; it is sent on death, knockout, sever and 118 scripted kills.
  - The handler `615520` moves each weapon into the DEAD context, where `rigidbody_npc_sword_dropped` starts.
  - Vanilla never lets the player take them; the elite pistol only gives 2 bullets.
  - NPC swords are static `.lwo` models with NPC damage decls; they are not the player's sword. (D)

## Verdict per combination

| Combination | Native | Route in the mod |
|---|---|---|
| Sword left + pistol/crossbow/grenade/power right (and back) | slot swap: partial (~25 sites + scenes) | **Hand mirror**: the native slots stay as they are; the mod chooses which physical hand drives slot 0 and which drives slot 1 (trigger, aim, hits, grip zone, weapon model). No engine change. |
| Two pistols | no | Native pistol + a **second mod-owned pistol**. Fire it by replaying `63b5f0 -> 639970 -> 639360 -> 63b3c0` from that hand's barrel, with the mod's own cooldown (666 ms) and reload (1.5 s). Ammo is native-shared. |
| Crossbow right + pistol left | no | Same: one native, one mod-owned. Alternative: hot swap via 64 with code `0x20\|0x40` on each pull (unverified, latency unknown). |
| Guard sword | no pickup in vanilla | Let the game drop it (0xb6). On grip, hide the native one and give the hand a copy that the mod moves (Teleport `87d1e0`, the dropped-sword pattern). Hits = contact damage with the player as inflictor. |
| Guard pistol | no | The same copy; the shot goes through the player's fire path from that hand. Ammo = the player's, +2 on pickup (as vanilla). |
| Two swords | no (one Slash) | The second sword uses contact damage only, with no native Slash. |

## Architecture (proposal)

One layer: **HandLoadout**. Each physical hand holds `{item, backing}`, where backing is one of:

1. **native slot 0 or 1.** The engine owns the item, ammo and save. The mod routes the trigger, aim pose, hit sweep, collision shape and model to the hand that holds it.
2. **mod-owned.** A second ranged weapon, a second sword or a guard weapon. The model is a copy teleported to the hand. The effect is a native call:
   - projectile replay for ranged weapons;
   - contact TakeDamage for melee;
   - a spawned and armed grenade.

Rules:

- A trigger press always acts on the item in that hand.
- Visual: the simplest uniform path is to hide the native weapon and always draw copies at the hand. That gives one code path for native and mod-owned weapons, and the `*_midnight.def` copies already exist. The cost is no native reload/fire animation on the copies.
  - The alternative for native items is attachment-joint substitution `53d3f0`/`53d6a0` (C, INFERRED). Fingers wrap the grip through finger_ik.
- Passing from hand to hand: grip the item held by the other hand. HandLoadout swaps the routing; no native equip is needed while both items stay in their slots.

What changes in the mod (A, table section 1):

- about 14 readers of `hand_weapon_bits`;
- `VrCombat` and gestures per hand;
- `sword_reversed` and `sword_tip` per hand;
- the aim block `dxgi_probe.cpp` 6040-6210, which today is "aim = the left hand";
- `SwordGrip`/`GunGrip` merged into one `WeaponGrip` per hand;
- the single `SwordFlight` copy becomes an array;
- holster zones become per-hand (back sword: mask 2u -> both; hip: one more).

Engine side effects keyed to a slot, already handled (A): Block puts the left item away (`native_block_eligibility`), and an empty left LT draws both weapons. HoldObject still puts both away.

## Per-hand wheel at the hand

- **Open.** X: `XWheelOwner` already turns X into LB (`vr_controls.hpp:17-31`).
  - The A hold exists in `WheelHands` (350 ms) but its call passes `a=false` (`dxgi_probe.cpp:10359`) because A is jump, mantle and swim.
  - Proposal: hold A on the ground = wheel, and a tap = jump on release. A in the air, in water or while mantling stays plain native A.
  - Cost: a jump comes ~press length later (to measure in the headset). If that annoys, fallback = B (the user's first choice).
- **Commit to the hand.** `7fb350` always asks 64. The mod already has `equip(53)` (`native_weapon_hands.hpp:630-640`), so the commit goes to HandLoadout instead: same slot = native equip, item already in the other hand = swap routing, second ranged = mod-owned.
  - The lease guard `native_manual_equip.hpp:104,178` is also tied to 64.
- **Ring at the hand.** Recommended: re-pose the captured wheel quad onto the opening hand: `world_ui_hand_pose(head, grip[hand], ~0.12 m)`, about 0.25 m wide and facing the eyes. This is the same pattern as the Black Market and notes (`game_xr.hpp:444-452`, `world_ui_pose.hpp:33-39`).
  - Ring zones and stage mapping scale with the width (`vr_controls.hpp:366-378`).
  - The one non-trivial change: the `WheelPointer` plane (`vr_controls.hpp:125-131`) must be at the ring, not 0.9 m ahead of the head.
  - Selection: the other hand's ray (the current laser), with the non-owner stick as fallback, and commit with the cursor hand's trigger.
  - The owner's thumbstick is busy (the thumb holds X/A).
- Not yet known: whether the sword is in the wheel list at all (it is the right-hand default).

## Phases

0. **Probes (sim, before any feature).**
   - a) A mod-fired bullet from Stage0: does it start its components, fly, damage, and consume `comp+0xa0`? This is the biggest risk, because mod spawns through `8ae9b0` have never started components.
   - b) `0xb6` on a guard: does the sword really fall and rest?
   - c) Preconditions of `7fb350`, and whether the sword is in the wheel.
   - d) The weapon on the left arm: attachment substitution vs a copy.
1. **Hand mirror**: HandLoadout with native backing only. Covers sword left + gun/grenade/power right, and passing between hands. Lab: per-hand routing unit test (trigger -> native action per loadout).
2. **Wheel per hand at the hand** (X left, A right with the jump gate).
3. **Mod-owned second ranged weapon** (two pistols, crossbow + pistol), only if 0a passes.
4. **Guard weapons**: swords first, then pistols.
5. Polish: per-hand haptics, recoil, reload gesture, two holsters.

## Risks

- 0a fails, so a mod-made projectile is dead. Fallbacks: post 0xc8 through the event queue, or fire from inside an engine tick.
- Save/load: the copies and a mod-armed grenade are not in the save. On load or level change, unhide everything the mod hid and remove the copies.
- `639970` is hooked twice (NativeShotAim + NativeFireSeamTrace). A grenade spawned by hand bypasses the `87c2a0` velocity hook.
- Two pistols share one magazine and one ammo pool (native one record per decl).
- No native fire/reload scenes on copies: we lose recoil and authored reloads.
- Found in passing: `dxgi_probe.cpp:6096` has `gate_in.right_trigger=...` inside a `//` comment, so the rule "no shot while the sword blocks" is dead. The user wants the pistol usable while blocking, so this may be the wanted behaviour; decide before "fixing" it.

## Implementation, first iteration (v752, knob `build/dual-wield`, off by default)

Built 2026-10-31. Static checks only: unit tests and the DLL build. Not yet run in the simulator or the headset.

| Piece | Where | What it does with the knob on |
|---|---|---|
| HandLoadout | `src/hand_loadout.hpp`, `tests/hand_loadout_check.cpp` | One bit, `melee`: the physical hand that drives slot 0. A squeeze at a sword holster (or the lying sword) moves the sword to that hand. A squeeze at the hip moves the gun there. A committed wheel puts slot 1 in the hand that opened it. A hand that holds the other weapon cannot take this one. |
| Routing | `src/dxgi_probe.cpp`: `mh`/`rh`, `melee_hand`, `aim_hand`, `sword_side`, `hand_loadout_bits` | The sword hand gets SwordGrip, swings/contact/block (VrCombat), the blade collision and haptics. The gun hand gets GunGrip, the native LT (`input.left_trigger` after the swap is the gun hand's trigger), the aim/crosshair/shot pose, the grenade throw and gadget aim, and the fist rules. |
| Holsters | `dxgi_probe.cpp` scabbard | The sword goes over the right shoulder for the right hand and over the left shoulder for the left hand. The gun is at the left hip, for either hand (cross-draw). |
| Wheels | `dxgi_probe.cpp` wheel block | X held opens the left wheel. A held for 350 ms on the ground opens the right wheel; a tap jumps on release. While the player moves up or down faster than 0.5 m/s, A is native at once. |
| Ring at the hand | `src/game_xr.hpp` `set_wheel_hand`/`wheel_plane`, `src/world_ui_pose.hpp`, `WheelPointer::update_plane` | The captured wheel quad sits 0.12 m above the opening hand, 0.6 m wide (the ring is about 0.2 m across). The other hand's ray selects on that moving plane. |
| Weapon copies | `src/native_weapon_hands.hpp` mirror service, `src/held_copy.hpp`, `tests/held_copy_check.cpp` | While mirrored, the native sword and gun/crossbow are hidden and `*_midnight.def` copies ride the opposite hands. The offset is measured from the native weapon versus its own hand, then mirrored across the hand's lateral axis. Grenades, mines and powers have no copy and stay drawn on the native left arm. |
| Status | `xr-live-status.json` `sword_grip.dual_wield` | `enabled`, `melee_hand`, `swaps`, `copies{mirrored, measured, spawned, teleports, hides, restores, failed, err_mm, offset}` |

Not in this iteration: two ranged weapons at once (two pistols, crossbow + pistol), guard weapons, and the right-hip holster. Each needs the phase 0 probes above first.

Known v1 limits:
- The fingers of a mirrored hand use the other weapon's authored pose.
- The right-hand gun aims by the controller, not the barrel.
- A surface swimmer holding A opens the right wheel (there is no swim flag).
- If the session stops while mirrored, the hidden native weapons may stay hidden until the next equip.
- A wheel moves slot 1 to its hand only once the slot-1 item changes. Choosing the item already equipped moves nothing: draw it from the hip with that hand instead.
- v752 carries the review fixes:
  - the A wheel stays open in logic 256;
  - the grenade release follows the gun hand's fist;
  - an empty wheel choice moves nothing;
  - busy hands never take a holster;
  - a rejected hip draw resets the gun hand;
  - the trigger swap happens only in gameplay.
- Knob-off parity was reviewed line by line and no deviations were found.

Build recipe: `artifacts/claude/dual-wield/build-dw.cmd <dll name>` runs the tests this feature touches, then builds the DLL. The full `tools/buildvr.cmd` stops earlier, at a pre-existing `dlss_mode_check` C4702.

## Headset v752 (user, 2026-10-31) and v753

The user tested v752 in the headset.
- **Worked:**
  - the sword in the left hand;
  - the pistol and crossbow in the right hand, with the bullet leaving the right hand;
  - aim, ammo, and powers (Blink) aimed correctly;
  - tap A jumps, hold A opens the wheel.
- **Asked:**
  - The left hand held the sword open.
  - Takedowns and finishers should use the sword in the right hand and give it back to the left afterwards.
  - Each wheel should be chosen by moving its own hand along the ring.
  - Still to do: per-hand weapon holding poses, and the power animations and effects on the hand that casts.

v753:
- The left fist closes on the sword. While mirrored, `hands.curls`/`index_curls[0]` are 1 and `t.hand_empty[0]` is set.
- An authored clip (`authored_arm_bits`) un-mirrors the weapons until 300 ms after it ends.
- The hand wheel is placed once per opening, centred on the hand and 5 cm behind it (`GameXr::wheel_hand_pose`, `wheel_open_gen_`). The opening hand selects through `WheelCursor`: its offset from where it opened, 3.5 cm dead zone. The other hand's laser is off.

v754 (holding poses):
- While mirrored, both hands are the player's procedural fingers (`t.hand_empty[0..1]`): neither arm's native clip holds what its hand holds. The sword hand is a full fist. The gun hand is a fist, with the index following the trigger.
- The left socket lease is kept for GunGrip's holster owner but no longer poses the left hand round the hidden native gun (`t.native_socket_token[0]=0`).
- A sword draw no longer hands the right arm to the native draw clip (`weapon_incoming[1]` only unmirrored).
- Not verified: the copy's offset is measured once per item, so the first mirrored frames after this switch could leave the copy slightly off in the palm. Watch `copies.err_mm`.
