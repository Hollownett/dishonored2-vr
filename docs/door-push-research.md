# Pushing doors open with the hands (VR)

Steam build 17576867, image base `0x140000000`. Addresses are RVAs unless written `0x14…`.

This is static analysis only:
* Python and capstone on `artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe`;
* the read-only Ghidra project (`artifacts/claude/impact/scripts/ghidra-decomp.cmd`);
* decl and asset text read from the game containers with `declx.py`.

The game was not launched or attached.

* **VERIFIED** means I read it in the disassembly or decompile, the PE bytes, the reflection tables, or the decl or asset data.
* **INFERRED** means I deduced it and it needs a live check.

The scripts are in `artifacts/claude/door-push/`:

| script | what it does |
|---|---|
| `refl.py <class-regex> [--fields]` | Dumps the engine's own reflection tables: class name, base, size and field offsets. |
| `msgsend.py <id>` | Finds the code that posts an `ARK_MSG_*`. |
| `door_anim_angles.py assets` | Prints the hinge angle per frame of the door clips. |
| `door_push_gate.py` | Reference for the push decision, with asserts (passes). |

The raw decompiles are `openable_decomp.c` and `decomp*.c`. The extracted decls are under `decls/` and `assets/`.

## Summary

1. **A door is animated, not simulated.**
   * An `arkDoor` entity is the leaf. It has a keyframed Havok body on `COLLISION_LAYER_MOVER`.
   * The leaf is bound at its hinge to a spawned "animated part" (`doors.md6`, 6 joints).
   * The animated part plays 1 s clips: open, close, locked, windblast.
   * The `arkCpntOpenable` component runs a 3-state machine: **Closed 0, Animated 1, Opened 2**.
   * There is **no continuous angle or open-ratio API**. The open angle is the animation time. (VERIFIED)
2. **The native entry points the mod needs already exist.**
   * The player's Use is a posted `ARK_MSG_USE` (0x96) carrying `{u32 user id}`. Its only poster is `877430`.
   * The door handler `74b6e0` toggles the door: closed opens away from the player; opened closes. It also handles locked doors, keys and HUD messages.
   * Scripts call `Open 74dbb0(cpnt, ccw, unlock)` and `Close 74ac50(cpnt)`.
   * Windblast posts `ARK_MSG_DOOR_PUSH` (0x138), which bangs a closed door open. (VERIFIED)
3. **Recommended first version: push = native Use.** The cost is a few lines.
   * The hand-collision sweep already stops the palm or blade at the door and reports the hit entity.
   * When the tracked hand is pressed 3 cm or more into an `arkDoor` leaf, the torque of the push agrees with the way the native Use will swing the door, and the door is idle and not NPC-owned:
   * post `ARK_MSG_USE {player id}` with the existing pinned `NativeWeaponHands::post()`. It needs **no new native function pins**.
   * The door then swings fully over 1 s, exactly as with the grip-Use the user already accepted.
4. **Phase 2, the door follows a slow hand, is staged** in `artifacts/claude/door-push/door_push_phase2.patch` (section 4c).
   * A fast push (1.2 m/s or more into the leaf) keeps the phase 1 native Use: the full swing.
   * A slow push starts the same Use. Then, every game frame, the native clip's time is set to the angle the hand needs, with `540d30`. Nothing else is pinned.
   * The door keeps the largest angle reached. Past 80°, or on a fast push, the native clip finishes, so the door ends in Opened.

## 1. How a door is represented

### 1a. Classes (VERIFIED from RTTI and reflection, `refl.py`)

| class | size | vtable | notes |
|---|---|---|---|
| `arkDoor : arkBreakable : idEntity` | 0x390 | `1cb7c68` | The door leaf entity. It is the class of `entitydef_ref/door_single_parent` (41 defs) and `door_breakable_parent` (20 defs) |
| `arkCpntOpenable : arkCpntInteractable` | 0x1f0 | `1c569c8`, second vptr `1c56ac0` at +0x28 | The door logic |
| `arkStateOpenable_Closed / _Animated / _Opened` | 0x20 / 0x38 / 0x20 | `1c56908 / 1c56948 / 1c56988` | State id vfunc +0x28 returns 0 / 1 / 2 (`6d040`, `1156ea0`, `10bd300`) |
| `arkDeclCpntOpenable` | 0x208 | `1c56af8` | Decl at cpnt+0x08 |
| `arkMsgDoorPush` | 4 | | `{idManagedClassPtr<idEntity> m_pusher}` |
| `arkCpntHealthDoor`, `arkDeclCpntConstraintLimitedHinge` | | | Breakable doors' health. The hinge constraint is **not** used by doors (no door decl references it) |

