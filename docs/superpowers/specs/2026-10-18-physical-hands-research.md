# Physical hands for the Dishonored 2 VR mod: feasibility and approach (2026-10-18)

Scope: the first Bonelab-like feature the user chose. The visible hand stops at walls, tables, doors and guards. It pushes loose
objects, is held back by heavy or fixed things, and snaps back to the controller when the gap gets too large. The player's
view and body are never pushed by the hands. This is a study and a recommendation. No mod feature was implemented.

Inputs used:

* Mod worktree `/home/hollownet/github/dishonored2-vr-fixes`, branch `fix/review-2026-10-17`, HEAD `f2d34d3`. It was read only.
* `.superpowers/sdd/2026-10-17-review-fixes/rea-evidence.md` (earlier REA facts: 0xc85dc0, 0x7506d0, 0x87d1e0, 0x1b4760).
* `~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` §1.4, §2.1–2.3, §6.2.
* New static RE for this study: REA 6.5.0 with the Ghidra 12.1.4 provider, in one persistent `rea --mcp` session on
  `Dishonored2.exe`. The REA-reported SHA-256 is `b671f824…6180`, which matches the pin. Image base 0x140000000.
  * Cold import plus auto-analysis took about 13 minutes (14:25–14:39). After that, every query took under a few seconds.
  * About 60 tool calls were made. Raw outputs are in the session scratchpad (`d2/res_*.txt`, `d2/dc/*.c`).
  * The session was shut down at the end and no Java process remains.

Evidence tags:

* **[V]** verified in decompiled code or bytes, with the RVA given.
* **[M]** verified in mod source or mod run evidence, with file:line given.
* **[I]** inferred.
* **[U]** unknown; it must be checked at runtime.

All addresses below are RVAs (subtract 0x140000000 from a VA).

---

## TL;DR

* Approach (A), query-clamped hands with fake pushes, **already exists in the mod**:
  * a chest-to-palm sphere-sweep clamp of the drawn hand (`WeaponCollision`);
  * 21-sphere "solid hands" that push loose props by writing velocity;
  * a 24-sphere overlap/MTD full-hand response that is off by default.

  None of it has passed headset acceptance, and the offline push lab still fails its own wall check: the drawn hand ends
  22.4 cm past the wall plane.
* Static RE shows that (B), real Havok hand bodies, is **feasible through engine-native APIs**:
  * **[V]** `hknpWorld::createBody` 0xc8e5b0;
  * **[V]** `destroyBodies` 0xc8f6b0;
  * **[V]** `setBodyMotionType` 0xc86800;
  * **[V]** `applyHardKeyFrame` 0xc86420;
  * **[V]** `setBodyTransform` 0xc85860;
  * **[V]** a per-body layer plus collide-with mask in the world filter, which can exclude the player;
  * **[V]** a thread-safe engine command queue (`arkThreadSafeExpandableQueue`) that defers hknp API calls while the world
    steps. The engine itself uses this queue for velocity, filter and activation changes.
* The blocking unknown for (B) is the **body userdata contract**. The world collision filter dereferences `body+0x90` (and
  its `+8`) for every candidate pair (0x93a40). The contact-impulse handler (0x8527e0) can call through `userdata+8`'s vtable.
  A mod-created body therefore needs engine-shaped userdata, or the engine has to create the body for us.
* **Recommendation: (C), reached in phases.**
  * Phase 1 (felt in the headset in days): tighten the existing query clamp into a proper "visual hand stops at surfaces"
    with an IK feed and snap-back, plus mass-aware pushes. No new engine RE is needed.
  * Phase 2: an isolated spike that creates one **dynamic** hand body, driven by a Bonelab-style force-capped velocity servo
    through the engine command queue, with the player layers masked out.
  * Phase 3: the drawn hand follows that body, and the query clamp stays only as an anti-tunnel guard.
* Carry rendering: the strongest new candidate comes from static evidence.
  * A transient `NativeHeldObject::clear()` restores the held body's RIGID_BODY layer.
  * The DEBRIS swap is only re-applied when the render scale is non-zero, and after the first frame it is 0.
  * So after any publication hiccup, a whale-oil tank can stay on the RIGID_BODY layer for the rest of the hold and collide
    with the player.
  * Details are in Q6.

---

## Q1. What the mod has today

