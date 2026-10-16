# Native weapon equip, holster, draw and world-copy spawn

Steam build 17576867, image base `0x140000000`. Addresses are RVAs unless written `0x14…`.
This is static analysis only, done with Python and capstone on `artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe`.
Decl text was read from the game containers with `artifacts/claude/impact/scripts/declx.py`. The game was not launched or attached.

* **VERIFIED** means I read it in the disassembly, the PE bytes or the decl text.
* **INFERRED** means I deduced it and it needs a live check.

The scripts are in the session scratchpad. `sx.py` provides `dis`, `callers`, `vt`, `owner`, `str` and `rtti`. The other scripts are `eqcalls.py` (every EquipChange request with its code and requester), `spawnargs.py`, `vtgrep.py`, `spawncalls.py` and `immscan.py`.

State pool indices come from the live dump `artifacts/research/native-arm-assets/native-carry-fsm-v95-44016.json`. To get a state: `S = *(*(FSM+0x90) + 0x10 + idx*8)`, where `FSM` is the player component with vtable `1c73d58`.

| idx | state | decl text (VERIFIED) |
|---|---|---|
| 44 (0x2c) | Block | `m_freeLeftHandDuration = 0.5 s` |
| **53 (0x35)** | EquipChange, **right hand** | `m_defaultEquipment = "models/weapons/ply_sword/ply_sword"` |
| 54 (0x36) | Draw, right hand | |
| **64 (0x40)** | EquipChange, **left hand** | `m_slot = SLOT_LEFT_HAND`, `m_defaultEquipment = crossbow_emily`, `m_defaultAmmo = bolt_regular`, `m_powerSwitchMidnight…` |
| 65 (0x41) | Draw, left hand | |
| 80 (0x50) | EquipChange, head | `m_slot = SLOT_HEAD` |
| 81 (0x51) | Undraw (the sheathe/draw toggle) | `m_holdDuration = 0.4 s` |
| 82 (0x52) | Powerwheel | |

## Summary

1. The FSM is asked to enter a state with **`8022e0(fsm, idx)`** (start, no params), **`8024d0(fsm, idx, params*)`** (start with params) and **`802a20(fsm, idx)`** (stop). All three append to the request queue at `FSM+0x220`, which `804850` drains each FSM frame. (VERIFIED)
2. Every equip and holster goes through **EquipChange** (53 = right, 64 = left). A 0x30-byte params block built by **`835590`** carries a **code bitmask** and a **requester**.
   * Block puts the gun away with code `0x81` and redraws it with code `0x06`.
   * HoldObject uses `0x21` and `0x16`.
   * The sheathe toggle (Undraw) uses `0` for both hands and `4` to draw both.
   * The weapon wheel calls `7fb350`, which requests 64 with the chosen item. (VERIFIED)
3. Runtime spawning: look the decl up with **`1e030(base+0x2434170, name, 0, 0)`**, then call **`8ae9b0(game, decl, &origin, &axis, owner, hidden, scale, &linVel, &angVel)`**.
   * `8ae9b0` writes spawn origin, axis and **initial linear and angular velocity**, then registers the entity.
   * `arkCpntRigidBody` OnStart (`809d80`) reads those velocities. (VERIFIED)
   * There is no physics def for the *player* sword or pistol. The shipped `models/weapons/sword_howlers/sword_howlers_movable.def` is a physical sword prop. (VERIFIED decl)
4. The drawn weapon (an `arkWeapon` entity, vtable `1ce1e98`) uses the base idEntity **Hide** (vfunc `+0x80`, `8784b0`) and **Show** (vfunc `+0x88`, `87c590`). Neither is overridden and there is no weapon-specific call. (VERIFIED)

## 1. Requesting a state (Goal 1)

| fn | args | body (VERIFIED) |
|---|---|---|
| `8022e0` RequestStart | `(fsm, int idx)` | `if (fsm+0x90 && idx < 0x5a) 802560(fsm+0x220, {1, idx, 0})` |
| `8024d0` RequestStartParams | `(fsm, int idx, params*)` | requires the state to exist and `params[1] == idx || params[1] == 0x5c`. Calls state vfunc `+0x90(S, params)`, which for EquipChange is `836f00` and stores a copy in the state's param list `S+0x30`. If that succeeds, pushes `{1, idx, params[0] /*request id*/}` |
| `802a20` RequestStop | `(fsm, int idx)` | pushes `{2, idx, 0}`. `854190(S)` is "stop self" |
| `803180` IsActive | `(fsm, idx)` | `*(u8*)(S+0x20)` |
| `8031b0`, `8034b0` | `(fsm, idx[, params])` | state enabled (`fsm+0x230+idx`, `fsm+0x28a+idx`) plus CanStart (`+0x38` with params) |

