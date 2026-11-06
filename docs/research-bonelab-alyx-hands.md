# Hands design research: BONELAB, Half-Life: Alyx, HIGGS/PLANCK

Date: 2026-11-06. Web research only, no code changes.

Legend: [V] = verified in a primary source I read this session. [S] = secondary (press, forum, fan wiki). [U] = unverified or my inference. Cost figures labelled "est." are my estimates, not measured.

## 0. Source quality (read this first)

- **HIGGS and PLANCK** are the only sources I could read at code level. Both are open source on GitHub under the author `adamhynek` (Nexus handle FlyingParticle): https://github.com/adamhynek/higgs and https://github.com/adamhynek/activeragdoll (PLANCK = "Physical Animation and Character Kinetics", https://www.nexusmods.com/skyrimspecialedition/mods/66025 per search snippet [S]). The READMEs say the Havok headers are not provided, so the repos do not build as-is, but the logic is readable. Everything in section 3 is from `src/hand.cpp`, `src/constraint.cpp`, `src/finger_animator.cpp`, `include/config.h`.
- **Marrow SDK** (https://github.com/StressLevelZero/MarrowSDK, docs at `Docs/*.md`) documents levels, pallets, avatars and zones. It does NOT document grips, the physics rig, hand poses or joints. The only grip content is an image `Docs/Images/Grips.png` and editor icons named grip-barrel/box/capsule in the repo tree [V]. The legacy repo has `RigidbodyInfo.cs` (a plain rigidbody snapshot: mass, drag, kinematic, CCD mode, inertia tensor) and nothing hand-specific [V]. So the Bonelab section below is mostly secondary and I flag it.
- **Alyx**: I found no GDC talk or transcript on hand interaction via search. Valve Developer Community pages (`prop_handpose`, ladders) returned HTTP 403 to my fetcher, so I only have search-snippet summaries of them. Treat Alyx specifics as [S].

## 1. BONELAB / Boneworks

What the sources actually support:

1. **Physics body, not floating hands.** Laatsch (SLZ) says the Boneworks physics rig was 6 rigidbodies; Bonelab added arms and chest for 11; Marrow 2 is said to have 18 (spine, legs, no ball). Source: his post https://x.com/BrandonJLa/status/2009741997897044046 (could not open the page; content via search snippet [S]). Fan wiki calls Boneworks "hexabody" and Bonelab "heptabody" with a physics rig that handles mass/jointing and an IK-driven skeleton: https://boneworks.fandom.com/wiki/1Marrow_Interaction_Engine (402 on fetch, snippet only [S]).
2. **Elbows are not tracked**; their position comes from hand position plus simulation (blog, [S]): https://blog.giovanh.com/blog/2022/10/22/boneworks-aesthetic-of-substantiation/ . That article says nothing on joint drives, force limits or wall behaviour.
3. **Non-rigid grips with friction.** UploadVR quotes the developers: hand poses are blended relative to the object so the hand is not locked rigidly; grips update as you move and you can slide or rotate the hand without letting go via a friction model; heavy objects are hard to move, light ones easy to throw. https://www.uploadvr.com/boneworks-feels-like-the-first-next-generation-vr-game/ and https://aufenbach.com/blog/recent-developments/stress-level-zeros-new-game-boneworks-revealed-striving-for-hyper-realistic-physics-based-interactions/ [S, via search snippets].
4. **Two-handing aim**: players report Boneworks' off-hand reduces recoil without changing aim, while Bonelab's off-hand shifts aim (Steam thread, player opinion) [S]: https://steamcommunity.com/app/1592190/discussions/0/3816284554971037013/ .

NOT found / unverified (do not treat as fact): ConfigurableJoint drive values, per-hand force limits, the "virtual hand snaps back" rule, the CylinderGrip/SphereGrip/BoxGrip/TargetGrip class list and their exact semantics, HandPose asset format and radius adaptation, ragdoll (PuppetMaster) grab internals, stab/impale and climbing internals. The grip-type names in the task brief come from the user's prior knowledge, and the SDK icons only confirm barrel/box/capsule-style grip shapes [V]. A YouTube teardown exists ("I Remade Bonelab's Physics Rig from Scratch", https://www.youtube.com/watch?v=GMJcr0ZyzII) but I could not read it; a human or a transcript-capable pass should check it if Bonelab internals matter. Unity's Meta Interaction SDK has analogous cylinder/sphere grab surfaces with auto-computed radius (https://developers.meta.com/horizon/reference/unity/v66/class_oculus_interaction_grab_grab_surfaces_cylinder_grab_surface [V]) but that is Meta's design, not SLZ's.

## 2. Half-Life: Alyx

- **Hands collide with the world** (not ghosts): Transform Interactive says Alyx hands collide with the environment (lean on walls, push doors, brush tables), and that when grabbing levers, knobs, cranks the virtual hand latches onto the object until release so it diverges from the real hand (technique credited to Job Simulator): https://transforminteractive.com/what-half-life-alyx-taught-us/ [S]. Players report leaks: hands passing through some surfaces and items clipping through the head: https://steamcommunity.com/app/546560/discussions/0/2137462524945044561/ [S]. So the design is "mostly blocked, imperfect", not a guarantee.
- **Invisible arms exist only for physics** (e.g. a drawer closing on your arm). Valve never got arm tracking good enough, so arms are not rendered. Robin Walker, Game Informer: https://gameinformer.com/interview/2020/03/23/valve-talks-half-life-alyx-and-why-arms-dont-work-in-vr [S, interview].
- **Authored hand poses per interactable.** A Valve artist says she spent most of her time adding custom hand poses to each interactable (hundreds) and tuning prop collision volumes so VR hands collide "just right": https://www.janeng.com/half-life-alyx/ [S]. The workshop entity `prop_handpose` swaps the VR hand to a set pose when the hand approaches it, used on ladder rungs with grab distance, extend size and auto-grip options, debug via `vr_hand_pose_debug` (search summary of https://developer.valvesoftware.com/wiki/Prop_handpose and the Ladders page; fetch 403) [S]. Procedural trace-based finger wrap in Alyx: [U], I found no source either way.
- **Gravity gloves**: always equipped, not a separate weapon (Kerry Davis, Engadget: https://www.engadget.com/2020/03/31/half-life-alyx-valve-interview-vr/ [S]); pulled objects reliably land in hand if you are roughly right (UploadVR, https://www.uploadvr.com/valve-index-alyx-interactions/ [S]); Edge calls them a counter to reach inaccuracy [S]. Exact snap/flick tuning: [U].
- **Held tools collide too**: weapons held in hand can push doors, swat objects, play the piano; the author says this stops "clipping" breaking immersion (Road to VR): https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/ [S].
- **Haptics**: no design source found [U].
- **Design rule**: one hand always kept free for locomotion and interaction (Forbes AMA recap) [S].
- **SteamVR skeletal input** (primary): per-finger curl 0..1 and splay summary, reference poses BindPose/OpenHand/Fist/GripLimit, motion range WithController vs WithoutController: https://github.com/ValveSoftware/openvr/wiki/SteamVR-Skeletal-Input [V]. Useful as the input contract for a curl-driven finger system.

## 3. HIGGS / PLANCK (code-level, Havok, retrofit into a non-VR-physics game)

All [V] from https://github.com/adamhynek/higgs unless noted. Skyrim units use Havok world scale; I quote values as configured.

### 3.1 Hand body for contact
- One **box shape per hand**, `MOTION_KEYFRAMED`, quality `KEYFRAMED`, deactivation off, max angular velocity 500; added to the world once (`Hand::CreateHandCollision`, hand.cpp ~560-640). Comment in code notes `KEYFRAMED_REPORTING` would be needed to get callbacks vs statics.
- **Moved by velocity, not teleport**: each frame `ApplyHardKeyframeVelocityClamped(target pos, quat, 1/dt, body)` (hand.cpp ~699). Same for a weapon collision body.
- **Collision filter**: custom layer (56) with the *player's collision group* in the high 16 bits so the hand does not collide with the player's own capsule, plus a ragdoll-layer bit (left/right hand) so it can collide with NPC ragdoll limbs. The group is rewritten every frame because the player group changes (mounting a horse). Bit 14 toggles collision on/off, followed by `hkpWorld_UpdateCollisionFilterOnEntity` only on change.
- **Hand collision is OFF** while physically holding a body or two-handing, and for `handWeaponCollisionEnableDelay` 0.1 s after creation; after a throw hand collisions are ignored for `throwIgnoreHandCollisionTime` 0.1 s.
- **Player not pushed by own hand**: this is achieved by the shared player collision group (no hand-vs-capsule contact) and by registering the hand body as "player-space" so the player's per-frame movement delta is applied to it before the velocity (`doPhysicsGrabPlayerMovementCompensation`, `RegisterPlayerSpaceBody`) [V].
- **Hands are not blocked by walls visually**: the keyframed hand pushes dynamic bodies only; it does not push back on the player. The visible hand stays at the tracked position (VRIK drives it). [V by absence of any hand-vs-static response in code; moderately confident.]

### 3.2 Holding: constraint with motors, not velocity writes
- Custom constraint `GrabConstraintData` (constraint.cpp): a ragdoll-style angular position motor (3 axes) plus 3 linear position motors, `hkpPositionConstraintMotor` with `tau`, `maxForce`, `damping`, `proportional/constantRecoveryVelocity`. Targets are updated every frame from the tracked hand.
- Config defaults (config.h): linear tau 0.03, damping 0.8, max force 2000 (weapons 9000, actors 2500), angular max force = linear / 12.5. Body-tau ramps from 0.01 (start) to 0.65-0.8 over `physicsGrabLerpTauTimeBody` 0.1 s; force fades in over 0.1 s (`grabConstraintFadeInTime`). Max force is scaled by object mass (`grabConstraintMaxForceToMassRatio` 500) and by fps via lookup tables; inertia is clamped (`grabbedObjectMinInertia` 0.01, max ratio 10).
- **Weight and colliding**: when the held object is colliding, tau drops to 0.01 (`grabConstraintCollidingLinearTau`/Angular), so the object goes soft against obstacles instead of fighting them.
- **Hand leaves the object when real hand and constrained hand diverge**: running average over several frames of distance between held-object hand and tracked hand; above `maxHandDistance` 0.7 m the grab is dropped (hand.cpp ~3900). Ignored for 0.2 s right after grabbing.
- **Player drag**: `slowMovementWhenObjectIsHeld` reduces move speed by object mass (proportion 0.675, up to 75%), jump height likewise, with fade-out 5 s [V config.h].
- **Throw**: velocity from the controller history, `throwVelocityThreshold` 1 m/s, `angularVelocityMultiplier` 0.6, tangential velocity cap 5 m/s, optional tangential inheritance.
- **Two-handed weapons**: factors for off-hand alignment/shift/rotation (0.5-1.0), rotation snap speed 1200 deg/s.

### 3.3 Finger wrap
- **Pose set = precomputed open/closed curves** per finger (`g_openFingerPositions` / `g_closedFingerPositions` in `finger_curves.h`) lerped by one scalar per finger (0 open, 1 closed), mirrored for the left hand; curves were generated at hand size 0.85 and scaled (`handSize / 0.85`) [V finger_animator.cpp, hand.cpp ~1450].
- **Curl from geometry**: at grab time, `GetIntersections()` intersects each finger's arc (normal, zero-angle vector, start position in hand space) with triangles of the nearby held mesh; no hit = curl fully; result min 0.2, otherwise value from the intersection angle; alternate thumb curve (index 5) tried too. This is a *one-time, CPU triangle test per grab*, not per frame (hand.cpp ~1475-1540, ~1784-1841). [V]
- **Animation**: fingers move toward target at fixed linear/angular speeds with double-speed ramp (grab 4 u/s, 630 deg/s; start 2, 135; end 2, 45; restore over 0.8 s), i.e. cheap speed-limited lerp. Before the grab (hover on a selected object) fingers go to curl 0.9 only if hand speed < 0.9 m/s.

### 3.4 Selection / grab
- Sphere casts (phantom) with near 0.05 m radius / 0.1 m dist, far 0.3 m / 5 m, plus a ray, requiring a 50-degree direction cone; closest-point queries against the hand body; pull (gravity-glove) velocity from fitted curve, angular damping 8 during pulls. Per-frame cost is a handful of Havok queries on a phantom sphere [V].

### 3.5 Haptics (config.h)
- Collision haptic: base 0.1 + 0.05 x speed + 0.03 x mass^0.6 strength, 10 ms duration, min speed 0.2 m/s; grab haptic base 0.25 + 0.06 x mass^0.6, fade 0.1 s; selection start 0.15/0.05 s, end 0.1/0.02 s, rolling 0.05 + 0.3 x proximity. Mass exponent 0.6 gives a compressive curve so heavy things do not saturate [V].

### 3.6 Problems HIGGS hit (visible in code/config)
- Physics step rate: `enableHavokFix`, `minPhysicsFrameRate` 70, up to 3 steps per update, to make Havok run at HMD rates [V]. Stale shadow updates for island bodies (`numShadowUpdates`).
- Force must scale with frame rate (fps-to-max-force tables) because motors are step-count dependent [V].
- Self collision of held objects vs player: held weapon/object uses layer 5 ("weapon") with player group, so it ignores the capsule [V]. Projectiles collide only with a specific pick layer, so the pick sphere uses a layer that hits everything [V comment].
- Hand body causes false "player" hits in triggers: `treatHandCollisionAsBelongingToPlayer`.
- No performance numbers are published in the repo; the author calls the mod "complicated" [V]. Performance: [U]. Est.: two keyframed bodies plus a few sphere casts per frame is well under a millisecond; one-time triangle intersection per grab.

### 3.7 PLANCK (physical animation of NPCs)
- Active-ragdoll bodies are enabled within 50-60 units of the player (`activeRagdollStartDistance/EndDistance`), blend in over 0.075 s, fade world-from-model computation 0.3-0.5 s [V config.h, activeragdoll].
- **Grabbing a living NPC limb**: stamina cost per second (3 normal, 10 hostile), movement slowdown by race size (35-85 percent), ragdoll on grab for small races, **yank to ragdoll** when hand speed exceeds 4 m/s (roomspace) with impulse spread over a 8-bone radius, decaying with `yankImpulseDecayExponent` 0.9 and directional multipliers 0.35-0.45; foot-yank needs stress 22 [V config.h]. Hit cooldown 0.25 s after contact stops; shove needs 2.5 m/s [V].
- Source of truth for these internals is the config file; the algorithm of the motor drive was not read [U].

## 4. Design to copy (cheapest technique that gives the feel)

Costs are est. for a Havok-style engine; relative, not measured.

| Feature | Technique | Per-frame cost (est.) | Pitfalls |
|---|---|---|---|
| Finger wrap on props | HIGGS approach: precomputed open/closed finger arcs, one curl scalar per finger, intersect arcs with the target shape ONCE at grab (not per frame), cache result, speed-limited lerp to it. For props with a known shape (our Havok shapes) intersect with the shape instead of triangles. Alyx adds authored per-object poses where wrap would look wrong [S]. | ~0 steady state; one burst of 5-6 arc/shape tests at grab | Min curl clamp (HIGGS 0.2) so fingers never over-curl; separate thumb curve; scale arcs by hand size; do not re-solve while the object moves in the hand |
| Wall contact | Alyx: hand stops at surfaces, latches visually while gripping (S). Cheapest: keyframed hand body (velocity-driven) used for pushing dynamics, and a visual-hand clamp only when needed. HIGGS does not clamp the visible hand at statics [V]. Your existing query-driven collision already does the clamp. | one shape cast per hand per frame (existing) | Leaks (Alyx players report pass-through [S]); hand must never push the player capsule: put hand body in the player's collision group, compensate player motion delta (HIGGS) |
| Punching impact | Keyframed hand body moved by velocity; transfer momentum through the engine's contact; gate damage on hand speed (HIGGS shove threshold 2.5 m/s, `collisionMinHapticSpeed` 0.2) and apply a short cooldown (0.25-0.7 s) so one punch is one event | 2 bodies (~0) | Tunneling at fast swings: use sweep/continuous quality or sub-step; velocity must be clamped; disable hand collision for 0.1 s after throw/release |
| Generic prop grab | HIGGS: custom constraint with position motors (tau 0.03, damping 0.8, max force ~ mass x 500) re-targeted every frame to the hand; tau and force fade in over 0.1 s; drop tau to ~0.01 while colliding. If you cannot add constraints, a velocity-motor write toward the target with clamped speed and mass-scaled limit reproduces the same feel (inference [U]). | 1 constraint or 1 velocity write per held body | Force scales with step count; unstable on very light/heavy bodies (clamp inertia); disconnect rule: drop or let go when hand vs object deviates above ~0.7 m (HIGGS) |
| Ragdoll limb grab | PLANCK: grab drives the limb with the same constraint, NPC set to ragdoll, movement slowdown and stamina cost by size; yank to ragdoll when hand speed > 4 m/s with an impulse decaying across nearby bones | 1 constraint + a few bone impulses | NPC must be in a pose where the limb body exists; limit range to active ragdoll bubble (50-60 units); ignore hand collision briefly after drop (0.2 s) |
| Two-handed | HIGGS: main hand drives position; off-hand blends orientation with factors 0.5-1.0 and a rotation snap speed; hand-to-hand alignment/shift; BONELAB: grip not rigid so hands can rotate [S] | cheap math | Off-hand should not move the muzzle for pistols (player complaints vs Boneworks [S]) |
| Throw | Release velocity from short controller history (HIGGS threshold 1 m/s), tangential speed cap 5 m/s, angular velocity x0.6, optional inherit of hand tangential velocity; ignore hand collision for 0.1 s after release | trivial | Needs the history buffer in the same frame step; do not use the last single frame |
| Weight feel | Constraint max force proportional to mass, slow player by mass (HIGGS 0.675 x mass, max 75%), haptic strength ~ mass^0.6 | trivial | Without the slowdown heavy items feel weightless |
| Haptics | HIGGS formulas (section 3.5): contact pulses 10 ms scaled by speed and mass^0.6; grab pulse that fades over 0.1 s; hover pulse proportional to proximity | trivial | Many short pulses can stack on some runtimes; pulses need a minimum hand speed to avoid buzz |
| Hand/finger smoothness | SteamVR skeletal curl/splay is the right input contract [V]; tween toward target with fixed speeds, double speed during the first 0.1-0.25 s | trivial | Never let tracked finger pose overwrite a held pose mid-grab |

## 5. Open items I could not resolve
- Bonelab joint/drive values, grip class semantics, HandPose radius adaptation, PuppetMaster ragdoll grab, stab/impale, climbing: no primary source reachable. If needed, transcribe the YouTube rig teardown above or ask the user for Bonelab modding wiki captures.
- Alyx: GDC talk not found; `prop_handpose` details only via search snippet.
- HIGGS performance in ms: not published.
