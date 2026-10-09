# Native hand references and player calibration

The controller palm mapping follows the OpenXR grip axes: forward runs from the
little finger toward the index finger, and the palm normal points away from the
left palm and into the right palm. The palm centre is currently estimated from
the native wrist and four MCP knuckles; headset physical alignment still needs
acceptance. [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1-khr/pdf/xrspec.pdf).

Finger controls blend the current native authored pose toward captured closed references, including the
metacarpals and muscle helpers. Trigger can drive the index separately from
squeeze. This follows the pose/reference approach used by
[Godot XR Tools](https://godotvr.github.io/godot-xr-tools/docs/hand_poses/) and its
[grab-point pose overrides](https://github.com/GodotVR/godot-xr-tools/blob/master/addons/godot-xr-tools/objects/grab_points/grab.gd).
There is no generated fist fallback: missing or mismatched references retain
native finger animation. This state must be displayed honestly in the VR UI.

## Capture the authored reference poses

Capture each side separately if the game cannot display both empty fists at
once. Obtain all 69 unmodified native model-space transforms before mod wrist,
finger or avatar retargeting. Capture an authored empty open hand and an authored
empty fist with the same character and exact native inverse-bind rows. Include
native hand helpers; a weapon-holding pose is not an empty fist. Preserve native
screenshots or equivalent visual evidence and record the specific native action
or animation responsible for each pose.

The exporter consumes four read-only rig reports with `body_samples[0].arms`,
verified names/parents, `sampled_joints` and stable `skinning_layout` data. A JSON
manifest next to those reports has these four keys: `left_open`, `left_closed`,
`right_open`, `right_closed`. Each entry has this shape:

```json
{
  "report": "left-closed-readonly.json",
  "unmodified_native_palette": true,
  "authored_pose_confirmed": true,
  "authored_state": "Describe the native action/animation and empty-hand pose",
  "visual_evidence": "left-closed-native.png"
}
```

These labels are capture provenance, not automatic visual proof. The exporter
also rejects identical endpoints, changed executable/hierarchy/inverse binds,
non-rigid transforms and changed main-bone offsets.

```powershell
python tools/export_native_hand_poses.py --manifest artifacts/research/native-arm-assets/hand-capture-manifest.json --output artifacts/research/native-arm-assets/native-hand-poses.bin
build/arm_mesh_simulation.exe artifacts/research/native-arm-assets/mesh-fixture.bin artifacts/research/native-arm-assets/native-hand-poses.bin
```

The exact native data remains private/ignored. Inspect deformation by surface,
mixed thumb/index edges and every curl endpoint before enabling the file. Then
test the result in the headset with open hands, partial grip, fists, wrist roll,
trigger isolation and native weapons. Offline replay cannot certify those views.

`read_native_hand_references(stream)` loads both references. Install each with
`ArmTracking::install_reference(side, reference)`. Matching verifies the current
inverse-bind rows, hierarchy and named finger chains before each pose applies.
Set `ArmTrackingFrame::independent_index=true` and publish `index_curls` from
trigger separately from grip `curls`. Enable each `empty_hands` bit only with
exact native empty-hand occupancy evidence. It defaults to zero: native pistol,
sword and other occupied-hand poses survive every squeeze amount. Zero curl
always retains the current native authored pose; partial squeeze smoothly starts
from that pose. Controller squeeze alone does not establish empty-hand state.

## Explicit player calibration

Use a visible paused calibration wizard. Tell the player to stand comfortably,
look forward and extend both arms sideways at shoulder height. Begin explicitly
with A or the calibration entry, then squeeze and hold both controller grips
closed until the visible stable-hold progress line fills. Do not
automatically capture the first gameplay frame.

`native_avatar_measurements(bindings, uploads, units_per_metre)` measures the
verified bind-palm span and upper-arm root separation. Feed those dimensions to
`AvatarCalibration::begin(reference_epoch, measurements, optional_floor_y)`.
Observe the raw XR head/controller poses, tracking-valid flags, both raw squeeze
actions (at least 0.8 on each controller), and reference epoch before settings
consume the controls. Calibration requires at least 20 stable samples over one
second and rejects crossed, uneven, moving or untracked hands. Releasing either
grip, losing tracking, leaving the valid T pose, movement outside the stable
window, or a sample interruption resets the progress and displays the reason.
The line reports that actual accepted window, rather than an independent timer.
Paused settings consume the squeeze actions so capture does not open the native
weapon selector. B/Y, gameplay or focus exit cancels capture; a same-space retry
keeps the prior accepted fit until a new complete hold replaces it.

The result contains player palm span, proportionally fitted shoulder width,
measured eye-to-shoulder drop and arm-length scale. Standing eye height is only
available when a measured floor datum was explicitly supplied. XR LOCAL origin
is never assumed to be the floor. Calibration is invalidated by a reference-space
epoch change.

Configure `ArmTracking` with the result and publish the reference epoch, level
body/head anchor and explicit world units per metre in the same owned frame as
the grips. Calibrated shoulder roots follow that level body frame; head
pitch/roll cannot steer the shoulders. Limb/sleeve lengths fit the measured reach,
hand geometry retains native size, and extra reach is bounded at eight percent.
This is a first measured arm fit; wrist/palm alignment, sleeve/body continuity
and physical headset comfort remain acceptance checks.

## Current verification

MSVC `/W4 /WX` isolated checks cover native reference parsing, authored endpoint
blending, helpers, independent index, parent-local geometry, explicit stable
calibration, epoch rejection, limb scaling and mixed-rig rejection. Python checks
cover exporter provenance and rig mismatch rejection. The existing private
native mesh with no approved fist references changes by less than 0.000002 native model units at
all requested curls; `finger_pose_applied=false` explicitly records that these
checks exercised safe fallback, not a natural fist.

## Native occupancy and source capture

`native_hand_state(read, image_base)` reads the player's owned inventory and
per-arm player locomotion graph. Right inventory slot is 0, left is 1. The
player graph's left/right equip-name lists are at +0xa0/+0xb0; generic
`arkDeclLocomotion` fields do not describe this graph. A null slot and exact
`Empty` name are both required. `NativeHandOccupancy::observe` requires 20
samples and one uninterrupted second, revokes on any mismatch, and resets on
ownership, epoch, focus or sample-gap changes.

Publish its `NativeHandState`, eligible bits, sample time and reference epoch
together. In the owned arm upload callback, first verify the current rig, then
call `native_empty_side(read, image_base, state, eligible_bits, verified_arm)`.
It returns only that arm's freshly revalidated eligibility bit and cannot
create eligibility. Reject old publications (maximum 200 ms), wrong epochs,
unfocused gameplay and session/rig transitions before invoking it. This final
read revokes cached eligibility when a native equip event occurs between
publication and upload. It does not install or certify a fist reference.

The installed native configuration maps `PAD_X_SQUARE` to `INTERACT`,
`PAD_LTHUMB` to `SPRINT`, and `PAD_LSTICK` to `MOVE`. The
[Bethesda Dishonored 2 Xbox manual](https://assets.ctfassets.net/rporu91m20dc/4wyjsRJV32cmKEcymOquUI/43ec13c9e82351efbc591dd33832f262/manuals_dishonored2_xone_en_pl.pdf)
identifies holding X as sheathing weapons. To investigate authored endpoints,
use native input to holster, wait in idle, then click the left stick while
moving forward. Empty sprint exists in the native player asset index; whether
it contains a usable fist must be established from the captured 69-joint
palette and private mesh, separately for each side.

Run the upload observer with tracked-arm writes disabled. A scoped reader can
capture current pose3 after sustained Empty/null state, including poses later
than the observer's initial source window:

```text
python tools/capture_native_hand_source.py --pid PID --seconds 8 --label empty-idle-then-native-sprint --output artifacts/research/native-arm-assets/hand-capture-idle-sprint-PID
```

Start idle; request native sprint around two seconds into this bounded capture.
The tool sends no input and opens the process read-only. It records focus,
ownership, eligibility and native skinning consistency. Requested action labels
are not pose certification. A tracked-arm run has already demonstrated the
expected large source/output mismatch and must not be exported as native hand
references. Before export, render the private mesh, verify native source/output
agreement, confirm at least four finger-chain flexions and thumb/helper geometry,
and attach the actual visual evidence to the four-endpoint manifest.

The unmodified v72 idle/holster/sprint trial captured 35 owned pose pairs.
Twenty-six matched the native renderer, five early pairs retained stale output,
and four pairs changed during the read. Comparing the matching 2.21-second idle
and 5.23-second sprint candidate preserved the exact hierarchy and inverse bind
and changed six left / four right main finger bones. Offline private mesh
replay nevertheless showed both hands remaining open. This trial supplies
native open-hand evidence and rejects sprint as a closed endpoint; no fist
archive was created from it. Bone-change counts alone cannot certify a fist.

## Next authored grasp sources

The installed index contains Emily's right-arm `sword_idle` and
`sword_parry_idle`, left-arm `gun_idle`, and bilateral `armattack_grabloop` /
`generic_emily_choke_loop`. These names identify candidates, not verified fist
geometry. The smallest next capture is a native weapon idle/parry grasp:
`PAD_RSHOULDER` maps to `BLOCK`; observe what the engine actually equips when
this action is used from holstered state. A weapon grip must remain labelled
as an authored object grasp unless mesh inspection demonstrates a usable closed
endpoint. Mantle is lower priority because a ledge grip may remain open.

`capture_native_hand_source.py --include-occupied` records stable owned weapon
states as diagnostic sources after one second / 20 samples. It records their
real slots and graph names separately from sustained empty bits. It never
grants empty-hand runtime eligibility, certifies fist geometry, or weakens
exporter validation. Compare each source to the v72 open evidence with
`compare_native_hand_sources.py` and inspect both thumb placement and all four
finger tips before proposing a reference archive. Occupancy alone does not
identify a non-choreographed idle pose: mantle/takedown ownership must also be
respected when runtime overrides are eventually enabled.

The [Godot XR Tools hand pose catalogue](https://godotvr.github.io/godot-xr-tools/docs/hand_poses/)
separates Grip, Grip Shaft, Pistol and other authored shapes and blends using
grip/trigger input. That supports named native endpoints and independent index
control; it does not supply a compatible Dishonored 2 skeleton or certify that
a captured weapon grip is a fist.
