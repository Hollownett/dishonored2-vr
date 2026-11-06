# Into the Radius 2: physical VR hands (static RE)

Sources: `IntoTheRadius2-Win64-Shipping.exe` (strings, plus disassembly of cvar registration code), the unencrypted `pakchunk0-Windows.utoc` directory index (40,443 asset paths), and `Manifest_UFSFiles_Win64.txt`. The game was never launched. Gameplay logic is mostly Blueprint, and that lives in `.ucas`, which is Oodle-compressed. Asset contents (drive values, hand poses) were NOT read. Scratch tools are in `%TEMP%\claude\itr2-re` (`utoc.py`, `xr.py`, `cv.py`, `paths.txt`).

Confidence key: [S] = string or path seen, [D] = value read from disassembly, [I] = inferred.

## 1. Plugins

Project plugins in the manifest [S]: `VRExpansionPlugin`, `OpenXRExpansionPlugin` (both in `Plugins/VRExpansionPlugin/`), `ALS-Refactored` (Advanced Locomotion System, body and overlay animation), `AICoverSystem`, `HTN`, `AdvancedSessions` and `AdvancedSteamSessions`, `Sentry`, `ITR_MobileShaders`. Neither VRE `.uplugin` was read, so no version number. The VRE class set is recent (it has `UGS_LerpToHand`, `EPhysicsGripConstraintType`, `bTeleportPhysicsGrips`), and the engine is UE 5.5+ ("deprecated (5.4) logic" string). The plugins are statically linked into the exe, and only `Sentry` has a plugin folder on disk.

**ITR2 is built on VRExpansionPlugin (mordentral).** Its grip stack is the foundation. ITR2 adds a C++ layer on top (`URadiusPhysicalHand` etc.) and the rest in Blueprint.

## 2. Architecture

### 2a. Player body [S]
`ARadiusPlayerCharacterGameplay` has these components:
- `PhysicsHandLeft` / `PhysicsHandRight`
- `PhysicsGripLeft` / `PhysicsGripRight`
- `GrabSphereLeft` / `GrabSphereRight` (`UGrabSphere`, `OverlappedGrabSpheres`)
- `LeftHandContainer` / `RightHandContainer`
- `GetPhysicalHandMeshes`

The assets are `BP_RadiusPlayerCharacter_Gameplay`, `BPC_BodyController`, `BP_GripSelectorsController` (a `AGripSelectorsController`, i.e. a selector for which object a hand would grab), and `BPC_PlayerCharacterHandHolster`.

Hand meshes live in `ITR2/BPs/Player/BodyAssets/SKM_PhysHands/` [S]. This folder holds:
- `SKM_PhysHand_Left/Right(_new)` with their own `_PhysicsAsset` and `_Skeleton`
- `ABP_PhysHands`
- `AN_PhysHand_fistBoth`
- `SKM_PhysHand_Forefiger_tap`
- `SKM_PhysHand_Open2`
- `PhysicalMaterials/PM_PhysHand`

So there is a **dedicated skeletal "physics hand" with its own PhysicsAsset** (per-finger bodies), separate from the visual XR hand (`A_XRHand_Grab_Right`, `A_XRHand_Idle_Right`). The VRE default `_VRE/Core/GraspingHands/*` (`GraspingPhysicsManny`, `PhysicsTossManager`, `GraspAnimBPManny`, `HandAnimState`) also ships, but it looks like unused template content [I].

### 2b. How the hand follows the controller: PID force/torque controller [D]
`URadiusPhysicalHand` is a C++ component with `OnGripped`, `OnDropped`, `OnGrippedSecondary`, `OnDroppedSecondary` and `OnLerpToHandFinish` [S]. Its tuning is exposed as a cvar family. I read the registration code and took the default from the constant loaded as the default argument:

| cvar | default |
|---|---|
| `R.Hands.PID.LocationCoefficient` | **500** |
| `R.Hands.PID.LinearVelocityCoefficient` | **20** |
| `R.Hands.PID.RotationCoefficient` | **3500** |
| `R.Hands.PID.AngularVelocityCoefficient` | **500** |
| `R.Hands.PID.AngularForceLimit` | 0 (probably 0 = no limit [I]) |
| `R.Hands.PID.UseLastFrameAngularForce`, `ZeroAngularVelocity`, `UseDeltaTimeInForces`, `UseReverseDeltaTimeInForces` | all false |
| `R.Hands.PID.DistanceGripTimeout` | 1.0 s ("Distance grip timeout seconds") |

