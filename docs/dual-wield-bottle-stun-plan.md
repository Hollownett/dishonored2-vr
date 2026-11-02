# Dual wield: bottle next to weapons, stun from thrown/bashed weapons (plan, 2026-11-01)

User (headset, dual wield): (1) taking a bottle makes the sword vanish and the pistol in the other hand is drawn wrong;
(2) every weapon except the sword, thrown into a guard or swung into him, should stun him for a moment, as a thrown bottle does.

## Findings

0. The last headset run used **v791** (performance-mode session, built from `build/v790src`), and that copy has **none of v781/v782**:
   no `hold_keeps_sword`, `instant_left_equip`, `gadget_drop::equip_key` or `pickup_blocked` (grep: 0 hits in build/v790src/src,
   status `game_sword_draws` has no `hold_kept`). The v782 bottle fix was never in the game.
1. Sword: HoldObject entry (requester 49) sends code 0x21 to state 53 = lock + empty the right slot, which holsters the sword.
   v782 `NativeManualEquipRuntime::keep_held_sword_locked` turns it into lock + draw-remembered while `sword_kept`. Unverified.
2. Pistol (root cause in the mod, `src/dxgi_probe.cpp` hand routing ~7287-7306): `hand_holding_object` is treated as "both hands":
   - line 7305: every hand is set `hand_empty` while an object is held (a v399 rule from before dual wield, when the game holstered all);
     the gun hand loses its native gun clip / mirror_native and is posed as an open player hand, so the gun riding joint 59 / the hand
     points up;
   - lines 7287-7291: `slot1_gun`, the copy-gun `mirror_native` and `gun_clip` are all disabled for both hands;
   - line 7303: the sword-incoming pose is also disabled for both hands.
   The same global rule hides the sword hand's grip, so the sword hand looks empty even if the sword stays drawn.
3. Stun: a bottle is a native throwable; `thrown_prop` gives it the game's thrown state 4c94e0, so its contact damage
   (`phys_damage`) and the stun reaction are the game's. Non-native props get `fly_prop` -> `strike(... phys_damage ...)`.
   Thrown guns (`gun_fly`) and dropped gadgets (`NativeGadgetDrop` flight) sphere-cast through guards
   (`static_support=false` only) and **never strike**. There is no contact for a held gun/crossbow swung into a guard.

## Plan (DLL range v783-v789; the perf session owns v790-v799)

1. **Merge first.** Build from the main `src/` (has v781/v782) and coordinate with the perf session so its next candidate takes
   v781-v783 (or ours takes its perf changes). Otherwise the user keeps testing without the bottle fix.
2. **Per-hand hold routing** (`dxgi_probe.cpp` ~7287-7306): only `hand_hold_side` is the holding hand. That hand: `hand_empty`,
   grip >= .8, no gun clip / mirror / incoming. The other hand keeps its routing (native gun clip, mirrored gun, copy gun, sword).
   Pull the decision into a small pure function in `src/hand_loadout.hpp` (or next to `palm_occupied_bits`) with a check in
   `tests/` (bottle in left + sword right, bottle in right + pistol left, mirrored dual wield, no dual wield = old behaviour).
3. **Sim probe of the native side during HoldObject** (after step 2): does the game hide/holster the slot-1 gun or change the
   left arm clip while state 49 runs? Read joint-59-vs-hand (the copy grip measurer, `held.kinds`) with and without a bottle.
   If the clip differs, the gun hand uses the remembered grip (body `item_key` memory) for the hold, not the HoldObject clip.
   Also verify v782: status `game_sword_draws.hold_kept` > 0, sword visible, HoldObject exit restores normally.
4. **Thrown weapons stun** (`native_weapon_hands.hpp` `gun_fly`, `native_gadget_drop.hpp` flight): when the cast hits an arkNPC,
   call the existing `strike()` with the `phys_damage` decl (as `fly_prop`), once per guard per throw, amount from speed
   (2-8 hp, a guard has 30-40). The gun then bounces off as now. Sword keeps its own sword-decl hit. Armed grenades are skipped
   (they explode). Status `props_thrown`/new `guns.hits`, journal `thrown_stun`.
5. **Weapon bash stun**: a held gun/crossbow/gadget (native or copy) moving > ~3 m/s: segment along the held item's frame
   (barrel axis, ~.15 m half length) through the same `strike()` + `phys_damage`, one hit per guard per 600 ms. Not the sword
   (sword contact owns it), not a power hand.
6. **Check what phys_damage does to a living guard** before tuning: sim `swordthrow`-style scenario with a gun (`--near npc
   --blind-ai`): the guard must play the hit/stun reaction, not die, and the bottle's native hit is the reference. If
   phys_damage via TakeDamage gives no stun, find the decl the native throwable hit uses (4c94e0 path) and use that one.
7. Lab/tests: the routing check (step 2), a GunCopiesFixture-style test that a flight crossing an NPC capsule strikes once.
   Then sim (bottle + sword, bottle + pistol, gun throw at a guard, gun bash), then one headset candidate.

Not verified by any of this: headset look of the gun beside a bottle, how the stun feels.