Classes that are **not** `arkDoor` are never touched by this feature:
* level-transition doors (`door_transition_parent`, 22 defs: `idEntity` with a usable-static component);
* commode and desk doors (`usable_parent`, `arkCpntUsableWithStateList`);
* safes.

### 1b. Fields

**`arkDoor`** (entity). The field names come from reflection.

| off | field | notes |
|---|---|---|
| +0xa8 | spawn id | Registry `game+0x3130b0 + (id>>16)*8` |
| +0xf0 | render model | The static leaf mesh, for example `door_interior_350cm_01.lwo` |
| +0x300 / +0x30c | world origin / axis rows | The leaf's origin is on the hinge (section 1c) |
| +0x370 | `arkCpntOpenable*` | VERIFIED: `783360` stores `GetComponent 877720(door, base+0x39c59a0, 0)` here |
| +0x378 | `m_lockerId` | `0x3fff0010` means free. The NPC door logic (`3f8de0`, `3f9550`) locks the door by compare-and-swap with its own id through `7833a0` and unlocks it through `783430` (VERIFIED code). Reading this as "NPC owns the door" is INFERRED |
| +0x380 | `m_users` | `idList<arkDoorUser{u32 id, float distance}>` |

**`arkCpntOpenable`**

| off | field |
|---|---|
| +0x08 | decl |
| +0x10 | owner `arkDoor*` |
| +0x18 | identity (`rig::started`) |
| +0x20 | `m_usableByPlayer` |
| +0x40 | `arkStateMachine` (see below) |
| +0x60 | `m_stateClosed` |
| +0x80 | `m_stateAnimated` (see below) |
| +0xb8 | `m_stateOpened`. +0xc8 is `m_counterClockWise` of the open |
| +0xd8 | `m_defaultState`: 0 opened, 1 opened CCW, 2 closed |
| +0xdc | `m_animatedPart` (entity id) |
| +0xe0 / e2 / e4 / e6 | joint indices: peek, front, back, handle |
| +0xe8 | linked openable (double doors), `{id, ptr}` checked through `3da910` |
| +0xf8 | `arkCpntAnimBase*` of the animated part |
| +0x110 | umbra gate id |
| +0x114 | `m_instigator` (entity id) |
| +0x118 | `m_isBlocked` |
| +0x120 | blocking entities |
| +0x140 | needed keys |
| +0x1e0..+0x1e7 | anim indices: openCW, openCCW, closeCW, closeCCW, lockedCW, lockedCCW, windblastCW, windblastCCW |
| +0x1e8 | `m_locked` |
| +0x1ea | `m_isPlayerPeeking` |
| +0x1ec | `m_isPushed` |
| +0x1ed | `m_isBroken` |

The state machine at +0x40 (`arkStateMachine`, VERIFIED reflection):
* +0x48 = `m_currentStateId`;
* +0x4c = `m_nextStateId`;
* +0x50 = `m_states` list.

`1406df740` applies a pending transition (`next` becomes `current`) inside the component's think `74ec80`.

`m_stateAnimated` at +0x80:
* params at +0x90: `isOpenAnim`, +0x91 `isOpenCloseAnim`, +0x92 `counterClockWise`, +0x93 `animIdx`;
* +0xa8 `m_animationPhase` (0 opened, 1 opening, 2 closing, 3 closed);
* +0xac `m_previousOcclusionValue`, which works as an open fraction;
* +0xb0 `m_nextState`.

`m_previousOcclusionValue` is `acos(|dot(leaf.axis0.xy, part.axis0.xy)|)/(π/2)`, computed by the Animated update `74e7a0`. `DAT 228a750 = π/2` (VERIFIED). Reading it as "the open fraction or occlusion" is INFERRED.

### 1c. Geometry and animation (VERIFIED from the asset data)

The structure model is `doors.md6` (decl `md6def/.../doors.md6.decl`, skeleton `doors.edgeskl`).

Its joints and bind translations:

| joint | bind translation |
|---|---|
| origin | 0 |
| **hinge_joint** | **0** |
| handle_joint | (0, -1.4, 1.2) |
| anchor_back | (-1.5, -0.75, 0) |
| anchor_front | (+1.5, -0.75, 0), turned 180° |
| anchor_center | (0, -0.75, 0) |

