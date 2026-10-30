# VR v696 (test candidate, 2026-10-30)

Headset v693 feedback: the thumb still sinks into the palm when it meets something, and fingers touching things jump from pose to pose instead of bending smoothly.

## Smooth finger motion
- The drawn finger pose follows the solved pose through a critically damped spring on every joint (`FingerPoseFilter`, finger_ik.hpp; 70 ms smooth time).
- A finger bends, wraps and lets go over a few frames, and its speed never jumps. Upstream jumps are now spread over several frames:
  - a contact arriving, where the opening was instant before;
  - the memo, and the fingers taking turns at being solved;
  - the thumb rule.
- Hand lab, held prop jittered ±3 mm a frame:
  - largest per-frame joint change 39.6° → 6.4° (book);
  - 22.9° → 3.4° (folder);
  - 8.0° → 1.3° (spoon).
- Knob: `pose_smooth=<seconds>` in `build/finger-touch.cfg` (0 = off, at most 0.3).

## The thumb never in the palm
- `thumb_out_of_palm` runs on the thumb's final angles in `finger_turns` and `finger_turns_of_angles`, the one place every path passes. It applies to:
  - holds and the free hand;
  - notes and the sphere grip;
  - G5 least curls.
- The IP joint or tip counts as in the palm when it is inside the knuckle span, short of 3 cm past the knuckles, and below the palm skin. The pose then opens toward straight, to the most closed pose that is clear.
- Cause found: a free hand's thumb pressed into a wall was buried, so G5 closed it across the palm "until out, else fully". 26–29 of 96 wall placements per hand ended in the palm; now 0 (finger_ik_check).
- Live hold snapshots (2439): 3 in the palm before (lab criterion), 0 after.
- `thumb_want` ramps the thumb cap over a 0.1 fold curl instead of switching at 0.4, so the thumb no longer flips when a finger hovers at the threshold.

## Checks
- finger_ik_check, finger_touch_curl_check, note_tablet_check, note_hand_pose_check, grip_pose_blend_check, finger_wants_stack_check and finger_snapshot_check pass.
- Pre-existing failures, also on clean HEAD:
  - body_ik_check "reverse grip keeps the hilt";
  - /WX warnings in finger_sphere_check and hand_contact_check.
- Not sim-tested or headset-tested.
