# VR v697 (test candidate, 2026-10-30)

Headset v696 feedback:
- Fingers are smoother.
- The thumb still touches and sinks into the palm. It should sit turned a little to the side.
- With a power in the left hand, the fingers freeze in the power's pose.

## The thumb clear of the hand
- v696's guard tested only a narrow box under the knuckles. A thumb lying on the index root, or inside a folded finger, was beside that box and not caught.
- The thumb now keeps clear of the hand's own body (`ThumbBody`, finger_ik.hpp):
  - the palm: a 3 cm box under the skin, over the knuckle span, from the thumb's root to the knuckles;
  - each long finger's three phalanges, as capsules at the angles they are drawn with.
- The thumb's MCP-to-tip half keeps a 3 mm gap from the palm. It may lie on a finger (a fist), but not sink into it.
- A thumb that is not clear moves to the nearest clear pose (`thumb_clear_of_hand`):
  - The search is a 7x7x7 grid, then two finer grids. About 110 µs, only when the thumb collides.
  - Changes to the CMC cost half, so the thumb swings out to the side and keeps its curl.
- It runs in `finger_drawn_pose`, which `finger_turns` and `finger_turns_of_angles` both use.
- G5 free hand: a thumb buried in a surface took the nearest pose out of the shape and clear of the hand, instead of closing across the palm.
  - With no pose clear of both (a palm within a thumb's width of a table), the hand wins.
  - The thumb may then lie in the shape by up to 6 mm (`thumb_shape_tolerance`).
- `thumb_want`: the cap now fades out between fold curl 0.4 and 0.5. v696 faded it in from 0.3, which closed the half-grip thumb further.
- Lab:
  - "Thumb on the controller, half grip": the thumb tip moved from 30/82/-7 mm (on the index root, inside it) to 85/84/0 mm, beside the index.
  - 813 live hold snapshots: 0 with the thumb in the hand.
  - Wall sweep (finger_ik_check): 13–18 of 96 per hand were in the hand before; 0 after.

## Power hand fingers
- A left equip with no model in the hand is a power (`is_power_equip_name`: not a gun, gadget or the Heart; pose "Blink" seen live).
- For a power:
  - The body solver draws the hand procedurally (`hand_empty`), not with the authored power pose.
  - The finger solver treats the hand as free: touch curl, surface contact and palm stop.
- Status: `gadget_aim.left_power`.
- Knob `build/power-native-fingers` restores the authored pose.

## Build
- Built from HEAD (v696) plus only these hunks. Another session's uncommitted HUD/cure work in the working tree is not in this DLL.

## Checks
- finger_ik_check, gun_equip_name_check, finger_touch_curl_check, note_tablet_check, note_hand_pose_check, grip_pose_blend_check, finger_wants_stack_check and finger_snapshot_check pass.
- finger_touch_routing_check.py, finger_sphere_routing_check.py and finger_collider_routing_check.py pass.
- Not sim-tested or headset-tested.