What follows from the bind pose and the decl:
* The leaf mesh is attached at prop tag `hinge` (parent `hinge_joint`, translation 0, rotation 90° about Z).
* So **the hinge axis is vertical through the animated part's origin**, and the leaf's origin sits on it.
* The leaf runs along local **-Y**.
* Local **+X** (part axis row 0) is the door normal: the stand points are at ±X.

The animated part is spawned by `74c1f0` at the door's spawn origin, with the door's spawn axis turned by π/2 through `10fae0`: `8ae9b0(game, decl->m_animatedPartDef, door+0x78, axis, door, …)`. It is static. The cpnt start `74cd90` binds the door to it with `875e80(part, door, decl+0x80 m_attachementTag, 0, 0, 1)`. That the tag is `hinge` is INFERRED: it is the only tag in the md6.

Hinge yaw from the clips (`models/interactive/doors/animations/player_*.edgeanim`, 30 fps, `door_anim_angles.py`):

| clip | duration | hinge yaw |
|---|---|---|
| `player_open_ccw` | 1.0 s | 0 → **+90°**, **linear 5.3°/frame for frames 0-17 (0.567 s)**, small overshoot to 93°, settles at 90° |
| `player_open_cw` | 1.0 s | 0 → -90° |
| `player_close_cw` | 1.0 s | +90 → 0 (closes a CCW-opened door) |
| `player_close_ccw` | 1.0 s | -90 → 0 |
| `player_windblast_*` | 1.0 s | 0 → ±90° in 0.2 s, bounces back to 82°, settles at 90° |
| `player_hit_*` | 0.5 s | ±1° wiggle |

The NPC clips (`door_*_walk_door_*`) sync the door to the NPC's own push animation.

How the side is chosen:
* Native Use picks **CCW** when `dot(part.origin - user.origin, part.axis0) >= 0`, that is when the user is on the -X side (`74b6e0`).
* CCW turns the -Y leaf to **+X**, away from the user.
* **Native Use therefore always opens the door away from the player.** VERIFIED by combining the bind pose, the clip and the code.

Collision, from the decl `rigidbody_door.cpntrigidbody` (inherits `cpnt_rigid_body_keyframed`):
* `COLLISION_LAYER_MOVER` (5);
* `blockPlayer` / `blockNPC` / `blockDynamic` = false.

The layer enum is VERIFIED from the enum table at `237d768`. The mod's world sweep mask `kWorldOccluderLayers 0xc43e` includes bit 5, so the palm and blade sweeps **should hit door leaves**. That they do is INFERRED; it is live check 1 in section 5.

The decls `components/interactive/openable_door_single.cpntopenable` are not in any container index. Their default anim names and tag are therefore unread (`idxnames.py` found no "openable").

## 2. Native door functions and messages (VERIFIED)

All functions are x64 `__fastcall`. `cpnt` is an `arkCpntOpenable*`. The prologues are the first 16 bytes.