How `804850` uses the params:
* With a zero param it calls CanStart `+0x30` and then activate `+0x50`.
* Otherwise it fetches the params with `+0x98(S, id)`, then calls CanStart `+0x38(S, p)` and activate `+0x58(S, p)`. Afterwards it calls `+0xa0`, which drops the params.

The function has no lock (`802560` is a plain idList append), so **call it only on the game thread**. (VERIFIED: no lock. INFERRED: which thread.)

* **Sheathe toggle = Undraw (81).**
  * Its update `846c00` stops itself unless its input is held. Bit `S+0x30` comes from `7f8870(ctrl, 0x51)`.
  * It accumulates `S+0x34` up to `decl+0x70` (0.4 s).
  * If both hands are empty (`inv+0x68` and `inv+0x70` are 0), it requests 64 and 53 with **code 4** and requester `0x5b`, and posts event `39aba80`.
  * Otherwise it requests 64 and 53 with **code 0** and posts `39abad0`. It then stops itself.
* **Draw (54 right, 65 left)** is the "press attack with an empty hand" state.
  * CanStart `831910` requires that hand to be empty and the remembered item `836280 → 7f88b0(ctrl, ecIdx)` to be non-null.
  * Activate `831670` requests EquipChange 53 or 64 with **code 8**. The code is `0x18` when the attack input is pressed and the item type is not 2.
  * Update `8319e0` waits until the hand is occupied, then requests the item's follow-on state (`itemDecl+0x118`).
* **EquipChange** is requested only through `8024d0` with a params block (68 call sites; the list is in `eqcalls.py`).

## 2. EquipChange: params, codes and the equip and holster paths (Goal 2)

### 2a. Params block `arkPlayerStateEquipChangeParams` (0x30 bytes), built by `835590(out, item, code, requester)`

| off | field | notes |
|---|---|---|
| +0x00 | u32 request id | taken from the global counter at `0x3994a30` |
| +0x04 | u32 target | always `0x5c` ("any") |
| +0x08 | `arkInventoryItem*` item | null means "use the code bits". `835520` also fills +0x10 |
| +0x10 | `arkInventoryItem*` secondary | ammo item. Only the wheel path (`835520`) sets it |
| +0x18 | u32 **code** | see below |
| +0x1c | u32 **requester** | a state index. `0x5a` is an anonymous lock. `0x5b` means none |
| +0x20 | u64 0 | when non-zero, the midnight start is skipped (`836f60`). Meaning INFERRED |
| +0x28 | u8 1 | |

Prologue bytes for pinning:
* `835590`: `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 8b`
* `8024d0`: `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 30 48`
* `8022e0`: `48 83 ec 38 48 83 b9 90 00 00 00 00 74 2a 83 fa`

### 2b. Code bits (processor `8363f0`, unequip `836f60`, equip `835e00`; VERIFIED unless marked)

| bit | effect |
|---|---|
| `0x001` | **lock**: `836920` adds the requester to `S+0x160` and increments `S+0x158`. While `S+0x158 > 0` no draw happens; a new selection is only remembered. **`836b40` prunes the lock of any requester state that is no longer active.** Requester `0x5a` only increments the counter and is never pruned. A `0x5b` lock is pruned at once |
| `0x002` | **unlock**: removes the requester, or decrements the counter for `0x5a` |
| `0x004` | draw the remembered item. If the code has no lock or unlock bits this is the *selected* item; otherwise it is the *restore* item. Does not touch the other hand |
| `0x008` | the same as 4, but when an item results, it **also requests the other hand's EquipChange with code 4** unless `0x100` is set |
| `0x010` | power-switch midnight variant (`836120`, "No powerSwitchMidnight") (INFERRED meaning) |
| `0x020` | instant unequip with no midnight (`8362c0` returns 0). Also cancels a pending op instead of deferring it |
| `0x040` | use `equipFastMidnight` |
| `0x080` | use `unequipFastMidnight` |
| `0x100` | do not request the other hand |

With no item, and neither 4 nor 8 set, **the current item in that hand is unequipped** (`8367d4 → 836f60`).

**Controller memory**, where `ctrl = *(FSM+0x2f8)` and `ctrl+0x58 == FSM`:

| hand | selected | restore |
|---|---|---|
| left | `ctrl+0x190/+0x198` | `ctrl+0x1a0/+0x1a8` |
| right | `ctrl+0x1b0/+0x1b8` | `ctrl+0x1c0/+0x1c8` |

