# Research: Bonelab-style physical hands, weapons and bodies for Dishonored 2 VR

**Status:** research only, 2026-10-13. Nothing here is implemented.

**Requested features:**
- full-hand gripping;
- dropping the sword or pistol and picking it up again;
- throwing the sword or gun at guards;
- grabbing a guard by an arm, leg or collar and dragging them;
- physical weapons and a physical player body.

**Evidence labels:**
- **VERIFIED**: seen in source code or measured in this repo.
- **EXE**: a class or field name present in `Dishonored2.exe` (RTTI or reflection strings). The name exists; the behaviour is inferred.
- **INFERRED**: deduced, not checked.

## 1. How the reference games do it

### Boneworks / Bonelab (Stress Level Zero)

**The player.** The player is a physics rig: 6 rigid bodies in Boneworks, 11 in Bonelab, 18 in Marrow 2. It weighs 82 kg and follows the tracked controllers through joints ([Laatsch](https://x.com/BrandonJLa/status/2009741997897044046), [gamedeveloper.com](https://www.gamedeveloper.com/design/how-classical-game-mechanics-and-physics-converge-in-vr-hit-i-boneworks-i-)).

**Class structure**, read from the open-source LabFusion mod ([source](https://github.com/Lakatrazz/BONELAB-Fusion)):
- `PhysHand` is a jointed chain: upper arm → lower arm → hand rigid body.
- Grabbing runs `Grip` → `InteractableHost`. Each grip has authored `HandPose` assets. `ForcePullGrip` handles far pulls.
- Holsters are `InventorySlotReceiver` matched against weapon slot types.
- NPCs are RootMotion **PuppetMaster** ragdolls ([docs](http://root-motion.com/puppetmasterdox/html/page5.html)). Each joint is a "muscle" spring pulled toward the animation; pin weights hold it to world targets. A grab joins the hand to a limb, and the puppet goes limp when forces exceed its limits.

**Gaps:** the joint constants and the damage formula are not public. The [Marrow SDK](https://github.com/StressLevelZero/MarrowSDK/wiki) covers content authoring only.

### Blade & Sorcery ([official SDK docs](https://kospy.github.io/BasSDK/))

- **NPC bodies:** each body part is a rigid body plus a joint, pulled toward the animation by springs. States include Standing, Destabilized and Inert.
- **Grabbing limbs:** limbs are grabbable handles, and a grabbed part's mass changes while held.
- **Throwing:**
  - `Throwable` with a fly direction makes blades fly point-first.
  - `throwedMultiplier` raises damage for thrown items.
- **Damage:**
  - Each weapon has pierce, slash and blunt zones.
  - `velocityDamageCurve` maps impact force to damage, with minimum velocities and a per-collider hit cooldown.
  - `addForce` pushes the hit part and its neighbours.

### HIGGS and PLANCK for Skyrim VR: the closest analogue

These mods retrofit physical hands into a flat Havok engine ([HIGGS](https://github.com/adamhynek/higgs), [PLANCK](https://github.com/adamhynek/activeragdoll); GPL-3, hooks tied to one exe build).

**HIGGS: hands and the equipped weapon**
- Hands are the mod's own keyframed Havok boxes, driven to the tracked pose with velocity clamps. They push objects, and nothing pushes them back.
- The equipped weapon's collision is cloned into a keyframed body. Weapon collision is switched off for about 0.5 s after a hit.

**HIGGS: two grab modes**
- Light props are made keyframed and moved with the hand.
- Ragdolls, actors and jointed objects get a **force-capped 6-DOF motor constraint**:
  - position motors with tau 0.03 and damping 0.8;
  - linear max force 2000, angular force = linear / 12.5; actors use 2500/40;
  - force capped at mass × 500 and faded in over 0.1 s;
  - the held body's inertia clamped to at most 10:1.
- The visible hand snaps onto the held object. If it drifts more than 0.7 m from the real hand, the object drops.

**HIGGS: fingers**
- Each finger's curl stops where its fingertip curve first meets the object's collision.

**HIGGS: throwing**
- Release velocity is the peak of the controller-velocity history (averaged with its neighbours), plus ω×r, plus the player's own movement. The threshold is 1 m/s.
- Hand contacts are ignored for 0.1 s after release.

**HIGGS: two-handed grip**
- Align the palm-to-palm axis, split the position error between the hands, and roll halfway between them.

**HIGGS: dropping the equipped weapon**
- The native `Actor::DropObject` turns the equipped weapon into a world object, which is then grabbed.
- Picking it up again goes through the native `Activate` / `PickUpObject`.

**HIGGS: the collision filter is the central hook**
- Hands and held objects get their own collision layers.
- The player controller ignores held objects, and hands don't block the use/activation ray.

**PLANCK: live active ragdolls**
- NPC ragdolls stay simulated while the NPC is alive, but only near the player (with hysteresis).
- It hooks Havok's ragdoll drive-to-pose step: a keyframe-hierarchy controller plus powered motors (max force 500).
- Animation stays the target, physics decides the displayed pose, and the AI still moves the actor.
- "Stress" is the gap between animation and ragdoll. It triggers knockdowns.

**PLANCK: grabbing a live NPC**
- Either ragdolls them, or turns the grabbed bone's displacement into AI movement through movement hooks. Holding them costs stamina.

**PLANCK: hits go through the game's own functions**
- Melee hits call `Actor::HitTarget`. Thrown objects use `HitData_PopulateFromPhysicalHit` and `Actor::GetHit` with the game's mass/speed damage tables.
- The player counts as the attacker for 5 s after holding the object.
- There is a cooldown per (actor, body part).
- Animation-timed hits are suppressed so nothing lands twice.

### Other retrofits

- **Fallout 4 VR:** [ROCK](https://github.com/brunocatani/ROCK) is a HIGGS-style mod. Its equipped-weapon drop places the native world object at the hand, then reseats it in the hand.
- **Half-Life 2 VR:** physical melee and holstering; no gun dropping.
- **Dishonored 1 VR** ([repo](https://github.com/VR-Stereo-Hub/Dishonored-VR)): gestures trigger native attacks and the native throw. No physical grab or drop.

### Research on how to show the hand

- Spring-damper coupling of the virtual hand to the tracked hand: Borst & Indugula, IEEE VR 2005, DOI 10.1109/VR.2005.1492758.
- Penetrating "ghost" hands perform best, but people prefer hands that stop at surfaces: Prachyabrued & Borst 2014 (DOI 10.1109/3DUI.2014.6798835); Canales et al. 2019 (DOI 10.1145/3343036.3343132).

## 2. What Dishonored 2's engine already has (EXE)

`Dishonored2.exe` contains Havok Physics of the newer "hknp" generation (VERIFIED: the mod already calls hknp functions), plus Havok Cloth and Havok AI. The class and setting names below show the native machinery a physical-interaction layer can build on.

| Area | Names in the exe | What it suggests |
|---|---|---|
| Ragdolls | `hkaRagdollInstance`, `hkpRagdollConstraintData`, `hkpRagdollMotorConstraintAtom`, `arkRagdollResource`, `arkDeclActiveRagdoll`, `arkActiveRagdollRuleFilter`, `m_consciousActiveRagdoll`, `m_burningActiveRagdollDecl`, `m_electricActiveRagdollDecl`, `m_activeRagdollDefaultTau`, `m_activeRagdollHingeTau`, `m_recoverFromRagdoll*`, `m_enableEarlyRagdoll`, `m_blockRagdoll`, `m_forbidRagdollRequests` | NPCs have **powered active ragdolls**, including while conscious (e.g. Windblast), with per-joint stiffness ("tau") and **get-up recovery**. This is a PLANCK-like layer that already exists. |
| Constraints | `arkCpntConstraint` with `BallAndSocket`, `LimitedHinge`, `Prismatic`, `StiffSpring` and `Attachment` decls; `arkConstraintMotorAngular`/`Linear`; `arkCallbackConstraintMotor`; `arkConstraintMassModifier`; `hkpPositionConstraintMotor`; `hkpSpringDamperConstraintMotor`; `hkp6DofConstraintData`; scripting nodes `ConstraintBreak`, `ConstraintMoveTo` and `PhysicsConstraintControl` | A data-driven constraint component with motors: the building blocks of HIGGS-style grab constraints. |
| Throwing and hits | `arkCpntRigidBodyThrowable`, `arkCpntPlayerSensorThrowHit`, `arkThrowHitInfo`, `m_doThrowHits`, `m_throwHitActiveRagdoll`, `m_throwHitLinearImpulse`/`AngularImpulse`, `m_minShoveThrowHitSpeed`, `m_genuineThrowableWeapon`, `m_isHoldAndThrowObject`; cvars `ark_throw_enable_aim` and `ark_throw_shifting_*` (aim assist) | A native **throw-hit pipeline**: a thrown object that hits an NPC can knock them into an active ragdoll with impulses. |
| Impact damage | `arkCpntContactDamage`, `m_minContactImpulseForDamage`, `m_maxContactImpulseForDamage`, `m_contactImpulseMaxDamage`, `m_swordImpulseScale`, `m_meleeImpulse`, `arkCpntAIPhysicsReaction` | Physics impacts deal damage by impulse natively, and sword hits push bodies. |
| Carrying bodies | `arkPlayerStateCarryCorpse` and `DropCorpse`, `m_playerRefJointNameForCarry`, `m_midnightCarryIdle`/`Walk`, `m_chokeToCarryKeyLocation`, `arkCpntCorpsePossession`, `arkCpntEnvironmentalCorpse` | A carried body is bound to a **named player joint** and animated by carry scenes. |
| Far Reach | `arkPlayerStateGrapple`, `GrabGrappledEntity`, `m_midnightGrabFromGrapple`, `m_lastGrappledEntity`, `m_canBeShadowGrappled`, plus `targetRagdollBoneIndex`/`allRagdollBoneIndex` strings | Far Reach already **pulls bodies and objects into the hand**, and the engine has code that targets a specific ragdoll bone. |
| Severed limbs and dropped equipment | `arkCpntSeverable`, `arkSeveredLimb`, `m_jointNameToSever`, `m_limbImpulse`, `m_dropEquipmentOnSever` | Severed limbs exist, and NPCs **drop equipment** when an arm is cut off, so guard weapons can exist as world objects. |
| Player weapons | `arkCpntWeaponMeleePlayer`, `WeaponGun`, `WeaponCrossbow`, `WeaponThrowable`, `WeaponPlaceable`; player states `Draw`, `Undraw`, `EquipChange`, `ThrowWeapon`, `PlaceWeapon` | No player "drop weapon" state was found. Grenades and mines have native throw and place states. |
| Player states | 80 states, including `HoldObject`, `ReleaseObject`, `CarryCorpse`, `DropCorpse`, `Choke`, `DropChoke`, `KnockedDown`, `Stumble`, `VersusGrab`, `Grapple`, `GrabGrappledEntity`, `ThrowWeapon` (live list from `tools/player_states.py`) | The native interactions a physical layer must hand off to, or sit beside. |

### Read-only investigation of guard ragdolls (sim v399, 2026-10-13, VERIFIED live)

Done with `ragdoll_spike.py` (scratch) on the black-market save. Targets were two of the level's 42 placed corpses and a living guard.

**Each guard (arkNPC) owns 17 Havok hknp bodies, one per ragdoll bone.**
- They sit in the world body pool (`*(game+0x60410)` → pool `+0x28`, stride 0xb0, 16384 records) as **consecutive records with consecutive handles**.
- The NPC's `arkCpntPhysicsSystemEvolved` holds the 17 handles: pointer at `+0x100`, handles at `+0x120 … +0x160` (u32).
- `arkCpntObservable+0x30` points at a second copy of the list.

**Living guards:** the 17 bodies are **keyframed** to the animation (flags `0x8e`, feet `0xce`; motion bits `&7 == 6`). Each guard also has one character capsule body (flags `0x4000004a`).

**Corpses:** the same 17 bodies are **dynamic** (flags `0x82` or `0xc2`; `&7 == 2`, the same motion type the mod's physical grab already drives with the velocity setter 0xc85dc0), lying on the floor.

**Bone order** is the same for every guard (same shape set). Mapped from a standing guard's body positions in his own frame:

| Index | Bone | Index | Bone | Index | Bone |
|---|---|---|---|---|---|
| 0 | pelvis | 6 | left forearm | 12 | left calf |
| 1 | spine | 7 | **left hand** | 13 | **left foot** |
| 2 | chest | 8 | right upper arm | 14 | right thigh |
| 3 | neck | 9 | right forearm | 15 | right calf |
| 4 | **head** | 10 | **right hand** | 16 | **right foot** |
| 5 | left upper arm | 11 | left thigh | | |

The labels are INFERRED from geometry: the chains are monotonic, and the left and right arms sit on opposite sides.

**What this means for dragging:**
- A corpse's wrist or ankle is an ordinary dynamic hknp body. The existing velocity-motor grab (0x89fb0 dispatcher plus 0xc85dc0) can most likely drive it directly, without any new constraint code.
- The engine's own ragdoll joints should then pull the rest of the body along, as in Source's ragdoll grab. This is still untested.
- The mod's grab today rejects these bodies only in `native_rigid_body.hpp`: wrong entity class, and not a single primary body.
- Next check, which is an engine write and therefore implementation: drive body 7 of a corpse with 0xc85dc0 for one second, and confirm the other 16 bodies follow and the corpse stays stable.
- Still open:
  - knocked-out guards: presumably dynamic like corpses, but not observed live;
  - Windblast and throw-hit active ragdolls: whether those flip a living guard's keyframed bodies to dynamic.

## 3. What the mod already does (VERIFIED in the source)

**Physical grab of loose props**, either hand (`native_grab.hpp`, `physical_grab.hpp`, `native_rigid_body.hpp`):
- It hooks the physics command dispatcher **0x89fb0** and drives the body with the velocity setter **0xc85dc0**.
- The motor is linear = 18 × error + hand velocity (max 10 m/s), angular = 12 × error (max 5 rad/s).
- Throw velocity is an exponential average of the hand's velocity with a 35 ms time constant (max 12 m/s).
- Only standalone single-body props qualify (classes idEntity and arkMovable). **NPCs and ragdolls are excluded.**

**Native held objects** (bottles, glasses, severed arms) through the game's `HoldObject`:
- The object is bound to the tracked hand (bind hook **0x7506d0**).
- Releasing it throws with the hand's velocity (hook **0x727100**) or drops it.

**Sword:**
- Contact damage goes through `TakeDamage` **0x879270** (game thread), with surface effects.
- The sword pushes props it touches.
- It stops at walls (Havok sphere cast **0xc89bc0**).

**Weapon attachment:** the sword and pistol follow the tracked hand through the attachment-reader hooks **0x53d3f0** and **0x53d6a0**.

**Holster:** the native sheathe toggle, by button or by reaching over the right shoulder.

**Corpse carry:** native only. The mod reads the state and adjusts the arms.

**Not started:**
- weapons as world objects;
- damage from thrown objects;
- grabbing NPC limbs or bodies;
- physical hand bodies;
- two-handed weapon grips;
- holster slots;
- fingers shaping themselves to an object.

## 4. Feature by feature: verdict and approach

### 4.1 Drop and pick up the sword or pistol

**Feasible, with a world-object stand-in.** Corvo's sword is a weapon entity bound to right-arm joint 59. No native player "drop weapon" was found.

1. Spawn a world rigid-body copy of the weapon model at the hand pose, with the hand's velocity.
   - Candidates: an NPC sword or gun decl (`m_dropEquipmentOnSever` shows such world items exist), or a model-only `arkMovable` with a throwable body.
2. At the same moment, holster the player's weapon (native `Undraw`) and hide it.
3. Grabbing the world copy with the existing physical grab, or a proximity grip, deletes it and draws the real weapon again.

This mirrors HIGGS `DropObject` + `PickUpObject` and ROCK's place-and-reseat.

**Reverse engineering needed:**
- spawning an entity from a decl at runtime;
- whether the weapon entity can be unbound (0x8762b0) and given a body directly (less likely);
- keeping the inventory and equip graph consistent.

**Risks:**
- a lost sword (it falls out of the world, or the player saves while it is on the floor);
- guards' reactions;
- save/load of the world copy.

**Mitigations:**
- auto-return to the holster after N seconds, or on load;
- treat the world copy as temporary.

### 4.2 Throwing the sword or gun at guards

**Feasible.** Throw the world copy from 4.1, then score a hit one of two ways:
- **(a) Native:** make the copy an `arkCpntRigidBodyThrowable` with throw-hit enabled (`m_doThrowHits`). The engine then applies `arkThrowHitInfo` impulses and `m_throwHitActiveRagdoll` itself.
- **(b) Mod-side, as PLANCK does:** a per-frame sphere cast along the flying copy (0xc89bc0, already used for the blade), then `TakeDamage` with inflictor = the thrown copy and instigator = the player. Damage scales with speed × mass, times a thrown multiplier, with a per-victim cooldown.

Start with (b): it reuses the verified sword-damage path. Point-first flight needs a fixed fly-direction rotation, as B&S `flyDirRef` does.

### 4.3 Grabbing and dragging a guard

**Dead or unconscious bodies: feasible, and the strongest native foundation.** Two routes:

- **(a) Constraint route (HIGGS physics grab):**
  - Find the ragdoll bone body nearest the hand.
  - Attach a force-capped constraint between a keyframed hand body and that bone: ball-and-socket or stiff spring, using the engine's own `arkCpntConstraint`/motor classes or direct hknp constraint creation.
  - Cap the force at about mass × 500, fade it in over 0.1 s, and clamp inertia.
  - Dragging by the wrist or ankle then falls out of the physics, and the body drags along the floor.
- **(b) Native-carry route:**
  - Start the native `CarryCorpse` on a squeeze at a limb.
  - Re-target `m_playerRefJointNameForCarry` (or the carried body's bind) to the tracked hand.
  - Cheaper, but it carries the body rather than dragging it.

Reverse engineering needed for (a):
- how the NPC ragdoll is laid out (one hknp body per bone, joint mapping, handles);
- when the ragdoll exists (created at death or knockout, or pooled);
- whether 0xc85dc0 or a new constraint can drive one bone without destabilizing it.

Start by reading the ragdoll from a corpse and watching its body pool, read-only.

**Living guards: possible, but the hardest part.** The engine already has conscious active ragdolls with recovery (Windblast, throw hits). Two routes:
- **(a) Ragdoll on grab** (PLANCK `ragdollOnGrab`): grabbing a limb requests the native conscious active ragdoll, which is then driven as above. Release lets the native recovery stand them up. Reasonable for stuns and shoves, and for dragging a stunned guard.
- **(b) Pull to AI movement:** turn the grabbed bone's displacement into the NPC's own movement, without any ragdoll. This needs reverse engineering of AI locomotion. Defer it.

**Design guard:** a living guard being grabbed must fight back or break free. A grab on an unaware guard should trigger the native choke instead, which the mod already gestures. Otherwise the stealth balance breaks.

### 4.4 Full-hand gripping and finger posing

Today the fingers follow the controller curl, and the mod mirrors the native sword grip.

**Next step:** curl each finger until its fingertip curve meets the object's collision box, as HIGGS does. The mod already has object bounds and per-finger curls, so this is cheap with box collision. Per-item authored poses are the Bonelab `HandPose` equivalent: author a few (sword, pistol, bottle, body limb).

### 4.5 Physical hands and body

**Hands:** add keyframed hand bodies (hknp box bodies driven by the velocity setter), so hands push props and doors, as HIGGS does. Needed for the HIGGS collision-filter rules: hands ignore the player capsule, and hands don't block the use ray.

**Body:** full Bonelab-style physics bodies conflict with the game's character controller and locomotion. That is out of scope; keep the IK body.

**Rendering:** draw the visible hand at the held object while grabbing, and auto-release past about 0.6–0.7 m of drift. Consider a faint "ghost" tracked hand for precision, as the research suggests.

### 4.6 Two-handed grips and holsters

- **Two hands on the sword or a heavy object:** use the HIGGS axis-alignment math. The game has no two-handed weapons, so this is a mod-side pose solve over the weapon attachment.
- **Holster slots** at the hip and shoulder: the mod already has the shoulder-sheathe gesture. A physical hip slot for the pistol would be the same gesture pattern triggering a native equip change.

## 5. Ranked lessons for this mod

1. **Damage goes through the engine's own hit and damage functions** (`TakeDamage`, native throw-hit), and the matching native hit is suppressed so nothing lands twice. The sword path already does this.
2. **Weapons on the floor are real world objects,** and picking one up re-equips the real weapon (HIGGS / ROCK).
3. **Two grab modes:** velocity-driven props (exists today), and force-capped motor constraints for ragdolls. Never unbounded force.
4. **Bodies:** constrain the hand to the nearest ragdoll bone. For living guards, request the native active ragdoll on grab rather than fighting the animation.
5. **Throws:** peak velocity from history, plus ω×r, plus movement. Ignore hand contacts for 0.1 s. The player is the attacker for a few seconds after a throw. Per-victim cooldown.
6. **The collision filter is the central hook** for hands and held objects (player capsule, use ray).
7. **The visible hand snaps to the held object,** with auto-release at about 0.7 m of drift.
8. **Fast thin blades tunnel through targets:** use sphere-cast sweeps (already done for the blade) or continuous collision for thrown weapons.
9. **Performance:** simulate active ragdolls only near the player, with hysteresis. Queue impulses to the pre-physics step.

## 6. Proposed phases (each needs headset confirmation)

1. **Spike, read-only (DONE, see the ragdoll investigation in section 2):** 17 hknp bodies per guard, handles in `arkCpntPhysicsSystemEvolved`, keyframed while alive and dynamic as a corpse.
2. **Drag corpses:**
   - First try the existing velocity motor on one corpse bone body: wrist, ankle, or chest/neck for the collar.
   - Fall back to the constraint route (4.3a) only if the ragdoll goes unstable.
   - Sim check: a corpse dragged by the wrist.
3. **Drop and pick up weapons:** world copy plus native Undraw/Draw (4.1); sim check of drop → grab → re-equip; save/load safety.
4. **Thrown weapons and props deal damage:** sphere cast plus `TakeDamage` from the thrown copy (4.2).
5. **Finger conforming and keyframed hand bodies** (4.4, 4.5).
6. **Living guards:** grab → native conscious active ragdoll, then recovery (4.3); design review for stealth balance first.

**Simulator support each phase needs:**
- selftest scenarios that place the sim hands at a corpse limb;
- a grip-and-pull script;
- read-only body-pool watchers (pattern: `tools/entities.py`, `player_states.py`).