| Piece | Where | What it does | Maturity |
|---|---|---|---|
| Synchronous shape cast | `src/native_world_trace.hpp:187-358`; `cast_rva` 0xc89bc0 (`:192`); sphere ctor 0xca6550 | Sphere sweep from→to with a closest-hit collector: position +0x30, normal +0x40, fraction +0x50, body handle +0x78, material +0x7c. There is an optional source-body exclusion (request +0x68/+0x14) and initial-overlap plus regular collectors. | `tests/native_world_trace_check.cpp`, copied-native fixtures (`docs/native-world-trace.md`). Runs live on the game thread in the interaction-sensor Stage0 scope (`native_interaction_aim.hpp:458-474`). Results are **one frame late by design** (posted by the render thread, consumed next frame). Hit correctness, cost and thread safety are listed as unverified (`docs/native-world-trace.md` §"Not verified" 1-7). |
| Visual clamp of the hand | `src/weapon_collision.hpp:60-115`, applied in `src/dxgi_probe.cpp:4101-4102` (`if(r.contact && !meeting.prop) g.position=r.grip; g.position+=meeting.shift`) | Chest-anchored sphere sweep to the grip. It stops one `palm_radius` (6 cm) off the first surface, eases out at 1.5 m/s and clamps at 45 cm. Raw grips are kept separately (`raw_grips`, `dxgi_probe.cpp:4041`). | `tests/weapon_collision_check.cpp`. Headset: PLAYING.md claims "the sword, pistol and crossbow stop at walls". The v530 push lab `wall_cast` **fails**: drawn hand 22.4 cm past the wall plane, against a 1 cm limit (`docs/HANDOFF-2026-10-14-v530.md:63,82`). |
| Solid hands (pushes) | `src/hand_contact.hpp` (math, 21 sphere proxies, `HandContactConfig` strength 2 kg / heavy 12 kg / 4 m/s), `src/native_hand_contact.hpp` (engine side), called from the 0x89fb0 dispatcher hook `native_grab.hpp:433-449` | Raises a loose prop's velocity along the push direction with 0xc85dc0. A `PropDrag` learner detects blocked props. The inverse mass is read from motion +0x26 (u16, `native_hand_contact.hpp:41-52`). | Sim costs: about 75 µs per service, 0.5 ms peaks (`native_hand_contact.hpp:11`). Push lab 182/187 checks. **No headset acceptance.** |
| Full-hand 24-sphere query response | `src/native_full_hand_contact.hpp`, `src/hand_palette_contact.hpp`, `docs/FULL-HAND-COLLISION.md` | Overlap probes, an 8 cm limited MTD, an arm-only re-solve and a re-query. The production writer is **off by default**. Budget is 3 ms with 48 queries per side. | Extensive offline/ABI fixtures (`full_hand_*_check`, `native_full_hand_contact_check`). No live or headset acceptance. |
| Hand-volume Use acquisition | `src/native_hand_volume.hpp` (0xca6550 sphere, 0x95370 overlap queue) | 0.45 m sphere query routed to native Use. | Offline checks; "exact in-game success not yet observed" (`docs/native-hand-volume-20261010.md`). |
| Velocity motor for grabs | `src/physical_grab.hpp:122-132` (`error*18+feed`, clamp 10 m/s; spin×12, clamp 5 rad/s), lanes in `native_grab.hpp` | Drives a dynamic prop body with 0xc85dc0 in the 0x89fb0 dispatcher callback. | Strongest live evidence. v56 sim: 139 motor samples, 23 cm lift, <1.9 cm target error. v80 in the real game: 132 motor samples, zero reach clamps (README.md:201-235). Mass-blind. |
| Body spawning | `src/native_weapon_hands.hpp:9-13,485-531` (0x8ae9b0 SpawnEntityDefAt, 0x87d1e0 Teleport, 0x883f60 remove) | Spawned entities' **components never start, and Havok never moves them** (sim v407/v413, `native_weapon_hands.hpp:11-13`). The thrown sword is flown with Teleport. | This path cannot produce a live physics hand body. |
| Keyframed bind of a held body | `src/native_held_object.hpp:275-372` (bind 0x7506d0 post-hook) | The engine keyframes the HoldObject body toward a target through velocities, and the mod substitutes the hand target. | Live since v376. The tank-carry stability was headset-rejected (v559-v562). |
| Arm IK to an arbitrary target | `src/full_body_driver.hpp:504-506` (`hand_target = grip × grip_to_attachment`), `:542-546` (separate `raw_hand`), `src/body_ik.hpp:870-958` (wrist target, clavicle shrug, Boneworks elbow pole, soft reach with ≤6% stretch), `src/arm_ik.hpp` (2-bone analytic), `src/hand_bind.hpp:15-43` (palm frame), `src/finger_ik.hpp` (contact wrap), `src/grip_anchor.hpp`/`grip_session.hpp` (force- and mass-limited wrench math, offline) | The driver already accepts any target for `in.hand[i]` and keeps a separate `raw_hand`. | `arm_ik_check`, `body_ik_check`, `hand_bind_check`, `finger_ik_check`, `grip_ik_pipeline_check`, `drawn_palm_residual_check`. Headset: tracked arms are opt-in and "deformation and physical grip alignment have not passed headset acceptance" (README.md:244-246). |

**Conclusion.** Every ingredient of approach A exists, and the velocity motor is the most mature engine write. Nothing in the
mod has ever created a Havok body (`hand_contact.hpp:5-6`: "creating a keyframed body is untested").

---

## Q2. Havok (hknp) in this engine

### 2.1 Body creation and destruction [V]

* `hknpWorld::createBody` is **0xc8e5b0** `(world, uint* outId, const hknpBodyCinfo*, addFlags, activation)`.
  * It reserves an id (0xc8e560 → body manager allocator 0xcae7f0, which raises the "Body buffer is full." error from
    `hknpBodyManager.cpp`).
  * If `cinfo+0x88 == 0x7fffffff` and `cinfo+0x28 != 0`, it creates a motion from the cinfo (0xc908f0). Otherwise it uses
    the given motion id.
  * It then calls 0xc92be0 to add the body.
* The attached variant is 0xc8e7c0, which shares another body's motion.
* There are 22 engine callers. Among them:
  * arkPhysicsPlayer 0x1b6ec0, which creates a second player body that shares the proxy's motion;
  * debris and particle bodies (0xcca020, 0xccb1f0);
  * rigid-body components (0x737300, 0x73b420/5b0/740);
  * ark world helpers 0x852560 and 0x808ea0.
* `hknpBodyCinfo` default ctor is 0xca65f0. Fields [V] by the ctor and by 0x1b6ec0's use:

  | Offset | Field |
  |---|---|
  | +0x00 | shape* |
  | +0x10 | collision filter info |
  | +0x14 | u16 material |
  | +0x20 | userdata, set by 0x18cca0; it becomes `body+0x90` via 0x18cc90 |
  | +0x28 | create-motion flag |
  | +0x30 | position |
  | +0x40 | orientation quat (default identity) |
  | +0x70 | float −1 |
  | +0x80 | u16 0xffff |
  | +0x84 | reserved id 0xffffff |
  | +0x88 | motion id 0x7fffffff |

* `addBodies` is 0xc8e930 (timer string "LtAddBodies"), `destroyBodies` is 0xc8f6b0 ("LtDestroyBodies", which frees the id
  via 0xcaea70), and `removeBodies` is 0xc8f080 ("LtRemoveBodies").
* Body handle layout [V] (0xcae7f0): `(generation+1)<<24 | index`, with the generation byte at record+0x73 and the full
  handle at +0x70. The stride is 0xb0 and the pool is at world+0x28. The mod's existing generation checks are therefore
  correct.

### 2.2 Motion types [V]

`setBodyMotionType` is 0xc86800 `(world, body, u8 type, u8 activation)`, dispatcher command 0x11. Its current-type
computation (lines 45-46 of the decompile):