The error string "Empty Physical Hands PID parameter" is also present.

Interpretation [I]: the hand body is driven by a PD controller. Force is about `500 * posError - 20 * linVel`, torque is about `3500 * rotError - 500 * angVel`. Those are acceleration-style gains, since they are large and the `UseDeltaTimeInForces` option exists. This is applied to the physics hand body, not a Chaos `PhysicsConstraint` drive. There is no `PhysicsConstraintComponent` hand-drive string anywhere. Chaos joint drive cvars are only the engine's own.

### 2c. VRE grip layer on the held object [S]
VRE grip types are available:
- `EGripCollisionType`: `InteractiveCollisionWithPhysics`, `InteractiveHybridCollisionWithPhysics`, `ManipulationGrip(WithWristTwist)`, `LockedConstraint`, `AttachmentGrip`, `PhysicsOnly`, `SweepWithPhysics`, `CustomGrip` and others.
- `EPhysicsGripConstraintType`: `AccelerationConstraint` / `ForceConstraint`.
- `EPhysicsGripCOMType`: `COM_Default`, `COM_AtPivot`, `COM_GripAt`, `COM_GripAtControllerLoc`, `COM_SetAndGripAt`.
- `BPAdvGripPhysicsSettings`, with `bUsePhysicsSettings`, `bTurnOffGravityDuringGrip`, `bUseCustomAngularValues`, `ConstraintStiffness`, `ConstraintDamping`, `ConstraintBreakDistance`, `GripBreakDistance`.
- `BPAdvancedPhysicsHandleSettings` / `...AxisSettings` (`LinearMaxForceCoefficient`, `AngularMaxForceCoefficient`, `LinearDriveStiffnessScale` and so on).
- Grip lifecycle and flags: `GripLateUpdateSetting`, `GripPriority`, `bAllowMultipleGrips`, `bDenyGripping`.
- Two-hand handling (the VRE secondary grip stack): `ESecondaryGripType` values `SG_Free(_Retain)`, `SG_SlotOnly(_Retain)`, `SG_ScalingOnly`, `SG_Custom`. Also `SecondaryGripDistance`, `bUseSecondaryGripDistanceInfluence`, `GripInfluenceDeadZone`, `GripInfluenceDistanceToZero`, `PrimarySlotRange` / `SecondarySlotRange`, and `bOnlyPenetrateWithTwoHands`, `PrimaryHandPhysicsSettings`, `SecondaryHandPhysicsSettings`, `SingleHandPhysicsSettings`, `MultiHandPhysicsSettings`, `bUsePrimaryHandSettingsWithOneHand`, `COMType`.
- Slot grips: `SlotDefaultGripType`, `bIsSlotGrip`.

Which weapons and items use which grip type is in Blueprints and was not read [I]. Most likely it is "attach to the physics hand with a constraint-like follow", with slot/hand-socket snapping for firearms.

`GS_LerpToHand` (`UGS_LerpToHand`) [S] eases a grabbed object into the hand. ITR2 wraps it: `OnLerpToHandFinish` is bound on the physical hand, with `bUseGlobalLerpToHand`, `bSkipLerpToHandIfHeld` and `CancelGlobalLerpToHand`.

### 2d. Hand poses: authored per-object sockets, not procedural fitting [S]
- `URadiusHandSocketComponent` is a subclass of VRE `UHandSocketComponent`.
- Related names: `HandSocketVer`, `GetClosestHandSocketComponentInRange`, `GetHandSocketsByTags`, `CanRegripHandSocketTag`, `DisableAllHandSocketsOnActor`, `AnimationTag`.
- Data: `Content/ITR2/Configurations/Infos/DT_HandSocketInfos` (a DataTable of sockets).
- Visual hand: `ABP_PhysHands`, `AN_PhysHand_fistBoth`, `SKM_PhysHand_Forefiger_tap`, `A_XRHand_Grab_Right`. Poses are two-pose blends of an open and grab pose, plus OpenXR `FingerCurls` / `AnimNode_ApplyOpenXRHandPose` when a hand-tracking pose exists.
- No finger-trace or procedural curl-fit strings were found.
- `CanGrabHandStateTag` and `CanGrabHapticTag`: the hand changes state and gives a haptic cue when it can grab.