| RVA | what | args | prologue | evidence |
|---|---|---|---|---|
| **`877430`** | **PostUse**(entity, out, userPtr): `8112c0(810240()+0xc8e8, 0x96, 4, entity+0xa8)`, then the payload is the user id | rcx entity, rdx out, r8 user managed ptr | `48 89 5c 24 10 57 48 83 ec 20 48 8b d9 48 8b fa` | The only `mov edx,0x96` before `8112c0` in .text (`msgsend.py`). Called from `785ad0` and `8cf8f3` |
| `74bd80` | `ARK_MSG_USE` handler: if `msg+0x10` sender is this door or its linked door, calls `74b6e0`. Otherwise it is a blocking entity's use (`74b390`) | (cpnt, msg) | `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57` | Registered by the cpnt start `74cd90` through `810810(…, 0x96, thunk 74edc0)` |
| `74b6e0` | **Use**: sets instigator = `**(u32**)(msg+8)`. A player Opened door gets the close anim (stored CCW). Otherwise CCW is chosen by the player's side; locked or blocked doors get the locked anim and `74c100` (HUD "locked" / "blocked", key check). Then requests Animated `{isOpen, 0, ccw, anim}` and handles the linked door | (cpnt, msg) | `48 89 54 24 10 56 48 83 ec 50 48 8b 42 08 48 8b` | Decompiled |
| `74ba10` | `ARK_MSG_DOOR_PUSH` handler: if not `m_immuneToWindblast` (decl +0x1f0) and state is Closed, sets the instigator to the pusher and CCW from the pusher's side, plays the windblast anim (locked anim if locked), sets `m_isPushed = 1` (NPCs hit by the door get `m_highOpenedDamage` = `pushed_door_damage`: BIG hit reaction, 5 hp) | (cpnt, msg) | `48 89 5c 24 18 48 89 54 24 10 55 56 57 48 83 ec` | thunk `74eda0` registered for 0x138. The only poster is `4d7e80`, called from the Windblast dispatcher `4d89a0` ("Unmanaged blasted entity %s") |
| `74dbb0` | **Open**(cpnt, ccw, unlock): if state is not Opened, optionally unlocks, picks the open anim (locked anim if locked), sets `m_isPushed = 0`, requests Animated `{1, 0, ccw, anim}`, and opens the linked door with `!ccw` | rcx, dl, r8b | `44 88 44 24 18 53 57 48 83 ec 28 83 79 48 02 41` | Kiscule door action `793440` |
| `74ac50` | **Close**(cpnt): only when the state is Opened and the door is unlocked. Close anim from the stored CCW; handles the linked door | rcx | `40 53 48 83 ec 20 83 79 48 02 48 8b d9 0f 85 f1` | Kiscule close `793230` |
| `74e4c0` | SetLocked(cpnt, locked, propagate) | | `48 8b c4 48 89 70 18 57 48 83 ec 70 48 89 68 10` | Kiscule lock `7933d0`, unlock `793570` |
| `74d7a0` | Retarget(cpnt, int 0 open / 3 close, ccw), used by the `STOP_USE` handler `74bbf0` | | `48 89 5c 24 08 48 89 6c 24 18 56 57 41 55 41 56` | |
| `74bf40` | bool `dot(part.origin - p, part.axis0) < 0` (which side p is on) | (cpnt, const float* p) | `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48` | |
| `877720` | GetComponent(entity, typeKey, flags). The openable key is `base+0x39c59a0` | | `48 89 5c 24 10 44 89 44 24 18 55 56 57 41 54 41` | |
| `6dc560` | entity from id (`7b5e00(game+0x3130b0, &id)`) | ecx id | `89 4c 24 08 48 83 ec 28 48 8b 0d 21 8d 2c 03 48` | |
| **`53eef0`** | **AnimBase_PlayBasic**(animBase, ch, char animIdx, float weight, int playMode, float rate, durationTime_t start, bool playOnCurAnimEnd). Fills `arkAnimBasicInputData` and bumps its version. Requires `animIdx > -1` and **weight > 0**; the rate is unchecked | rcx, edx, r8b, xmm3, then the stack | `48 83 ec 68 41 80 f8 ff 0f 8e 8d 00 00 00 0f 57` | Animated activate `74c5c0` calls `(ab, 0, anim, 1.0, 1, 1.0, t0, 0)`. A door that starts opened (`74cd90`) calls `(…, 4, 1.0, 0, 0)` |
| `53e770` | bool AnimBase_IsPlaying(animBase, ch) | | `48 63 c2 48 8b 94 c1 a0 03 00 00 48 85 d2 74 0c` | |
| **`540d30`** | **AnimBase_SyncTime**(animBase, ch, durationTime_t expectedDuration (r8d), float fraction (xmm3)): when the newest entry of node "simpleAnim" plays a clip of exactly that duration (±FLT_EPSILON), sets its time to `duration*fraction` | | `44 89 44 24 18 53 48 83 ec 40 48 63 c2 0f 29 7c` | The Animated update syncs NPC door pushes through it |
| `810240` / `8112c0` | event manager / post `(mgr+0xc8e8, u16 msg, size, u32 target)` returning the payload | | already pinned in `NativeWeaponHands::post` | |

Play modes (`arkAnimBasicPlayMode_t`, enum table `237b380`):

| value | mode |
|---|---|
| 0 | PLAY_ONCE |
| 1 | PLAY_AND_STAY |
| 2 | LOOP |
| 3 | STAY_ON_FIRST_FRAME |
| 4 | STAY_ON_LAST_FRAME |

How an AnimBasic entry advances (`1406e4850`):
* time += dt × rate while it is "advancing" (modes 0-2);
* PLAY_AND_STAY clamps at the end and stops advancing;
* the "is playing" output is: any entry advancing or blending.

The Animated state ends (`74adf0`, giving `m_nextState`) when it stops playing. So **PLAY_AND_STAY at rate 0 holds the pose and keeps the state Animated**. The code path is VERIFIED; the behaviour is INFERRED until live.

Message ids come from the table at `2381fb0`, stride 0x18. This is consistent with `START_RAGDOLL` 0xb9 in the memory notes.

