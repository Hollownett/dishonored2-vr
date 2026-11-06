# VR v1023 (2026-11-05) — the crossed weapon rides the hand that holds it

The v1020 headset report: weapons no longer flicker, but the **non-native** hand still lags — the sword in
the left hand, or the gun/crossbow in the right hand, trails the hand by a couple of frames, while the
same-arm (native) weapon in the other hand is exact. v1023 makes the crossed weapon use the same pose
source as the native one.

## Why the crossed item lagged

With mirrored dual wield the engine's item for one arm is composed from the **other** arm's hand. The
mod writes that hand into the destination arm's palette joint 59 (`NativeAttachmentPose::cross_pair`).
Before v1023 that cross was built from:

- the **origin arm's palette joint** (`origin.joints[59]`), i.e. the last upload's pose, not the pose the
  render draws, and
- the two arms' **published model frames** (`origin.source.model_frame`, `destination.source.model_frame`),
  chosen in v776 because the origin arm's *live* frame can be one game frame behind its sibling's.

A same-arm (native) item never goes through that conversion: `advance_attachment` gives it the origin
hand's **drawn image** joint (`full_body.image_world(side,59,frame)`) moved on by this tick's hand motion
(`full_body.item_advance(side)`), in the arm's live model frame. That is why the native side is exact.

The v1020 live status measured the published-frame disagreement: `dual_wield.cross` peak **160.4 mm**
(`2506` cross substitutions, `180` skipped, `142` cross substitutions). At hand speed that is several
frames of motion — the reported lag.

## The change

`src/native_attachment_pose.hpp`, `cross_pair`, joint 59 only, cross mask set, live arm frames readable:

1. Ask the **same** advance callback a native item uses, for the **origin** physical side:
   `advance(origin, origin.source, origin.joints[59], 59)`. That returns the origin hand's drawn image
   joint moved on by this tick's `item_advance`, in the origin arm's live model frame.
2. Reconstruct its **world** pose from the origin live model frame (`rig::ModelFrame(from).world_point /
   world_direction`).
3. Convert it into the **destination arm's live model frame** (`rig::ModelFrame(to)`) — the frame the
   engine composes the item in — and apply the existing weapon mirror correction
   (`power_hand::cross_joint_both` with `cross_attachment_[destination]`).

Nothing else changes. When the advance callback has no image or no motion (image skin off, teleport,
missing track data), or for any joint other than 59, the v776 conversion runs unchanged.

The item therefore takes the hand pose the hand is **drawn** with, from one live frame, exactly like
the native item in the other hand.

## Build and checks

- `build/d2vr_render_probe_arm_v1023.dll`, 14,905,344 bytes, SHA-256
  `ebae8c224da60b84562b510be89658396115ecd1727e374135463f079e3a027a`, built by
  `build/compile-v1023.cmd` (`dll_exit=0`).
- `native_attachment_pose_check` PASS (compiled from this source; it exercises the native/fallback paths),
  `object_motion_check` OK, `weapon_draw_place_check` OK, `shoulder_carry_check` PASS,
  `grip_decision_check` PASS, `held_release_check` PASS, `shoulder_carry_routing_check ok`.
- Launcher: `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1023.exe`, 83,183,600 bytes, SHA-256
  `17673487b44bbab41987ec6a20527f1e54595182e40ed5e4b34c18c77aa14385`; publish printed
  `Embedding d2vr_render_probe_arm_v1023.dll from repair-v1023-test-candidate.json`. A headless
  `--page play --screenshot` run installed v1023 as the only probe DLL in the mod home.

v1023 also carries the worktree's v1022 (shoulder carry stays on the shoulder — auto-drop opt-in,
held Use kept 700 ms), v1021 (VR stays in VR across a render-resolution change) and v1020 (the weapon
rendered as a normal engine object), plus the v1018/v1019 weapon publisher code.

## What to test in the headset

1. Mirror the loadout so the sword is in the **left** hand and hold it next to the wall/guard: move it
   slowly, then fast, and compare it with the same-arm weapon in the right hand. Expected: both track
   the hand the same way; no trailing couple of frames.
2. The gun and the crossbow in the **right** hand (the other cross direction).
3. Both eyes; the v1020 flicker fix must stay (the weapon is still drawn as a normal object).
4. If a crossed weapon still trails, note which hand and whether `dual_wield.cross` peak is still large;
   the fallback path (no image/motion) is the only way the old code can run.

## Not verified

No headset run of v1023. The change is proved by compilation and the attachment-pose check's native and
fallback paths; the new world-hand path itself has no automated fixture, and only the headset can show
that the crossed sword/gun/crossbow now tracks like the native one.