### 2e. Distance grip [S]
`UDistanceGripManager`, `SyncTransformComponent::OnDistanceGripChanged`, `DistanceGripTag`, `DistanceGripCandidate`, `bDenyDistanceGrip`, `bCanDistanceGripStoredItems`, plus tuning names: `DistanceGripRadius`, `DistanceGripLength`, `DistanceGripDeadZone`, `DistanceGripSpeedCoeff`, `DistanceGripDampingCoefficient`, `DistanceGripDurationInfluenceSpeedCoeff`, `DistanceGripFinishLerpingDistance`. Particles are `PS_DistanceGrip(_L/Select/SelectR)`. A pointed/selected item flies to the hand (speed and damping coefficients, finish distance), with a 1 s timeout cvar. The values are in asset data and were not read.

### 2f. Holsters and inventory [S]
`URadiusHolsterComponent` (overlap-based, `OnBeginOverlapCustomCollision`), `BPC_PlayerCharacterHandHolster`, `BPC_AutoReturnHolster` (`AutoreturnHolster`), `BPC_BackpackHolster`, `HelmetHolsterTag`, `BackpackHolsterTag`. Items are data-driven (`UItemInfo`, `URadiusItemStackComponent`, `ARadiusItemActor`), weight-driven (`CalculateContainerItemsWeight`, `OnWeightRecalculateEvent`, `Curve_OverweightMovementSlowdown`). Hand shapes for inventory: `E_ShowedInfoHand`.

### 2g. Climbing, haptics, throwing [S]
- Climbing: VRE climbing (`EGripType::Climbing`, `VRClimbingStepHeight`, `VRClimbingMaxReleaseVelocitySize`, `VRClimbingStepUpMultiplier`, `bClampClimbingStepUp`, `VRMOVE_Climbing`). ITR2 adds `ClimbActionTag`, `BP_ClimbableActor_Handler`, `BP_Climbable_Hand_1..5` (hand-hold actors), and `ClimbingStepUp`.
- Haptics: assets `Haptic_Grip(_Vive)`, `Haptic_Interact(_Vive)`, `Haptic_PM_fire(_Vive)`, `Haptic_HeartBeat*`, `HapticFeedbackDetails_Curve`, `SetGripHapticState`, `GripHaptic`, OpenXR `xrApplyHapticFeedback`.
- Throwing: no ITR2 throw-specific strings. VRE default release velocity and `PhysicsTossManager` are probably used [I]. `VRClimbingMaxReleaseVelocitySize` is climbing only.
- Collision of the hand against the world: the hand is a real physics body with `PM_PhysHand` (physical material), so it collides and gets blocked [I]. No explicit "ghost hand" strings were found. A max-distance or teleport of the hand is `bTeleportPhysicsGrips` (VRE) [S].

## 3. What I could not read
The .ucas is Oodle-compressed (no oo2core DLL ships; it is linked into the exe). The following are therefore unknown: actual per-weapon grip types, `BPAdvGripPhysicsSettings` values, hand-socket transforms, `DA_PlayerConfig` / `DA_WeaponsConfig` numbers, and project `Config/Default*.ini` (physics substepping, solver iterations). The `.pak` holds only Engine/plugin `.ini` files, not project settings.

## 4. What to copy into a Havok retrofit (feel, not code)
1. **Two bodies per hand.** The tracked controller pose is the target. A separate rigid hand body (with finger collision) is chased by it. Never move the body directly.
2. **PD force and torque controller with acceleration-style gains.** The hand's controller gains are position 500, velocity damping 20, rotation 3500, angular damping 500, with no torque cap by default. Tune this ratio. Linear damping is about critical (`2*sqrt(500)` is about 45, so 20 is underdamped and snappy), and angular is about critical (`2*sqrt(3500)` is about 118, so 500 is overdamped). Expose these as live knobs.
3. **Held objects ride the same hand body** (constraint or attach), with a short lerp-to-hand easing on grab (`GS_LerpToHand`) so they do not pop.
4. **Per-object authored grip sockets plus open/grab pose blend.** Snap hand pose to the socket when in range. Fall back to curl-by-trigger. No procedural finger fitting is needed.
5. **Primary and secondary grip.** Primary sets the pose, secondary aims it (slot-only or free). Blend the secondary's influence by distance with a dead zone. Use a separate physics setting set for one hand vs two hands.
6. **Distance grip.** A selection ray with a candidate highlight, then fly the item to the hand with a speed coefficient, damping and a timeout.
7. **Grab spheres per hand** and a "can grab" hand state plus haptic tick on entry.
8. **Release.** Fire grip/drop events, keep the physics velocity on release (throw) and clamp climbing release speed.
9. **Weight and holster** are design features, but separate from the hand physics.