| id | name |
|---|---|
| 0x96 | USE |
| 0x97 | STOP_USE |
| 0xa1 | BREAKABLE_BREAK |
| 0xa2 | DAMAGED |
| **0x138** | **DOOR_PUSH** |

The NPC "push" fields (`m_pushToOpenDoor`, `m_canOnlyPushDoors`) belong to the NPC locomotion config (door traversal clips). They are not a door API.

## 3. Detecting hand-door contact (reuse, nothing new)

* **Palm and blade sweeps already exist.** In `dxgi_probe.cpp` (around line 3087), `WeaponCollision::update` is fed by `NativeInteractionAim::post_weapon_segment` and `weapon_hit`. The segments are swept on the game thread with `NativeWorldTrace::sphere_cast` (0xc89bc0, mask 0xc43e).
  * slot 0 = palm: chest to grip, r 0.05;
  * slot 1 = weapon (sword blade or gun barrel);
  * slot 2 = the sword's reach.
* Each hit carries `point`, `normal` and `entity`. The entity is mapped from the body handle by `NativeWorldTrace::entity_of`.
* `CollisionResult::shift` (the clamped grip minus the raw grip) is how far the tracked hand is pressed *through* the surface. That is the push depth.
* **A door test** is `rig::typed(read, base, entity, 0x1cb7c68)`, then `cpnt = *(entity+0x370)` with `typed(cpnt, 0x1c569c8)`, `*(cpnt+0x10) == entity`, and `rig::started(cpnt)`.
* **The hinge is the animated part's origin.** That part is `6dc560(*(u32*)(cpnt+0xdc))`, or registry index `id>>16`, with origin at +0x300 and axis rows at +0x30c. It never moves. Use it rather than the leaf for the side and torque tests.
* `native_rigid_body.hpp` (`nearest_grab_body`) is **not** usable: it accepts dynamic bodies only, and door bodies are keyframed. A model+0x1840 world AABB of a turned leaf is loose. Both are kept only as a fallback if live check 1 fails: a proximity test against the leaf OBB rebuilt from the hinge, part axis rows and leaf length.

## 4. Implementation plan

### 4a. Phase 1: push to swing (recommended, smallest diff)

**Render thread** (the collision loop in `dxgi_probe.cpp`, per side, after `weapon_collision.update`):
1. In the `query` lambda, also keep the slot 0 hit (`palm_hit`), as `blade_hit` is already kept for slot 1.
2. A push needs all of these:
   * the hit entity is an `arkDoor`;
   * the palm depth is `dot(r.shift, hit.normal) >= 0.03` m (knob `build/door-push-depth`);
   * the depth holds for 2 frames or more;
   * the empty hand's grip is **not** held (the grip is native Use or grab).

   With a weapon, use the slot 1 hit instead:
   * the hand has moved 3 cm or more along `-normal` since the contact began;
   * the tip speed is below 1.5 m/s (a slash is not a push).
3. Queue `NativeWeaponHands::request({Op::door_push, pos=hit.point, vel=-hit.normal, target=door, side})`, at most once per 300 ms per side.

**Game thread** (`NativeWeaponHands::service`, inside the stage-0 scope, gameplay only, which is where `post()` already runs):
1. Re-validate the door:
   * the registry entry at `id>>16` is this entity;
   * the vtable is right;
   * the cpnt checks above pass.
2. Skip the door when:
   * `state != next` (+0x48 / +0x4c) or `state == 1`;
   * `+0x378 != 0x3fff0010` (an NPC owns the door);
   * `+0x1ea` (peeking) or `+0x1ed` (broken) is set;
   * `+0x20` (`m_usableByPlayer`) is 0, which covers scripted or cinematic doors;
   * the per-door cooldown of 1.5 s has not passed.
3. Run the gate in `door_push_gate.py`. `H` = part origin, `τz = ((c-H)×p).z`.
   * Closed: post only if `(τz > 0) == (dot(H - player.origin, part.axis0) >= 0)`. That means the swing the native Use will make matches the push.
   * Opened: post only if `(τz < 0) == *(u8*)(cpnt+0xc8)`. The push turns the leaf back.
   * Locked (+0x1e8) or blocked (+0x118): post anyway, with a 3 s cooldown. The native rattle, the "Locked" or "Blocked" message, and the key unlock are the feedback.
4. Post `0x96`, size 4, target `*(u32*)(door+0xa8)`, payload = the player's id (`*(player+0xa8)`).
   * This is exactly what `877430` sends for the player's Use. AI attribution (`m_instigator`), keys, HUD and double doors all stay native.
   * Count `door_pushes` and `door_push_rejects[why]` in the status.