* Getters are `7f88b0` and `7f88e0`; setters are `7fb000` and `7fb040`. Each entry is a pair `{item, ammo item}`.
* Unequip `836f60` sets *selected* to the item it put away. It sets *restore* to that item when the code has `0x1`, and clears it otherwise.
* The right hand alone falls back to the decl default (the sword, `decl+0x88`) when both the current and the selected item are null.

### 2c. Native callers (VERIFIED from `eqcalls.py`)

| caller | state | code | requester |
|---|---|---|---|
| **Block update** `85a160` (left hand occupied, `S+0x38 ≥ decl+0x100` 0.5 s, `8031b0`/`8034b0` OK) | 64 | **0x81** (lock + fast unequip) | 44 |
| **Block exit** `859640` (if it holstered) | 64 | **0x06** (unlock + draw restore) | 44 |
| Block slot 22 `859ff0` | 64 | 0x14 | 44 |
| HoldObject entry `83d770` | 53 (and 64 for two-hand) | 0x21 | 49 |
| HoldObject exit `83ce20` | 53 and 64 | 0x16 | 49 |
| Undraw `846c00` | 53 and 64 | 0 / 4 | 0x5b |
| Draw `831670` | 53 or 64 | 8 / 0x18 | 0x5b |
| Slash activate `86aa80` | 64 | 4 | 39 |
| Mantle, CatFall, Stun, Choke, Peeking, Assassination | 53/64 | 0x21 or 0x81 on entry; 0x46 or 6 on exit | self |

**Weapon wheel (VERIFIED):**
1. `arkGameLogicStatePowerwheel` (vtable `1b6b918`) slot 8 `4f6030` calls `4f65b0`, which registers the UI callback `"WheelClosing"` → `4f64b0` → `4f5710`.
2. `4f5710` reads `wheelData.selectedInventoryIndex` and calls **`7fb350(ctrl, itemDecl, ammoDecl)`** for the matching entry (`this+0xfd0`, stride 0x28: `+8` index, `+0x18` item decl, `+0x20` ammo decl).
3. `7fb350` requires `ctrl+0x171 != 0`. It finds both items with `614e60(inventory, decl)` and skips if they are already in the left hand.
4. It then builds params with `835520(&p, item, ammo, 0x20 or 0x30, 0x5b)` and calls `8024d0(fsm, 64, &p)`.
5. If Block is active and has holstered the gun, it also writes the new item into *restore* (`ctrl+0x1a0`), so that Block's exit draws the new item.
6. `4dff00` (quick-select, INFERRED) calls `7fb350` as well.

**Message paths (VERIFIED):** both handlers are registered by `arkCpntPlayerController` slot 5 at `7f9849` and `7f987c`.
* `ARK_MSG_INVENTORY_EQUIP_ITEM` (0x9d) → `7f8d60`.
  * Payload `{itemDecl*, u8 flags}`: flag 1 means "only if the hand is empty", flag 2 selects code 0x30 instead of 0x20, flag 4 adds `0x100`.
  * It requests 53 or 64 with the item and requester `0x5b`.
* `ARK_MSG_INVENTORY_UNEQUIP_ITEM` (0x9e) → `7f9140`.
  * Payload `{itemDecl* or null, u32 slot}`. It requests 64 for slot 1 and 53 otherwise, with **code 0** and requester `0x5b`.
* Both need `ctrl+0x171`. Events are posted with `p = 8112c0(810240()+0xc8e8, id, 0x10, playerId)`.

Low-level slot writes are `614b20` (equip into slot; posts `0x9f ITEM_EQUIPPED`) and `614ae0(inv, slot)` (remove). They skip the FSM, the animations and the controller memory. **Do not use them.**

## 3. Spawning a world entity (Goal 3)

| fn | signature | evidence |
|---|---|---|
| `1e030` FindDecl | `decl* (declType*, const char* name, bool makeDefault, bool)` | the entityDef type object is **`0x2434170`**. It is registered at `13525c3` with `"entityDef"` and `"idDeclEntityDef"`. Names drop the `entitydef/` folder and `.decl`, for example `"models/weapons/gun_player/gun_player.def"`; shipped `m_equipment` values use this form. The lookup form itself is INFERRED |
| game vfunc `+0x1c0` `8a78f0` SpawnEntityDef | `ent* (game, decl, spawnArgs*)` | `spawnArgs` comes from `146520` (11 bytes) |
| **`8ae9b0` SpawnEntityDefAt** | `ent* (game, decl, const idVec3* origin, const idMat3* axis, idEntity* owner, bool hidden, const idVec3* scale, const idVec3* linVel, const idVec3* angVel)` | see below. 67 callers |
| game vfunc `+0x1e8` `8a7bf0` | `ent* (game, entName, declName)` | FindDecl(makeDefault=1) → vfunc `+0x1d0` → SetName |

