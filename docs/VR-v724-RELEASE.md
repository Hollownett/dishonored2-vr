# VR v724 (test candidate, 2026-10-31)

The user tested v723: the dialogue choices still showed head-locked ("theater"), and the boat cutscene camera still jumped when sitting down, with the boat looking raised above the water.

## Dialogue choices
- v723 `world-ui-movies.jsonl`: under logic 512 the world-UI draw hook saw exactly one movie, `ui/narrative.iggy`, never `ui/choice_*.iggy`.
- `dialogue_capture` was [0,0,0,0,0,0,607,0,0]: no scope ever began, because the owner lookup gave `narrative.iggy` no kind.
- Fix: `narrative.iggy` is a journal-kind world panel (the dialogue panel), and `begin_world_ui` takes it only under logic 512. It also draws subtitles, so it stays out of the journal and black-market panels.

## Boat cutscene
- `cutscene-anchor.csv` (v723, per frame):
  - The ride's x/y are smooth from frame to frame. Only the game camera's height jumps, by about ±1 m every frame, in steps of about 3 cm. Those jumps don't correlate with the head pose.
  - Boarding: a 1.3 m shot change at the cutscene start. v723's 2 m cut threshold and horizontal filter slid the view across it for about a second.
  - Ride start: a 33 m cut. Its first frame sat 1.6 m above the shot's mean height, and the slow height smoother then sank the view from there for several seconds.
- Fix:
  - The v723 horizontal filter is removed, so x/y pass through.
  - `cut_jump_m` is 1 m, so the boarding change is an instant cut.
  - The cutscene height smoother has a warm start (`warm_frames` 60). After a reset it holds the running mean of the first second.
- Unknown: whether the boat model moves with the game's jumping camera height. If it does, the smoothed view can still show the boat moving.

## Checks
- These pass:
  - `anchor_smoother_check` (warm start; gameplay unchanged);
  - `cutscene_view_check` (1 m cut; the 1.3 m boarding change is a cut);
  - `native_world_ui_check` (narrative.iggy owner);
  - `world_ui_pose_check`;
  - `vr_combat_check`;
  - `native_menu_state_check`.
- The DLL builds.
- Not headset-tested.