```c
if ((*(uint *)(body + 0x44) & 1) == 0) {            // not static
  cur = 2 - (uint)((*(uint *)(body + 0x44) & 4) != 0);  // keyframed -> 1, dynamic -> 2
}                                                    // static -> 0
if (newType != cur) { ... }
```

* Motion types: 0 = static, 1 = keyframed, 2 = dynamic.
* Body flags +0x44: bit0 static, bit1 has a motion, bit2 keyframed, bit3 active.
* This matches the live guard observation: alive guard bodies are `&7==6` (keyframed), dead ones `&7==2`
  (`native_rigid_body.hpp:89-92`).

### 2.3 Driving a body toward a target [V]

* `setBodyVelocity` 0xc85dc0 was already known; it is dispatcher command 9.
* **`applyHardKeyFrame` is 0xc86420** `(world, body, const float pos[4], const float quat[4], float dt, activation)`,
  dispatcher command 0x32.
  * It computes the linear velocity as `(pos + R·com − motionCom)/dt` and the angular velocity from the quaternion delta
    divided by dt.
  * It then calls 0xc85dc0. It is a velocity keyframe, not a teleport.
* **`setBodyTransform` is 0xc85860** `(world, body, const hkTransform*, activation)`, command 6.
  * It skips the write if the change is within 1.19e-7.
  * Otherwise it writes the transform (0xca6a20), updates the motion (0xc88590) and fires the body-changed signal
    list at world+0x8c0.
  * Use it for snap-back.
* The engine's own velocity wrapper is **0x18d020**. It multiplies lin/ang by a per-motion-properties time factor
  (`*(world+0x990)+0x30 + propsId*0x40 + 0xc`) before calling 0xc85dc0 or queueing command 9.
  * The arkPhysicsWorld step (0x97bc0) rescales velocities through 0x18d2b0 when that factor changes. This is Dishonored's
    time dilation (Bend Time).
  * **The mod's raw 0xc85dc0 writes ignore this factor** [V for the engine, I for the consequence]. Hands and pushes would
    run at real-time speed inside slowed time.

### 2.4 Collision filter, layers and excluding the player [V]

* Layers come from the enum init near 0x19f0c60, which assigns 0,1,2… sequentially; the first 12 values were verified in the
  bytes:

  | Value | Layer | Value | Layer | Value | Layer |
  |---|---|---|---|---|---|
  | 0 | WORLD_LOW | 9 | NPC | 18 | PLAYER_PHYSICS |
  | 1 | WORLD_LOW_HIGH | 10 | NPC_BODY_ANIMATED | 19 | DECAL_QUERY |
  | 2 | WORLD_HIGH | 11 | PROJECTILE | 20 | WEAPON_MELEE |
  | 3 | RIGID_BODY | 12 | CLOTH | 21 | CHAIN |
  | 4 | DEBRIS | 13 | CLOTH_COLLIDER | 22 | TRIGGER_WATER |
  | 5 | MOVER | 14 | NPC_BODY_RAGDOLL | 23 | PICKUP |
  | 6 | PLAYER | 15 | INTERACTABLE | 24 | 24 |
  | 7 | PLAYER_HITBOX | 16 | TRIGGER | 25 | GHOST |
  | 8 | PLAYER_DEAD | 17 | VISION | 26 | HIDDEN |

  The values from 12 on are [I], continuing the verified sequence.
* The world filter is **arkCollisionFilter**: vtable 0x1ad4c08, COL 0x1f72508, wrapped by `hkfxCombinedFilter<arkCollisionFilter>`.
  Its body-pair predicate is 0x992c0:

  ```c
  A = *(uint*)(bodyA + 0x6c);  B = *(uint*)(bodyB + 0x6c);
  if (((B >> 5) >> (A & 0x1f) & 1) && ((A >> 5) >> (B & 0x1f) & 1) &&
      FUN_140093a40(filter, bodyA, bodyB) == 0)   // explicit disabled pair / same-owner self-collision
      keep pair;
  ```

  * Each body's filter word is `layer (bits 0-4) | collidesWith mask (bits 5-31)`, and **both directions must agree**.
  * The query predicate 0x99610 uses the same mutual test against a query's filter info. This is the same family as the
    mod's 0x996d0.
  * **So a hand body whose collidesWith mask omits PLAYER(6), PLAYER_HITBOX(7), PLAYER_DEAD(8) and PLAYER_PHYSICS(18) can
    never collide with, or be hit by casts from, the player's own bodies.** Those casts include the character proxy's, if
    they use this filter [I].
* There is also a pair-disable hash (filter+0x20, mask +0x2c, keyed on full body ids, 0x93b00) and an owner self-collision
  matrix (0x93a40). The routine that adds disabled pairs was not found [U]. It would let a hand ignore the object it holds.
* An `hknpGroupCollisionFilter<5,5,5,16>` with a 32×32 layer matrix (0x8edb0/0x8f170) also exists. Which filter the world
  actually uses is [I]: arkCollisionFilter, because the mod's per-body `+0x6c` masks behave as this predicate predicts.
* Setting a body's filter: the engine wrapper is 0x97600 `(game+0x60280, handle, filter, rebuild, busy)`.

  ```c
  if (busy) { cmd.size=0x20; cmd.type=0x1a; cmd.body=handle; cmd.filter=filter; push(arkPhys+0x148288, &cmd); return; }
  hknpWorld_setBodyCollisionFilterInfo /*0xc87640*/(*(arkPhys+0x190), handle, filter, rebuild==0);
  ```

  The layers the mod's world cast uses (`kWorldOccluderLayers` 0xc43e) are 1,2,3,4,5,10,14,15. That set excludes PLAYER and
  includes DEBRIS, so a held DEBRIS prop is hit [V by the mask arithmetic].

### 2.5 Contact events [V]

* At init (0x852400), arkPhysicsWorldLocal subscribes to the world event signals through `0xc92220(world, type)`, which
  reads `world+0x9e8`, and `0x851550(signal, obj, fn, name)`:
  * type 2 → `OnContactImpulse` 0x8527e0;
  * type 3 → `OnContactImpulseClipped` 0x852cb0;
  * type 1 → `OnManifold` 0x852e20;
  * type 10 → `OnExitedBroadPhase` 0x8527a0.
