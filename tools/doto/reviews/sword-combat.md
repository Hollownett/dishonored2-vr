# Sword drop/throw and strike registration (DOTO v145730): causes and fixes

Static review of 2026-11-22 (D2 `Dishonored2.exe`, DOTO `Dishonored_DO.exe`, rules at HEAD ca688bd/a4c2638, generated tree regenerated into the scratchpad
`sword/out2/src` because `build/doto-src` predates the latest rules; v145730 DLL inspected directly). Nothing in `src/`, `offsets.json` or the game was changed.
Scratch: `scratchpad/sword/*.py` (pefile + capstone; `pins.py` byte-pin check, `rtti.py`, `vt.py`, `ex.py`/`getres.py` resource-index/decl dump via `tools/extract_arm_assets.py`).
Rule format: `text` rules match `contains` in the D2 source line and replace `old` in the generated line; "source": "review/sword-combat".

## A. "I cannot drop or throw the sword"

Path (D2 code, unchanged in logic): `SwordGrip::update` (vr_controls.hpp:548) -> release outside the shoulder zone with `in.drawn || pending_` -> `Action::drop`
(dxgi_probe.cpp:16260) -> `NativeWeaponHands::request(Op::drop)` -> game thread `service()` (native_weapon_hands.hpp:209): `NativeMelee::sword`, `equip(0x20)`,
`alive()` ? `visible(copy_,true)` : `spawn()` (FindDecl 0x1e030 type 0x2434170 + SpawnEntityDefAt 0x8ae9b0), `launch(r)`, then `fly()` Teleports it every frame
(`0x87d1e0`). Pickup/recall = `stash()` -> `visible(copy_,false)`.

