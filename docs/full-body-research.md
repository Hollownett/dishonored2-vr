# Full VR body and player calibration: research

Research, offline assets and an offline solver, 10 October 2026. Nothing here is
implemented in the game or checked in the headset. **VERIFIED** means measured in a repo
artifact, read in source, or queried from the installed runtime. **INFERRED** means not yet
observed live. Detailed evidence index: `artifacts/claude/fullbody/native-sweep.md`.

## 1. What the game already gives us

- **A full player body mesh exists and is posed during gameplay. VERIFIED.** The player owns
  three skinned models: `models/characters/player/emily/body.md6` (torso and legs, no arms;
  7,951 vertices) and separate `left_arm.md6` / `right_arm.md6`. Offline CPU skinning matches
  the native GPU palettes to within 1.6e-7 m, and the replay
  (`artifacts/research/native-body-replay/native-body-and-arms.png`) shows a standing body
  about 1.65 m tall. That suggests 1 native unit ≈ 1 m (INFERRED, still unmeasured).
- **Body skeleton, 20 joints. VERIFIED.** `origin, center_root, spine01-03, neck, head,
  head_end, camera_jnt, femur/tibia/ankle/toes/toes_end ×2, anchor_jnt`
  (`artifacts/native-fullbody-rig-23072-readonly.json`, `rig_samples[0]`). There are no arm
  joints. Each arm rig (69 joints) is rooted at its own `origin`, which coincided with
  `center_spine03_jnt` to 1.6e-9 m in one sample. Torso yaw/pitch therefore carries the
  shoulders for free.
- **Write seam. VERIFIED (static), INFERRED for the body.** The arm palette is replaced at
  the renderer upload `0x25efe0`. That function takes its joint count from the model's own
  skeleton (`skeleton+0x78`), so it is not arm-specific
  (`artifacts/re-toolchain/direct-ghidra/arm-render-upload/function-14025efe0.txt:37`). The
  body uses the same `arkCpntAnimEvolved` pose pipeline (pose state 3 =
  `renderer_upload_source`). Today `side_for_model` drops every non-arm model, and
  `game_rig.hpp:78,152` hard-require 69 joints.
- **Why the body is invisible today. VERIFIED (asset decls).** Emily's body surface uses
  `models/characters/player/base/materials/player_body_hidden.material`
  (`invisible 1`, `mainProgram/shadowProgram/zPrepassProgram arkShadowOnly`). Corvo's uses
  `corvo/textures/player_body_hidden.material` (constant grey). The shipped textured
  materials `player_corvo_body.material` and `player_emily_body.material` (the latter is Emily's
  NPC `emily_body_*` textures) fit the body mesh exactly: its texcoord0 wireframe traces the
  `player_corvo_body_d` atlas islands (`artifacts/fullbody-assets/textures/corvo_uv_overlay_fixed.png`).
  A full textured body therefore needs a **material swap on one surface, not new art**. The
  body mesh has no head (the collar is the top), so nothing needs hiding around the camera.
  Still unverified in game: whether swapping is enough (`showInRenderMode = false` in
  `base/body.def`). See section 7.

## 2. Tracking sources