What `8ae9b0` does, in order (VERIFIED):
1. Calls SpawnEntityDef.
2. Writes the spawn origin with `87c320` (`ent+0x78`) and the axis with `87c2c0` (`ent+0x84`).
3. If an owner is given, sets `ent+0xac = owner id` and calls `87c360(ent, owner+0xc0)`.
4. If `scale` is given, calls `ent->vfunc[0x60](scale)` (`87c340`, which writes the model at `+0xc0`; the meaning "scale" is INFERRED).
5. If `linVel` is given, writes `ent+0x33c` (`87c2a0`); if `angVel` is given, writes `ent+0x348` (`87c280`).
6. If `hidden`, calls `87c350` (`+0xd0 |= 1`, `+0xd1 |= 1`).
7. Calls `8aecc0(game, ent, -1)`, which registers the entity in `game+0x3130b0`.

The prologue is `40 57 41 56 41 57 48 83 ec 40 48 c7 44 24 20 fe`.
* Identity constants: `0x31af668` is `vec3_origin` and `0x228a780` is `mat3_identity`, as `59e9e8` uses them.
* `arkCpntRigidBody` OnStart (`809d80`, vtable slot 5) loads `ent+0x33c/+0x340/+0x344` and `+0x348..+0x350` as the body's initial velocities, unless a saved-state object exists (`809fc9`, `80a00c`). So **the spawn velocity is applied natively**. (VERIFIED)
* A velocity can also be set later with event `0x149 ARK_MSG_VELOCITY_IMPULSE`, handled by `727100` (see `docs/native-hold-object.md`).
* An NPC throwable projectile (`454890`) uses exactly this pattern, passing real `linVel` and `angVel` at `[rsp+0x38]` and `[rsp+0x40]`.
* **Removal** works the way `killEntity` (`8863e0`) does it: `883f60(ent, base+0x39a6f30 /*remove event*/, 0)`. The same event is used for loot and dropped weaponry (`744c07`, `614a37`). The name "Remove" is INFERRED.

**Loot and sever (partial):**
* `arkCpntLoot` pre-spawns its loot entities **hidden at the origin** (slot 3 `744b30`: `8ae9b0(..., &vec3_origin, &mat3_identity, owner, hidden=1, 0, 0, 0)`).
* The reflection offsets of `m_lootSpawnImpulse`, `…Yaw` and `…Pitch` are `0x80`, `0x84` and `0x88`. They sit in a decl that also has `m_swarmDef`, `m_lootDef` and `m_spawnOffset`; the class is INFERRED to be a swarm nest. I did not trace that consumer.
* The `arkCpntSeverable` limb (`762070`) spawns with `8ae9b0` at the NPC origin and axis with no velocity, then runs ragdoll setup.
* The `m_dropEquipmentOnSever` consumer was not found statically.
* NPC weapon defs carry `components/equipment/rigidbody_npc_sword_dropped.cpntrigidbody` (`m_entityContext = ENT_CONTEXT_FLAG_DEAD`, `COLLISION_LAYER_DEBRIS`). So NPC swords and guns *are* the dropped physical objects.

**Candidate decls** (VERIFIED decl text):

| decl | contents | verdict |
|---|---|---|
| `models/weapons/sword_howlers/sword_howlers_movable.def` | `idDynamicEntity` inheriting `movable_parent` (rigid body + health), model `sword_howlers.lwo`, the dropped-sword rigid body, `po.metal`, audio | **best sword stand-in**: physics only, no weapon component. The look differs from Corvo's sword |
| `models/weapons/ply_sword/ply_sword` | the player sword: `arkWeapon`, `sword_player.md6`, `cpntweaponmeleeplayer` | **no rigid body**. Do not spawn it as a world copy |
| `models/weapons/gun_player/gun_player.def` | the player pistol: `cpntweapongun` | no rigid body |
| `models/campaign/dunwall/midnightassets/sword_player_midnight.def`, `gun_corvo_midnight.def`, `gun_emily_midnight.def` | the exact player models (`sword_player.md6`, `gun_player.md6`) as a cinematic `idDynamicEntity` (refframe + animevolved) | **no physics**. Usable as a kinematic visual the mod moves itself (teleport `87d1e0`) |
| `models/weapons/gun_base/gun_base.def`, `gun_elite/elite_gun.def` | NPC gun: dropped rigid body + `weapongunnpc_elite` + bullet pickable | physical, but carries an NPC weapon component and an ammo pickup. Risky standalone (INFERRED) |

