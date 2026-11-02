# VR v725 (test candidate, 2026-10-31)

The user tested v724: the dialogue choices now show in the game. Once they opened in the right place (below the middle of the view), and once far away. The boat ride camera was stable, but "the boat was turned around and went backwards".

## Dialogue panel placement
- Both boat-scene dialogues followed a cutscene (logic 32 at 93354796 and 93374890, then 512).
- The v722 speaker placement uses the last Talk prompt's point in XR space, held for 30 s. A cutscene camera move in between breaks that mapping, so the panel stood at a stale point.
- Fix: any cutscene frame forgets the speaker point. The choices then open 1.6 m ahead and 0.2 m below eye height. They still stand at the speaker after a direct Talk with no cutscene in between.

## Boat ride heading
- v724 `dxgi-camera-probe.csv`:
  - During the ride the scene camera's yaw stayed at about 40 degrees while it travelled at about 164 degrees (6-9 m/s), a 125-degree difference.
  - The head moved freely, from -135 to +79 degrees.
  - The player's heading at boarding was 159 degrees.
- The VR comfort heading follows the scene camera's yaw, so the ride looked backwards.
- Fix (`TravelHeading`, cutscene_view.hpp):
  - A comfort cutscene view that moves at 3 m/s or more for 1 s faces its direction of travel (EMA velocity, tau 0.5 s), with one re-aim.
  - A cut resets it.
  - Slow camera moves, such as a dolly around a character, never reach the threshold.
- The ride's height was steady this time: no per-frame jumps in the raw camera.

## Checks
- These pass:
  - `cutscene_view_check` (travel heading: a 7 m/s ride faces travel after 1 s; a 1.5 m/s dolly never does; reset);
  - `anchor_smoother_check`;
  - `world_ui_pose_check`;
  - `vr_combat_check`;
  - `native_world_ui_check`.
- The DLL builds and includes the dual-wield session's code, behind `build/dual-wield` (off).
- Not headset-tested.
