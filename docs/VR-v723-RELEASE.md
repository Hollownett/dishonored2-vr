# VR v723 (test candidate, 2026-10-31)

The user tested v722: the dialogue choices are still not inside the game, and the boat cutscene is still extremely unstable ("the camera, or the boat itself?").

## Boat cutscene
- v722 log (`dxgi-camera-probe.csv`, every 6th Present):
  - The scene camera height alternated by about 0.7 m between samples.
  - The camera also jumped up to 1 m sideways in one frame.
  - Status:
    - `cuts` was 18; the last was 1.0 m and 1 degree.
    - `scene_speed_peak_mps` was 30.
    - `cutscene_untracked` was [0,0], so a cutscene view was open. The v722 untracked path was not the problem.
- Each 0.5 m+ sideways jump counted as a camera cut. A cut:
  - re-aimed the view at once;
  - restarted the height smoothing.
- The vignette also read the raw jitter as speed.
- Fix:
  - A cutscene view's horizontal position goes through `ScenePositionFilter` (alpha-beta, 0.04 / 0.001 per 60 Hz frame). It follows constant velocity with no lag, settles jitter, and restarts past 3 m from the prediction.
  - The height keeps the slow cutscene smoother.
  - `cut_jump_m` is 2 m (was 0.5 m).
  - The comfort vignette reads the filtered position.
- New diagnostic `cutscene-anchor.csv`: every cutscene frame's raw and held position (40000 rows).
- Unknown: whether the boat model itself jitters with its camera. If it does, a steady camera can show the boat moving instead.

## Dialogue choices
- v722 status:
  - `dialogue_frames` [595 stale, 0 fresh];
  - `world_ui_hook` rejects 0/0.
- No choice frame was ever captured, and the v722 gate was never the reason. Either:
  - the choice movie is never seen by the world-UI draw hook (0x2dd580), or
  - it is seen and the capture fails later.
- New diagnostics:
  - `world-ui-movies.jsonl`: every Iggy movie the hook sees while logic is 512, once each, with its file path and kind.
  - Status `dialogue_capture`, in order:
    1. scopes begun;
    2. target callbacks;
    3. not COM;
    4. immediate context off the present thread;
    5. redirect failed;
    6. redirected;
    7. Present: no image;
    8. Present: not completed;
    9. Present: older than 250 ms.
- No capture change beyond v722.

## Checks
- `anchor_smoother_check` (scene filter):
  - boat-like data at 7 m/s with 0.7 m height jitter and 1 m sideways spikes;
  - x lag < 0.1 m, height spread < 0.25 m;
  - restarts on a real cut and on a new epoch.
- `cutscene_view_check`: the 2 m cut threshold; a 1 m sideways jump is no cut.
- `world_ui_pose_check`, `native_world_ui_check` and `vr_combat_check` pass.
- The DLL builds.
- Not headset-tested.