## 4. Hiding and showing the drawn weapon (Goal 4, VERIFIED)

`arkWeapon` (vtable `1ce1e98`) slots 16 and 17 are the base functions:
* **Hide `8784b0`**: if `+0xd0 & 1` is clear, sets it and jumps to `87e040`.
* **Show `87c590`**: if the bit is set, clears it and jumps to `87e040`.

`87e040` derives the effective hidden bit (`+0xd0` bit 1) from the entity's own bit OR its bind master's bit, and pushes it to the model. So a sword shown while its master arm (`body+0x680`) is hidden stays hidden.

`774a10(u32* idField, bool show)` is only "resolve the id, then call +0x88 or +0x80". There is no dedicated weapon-visibility API.

Holstering also clears the hand slot, so `inventory+0x68` or `+0x70` becomes 0. Whether the weapon entity itself is destroyed or only hidden was not traced (INFERRED: it persists, because `619396` spawns a weapon entity only when `item+8` does not resolve).

## 5. Recommended programmatic approach

Run all of these on the game thread, the same hook scope the mod already uses for Stage0 and arm show. Pin the prologue bytes listed above.

```cpp
// p: 0x30-byte buffer, 16-byte aligned. fsm = player FSM component (vt 1c73d58).
call<void(void*,void*,int,int)>(0x835590)(p, nullptr, code, requester);
call<void(void*,int,void*)>(0x8024d0)(fsm, 53 /*right*/ or 64 /*left*/, p);
```

* **Sword sheathe and draw:**
  * Sheathe: state 53, code `0x00`, requester `0x5b`. Use `0x80` for the fast clip or `0x20` for instant.
  * Draw: state 53, code `0x04`, requester `0x5b`. This draws the *selected* right item, and falls back to the default sword.
  * Do not use Draw (54) or code 8: they also pull out the left weapon.
  * This is the per-hand version of the native toggle. The 0.4 s Interact hold stays as it is.
* **Pistol holster and draw (left, 64):**
  * Holster: code `0x00` (add `0x80` for fast), requester `0x5b`. The holstered item becomes *selected*.
  * Draw last: code `0x04`, requester `0x5b`.
  * Gate both on `pool[64]+0x20 == 0` and `pool[53]+0x20 == 0`, or simply re-request; EquipChange allows itself to restart and defers to `S+0x128`.
  * Alternative: post `0x9e` with `{nullptr, 1}` to holster.
* **While a dropped copy exists**, take an **anonymous lock**: holster with code `0x01` (or `0x81`) and requester **`0x5a`**.
  * While locked, the wheel, Undraw, Draw, Block exit and HoldObject exit only *remember* selections, so nothing can redraw the weapon. (VERIFIED)
  * Release on pickup with code `0x06` and requester `0x5a`. This draws *restore*, or the newest wheel choice.
  * Risks: the counter `S+0x158` may be saved (`arkPlayerStateEquipChangeSaveData` exists). A lock that is never released leaves the hand stuck. Release it on unload, load and death, and verify this live (INFERRED).
* **Spawn a weapon copy:**
  1. `decl = 1e030(base+0x2434170, "models/weapons/sword_howlers/sword_howlers_movable.def", 0, 0)`.
  2. `e = 8ae9b0(game, decl, &handPos, &handAxis, player, false, nullptr, &handLinVel, &handAngVel)`.
  3. Hide nothing extra: holstering takes the real weapon out of the hand.
  4. On pickup, call `883f60(e, base+0x39a6f30, 0)` and then draw.
  * For the exact player look, spawn `sword_player_midnight.def` or `gun_corvo_midnight.def` / `gun_emily_midnight.def` instead. These have no physics, so the mod would have to simulate the fall itself and move the prop.
  * There is no shipped physical pistol prop except the NPC gun defs (risky).

## 6. Open live checks

* Whether the howler sword prop collides with the world and settles once spawned standalone. It overrides `m_collisionSettings` with every block false, uses the debris layer and `ENT_CONTEXT_FLAG_DEAD`.
* The FindDecl name form (`….def` suffix) and the null return for an unknown name with `makeDefault = 0`.
* That code 0 and code 4 on 53 and 64 play the normal holster and draw clips, and do not fight Block's own `0x81` / `0x06` cycle.
* Whether the anonymous lock (`0x5a`) survives save and load.
* Whether calling `8024d0` from the mod's Stage0 hook is on the FSM's thread.