### A1 (certain): `visible()` still compares against D2's idEntity Hide/Show, so it never shows or hides anything
* `out2/src/native_weapon_hands.hpp:752` is `const std::uintptr_t want=base+(on?0x87c590:0x8784b0);` (D2 Show / Hide). The v145730 DLL has the same constants:
  `mov eax,0x87c590` at RVA 0x106ea4 and `mov edi,0x8784b0` at 0x106eb2 (the DOTO twins 0x8e9f40 / 0x8e5de0 appear only in `npc_weapon_pickup`'s code, 0x106ff4 / 0x107002),
  plus two inlined `cmp rbx, base+0x87c590 / 0x8784b0` pairs (0xeb494, 0xebdb6, 0xebc98, 0xdd568).
* Why the existing `rvas` rule (offsets.json "from 0x8784b0 -> 0x8e5de0", source review/pin-audit) did not reach it: port.py classifies both literals as field offsets because the text before
  them ends in `(on?`; they are listed under `offset_review` (`kind none`) in port-report, never as `rva-code`.
* DOTO truth: idEntity Hide = `0x8e5de0` (`or byte [rcx+0xd8],1; jmp 0x8eb990`), Show = `0x8e9f40`, in vtable slots +0x80 / +0x88 of all 54 idEntity-derived vtables
  (`vt.py`: e.g. 0x1c534e8, 0x1c77a48 ...). The slots did not move.
* Consequence: `visible()` returns false every time (`++visibility_skips`, not exported in the status JSON). `spawn()` never calls it and DOTO's copy def is
  `knife_midnight.def` with `edit.flags { hide = true; }` (D2's `sword_player_midnight.def` has no such flag; extracted with `ex.py`, decl text below). So the dropped blade is created hidden
  and can never be shown; every later drop reuses the entity (`alive()`) and `visible(true)` fails again; pickup's `visible(false)` also fails (the entity stays in the world).
  The player sees the sword leave the hand and nothing fly: "cannot drop or throw".

```json
{"file":"src/native_weapon_hands.hpp","contains":"const std::uintptr_t want=base+(on?0x87c590:0x8784b0);","old":"(on?0x87c590:0x8784b0)","new":"(on?0x8e9f40:0x8e5de0)",
 "why":"idEntity Show/Hide the visible() test and call use: D2 0x87c590/0x8784b0 -> DOTO 0x8e9f40/0x8e5de0 (vtable +0x88/+0x80 unchanged). Both literals are classified as field offsets by port.py, so the rvas rule never applied.","source":"review/sword-combat"}
```
(Confidence: certain for the constants; high that this alone makes the drop show up if the def's hide flag is cleared by Show, see A2.)

### A2 (high): the copy def spawns hidden; show it right after the spawn (src change, trait)
`decl = entityDef knife_midnight.def`: `inherit entitydef_ref/midnight_animated_parent.def` (loddriver, midnightproxy, animevolved_midnight, referenceframe comps),
`flags { hide = true; }`, `renderModelInfo.model = models/weapons/dlc01/player/billie/knife01/ply_billie_knife01.md6`. The parent class is `idEntity`; D2's sword def sets `class = "idDynamicEntity"`.
The def does exist in `game2.index`, `game2_001.index` and `game2_patch.index` (generated/decls/entitydef/.../midnight_props/knife_midnight.def.decl), the model md6 / skeleton are in game1.
(Whether the engine turns `flags.hide` into +0xd8 bit 0 at spawn was not proven statically; reading +0xd8 of the copy live settles it. Calling Show is harmless if it is already visible.)

Change: `src/game_profile.hpp` add `copy_def_spawns_hidden` (DOTO true, D2 false) and `copy_needs_present` (DOTO true, D2 false); in `native_weapon_hands.hpp`:
* `spawn()` (line ~735), after `if(!entity) return false;`: `if(game_traits::copy_def_spawns_hidden) visible(base,read,entity,true);`
* `fly()` (line ~315, after the `Teleport` call) and once at the end of `spawn()`: `if(game_traits::copy_needs_present) present_model(base,copy_);`
  (idEntity instead of idDynamicEntity: Teleport may not move the render model on its own; `present_model` = 0x8eb680 here, __try-guarded, the same call the held copies already use at line 820-829.
  Medium-low confidence, needs the live check below; drop it if the model already follows.)

### A3: checked and fine (not the cause)
* FindDecl type `0x24fef60` (D2 0x2434170): 21 lea refs in each exe, same neighbours; `knife_midnight` FindDecl needs no other change (`spawn` returns false only if the decl is missing: `native[1]` failures would count).
* SpawnEntityDefAt `0x91d5f0` vs D2 `0x8ae9b0`: identical prologue and argument use (owner at [rsp+0x80], hidden/scale/vel/ang at +0x88..+0x98, vslot 0x1c0 -> 0x1c8, id +0xa8 -> +0xb0, owner id +0xac -> +0xb4).
* The six `verify()` pins (0x89db70 params builder, 0x869760 equip request, 0x1eb70, 0x91d5f0, 0x8f18f0 remove, 0x8eab40 teleport) all match the DOTO image; `alive()` registry `game+0x327770`, id `+0xb0`; inventory slot offsets (+0x68/+0x70, items +0x38) identical in all `arkCpntInventory` vtable functions; equip codes and requester (0x5b -> 0x68, message 0x5c -> 0x69) are the generator's consistent +13 shift.
* Gesture: `drawn` comes from `hand_weapon_bits&2` (slots[1]); the draw/hold/sheathe half of SwordGrip works in the headset, so the drop branch is reached.
* NativeMelee struct offsets used for the thrown-sword damage identity: unchanged ctor `0x7e41b0` (instruction-identical), take `0x8e6bb0`, hitlist `0x6850b0`, all pins match.

## B. "Sometimes my strikes do not register"

Hits come from two sources: (1) the mod's physical blade contact (render thread: blade casts vs NPC -> `capture_sword_contact` -> game thread `NativeMelee::apply`, native_interaction_aim.hpp:988-1085),
(2) the game's own Slash (state 39) after `AttackIntent`/`AttackPress` forwards a swing >= 3 m/s as the attack button.

### B1 (certain): in v145730 every physical contact is rejected (KillCam state index 83 is Powerwheel in DOTO)
* `capture_sword_contact` (native_sword_contact.hpp:104) requires `native_contact_scene`, which walks `native_action_camera_states`: for each non-combat entry the pool slot `*(pool+0x10+index*8)` must be typed
  with the listed vtable, `state+8==index`, inactive. The generated table in v145730 holds `{83,0x1cc2f68,false}` (DLL data at 0x699e38: `53 00 00 00 ... 682fcc01 ...`).
  DOTO inserted `ChangeWeaponState` at state 74, so ids >= 74 are +1 (valve-carry.md): KillCam is 84, slot 83 is another state. The check fails for every contact: `++sword_capture_rejected_`, no `NativeMelee::apply`, no stealth hold.
* Fixed in the tree at ca688bd (offsets.json:3597 `{83,0x1c3fc08,false}` -> `{84,`; my regeneration shows `{84,0x1cc2f68,false}`), but no build contains it (last build v145730).
  This is why only some strikes land: the ones where the swing also presses the native attack and the game's own Slash hits.
* Other ids >= 74 in the combat path: none found (`native_player_state(...,86,...)` crank rule exists; assassination 13, choke 41, slash 39, block 44, equip 53/64 are unchanged).

### B2 (certain, already fixed in HEAD): slash scene rate was off in v145730
`native_slash_scene_rate.hpp` skipped its install (steal 8 vs DOTO's 11-byte prologue, pin-audit row 9); a4c2638 (v145731) adds `game_traits::slash_perform_steal/unwind`. Without it the native Slash plays at 1x while the VR swing is faster, so the hit window (active frames 200-270 ms after the press) misses a moving target.

### B3 (medium, unmeasured): the blade is a D2 constant
`dxgi_probe.cpp:6075 sword_blade_length=.65` (cast segment, sword tip, hand-clamp body at 10724/11262/11757/11791; vr_combat.hpp:28 `blade_len`; spacewarp_pass.hpp:69 comment). D2's `sword_player.md6` header bbox is x -0.22..0.651 from the grip origin, so .65 is that blade.
Billie's `ply_billie_knife01.md6` is a short folding knife (DOTO's bmd6 has no bbox header; not measured here). With .65 the contact casts reach 2-3 times past the visible blade and the rear/side test uses the contact point, so
strikes register in air or at the wrong moment. Make it a trait: `game_traits::sword_blade_length` (D2 .65, DOTO measured), used at the four sites; `blade_len` in vr_combat.hpp:28 likewise. The measurement must come live (render-bounds of the knife entity model, or the joint positions in `ply_billie_knife01.edgeskl`).
Also noted: the game's native hit volume `m_phantomShapeDimensions.x` is 2.8 for Billie's dagger (`ply_billie_knife01` inline) and 3.0 in D2's sword decl: about 7% shorter native reach, not changeable by the mod.

### B4: checked and fine
`NativeMelee::verify` pins (ctor 0x7e41b0, take 0x8e6bb0 + vtable slot +0x148, hitlist 0x6850b0, unaware 0x6143c0, swarm kill 0x718930, sever pin 0x7c2b8f, impact play 0x65f2e0) all match; RTTI of every typed vtable (arkNPC 0x1ce2f28, arkCpntWeaponMeleePlayer 0x1cada28, its decl 0x1cad8d8, arkDeclInventoryItem 0x1caf028, arkCpntInventory 0x1caadd8, Assassination/Choke/Slash/Block states) names the same classes;
Billie's knife keeps `arkCpntWeaponMeleePlayer` (decl `sword.cpntweaponmeleeplayer`, `m_billieDagger = true` inline) plus an extra `billiedagger` component, so `NativeMelee::sword` finds it; NPC dead/health flags (0xd9/0xdf), forward row +0x31c, origin +0x310, entity id +0xb0 are translated (out2/src/dxgi_probe.cpp:11000, 11016-11018).

## How to verify (headset-free sim, tools\doto\doto_sim.py; run only when the PC is idle)
1. `python tools/doto/port.py`; `grep -n "on?0x8e9f40:0x8e5de0" build/doto-src/src/native_weapon_hands.hpp` and `grep -n "{84,0x1cc2f68" build/doto-src/src/native_action_camera.hpp` must hit; build the next candidate.
2. Drop/throw: `python tools/doto/doto_sim.py --name sword1 --dll build/doto/<new dll> --scenario sword_regrab --knob sword-grip-trace --seconds 120` (draw, sweep, release away from the zone, regrab; `holster_draw` adds the tossed sword).
   Read `dxgi-probe-result.json`: `sword_grip.native` = [ops, failures, spawned, removed] expects spawned >= 1, failures 0; `sword_grip.copy` non-null with `pos` changing while `flying`; `sword_grip.thrown[1]` (flight frames) > 0; screenshots `shot-*.png` must show the knife on the floor.
   Live read (allowed while the game runs): the copy entity's flags byte at +0xd8 (bit 0 = hidden must be 0 after A1/A2) and its model position (model = entity +0xf8, position +0x1860) equal to `copy.pos` (settles A2's `present_model`). `visibility_skips` should be added to the status JSON next to `sword_grip.native`.
3. Strikes: `sword_sweep` (phase 2 is the 3.5 m/s slash) in a save with a guard in reach; with the fix `sword_contact.capture_rejected` stays 0 and `sword_contact.enqueued/invoked/native_returned` and `contact_gates[7]` rise (before: capture_rejected == contact count, invoked 0). Headset needed for the blade-length feel (B3).