* `OnContactImpulse` reads `event+8` and `+0xc` (body A/B) and `+0x18` (state 1 = started).
  * For each body with flag bit7 (`+0x44 >> 7 & 1`), it calls `userdata(+0x90)->(+8)->vtbl[0x98/8](…)`.
  * When the two bodies' flag bit19 differ, it calls `userdata(+0x90)->(+8)->vtbl[0]()` on one of them.
  * It then emits impact FX (0x60a480) and possibly an AI-relevant event (0x80e910 on game+0x3ea5d) [I: noise].
* Post-step, 0x94700 walks each body's userdata contact list (`+0x60`, count `+0x68`) and calls `owner->vtbl[1](owner, body,
  started)`. These are begin/end touch notifications for engine-owned bodies.
* For a mod hand body the simplest contact signal needs no events: compare the velocity we commanded with the velocity the
  solver produced on the next tick (Bonelab's "empty and colliding" mass estimate, §1.4). Hooking 0x8527e0/0x852e20 is the
  fallback if events are needed.

### 2.6 Physics step, threads and where the mod can run [V/I]

* The arkPhysicsWorld object is `game+0x60280`, and its world pointer is at +0x190. That equals `game+0x60410`, the mod's
  `world_offset`.
* The step task is **0x97bc0** (timer strings "TtarkPhysicsWorldStep"/"TtarkPhysicsStep"). It:
  * computes dt and a 120 Hz tick count;
  * rescales time-dilated velocities;
  * runs the hknp step through the job graph (0xc8e0a0, 0xaab400);
  * then runs post-step notifications (0x94700).
* "Physics busy" is 0x8a7820 `(game)`: true if `game+0x337121`, `game+0x337122`, or `*(game+0x1e8960)+0xc34` is set.
* **The engine's mutation rule** [V], seen in 0x97600, 0x807bc0, 0x18d020 and 0x18d2b0: if busy, push an hknp API command
  record into the thread-safe queue at `arkPhys+0x148288` (0x90fb0 → 0x89ea0 → 0xfa6a0/0xfa4f0
  `idlib\parallelism\arkThreadSafeExpandableQueue.cpp`). Otherwise call the hknp API directly.
* The queue is drained by **0x89fb0**, the dispatcher the mod already hooks. Its switch matches the debug printer 0x8a730
  exactly:

  | Command | Function | Command | Function |
  |---|---|---|---|
  | 1 | addBodies | 0x11 | setBodyMotionType |
  | 2 | destroyBodies | 0x13 | setBodyMass |
  | 3 | removeBodies | 0x1a | setBodyCollisionFilterInfo |
  | 6 | setBodyTransform | 0x1e | setBodyActivationState |
  | 9 | setBodyVelocity | 0x32 | applyHardKeyFrame |

* Record header [V] (from 0x18d020/0x97600): u16 size at +0, u16 0x0200 at +2, u16 type at +4, body id at +8, payload from
  +0x10.
  * The velocity record is `{0x30, 0x0200, 9, id, lin[4] @+0x10, ang[4] @+0x20}`.
  * The hard keyframe record is `{…, 0x32, id, pos @+0x10, quat @+0x20, dt @+0x30}`.
* **Implication:** the mod can submit hand-body commands from any thread with engine-native timing, without guessing a safe
  phase. Two things are [U]:
  * which thread drains the queue, and when relative to the step;
  * whether a drain happens every frame. The mod's hook only runs when commands exist, so a self-queued per-frame
    command would guarantee a tick.
* The turning step **0x1b4760** is the arkPhysicsPlayer character-proxy integrate (proxy casts 0x852d0/0x85a40), not the
  hknp solver step. It is a good point to read the player root after locomotion, but not a place to mutate bodies.

---

## Q3. Shape casts per frame for both hands

* **API fit [V/M].** 0xc89bc0 takes any hknp shape pointer; dispatch uses shape type byte +0x22. The mod builds a sphere
  (0xca6550) and gets the hit position, normal, fraction, body and material.
  * `hknpCapsuleShape` and `hknpBoxShape` exist (RTTI 0x23ecb48 and 0x23ecb20), but their ctor RVAs were not located [U].
  * Two or three sphere sweeps (palm, knuckles, fingertips) approximate a capsule.
* **Thread and reentrancy [V/M].** The call is synchronous on the calling thread. It uses the TLS frame allocator (Q5 of
  rea-evidence), and the engine calls it from 26 sites, including per-frame AI line of sight.
  * The mod runs it only inside the game-thread interaction-sensor Stage0 scope, the same scope as native 0x3b7b90
    (`native_interaction_aim.hpp:458-474`).
  * The closest-hit collector is native and stack-owned, so there is no callback into the mod.
  * It is not reentrant into the mod.
  * Whether the physics step can overlap Stage0 is [U]. To check it, log `0x8a7820(game)` at cast time.
* **Cost [U].** Nothing was measured locally. The status JSON exposes `cast_us`, the mean µs per cast
  (`dxgi_probe.cpp` status block, `native_interaction_aim.hpp:470`).
  * The existing full-hand service budgets 3 ms for at most 48 queries per side.
  * [I] A sphere sweep of 1 m or less against hknp's broadphase is typically tens of µs, so 2–6 sweeps per hand per frame
    should stay well under 0.5 ms. Measure before relying on it.
* **Latency [M].** Results arrive one frame late, because the render thread posts and Stage0 consumes. A clamp that
  sweeps from the *previous clamped hand* rather than from the chest stops the drawn hand at the surface within one frame
  at 90 Hz. That is about 2.2 cm at 2 m/s, so the margin should absorb it.
* **Verdict.** Per-frame use for both hands is fine, with a sphere (or 2–3 spheres) of hand size (4–6 cm). Keep it on the
  Stage0 scope.

---

## Q4. Approaches

Bonelab force law, adapted to D2. This is a velocity servo, because hknp exposes velocity writes and not forces
(Bonelab §2.1). For each servo tick, with dt the time since the last write:

```
target   = controller palm pose (world), reach-clamped to the arm length (Bonelab UpdateArmTargets maxLength)
v_des    = (x_target − x_hand)/dt_reach + v_target_ff     (dt_reach ≈ 1–2 physics steps; feed-forward = controller velocity)
Δv       = v_des − v_hand (read back from motion +0x40)
Δv_cap   = F_max(dir)·dt / m_eff        (muscle-space asymmetric: along forearm ±320–450 N, lateral ±90–112 N; m_eff = hand + held)
Δv       = brake_blend(clampPerAxis(Δv, Δv_cap))      (stopping-distance test: v²/2a vs remaining distance, factor −1.2)
v_write  = (v_hand + Δv) · timeFactor(motionProps)     (match 0x18d020 so Bend Time is respected)
ω similarly: axis-angle(q_target·q_hand⁻¹)/dt, stiffness drops above ~30° error, torque cap ~25–30 N·m / I
no reaction force on the player (user rule; Bonelab's chest reaction is dropped)
snap-back: |x_target − x_hand| > 0.35 m for > 0.25 s, or any non-finite → setBodyTransform(cmd 6) to target after an overlap check; zero v
```

The solver then handles everything Bonelab gets from its joints:

* a wall stops the hand (contact);
* a light prop moves (mass ratio);
* a heavy or fixed object holds the hand back;
* a guard's keyframed bodies (infinite mass) stop it.

### (A) Query-based: shape-cast clamp, IK to the clamped hand, fake pushes by setVelocity

* **Feel versus Bonelab.** About 60% of the way. The hand stops at walls and props (visual only) and pushes light props.
* **Feel limits.**
  * There is no true resistance from heavy things; it is a heuristic, `PropDrag`, plus a mass cap.
  * There is no friction or sliding along surfaces beyond the clamp.
  * Pushes are kinematic (the velocity is raised and never lowered).
  * The hand cannot hold a door shut or brace on anything.
* **Risks.**
  * AI: low. Only engine-visible prop motion, as today.
  * Explosions: low, because writes are capped.
  * Player proxy: none.
  * Save/load and streaming: none, since there is no owned state.
  * Multithreading: already handled.
  * Frame time: casts on the game thread, plus 75 µs to 0.5 ms for the push service.
  * Known weakness: tunnelling and one-frame-late casts. The push lab's `wall_cast` check fails today.
* **RE still unknown.**
  * Little remains: cast cost and Stage0-versus-step overlap.
  * The mass read at motion+0x26 is used without a documented scale.
* **Tests.**
  * Offline:
    * clamp policy (sweep from the last clamped pose);
    * snap-back hysteresis;
    * IK-feed composition (grip_ik_pipeline style);
    * push math with mass and time factor;
    * the existing push lab with `wall_cast` fixed.
  * Headset: wall and table feel, corner sliding, fast swats.
* **Size.** Small to medium: about 300–600 changed lines, mostly in `weapon_collision.hpp`, `dxgi_probe.cpp` (hand
  publication) and `hand_contact.hpp`.

### (B) Real Havok hand bodies driven by the force law

* **Feel versus Bonelab.** About 85–90%.
  * It gives true contact, resistance, sliding and stacking, door bracing, and ragdoll interaction.
  * Later grabbing can use constraints (`hknpWorld::createConstraint` 0xc90de0 exists [V]) or the existing velocity
    motor.
  * Missing: Bonelab's body reaction and climbing, which are excluded by the user rule anyway.
* **Risks.**
  * **Userdata contract, high.**
    * 0x93a40 dereferences `body+0x90` of both bodies and compares `userdata[1]`. A null userdata crashes the narrowphase.
    * 0x8527e0 may call `userdata->+8->vtbl[0]`.
    * Fix: a fake userdata with a unique `+8` and a benign vtable, or have the engine create the body.
  * **Player proxy.** Mask out layers 6, 7, 8 and 18 [V mechanism]. The actual proxy filter words are [U]; the mod already
    reads `body_filter_info` in `native_world_query_source.hpp`.
  * **AI reactions.** Hand-to-prop impacts go through the native impact FX and noise path, as props already do. Contact
    with guards' animated bodies could raise native perception events [U].
  * **Physics explosions.** If the body starts inside geometry, depenetration fires the hand outward, and capped Δv limits
    the damage. Snap-back must overlap-check its target.
  * **Save/load and streaming.** A mod body is not serialized. It must be destroyed before the world goes away and created
    again after.
    * The world pointer changes, and a stale handle crashes 0xc85dc0 (rea-evidence Q1).
    * Watch the world identity, the game RTTI and the load state.
  * **Multithreading.** Use the engine command queue, which is thread-safe [V], or the direct API only when 0x8a7820 is
    false.
  * **Frame time.** One or two extra bodies is negligible for hknp [I].
  * **Time dilation.** Scale by the motion-properties factor.
* **RE still unknown.**
  * the userdata or owner contract (vtables and fields an owner needs);
  * a shape ctor for a box or capsule;
  * motion-properties ids (gravity factor 0);
  * the drain thread and timing;
  * the disabled-pair add routine;
  * what flag bits 7 and 19 mean;
  * whether `createBody` add flag 2 defers the add.
* **Tests.**
  * Offline:
    * servo-law unit tests (step response, caps, braking, snap-back, NaN);
    * command-record byte encoders;
    * a reimplementation of the 0x992c0/0x99610 predicate, checking that the hand mask excludes the player and includes
      the world layers;
    * a lifecycle state machine for world change, load and focus loss.
  * Copied-native fixtures for the filter functions, like the existing copied-native query fixtures.
  * Runtime only: creation, stability, solver feel, AI, save/load and streaming.
* **Size.** Large: 1,500–3,000 lines, plus 1–2 RE spikes.

### (C) Hybrid: physical hand body for pushing, query clamp as the visual guard

* **Feel.**
  * Recommended variant: a dynamic body with the servo above, plus the query clamp as an anti-tunnel guard. This feels
    like (B).
  * HIGGS variant: a keyframed body plus the query clamp for the visual.
    * It pushes everything with infinite mass and is never held back by heavy things.
    * Moving it with `applyHardKeyFrame` 0xc86420 is trivial.
* **Risks.** The same as (B). In the dynamic variant the clamp also bounds visual error when the body is stuck or
  separated.
* **RE.** The same as (B).
* **Tests.**
  * Offline: (B)'s tests plus selection of the visual source (body pose, clamped pose, or controller) and the transition
    rules.
* **Size.** (B) plus about 300 lines.

### Comparison

| | (A) query clamp + fake pushes | (B) dynamic hknp hand body + force law | (C) hybrid (B body + A visual guard) |
|---|---|---|---|
| Stops at walls/tables | visual only; one frame late; tunnels at speed | yes (solver) | yes, with guard |
| Pushes loose props | heuristic velocity raise | yes, mass-correct | yes |
| Held back by heavy/fixed | heuristic (`PropDrag`, 12 kg cutoff) | yes | yes |
| Guards (keyframed bodies) | clamp only | yes (infinite-mass contact) | yes |
| Player never pushed | yes | yes, via collidesWith mask (needs runtime proof) | same |
| Basis for grabbing/doors later | weak (motor only) | strong (constraints, contact frames) | strong |
| Engine RE still unknown | low | medium-high (userdata, shape ctor, drain timing) | same as B |
| Crash risk | low | medium (userdata, stale handle on unload) | medium |
| Offline-testable share | high | medium (law, encoders, filter math, lifecycle) | medium |
| Size | S–M | L | L+ |
| Feel versus Bonelab | ~60% | ~85–90% | ~90% |

---

## Recommended approach

Target (C), using a dynamic hand body and the force-capped velocity servo, reached in four phases. Each phase can ship on
its own.

1. **Phase 1, the smallest thing the user can feel (no new engine RE, about 1–2 days).** "The hand stops at surfaces."
   * Change the chest-anchored clamp so it sweeps from the *previous drawn hand* to the current controller palm, with the
     palm sphere (4–5 cm) plus a fingertip sphere. That fixes the push lab's `wall_cast` failure mode.
   * Hysteresis and snap-back:
     * when the gap exceeds 0.35 m for 0.25 s, release the clamp and re-acquire through a sweep from the chest;
     * never render inside a surface;
     * keep the existing 1.5 m/s ease-out.
   * Feed the clamped pose to the arm driver as `in.hand[i]` and keep `raw_hand` raw. The seam already exists:
     `full_body_driver.hpp:542-546`, `dxgi_probe.cpp:4041,4101-4102`.
   * Clamp the target to the arm's reach *before* the clamp, so the drawn hand and the IK agree.
   * Make solid-hand pushes mass-aware (`Δv ≤ F·dt/m`) and time-scaled (0x18d020's factor). Give the user a haptic pulse
     on first contact (already wired in `WeaponCollision`).
   * Headset check: press a palm onto a table and a wall; drag along a wall; swat a bottle; push a crate (it should
     resist); touch a guard.
2. **Phase 2, physical-body spike (RE plus an isolated harness; the user sees nothing yet).**
   * Read the player bodies' `+0x6c` words and a prop's userdata live, read-only.
   * Build one hand body:
     * engine-shaped userdata (unique `+8`, benign vtable), or adopt the engine's creation path;
     * sphere shape 0xca6550 at first;
     * filter word = a chosen layer (start with WEAPON_MELEE 20 or RIGID_BODY 3) plus a collidesWith mask of layers
       {0..5, 10, 14, 15} that excludes {6, 7, 8, 18, 23};
     * dynamic motion, gravity compensated.
   * Create it with 0xc8e5b0 while not busy, or with command 1 after creation.
   * Drive it with the servo through command-9 records. Destroy it with command 2 on world change, load or exit.
   * Prove four things: no crash for 10 minutes near props, NPCs and doors; the player is never pushed; save/load is clean;
     the frame-time delta is under 0.2 ms.
3. **Phase 3, the visible hand follows the body (C).**
   * The drawn hand comes from the body pose read back on the next frame and predicted to the display time.
   * The query clamp stays only as a tunnelling guard: if the body is behind a surface relative to the camera, or far
     from the controller, snap back.
   * Fingers: feed body contacts or cast hits into `finger_ik` as contact shapes.
4. **Phase 4, grabbing on these hands.**
   * Props: keep the existing velocity motor, but target the hand body.
   * Ragdolls: a constraint (0xc90de0) or the existing limb motor.
   * Doors: the MOVER layer (5) is already collidable.

## Open RE items that need runtime verification

1. The userdata or owner contract for a mod body: the minimum fields and vtable slots that 0x93a40 (`[+8]`, `vtbl+0x20/+0x28`),
   0x8527e0 (`vtbl[0]`, `vtbl+0x98`) and 0x94700 (`+0x60/+0x68`, `vtbl+8`) touch. Alternatively, find an engine path that
   creates a component-owned body at runtime. 0x8ae9b0 spawns do not start components (sim v407).
2. The real filter words of the player's bodies: the proxy body created at 0x1b6ec0 (`param_1[0x86]`), the
   `NativeWorldQuerySource` body and the query filter. Then confirm the proxy's casts use arkCollisionFilter.
3. Which filter the world installs at world+0x470: arkCollisionFilter, the hkfx combination, or the group filter.
4. The queue drain: its thread, its timing relative to 0x97bc0, and whether 0x89fb0 runs every frame without mod-queued
   commands.
5. Whether the game-thread Stage0 cast overlaps the physics step: log 0x8a7820 at cast time.
6. 0xc89bc0 cost per sweep in the real level (`cast_us`).
7. The ctor and RVA of `hknpCapsuleShape`/`hknpBoxShape`, and the shape lifetime contract for a persistent body (the sphere
   storage marker 0x1ffff and refcount).
8. Motion-properties ids (gravity factor), the motion+0x26 inverse-mass encoding, and the time-factor table at
   `world+0x990`.
9. `createBody` add flag 2 and activation semantics; what body flags bit7 and bit19 mean.
10. AI reaction to hand contacts with live guards' NPC_BODY_ANIMATED (10) bodies.
11. The routine that inserts disabled pairs into the filter+0x20 hash (hand versus held object).
12. Carry: whether 0x753c10's native rebind block (below) runs during HoldObject, and the render-scale behaviour in stereo.

## Offline tests that can pin the logic

* `hand_servo_check`:
  * the force law: step response with no overshoot under the braking blend;
  * asymmetric per-axis caps;
  * a heavy target produces lag and no runaway;
  * time-factor scaling;
  * NaN, Inf and huge-target rejection;
  * snap-back hysteresis (enter and exit thresholds, timer);
  * the reach clamp.
* `hand_command_encoding_check`: byte layout of commands 1/2/6/9/0x11/0x1a/0x32 against the decompiled readers
  (size, type, id, payload offsets).
* `hand_filter_mask_check`: reimplement 0x992c0/0x99610 and assert:
  * the hand collides with layers 0–5, 10, 14 and 15;
  * it never collides with 6, 7, 8 or 18, in either direction;
  * DEBRIS-layer held objects are excluded when requested.
* `hand_body_lifecycle_check`: the create, drive, destroy state machine across world-pointer change, load screen, focus
  loss, tracking loss and DLL stop. Stale handles and generation mismatches must never write.
* `hand_clamp_check` (Phase 1):
  * sweep from the previous pose;
  * corners and grazing angles;
  * fast approach under a one-frame-late result;
  * a deep starting overlap triggers snap-back;
  * an extended push-lab `wall_cast` scenario that must pass.
* `hand_visual_source_check`: choose between body, clamp and controller, and blend the transitions.
* `grip_ik_pipeline_check`, extended so the clamped or physical hand is fed to `fullbody::Driver` and the drawn palm stays
  within 1 cm of the commanded pose, inside reach.

---

## Q5. The visible hand and arm

**Path today [M].**

1. The OpenXR grip pose is turned into a game pose (`dxgi_probe.cpp:4035-4040`, `to_game_pose(camera_relative_controller(...))`).
   No controller-specific offset is applied.
2. The world-collision clamp shifts it (`:4101-4102`).
3. `hand_target = grip × grip_to_attachment` (`full_body_driver.hpp:504-506`). Here `grip_to_attachment = inverse(bindPalm) ×
   bind(hand_attachment_jnt)` (`:497-499`).
4. `body_ik.hpp:884` computes `wrist_target = hand × inverse(bind_attach)`. A clavicle shrug, a Boneworks elbow pole, and
   soft reach with at most 6% stretch follow; the wrist stops short beyond that (`:910-924`).
5. The native palette is uploaded.

**To make the arm follow the physical hand,** replace `track_.grip[i]` (the `in.hand[i]` source) with the physical or
clamped hand pose, expressed in the same "grip" convention, and keep `raw_grip` for desired motion and grabs. The driver
already separates them (`full_body_driver.hpp:544-546`).

Reach-clamp the controller target to the arm length *before* it drives the body or clamp, as Bonelab does. Otherwise the
soft-reach IK stops the wrist short of the body. That gap is the "avatar 25–40 cm short of the controller" seen in
sim v379/v525 (`dxgi_probe.cpp:2563-2565,4397`).

**Where the reported hand-to-controller misalignment most likely comes from,** in order:

1. **The controller grip-pose offset (most likely, constant, about 3–5 cm plus 10–20°) [I, high].** The controller's grip
   origin is mapped straight onto `hand_bind_grip`'s origin. That origin is *the midpoint of wrist and mean MCP, inside the
   flesh* (`hand_bind.hpp:35-39`, "interior skeletal estimate … does not measure a controller-to-palm offset or wrist
   orientation").
   * OpenXR's grip origin lies inside the controller handle. The handle centre sits roughly one handle radius plus half the
     palm thickness off the skeletal mid-palm, along the palm normal.
   * The audit measures the bind origin about 2.2 cm under the palm flesh (`docs/native-palm-controller-alignment.md` table).
   * The bind frame comes from the *open* bind pose, not a closed grip around a handle, so the wrist and ulnar deviation of a
     real grip is missing.
   * The docs say plainly that no per-controller 6-DoF offset exists (`native-palm-controller-alignment.md` §"What
     calibration establishes").
2. **Arm-length calibration and soft reach (grows with extension) [M].** The wizard fits span, drop and shoulder proportions
   only. If the avatar arm is shorter than the user's, every extended reach loses up to 25–40 cm (v379, v525). The
   carry code even seats objects on that drawn palm (`dxgi_probe.cpp:2557-2561`).
3. **The wrist-to-palm offset in the bind (small, about 2 cm) [M].** The mid-palm skeletal origin versus the surface palm
   patch (0.022 native units).

**Fix direction.** Add a per-side, per-interaction-profile grip→palm offset calibration (translation along the palm normal
and the handle axis, plus a pitch/yaw tilt). Use an explicit "hold the controller like this, confirm" step, as the docs
propose. Report reach-clamp frames in the status, so the user can tell a calibration problem from an offset problem.

---

## Q6. Rendering breaking while carrying: candidates (none fixed)

Context [M]:

* The user rejected tank stability on v559–v562: "rapidly switching tank placement", "tank moves relative to the hand",
  and unstable while standing still too (`docs/HEADSET-REPAIR-v559-PLAN.md:5`, `docs/VR-v562-TEST-RESULTS-2026-10-16.md:62-64`).
* Commit 7e81166 re-bases the published target on the master's motion between publication and the bind tick.

Ranked by plausibility:

1. **The layer swap is lost after any transient clear, so the held body collides with the player (high, static evidence).**
   * The DEBRIS swap happens only inside `world_render()` (`native_held_object.hpp:153-162`).
   * `world_render()` is requested only when the entity's render scale `+0xc8` is non-zero at publication
     (`dxgi_probe.cpp:2906`).
   * Every `NativeHeldObject::clear()` sets `layer_restore_` (`native_held_object.hpp:89-90`), and the game thread restores
     the RIGID_BODY layer (`native_interaction_aim.hpp:550`). `clear()` runs on any publish failure (`dxgi_probe.cpp:2778`,
     `2899-2900`, `4399`).
   * After the first frame the render scale is already 0, so the swap is never re-applied while the hold continues.
   * The tank, a RIGID_BODY throwable, then collides with the player beside the capsule, which is exactly why 7baff29
     added the swap. The keyframed bind keeps pushing it into the player each step, which gives jitter or "moves relative
     to the hand", and possibly a jolt to the player or camera.
   * Verify by logging `layers_swapped`/`layers_restored` against hold frames.
2. **The native rebind block in 0x753c10 overwrites the mod's anchor (medium) [V code, U trigger].** In the per-entity
   physics-binding update 0x753c10, when `0x753a90(rf, dt)` returns true and `rf+0x408` bit 3 is clear, the engine
   recomputes the bind offset from the entity's *current* pose relative to the master:

   ```c
   if (local_res8 != 0) {           // set from 0x753a90(...) && !(rf+0x408 & 8)
     ... master pos/axis (or joint via 0x53d6a0) ...
     *(float*)(rf+0x3d4..0x3dc) = axisᵀ·(entity+0x300 − masterPos);        // offset
     *(quat*)(rf+0x3e0) = ToQuat(entityAxis × masterAxisᵀ);                  // rotation
   }
   ```

   This races with the mod's `anchor()` writes to the same fields (`native_held_object.hpp:348-372`). It would produce a
   switch between "where physics put it" and "where the hand wants it".
3. **Torn or racing anchor writes from engine task threads (medium).**
   * `bind_hook` runs on engine task threads (`native_held_object.hpp:5-7`).
   * `anchor()` writes `rf+0x3d0/0x3d4/0x3e0` with plain stores and `memcpy`.
   * 0x753c10 reads and writes the same fields on another thread.
   * A torn quaternion is non-unit. ToMat3 0x111410 does not normalize it (rea-evidence Q3), so the frame draws a sheared
     transform.
4. **The view-model render-scale window (medium) [M/I].**
   * HoldObject sets the entity's first-person scale `+0xc8`, so the object is drawn as a view-model pinned to the camera
     (`native_held_object.hpp:37-38`).
   * The mod clears it only later, on the game thread (`request_held_world` → `native_interaction_aim.hpp:549`).
   * Until then, and on any re-entry, the object is rendered through the view-model path. In the native side-by-side
     stereo with the replaced eye projections (`game_stereo.hpp:43-60`), that path may use a scalar-FOV or weapon projection
     that is wrong per eye.
   * The visible result would be a flash or doubled object at the eye.
5. **Stale drawn-palm residual (medium).**
   * `held_palm_frame` seats the object on `grip + residual`, where the residual is the arm solver's palm−grip offset from
     the *last* solve (`drawn_palm_residual.hpp:20-41`).
   * The residual is up to 500 ms old, covers position only, and drops to 0 when the fit or mapping revision changes.
   * When the arm toggles between reached and reach-clamped (palm 10–50 cm from the grip, v558), the residual jumps.
   * One stale frame offsets the object from the drawn hand by the residual change. Fully losing it shifts the object by up
     to 50 cm.
   * 7e81166 attributed the v558 "jumps" to a diagnostics pairing artifact (8704df5). That was not re-measured on the
     drawn image.
6. **Master re-base mismatch (low to medium).**
   * `carry()` (`native_held_object.hpp:333-346`) moves the target by the master's (player entity's) motion since
     publication. It reads `master+0x300/0x30c` non-atomically while the game thread may be writing them.
   * If the arm upload is carried by a different root (camera, crouch, lean, mantle or authored offsets), the object
     double-moves or under-moves relative to the hand.
7. **Temporal AA and upscaler history on a teleported object (unknown).**
   * The bind moves the drawn model by the target each step. Previous-transform and motion-vector consistency for such
     objects (and for view-model-flagged ones) is unknown.
   * Per-eye FSR or TAA history would smear or ghost it. The docs already defer this: "stable native model with unstable
     image requires … temporal capture".
8. **Keyframe velocity spikes (low).** `commit()` writes `lin=(target−current)/dt`, clamped to 25 m/s, and angular
   velocity clamped to 40 rad/s. With small dt and large target jumps (candidates 1, 2 and 5), the body slams into nearby
   props or the player. That causes physics noise and pushes.

Suggested first diagnostic (not a fix): log per frame:

* the layer word of the held body;
* the render scale;
* the `rf+0x3d4/0x3e0` values before and after `anchor()`;
* the 0x753c10 rebind branch (count);
* the residual delta.

Correlate those against a captured tank-visible interval.

---

## Key RVAs added by this study

| RVA | Meaning | Tag |
|---|---|---|
| 0xc8e5b0 / 0xc8e7c0 | hknpWorld::createBody / attached variant | V |
| 0xca65f0 | hknpBodyCinfo ctor | V |
| 0xcae7f0 | body id allocator (generation<<24 \| index) | V |
| 0xc8e930 / 0xc8f080 / 0xc8f6b0 | addBodies / removeBodies / destroyBodies | V |
| 0xc85860 | setBodyTransform | V |
| 0xc86420 | applyHardKeyFrame | V |
| 0xc86800 | setBodyMotionType (0 static, 1 keyframed, 2 dynamic) | V |
| 0xc87640 / 0x97600 | setBodyCollisionFilterInfo / ark wrapper (defers when busy) | V |
| 0xc90de0 | createConstraint | V |
| 0x89fb0 | hknp API command dispatcher (mapping from printer 0x8a730) | V |
| 0x90fb0 → 0x89ea0 → 0xfa4f0 | push to the thread-safe command queue at arkPhys+0x148288 | V |
| 0x8a7820 | physics-busy predicate | V |
| 0x97bc0 | arkPhysicsWorld step task | V |
| 0x94700 | post-step touch notifications | V |
| 0x992c0 / 0x99610 / 0x93a40 / 0x93b00 | arkCollisionFilter pair, query, owner and disabled-pair checks | V |
| 0x1ad4c08 | arkCollisionFilter vtable | V |
| 0x852400 / 0x8527e0 / 0x852e20 | event subscription / OnContactImpulse / OnManifold | V |
| 0xc92220 | world event signal getter (world+0x9e8) | V |
| 0x18d020 / 0x18d2b0 | ark setVelocity with time factor / time-dilation rescale | V |
| 0x753c10 / 0x7545a0 | per-entity physics-binding update (callers of bind 0x7506d0) | V |
| 0x1b6ec0 | arkPhysicsPlayer proxy body creation | V |