The only new ABI is reads: `1cb7c68`, `1c569c8`, +0x370, +0x378, cpnt +0x20 / +0x48 / +0x4c / +0xc8 / +0xdc / +0x118 / +0x1e8 / +0x1ea / +0x1ed. Pin the `arkDoor` vtable's first slot (`8e0bd0`) and the cpnt's (`8eb600`) if desired; `typed()` already compares the exact vtable address.

If live check 4 ever shows the gate refusing ordinary pushes (the Use side and the push disagree), call `Open 74dbb0(cpnt, τz > 0, false)` or `Close 74ac50(cpnt)` directly instead. First write `*(u32*)(cpnt+0x114) = player id`; both native handlers do that first. That costs two more pins.

### 4b. Phase 1b (optional knob): shove

A fast palm shove (≥ 2.5 m/s into a closed door) posts `0x138 {u32 player id}` instead.
* It is the Windblast bang-open: 0 to 90° in 0.2 s.
* `m_isPushed` makes the leaf knock NPCs behind it down with `pushed_door_damage`.
* It is closed-only, and it skips doors with `m_immuneToWindblast`.
* Its balance (it is Windblast's effect without the power) is the user's call.

### 4c. Phase 2: slow push = the leaf follows the hand (staged patch)

The user asked for this on 2026-10-14: "slowly if pushed slowly, fast and fully open if pushed with speed; open a door a little and peek".

**The patch.** `artifacts/claude/door-push/door_push_phase2.patch` was made against the working tree with these files:

| file | md5 |
|---|---|
| `native_weapon_hands.hpp` | `7060d1b1…` |
| `dxgi_probe.cpp` | `a057a6ab…` |
| `vr_selftest.py` | `bbba3adb…` |

* `git apply --check` passes.
* It was **not** built or run.
* The scripts that produce it are `edit_*.py` in the same folder. Re-run them on copies of the files if the tree has moved on.

**Threads.**
* Render thread (the collision loop, block "Door push: an open hand"): only detects. Every frame a palm or slow blade touches an `arkDoor` leaf, it posts the latest contact per hand to `NativeWeaponHands::door_contact` (a mailbox, not the op queue).
* The contact carries:
  * the tracked hand `raw = r.grip - r.shift` (the undeflected blade point for a weapon);
  * its velocity, windowed over 60 ms (`publish_hand`'s method; per-frame differences alias);
  * the depth `|shift|`;
  * the frames 3 cm deep or more;
  * the hit normal and point.
* Game thread (`door_service`, called from `NativeWeaponHands::service`, stage-0, gameplay only): decides and calls the engine.

**What the game thread does.**
1. **Validation.** Every contact and every held door is re-checked through `door_of`:
   * the registry id and the `arkDoor` vtable `1cb7c68`;
   * cpnt `1c569c8` with owner = the door;
   * the animated part from the registry (cpnt+0xdc), whose origin, axis row 0 and axis row 1 are the hinge, X and Y;
   * the frame must be upright and right-handed (`cross(x,y).z > 0.95`);
   * anim = cpnt+0xf8, vtable `1b7f408` `arkCpntAnimBase`, owner = the part;
   * model = door+0xf0, static model `1b0dd88`.
2. **Phase 1, unchanged gate.** Pressed 3 cm for 2 frames, state 0/0, usable, not peeking, not broken, locker free, cooldown 1.5 s (3 s when locked): post `ARK_MSG_USE {player id}`.
   * Then a **hold** starts unless one of these holds:
     * the push is fast (`-dot(vel, n) >= 1.2`);
     * the door is locked or blocked (rattle only);
     * the geometry or anim checks failed;
     * the `540d30` prologue differs;
     * the push would not turn the leaf the way the Use swings it (refusal 6).
   * The swing side `s` is the native Use's rule, `dot(hinge - player.origin, x) >= 0 → CCW`.
3. **Angle the leaf needs.**
   * `deg(p) = atan2(s·(p-H)·x, -(p-H)·y)`: the opening angle of a point around the hinge. The closed leaf runs along -Y and opens toward +X for CCW.
   * Add a margin of `0.08 m / r` (palm radius plus half the leaf) so the hand is not inside the leaf.
   * The hold keeps `max` over the contacts. It only grows: the door never closes by itself, and pushing further opens more.
4. **Every stage-0 call, for each held door** (at most 8; the oldest finishes natively):
   * Wait up to 0.5 s for the posted Use to reach Animated.
   * Then require all of these:
     * state = next = 1;
     * the locker is free;
     * the instigator (cpnt+0x114) is the player;
     * the Animated params (cpnt+0x90..0x93) are `{1, 0, ccw == s>0, openCW/CCW index from cpnt+0x1e0/+0x1e1}`.

     Otherwise the door is let go (release reason 3/4/5) and the native code carries on.
   * First active frame: the leaf's measured angle (centre of its static model's world box +0x1840 around the hinge) must lie between -10° and 80°. Otherwise it is release 7 (the asset convention does not hold, so the native swing finishes). The hold starts at `max(need, measured)`, so it never pulls back behind what the native clip already did.
   * Then `t = clamp(deg/90 × 17/30 − dt, 0, 0.55)`, and call `540d30(anim, 0, 0x3f800000 /*1.0 s*/, t)`. The −dt allows for the native clip running one frame at rate 1 between this write and the anim evaluation. A false return is release 8 (counted in `door_sync_fails`).
   * At 80° or more: release 6, and the native clip finishes, so the door ends **Opened**.
   * A fast push on a held door: release 1, same result.

**Status.** `sword_grip.doors` =

| index | value |
|---|---|
| 0 | pushes |
| 1 | refusals |
| 2 | last refusal (1 door, 2 cpnt, 3 state, 4 usable/peek/broken/NPC, 5 cooldown, 6 wrong-way push) |
| 3 | holds |
| 4 | releases |
| 5 | last release (1 fast, 2 gone, 3 state, 4 NPC, 5 params, 6 ≥ 80°, 7 frame, 8 sync) |
| 6 | hold deg |
| 7 | measured leaf deg |
| 8 | contact-point deg |
| 9 | sync fails |

**Why these primitives** (VERIFIED code, `decomp_animbasic*.c`):
* The door's own Use enters Animated with `53eef0(ab, 0, open, 1.0, PLAY_AND_STAY, rate 1, 0, 0)`.
* An AnimBasic entry keeps "playing" (`1406e4850`: advancing until its time reaches the clip end) while its time stays below the end. Setting the time every frame therefore keeps the state Animated indefinitely.
* `540d30` writes the newest entry's time directly, with no blend. A new `53eef0` request would add an entry with the anim's 0.2 s blend-in; the `arkDeclAnimBasic` default is `0x3e4ccccd`, and `door_single.animbasic` sets none. While the old rate-1 entry fades it keeps advancing: about 8° of overshoot. So phase 2 uses only `540d30`.
* All four door structures (door, grid, heavy, metal) use the same `player_open_*` clips: 1.0 s, header `0x3f800000`, hinge 0→90° linear over frames 0-17.
* Release costs nothing: stop writing.

**Door state, collision, saves, NPCs**

| subject | what happens | status |
|---|---|---|
| State | While held the door is in state 1 Animated, so the umbra gate is open (it can be peeked through). The gate vfunc is called with "open" because the state is already 1 when `74c5c0` runs. The AI got the native "opened" event from the Use. The navmesh only reports "opened" in state 2, so to NPCs the door is a closed door they can open | VERIFIED code |
| Leaf collision | The leaf is bound to the part's hinge joint, and its keyframed body follows the entity transform, the same path as the native swing. Setting the clip time should move the body too | INFERRED; live check 4 |
| Save / reload | The state machine saves Animated with its params and `m_nextState` (`74e080` → `74e200`), but not the clip time. A reload replays the clip, so a held door comes back fully opened | VERIFIED code, outcome INFERRED |
| NPCs | A guard who wants the door takes the locker (`7833a0`). The hold lets go at once and the native clip finishes opening, so he walks through. A guard's Use on a held door finishes the Animated state to Opened first (`74b6e0`), then the hold sees state ≠ 1 and lets go | INFERRED; live check 7 |
| Native Use on a held door (grip) | The player path when Animated: finish to Opened, then open again from t=0. This is a snap-shut glitch, and vanilla does the same if Use is pressed during the swing. A fast push is the clean way to finish | INFERRED; live check 8 |
| Pause | Stage-0 does not run, and neither does the anim | INFERRED |
| Kill cam | Stage-0 does not run but the game does, so held doors finish opening | INFERRED |

**Fast threshold of 1.2 m/s.**
* The native open clip turns the leaf at 159°/s (2.77 rad/s). Its surface outruns a 1.2 m/s hand anywhere from 0.45 m off the hinge, so after the hand-off the hand is never inside the leaf.
* Casual reaching is 0.2-0.8 m/s.
* The constants are `door_fast` and `door_open_deg`.

**Selftest** (in the patch): `python tools/vr_selftest.py --dll <new> --scenario doorpush --near door`.
* `--near door` picks the nearest arkDoor on the player's floor within 30 m that is closed, free, unlocked, not broken and usable. It stands on the player's side so the simulated right hand (0.4 m ahead, 0.2 m right, 0.35 m down) rests 18 cm before the leaf middle.
* The script:
  1. pushes slowly 0.4 m at 0.2 m/s (about 22° expected);
  2. holds 3 s;
  3. takes the hand back;
  4. waits 2 s;
  5. shoves 0.7 m at 7 m/s.
* It samples the door's state and measured leaf angle every 0.2 s. Its checks:
  * the door is found;
  * a hold started;
  * the leaf followed slowly (5-60° while pushing);
  * it stays ajar with the hand gone (state 1, 8-75°, within 6°);
  * the shove ends in state 2 at 80° or more.

## 5. What to verify live (in this order)

Checks 1-3 are for phase 1, checks 4-10 for phase 2; the sim selftest covers 4-6.

1. **The sweep hits doors.** Aim the `trace` test command at a closed door and at an opened door. The hit entity's vtable should be `base+0x1cb7c68`. This proves the MOVER leaf is in the 0xc43e sweep mask and that `entity_of` maps to the `arkDoor`.
2. **Read-only probe of one door:**
   * `+0x370` is a cpnt with vtable `1c569c8` and owner = the door;
   * `+0x48/+0x4c` are 0/0, then 1, then 2/2 over a grip-Use;
   * `+0x378 = 0x3fff0010` with no NPC near;
   * the part origin is the hinge: the leaf's box centre circles it during the swing;
   * `door_of`'s frame check passes: `doors[2]` is never 6 or 7.
3. **Phase 1:** a palm pressed into a closed door opens it away from the player within about 1 s; a locked door rattles once per 3 s.
4. **`--scenario doorpush --near door`:** all 5 checks PASS.
   * `doors[3] >= 1`, `doors[9] == 0`.
   * `doors[6]` (hold) and `doors[7]` (measured) agree within about 3°. If not, tune the −dt term: stage-0 can run more than once per game frame.
   * `doors[8]` (contact) is close to `doors[7]`, which proves the leaf's collision followed the clip.
   * If release 7 fires, the leaf box or the -Y/+X convention does not hold for that door. Print `door_samples` and the box.
5. **Comfort in the headset:** no judder while held. The door must not snap back a few degrees at the start; if it does, raise the starting `max`.
6. **A held door survives 30 s** with the player walking around it, and its gap is see-through (umbra).
7. **A guard pathing through a held door:** release 4 or 3, the door finishes opening, and the guard walks through.
8. **Grip-Use on a held door:** see the risk in 4c. If the snap-shut glitch shows, make the render thread send a fast contact (release 1) when the grip is pressed on a held door.
9. **Sword or pistol** held slowly against a door: it follows; a slash does nothing. With the left hand as well.
10. **Double doors, breakable doors, and save/reload** while a door is held (on a sim save slot only, never F5 on the user's slots).

## 6. Verified and inferred

**VERIFIED** (bytes, decompiles, reflection or assets read):
* class sizes, vtables and field offsets;
* state ids 0/1/2;
* the bodies of `74b6e0`, `74ba10`, `74bd80`, `74dbb0`, `74ac50`, `74e4c0`, `74c5c0`, `74e7a0`, `74cd90` (handler registration), `783360`, `7833a0`;
* the message ids and their senders (`877430` the only USE poster, `4d7e80` Windblast the only DOOR_PUSH poster);
* the play-mode enum;
* the AnimBasic entry update `1406e4850`;
* `540d30`'s "simpleAnim" time write;
* the clip angles and durations, and the skeleton bind pose;
* the collision layer of the door body (decl);
* the mod's sweep mask containing it;
* all the prologues above.

VERIFIED for phase 2: `540d30`'s signature (r8d durationTime_t, xmm3 fraction, "simpleAnim" node, FLT_EPSILON duration match); the AnimBasic play and advance logic (`1406e4af0`, `1406e4850`, `1406e3fa0`); the 0.2 s default blend; the shared clips of the four door structures.

**INFERRED** (live checks 1-10):
* the sweep hits and maps the leaf entity;
* `+0x378` is the NPC ownership;
* the leaf is bound at the `hinge` tag;
* `m_usableByPlayer` gates scripted doors;
* the ajar behaviour in phase 2;
* the AI consequences of a pushed door are identical to a Use. This should hold: the same message and the same instigator.
