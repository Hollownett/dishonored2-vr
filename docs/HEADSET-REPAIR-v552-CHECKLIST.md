# v552 controlled headset checklist

This is a repair candidate, not release acceptance. Read its exact candidate manifest first. A blocked manifest must not be bypassed. Do not inject into the retained, disabled v551 process. Root coordinates one clean Steam restart after the build passes and the user is ready. Preserve saves, Steam Cloud, accepted transforms and current SteelSeries audio settings.

## Before gameplay

- Verify the executable and loaded numbered DLL hashes, frozen source provenance, complete required build and exact PID.
- Start passive `record_live_session.py` with an explicit3600-second budget, projection interval0 and the exact candidate manifest. Preserve both timing and boundary rotations until actual stop. Record mirror video in bounded segments, separately from clean performance windows; it has no audio or headset-compositor proof.
- Confirm the manifest's enabled features and explicit gaps. New native full-hand collision and weapon smoothing must not be assumed enabled merely because their pure tests exist.

## Ordered acceptance

1. **Choke:** release both right controls, show the game's exact native Choke hint, then hold only right grip. Require an unconscious guard, not a kill. Repeat with no native hint: no choke. Test ordinary body pickup/carry separately. Save exact hint-observer reason counters if it fails.
2. **Y Talk:** fresh Y on an offered NPC Talk/shop prompt reaches that exact NPC. Grip cannot talk. Y alone cannot lean or alter movement/turning; physical lean, crouch, menus and wheel quick actions still work.
3. **Both-hand grip:** test left, right and alternating fresh presses on doors, loot and loose objects; test full-fist pickup separately. Log delayed/ignored press Tick, hand, intended object and native outcome. Do not label every delay a frame-rate problem.
4. **Weapon ownership:** draw from hip, fire while the opposite hand uses an object, release/holster, and acquire objects with either hand. A plain object grip must never grant a new gun draw. Repeat wheel select, cancel, focus interruption and quick reopen. Check gun-in-hand/holster/prop overlap, native block restore and powers. Include rapid holster→wheel and holster→hip to test queued ordering.
5. **Shadow Walk:** activate, turn by real head/camera and by stick, move, swing to trigger the native attack animation, and exit. Confirm ordinary controls return; rat tunnel and authored finisher retain native ownership. Stop if the camera becomes uncomfortable.
6. **Performance:** capture matched no-video/no-readback gameplay at unchanged settings, then a separate video window. Review worst contiguous retained stalls, native GPU/compositor evidence and source gaps—not just median CPU submission time or a smaller validation-read count.
7. **Regression and stop:** reload twice, test audio and accepted locomotion/turning, then stop. Verify every hook drain, input restoration, body material restoration, pending audio state and faults before calling the exit clean. A pending/poisoned manual-equip queue blocks reuse of that resident runtime; hook disablement does not mean a native queued request was canceled. Require a fresh Steam process before another candidate.

Full native hands and tiny weapon smoothing require their own completed native ownership/consumer gates before a later enabled candidate can pass those milestones. A successful subset does not pass the complete VR iteration.