- **Quest 3 body tracking over Virtual Desktop is advertised. VERIFIED (extension list only).**
  The active runtime is VDXR (`virtualdesktop-openxr.json`). Its instance extensions include
  `XR_FB_body_tracking`, `XR_META_body_tracking_full_body`, `XR_META_body_tracking_fidelity`
  and `XR_META_body_tracking_calibration` (enumerated 2026-10-10). That contradicts the
  project's own [compatibility wiki](https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Application-Compatibility),
  which is older. An advertised extension can still report `supportsBodyTracking=false` or
  fail at tracker creation. This needs the headset connected; see
  [`XrSystemBodyTrackingPropertiesFB`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSystemBodyTrackingPropertiesFB.html).
  If it works, Meta's Inside-Out Body Tracking gives a real torso/hip estimate, and the full-body
  joint set adds inferred legs ([Meta Movement docs](https://developers.meta.com/vr/documentation/native/android/move-body-tracking/)).
- **Fallback: 3-point IK** from the head and two grips. It cannot measure hips or feet, so
  the pelvis and legs are inferred.
- **Native legs.** The game already animates the 20-joint body for walking, crouching and
  mantling. Its 211 authored body clips (lean, keyhole, swim, possession, takedowns, vault,
  cablecar) mark the states where the native torso should keep ownership.

## 3. Public references

- **VRIK (Skyrim VR)** calibrates height, body size and scale with an in-game power, plus an
  MCM menu. A head that sits too low stretches the forearms, so height is the main lever.
  [Nexus](https://nexusmods.com/skyrimspecialedition/mods/23416).
  **FRIK (Fallout 4 VR)** is full-body IK in the same style.
  [Nexus](https://nexusmods.com/fallout4/mods/53464). Players report that a VR scale change
  rescales the world, so scale the avatar, not the world.
  [Steam](https://steamcommunity.com/app/611660/discussions/0/1815422173040351155).
- **Final IK VRIK** scales the avatar root by the head-target height divided by the avatar's
  head-bone height, measured while the player stands straight. Elbow and knee bend direction
  comes from a slightly bent rest pose. Procedural feet are planted and step past a distance
  threshold. [Docs](http://www.root-motion.com/finalikdox/html/page16.html).
- **Bonelab** remaps the body to the player's real proportions.
  [Meta blog](https://www.meta.com/blog/bonelab-boneworks-stress-level-zero-vr-physics/).
  Boneworks players report that calibrating too low pins the body at its limits.
  [Steam](https://steamcommunity.com/app/823500/discussions/0/3963662399212243926/?ctp=2).
- **Half-Life: Alyx** chose hands only: "if you get it wrong, it stands out an enormous
  amount". [Game Informer](https://gameinformer.com/interview/2020/03/23/valve-talks-half-life-alyx-and-why-arms-dont-work-in-vr).
  Any body must be a toggle, with hands-only kept as a fallback.
- **id Tech 4** (the public ancestor) applies joint overrides through
  `idAnimator::SetJointPos/SetJointAxis` and does foot IK with `idIK_Walk`
  ([Anim.h](https://github.com/id-Software/DOOM-3/blob/master/neo/game/anim/Anim.h),
  [IK.h](https://github.com/id-Software/DOOM-3/blob/master/neo/game/IK.h)). Void has no
  public source. The `arkCpntAnimEvolved +0x468/+0x470` IK pointers are the only native IK
  candidates found, and they are unverified.
- **Embodiment research.** Body ownership in VR is modest but robust to size and appearance
  differences ([Mottelson meta-analysis](https://hcds.itu.dk/bibliography/mottelson2023),
  [Frontiers review](https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2018.00074/full)).
- Formula-level notes with sources (Parger 2018 elbow, MIT VRArmIK, FRIK as GPL reference only,
  One Euro filter, Quest joint sets): `artifacts/claude/fullbody/math-research.md`.

## 4. Calibration

- `src/avatar_calibration.hpp` already implements the T-pose wizard: both grips held, at
  least 20 samples over 1 s, level hands, span 0.9–2.4 m. The production call never passes a
  floor, so `standing_eye_height` is always empty, and nothing is persisted.
- The solver needs eye height and T-pose hand span (section 6). Still missing: the floor
  from stage space or `XR_META_body_tracking_calibration`, persistence in `vr_preferences`
  with a Recalibrate entry, and a seated mode.

## 5. Offline asset pack

`python tools/extract_player_raw.py` copies raw resources out of the game packages
(read-only) into `artifacts/fullbody-assets/raw/`. `python tools/build_player_asset_pack.py`
writes `artifacts/fullbody-assets/pack/`:

- `skeleton_body.json` (20 joints) and `skeleton_left_arm.json` / `skeleton_right_arm.json`
  (69 each): names, parents, local bind TRS, model bind and inverse bind. Corvo reuses
  Emily's three `.md6skl` files.
  - Format: `60SE`, 48-byte joint records (quaternion xyzw, translation, scale);
    `model = parent · T·R·S`.
  - **Inverse binds match the live game to 2.3e-7.**
- `<emily|corvo>/{body,left_arm,right_arm}.npz`: positions, texcoord0/1, normals, tangents,
  8 weights/joints and triangles per surface, plus materials. 44 bytes per vertex, following
  the live vertex-element report: positions, texcoord0|texcoord1 interleaved half2 (input
  slot 1), int8 normal (slot 2), int8 tangent (slot 3), skin (slot 4).
- `tags.json`: decl attach tags. Both arms attach at `center_spine03_jnt` with an identity
  offset; `camera_tag` sits on `camera_jnt`.
- Bind proportions, in metres:

  | Measure | Value |
  |---|---|
  | `camera_jnt` (eye) | 1.700 |
  | Pelvis | 1.055 |
  | Femur | 0.475 |
  | Tibia | 0.411 |
  | Ankle | 0.123 |
  | Shoulder width | 0.311 |
  | Upper arm | 0.264 |
  | Forearm | 0.264 |
  | Wrist to `hand_attachment` | 0.082 |
  | Fingertip span (≈ height) | 1.831 |
- Diffuse textures are decoded from BC7 with `tools/decode_bimage.py` into
  `artifacts/fullbody-assets/textures/`.

Private extracted game data stays under `artifacts/` and is not for redistribution.

## 6. Solver (offline)

`src/body_ik.hpp` (`d2vr::body::BodySolver`) is pure math. It takes the HMD eye pose, both
`hand_attachment_jnt` targets and the controller grip/trigger values, and outputs world
transforms for the 20 body joints and both 69-joint arms. Arm frames equal
`center_spine03_jnt`.

- **Calibration.** Avatar scale = eye height / `camera_jnt` height (1.70 m). The arm
  segments then fit the measured T-pose hand span, between 0.85× and 1.2×.
- **Head.** Exact. The neck carries half of the head's rotation.
- **Torso.**
  - Yaw uses `rig::ChestYaw`: a 40° dead zone with a pull from both hands.
  - Standing still with a small offset, the pelvis stays over the feet and the spine leans
    (spread 0.2/0.45/0.7/1.0). When the player moves or reaches far, it follows the neck.
  - A crouch lowers the pelvis by 0.62× the drop and pushes the hips back by 0.35× the drop.
  - The pelvis is capped at full leg reach. The neck may sit under the head, which is
    invisible because the mesh is headless.
- **Arms.** Clavicle shrug for far or high reaches; `rig::boneworks_elbow_pole` with a rate
  limit; two-bone IK with up to 6% visible stretch, then the hand stops short. Rotations map
  the bind hinge frame onto the solved one. Forearm roll is spread over `lower_armhelper1-3`
  and `wrist_rotation`; the wrist is exact.
- **Native arms reused.** `Inputs::native_arm` takes the game's own animated 69-joint arm
  pose (arm-model space). The whole hand subtree keeps the native local transforms: fingers,
  palm and muscle helpers, `hand_attachment`. The solver modifies only the clavicle, upper
  arm, elbow, forearm twist and wrist placement, as `rig::pose_arm` does in the mod today.
  Captured poses (`sword_idle`, `capture_23072`) are exported into the fixture by
  `tools/gen_rig_fixture.py`.
- **Soft IK.** Near full reach the elbow straightens along an exponential (soft zone 8% of
  the arm length). The hand stays exact through at most 6% bone stretch; beyond that the
  wrist stops short with the same arm shape. This removes the two-bone singularity pop and
  the forearm-twist jump it caused. Forearm roll is unwrapped frame to frame.
- **Ring.** The ring mesh sits on the middle finger's proximal segment in bind, but is
  skinned to `helper_thumb_1_l_bot` (a thumb muscle helper; Emily also uses the
  `index_end` accessory leaf). So it drifts whenever the thumb moves relative to that finger.
  Thumb flesh carries up to 0.69 weight on the same helper, so moving the joint is not an
  option. The pack re-binds only the ring surface rigidly to `medium_02`
  (`weights_native`/`joints_native` keep the originals). In game this needs the ring
  surface's skin indices patched, or its draw hidden.
- **Fingers (fallback, no native pose).** Each finger joint curls about its own flexion axis, taken from the T-pose bind
  (palm −Z). `grip` drives the middle, ring and little fingers, with the little finger curling
  most; `trigger` drives the index; the thumb folds toward the middle knuckle. A hand at zero
  input is slightly relaxed, not flat. Angles are in `finger_local`.
- **Legs.** Procedural steps, one foot at a time; the trailing foot may lift after 60% of the
  other foot's swing. The trigger distance is 0.16 m, dropping to 0.10 m at speed, or a 35°
  turn. Each step lands ahead of the predicted support point, with swing time plus half a
  stance of lead. Speed comes from the HMD, never from the pelvis. A squat pushes the hips
  back without moving the feet. Knees bend forward along the foot heading, and feet stay flat.

`tests/body_ik_check.cpp` runs on the real rig (`tests/player_rig_fixture.hpp`, generated by
`tools/gen_rig_fixture.py`; build with `artifacts/claude/fullbody/body-ik-check.cmd`). It has
11 scripted scenarios at 90 Hz: T-pose (fists), idle, look down, crouch, reach up, cross-body,
a 180° turn, a 1.4 m/s walk, lean forward, aim and peek.

- It checks that the head and hands are exact, the arms stay attached, the leg lengths hold,
  planted feet stay on the floor, the knees point forward, and no joint rotates more than
  12° per frame. **All pass.**
- `python tools/render_body_ik.py <frames.json> <dir>` renders front, side, three-quarter and
  first-person eye views; `tools/render_hand_closeup.py` renders thumb-side, back and palm
  close-ups. Galleries: `artifacts/fullbody-ik-render/index-v3.html` (native hands; Corvo and Emily).
- v2 fix: the pack builder stored each surface's triangle start as the surface number, so
  the hand skin was drawn over sleeve and shoulder triangles ("skin through the clothing").
  Fixed in `merge_surfaces`.

Not yet done: per-foot ground height (stairs), toe or heel roll, hand-tracking fingers,
and running the full-body solver in the game. Native torso blending is implemented
and checked offline, as described below.

## 6b. Blending with the game's own animations (jump, finishers, assassinations, mantle)

**Player state machine. VERIFIED (live, read-only, `tools/player_states.py`).** The FSM
component (vtable `0x1c73d58`) is in `player+0x268`. Its state pool is at `+0x90`, state N
at `pool+0x10+N*8`, with index at `+8`, owner at `+0x10` and an active byte at `+0x20`.
The 80 states include:

| Index | State | Index | State |
|---|---|---|---|
| 2 | Jumping | 3 | Falling |
| 4 | CatFall | 7 | Sliding |
| 8 | Mantle | 9 | Vault |
| 13 | Assassination | 14 | Stun |
| 15 | Dead | 16 | KnockedDown |
| 17 | Stumble | 24 | Peeking |
| 31/32 | Versus/VersusGrab | 33 | SyncAnim |
| 38 | NpcFatality | 41 | Choke |
| 42 | DropChoke | 45 | CarryCorpse |
| 55 | BloodthirstyFatality | 83 | KillCam |
| 85 | Crank | 87 | EatRat |

**Ownership policy** (`native_ownership(state)` in `src/body_ik.hpp`). Each region (legs,
torso, left arm, right arm) gets a native weight:
- **Full-body authored moves** (assassination, choke, fatalities, versus, sync anims,
  knockdown, death, stun, stumble, kill cam, crank, keyhole, mantle, vault): everything native.
- **Airborne** (jumping, falling, cat-fall, slipping): legs native; torso and tracked hands stay VR.
- **Sliding:** legs native, torso 0.6.
- **Carry corpse:** arms native, torso 0.3.
- **Eat rat, heart, stone shroud:** arms native.
- **Everything else:** VR.

**Blending math.**
- Each region's weight is a linear progress eased by smoothstep (C1). Blend-in is 0.1–0.2 s;
  blend-out is slower, 0.2–0.35 s. Arms take 1.6× and fingers 2.6× longer, because a tracked
  hand and an authored hand can differ by up to 150°.
- The native pose is anchored so its `camera_jnt` is the HMD eye. In game the eye already
  follows the native camera animation, so an authored kneel keeps the feet on the floor.
- Order of operations:
  1. Solve the VR torso, then blend it (pelvis in world, spine and neck in local space).
  2. Solve the VR legs from the final pelvis, then blend them in local space.
  3. Solve the VR arms from the final chest, then blend them in local space.

  The head stays HMD-exact. Local-space blending keeps bone lengths.
- While native owns the legs, the planted feet migrate toward the native feet in proportion
  to the weight. A foot that is mid-swing keeps its step and only its landing point moves.
  VR stepping therefore resumes after a landing with no slide or snap.
- The last native pose is cached, so a blend-out completes even if the source disappears.
- Two-bone soft IK and continuous reach caps avoid singular pops.

**Checks** (`tests/body_ik_check.cpp`): scenarios for a walk with a jump and landing, a
full-body assassination (camera follows the clip), and a mantle onto a 0.9 m ledge. They use
synthetic authored clips on the real rig, with the captured native idle arms as the clip
rest pose. They verify:
- the head is exact and the hands are exact whenever VR owns them;
- no joint rotates more than 12° per frame through every blend-in and blend-out;
- no pelvis jump over 6 cm per frame;
- planted feet do not slide after landing;
- leg lengths hold.

These pass with native sword-idle hands, the second capture, and procedural fingers.
Renders: `artifacts/fullbody-ik-render/v4-blend/*-strip.png`.

Real decoded assassination, vault, fatality and choke clips are now included in
`tests/player_clip_fixture.hpp` and exercised by `tests/body_ik_check.cpp`.
Pending in game: reading active states each frame plus the native body and arm
upload as `NativePose`. Offline clip checks do not verify the runtime ABI or headset quality.

## 6c. Regression fixes and integration contract (2026-10-10)

The current live v205 path uses `ArmTracking`; it does not invoke `BodySolver`.
Full-body solver improvements therefore require an explicit native renderer
integration before they can affect the headset. Existing locomotion/turning is retained.

- `BodySolver::reset()` clears feet, velocity, chest heading, authored blend sources
  and paths. Call it on reference-space/pose epoch changes, native player or model
  replacement, and teleport. Reusing old feet after those changes is invalid.
- Solver input validation runs before temporal state mutation. Skeleton vectors and
  parents, calibration, tracked transforms and native palette counts must be valid.
  An authored action requesting body/arm ownership requires those native palettes;
  it must not silently receive procedural hands. Catch invalid input at the renderer
  boundary and retain the original native upload.
- The wheel regression is in the production caller: only gameplay logic `2`
  publishes hands, while the wheel renders the avatar at logic `256`. At 250 ms
  `ArmTracking` rejects the stale pose, and the uploader submits complete native
  arms. Publish fresh valid controllers whenever the world scene renders, including
  world UI, while retaining separate gameplay input gates. A test verifies that a
  fresh wheel-period publication moves the palm and preserves hands-only suppression.
- `authored_eye_camera(native, centre, eye, units)` in `body_anchor.hpp` preserves
  native first-person position and full pitch/yaw/roll and adds only physical stereo
  eye separation. It deliberately excludes roomscale/crouch/head rotation. The ordinary
  level `BodyAnchor` and `to_game_pose` path is unchanged. Use the same paired native
  centre source for both eyes and a verified authored action discriminator. Generic
  camera `mode/target_mode/blend_body_yaw/code_lock` is broader than a finisher state.
- `authored_fov_angle(angle, 1.05)` provides a modest signed-angle tangent expansion.
  Apply it consistently to the rendered frustum and submitted XR FOV; widening only
  the native render projection misstates the image to the compositor. This follows
  the [OpenXR projection-view contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerProjectionView.html).
  FOV policy is not active in the live build until the owning caller integrates it.

Native ownership facts remain scoped: the 69-joint arm upload is validated; the
20-joint body consumer and material/draw visibility still need runtime proof.
The public [id Tech IK implementation](https://github.com/id-Software/DOOM-3/blob/master/neo/game/IK.cpp)
is a mathematical reference, not evidence of Void's private ABI. Do not enable
third-person body rendering or replace unqualified native allocations to bridge this gap.
The offline solver caps arm stretch at 6% and can miss controllers beyond that reach;
this still needs calibrated reach policy and headset validation before claiming exact
full-body hand placement at every physical reach.

Focused MSVC `/W4 /WX` checks passed: body anchor (tilted authored camera/IPD), body IK
(real rigs and clips, invalid inputs, epoch reset), arm tracking, arm body and arm palette.
No live game control, candidate build or deployment was performed by this helper.

### Guarded native body upload helper

`src/body_renderer.hpp` now provides `rig::body_upload(read, base, owned_arm,
body_bind, model, source, count)`. It accepts only the currently owned player body
model, its started animation and exact 20-joint skeleton, with the same semantic
names/inverse binds and identities as `native_body_bind`. State 3 must contain a
contiguous 20-entry remap, and the incoming source must equal the native transform
list plus that remap's first index times 64. The reader copies 20 rigid transforms
and the model frame and rechecks ownership, list metadata and remap afterward.
It rejects a 69-joint arm, foreign model, recycled owner and malformed source.
This is a guarded extension of the known upload contract, not evidence that a
live body call has yet traversed the hook.

`BodyPalettePublication` stores one owned 20-transform copy with epoch/tick.
`palette(owner, epoch, tick, native_authored)` returns nothing on authored actions,
owner/epoch mismatch or poses older than 250 ms. Call `body_upload` at the native
upload boundary before looking up a replacement. Use a caller-owned aligned copy
for the single original call; preserve original source on any failure. `clear()`
belongs to native owner/reference epoch invalidation. Scale is conservative: only
rigid model-space palettes are accepted; native attachment/skinning scaling is not
assumed from offline export conventions.

`tests/body_renderer_check.cpp` passes MSVC `/W4 /WX` with owned 20-joint fixture,
foreign model, wrong source/count, noncontiguous mapping, nonrigid transform,
invalid model scale, recycled identities and authored/stale/epoch publication tests.
The test verifies that source memory remains unchanged.

Visibility remains distinct from posing. Emily's `player_body_hidden.material`
uses `invisible 1` plus shadow-only main/shadow/z-prepass programs; Corvo's hidden
material uses `arkDefault` and a constant grey diffuse map. A universal material
flag change is therefore invalid. Surface `+0x28` points to its material, which may
be shared; a future override must scope the exact owned body surface/draw and
restore it without changing global material objects. `showInRenderMode=false`
appears under the body declaration's editor block, so it does not establish a
runtime first-person visibility gate. The observed model `+0x1c30` bit `0x10`
is material-derived renderer eligibility, not a standalone body visibility toggle.

Fresh read-only Ghidra exports establish the route:
`artifacts/body-render-model-flags.txt` indexes native flag accesses, and
`artifacts/body-render-model-visibility.txt:15` shows `0x2370e0` recomputing high
flags from model draw materials. Bit `0x10` derives from each material's
`+0x4b & 0xc != 4`; `0x2b3dc0` filters models on this bit (`:157`).
`artifacts/body-material-route.txt` shows `0x25ed30` selecting a model-local
material override from `model+0x18a0/+0x18a8` when nonnull, otherwise selecting
the surface material at `+0x28` (`:112`). It fills model-owned draw records
behind `model+0x1c00`, stride `0x80`, material pointer at offset zero, then calls
`0x2370e0`. A raw surface pointer swap alone can therefore leave stale draw
records/flags, and copying arm model flags bypasses the derivation.

`body_material_route(read, base, owned_arm, body_bind)` now inventories this
exact owned route without writes: bounded draw/override counts, current pointers,
model flags and a rechecked snapshot. Its fixture test verifies the single body
draw and rejects oversized records. A production override must use the model-local
slot plus the native refresh contract. The setter/allocator route is now statically
verified (below); material lifetime and safe runtime call scope still require proof.

### Native model-local material setter (static ABI verified)

New read-only exports: `artifacts/body-material-setter-index.txt`,
`artifacts/body-material-vtable.txt`, `artifacts/body-material-setter-decomp.txt`.
The skinned-model vtable `0x1b0d628` has `+0x58 -> 0x2367b0`,
`+0xb8 -> 0x25ed30`, and `+0x78 -> 0x25f3c0`. Native `0x2367b0` is
`void(model, materialPointer)`: it clears/reallocates the model-local override
list using the engine allocator and appends the pointer once per cached draw.
`0x235990` is `void(model)` and refreshes by calling the model's `+0xb8`
rebuild and `+0x78` bounds/attachment update.

`BodyMaterialOverride::apply/restore` is a reviewable guarded transaction using
caller-supplied native callbacks, callable only from an exact owned renderer scope.
It requires the proven vtable slots, one body draw and at most one original override.
`body_material_candidate` selects an already loaded body material from this player's
owned arm draw records, with exact character resource/material names. It rejects
cross-character or global candidates. Repeated application is idempotent; apply
verifies the rebuilt draw material and eligibility flag. Restore checks the same
owner and only restores if its replacement remains present, preserving foreign edits.
No destructor calls the game. Shared material objects and surface resources are untouched.

Restoration is semantic: an originally empty override list can become one null
entry through the native setter, which selects the same original surface material.
It is not byte-for-byte list metadata restoration. The test mocks this native route,
checks single application and refresh, restoration, foreign-edit preservation,
cross-character rejection, and unchanged source material bytes; `/W4 /WX` passes.

This does not prove headset visibility. `0x2b3dc0` also requires `model+0x18bc`
bit zero to be clear. That gate's meaning/safe ownership has not been established;
do not toggle it to bypass culling. The full-body solver remains separate from this
material transaction. Native body upload receipts and actual eye pixels are the
next runtime evidence required before enabling body posing or claiming visibility.

## 7. Live findings (Emily save, read-only scans plus reversible single-pointer tests)

- **Surface material.** The body mesh surface's `idMaterial*` sits at `surface+0x28`, with
  its name `char*` at `material+0x8`; geometry (`idTriangles`) is at `+0x90`.
  `tools/scan_body_material.py` and `tools/rtti_fields.py` find it. The arms' loaded
  `female/emily/textures/emily_body.material` carries the same body textures.
- **Material swap alone shows nothing.** Swapping in that material, looking down in flat
  first person, showed no body. Something else gates the draw.
- **Flags differ by one bit.** The body and arm `arkSkinnedRenderModel` objects differ in only
  two small fields: `+0x1fe0` is the joint count, and the flags at `+0x1c30` read `0xb424`
  for the body and `0xb434` for the arms (bit `0x10`). The test that set this bit together
  with the swap was **inconclusive**: the game was in the pause menu during the capture.
- **That bit is `m_mayBeDrawn`. VERIFIED (static).** The render model's reflection table
  (registration around `0x141533cfc`) names the bitfield at `+0x1c30`: `m_unlinked`,
  `m_needsUpdateInView`, `m_shouldBeGathered`, `m_needsReferences`, **`m_mayBeDrawn`**
  (setter `0x140990d80` writes bit `0x10`), then `m_mayCastShadows`, `m_hasEmissiveMaterial`.
  - The setter is referenced only by that reflection table, which applies the
    declaration's `renderModelInfo` at model creation.
  - The model teardown (`0x140233ff0`) clears the bit.
  - No other code sets or clears it (byte-pattern search for and/or/test on `+0x1c30` bit
    `0x10`), so the game does not re-hide the body every frame.
  - Expected recipe for a visible body: set `m_mayBeDrawn` (`model+0x1c30 |= 0x10`) and
    point the body surface's material (`surface+0x28`) at the textured body material. This
    still needs one in-game confirmation in gameplay.
  - Tooling: `artifacts/claude/fullbody/ghidra-decomp.cmd` runs read-only headless Ghidra;
    outputs are in `artifacts/claude/fullbody/re/`.
- **Cvars.** Writing `pm_thirdPerson=1` (object at `+0x39a3390`; int at `+0x28`, float at
  `+0x2c`, string at `+0x10`) had no visible effect. `Ansel_PlayerVisible`'s cvar object is
  at `+0x39a52e8`.
- **Restored.** Every test write was restored; the scripts confirmed it.

## 8. Next experiments, ranked by how much they block

1. In gameplay, looking down: set `m_mayBeDrawn` and swap the body material
   (`poke_body.py`-style, reversible), and confirm the body renders. The flag is identified
   statically; only the visual confirmation is left.
2. **Live, read-only:** log every `0x25efe0` call `(model, skeleton+0x78)`. Is the 20-joint
   body uploaded every frame through this seam?
3. Integrate `BodySolver` behind a `build/full-body` knob. Feed it the mod's tracked head,
   hands and grip/trigger, upload the body and arm palettes, and render with the swapped
   material. Verify in simulated eye captures first.
4. **Headset (needs the user):** extend `build/xr_probe.exe` to query `supportsBodyTracking`
   and sample a few seconds of joint data under VDXR on Quest 3.
5. **Units and floor:** measure 1 native unit against a known in-game length, and take the
   floor from the stage space.
6. **Torso and arm attachment** through crouch, lean, mantle and for both Corvo and Emily.
