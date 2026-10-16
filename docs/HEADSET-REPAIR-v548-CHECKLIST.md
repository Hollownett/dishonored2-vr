# v548 controlled headset test

Status: awaiting the complete required build and a fresh Steam process. The user approved restart and launch only after that build passes. No headset acceptance is claimed.

## Safety and identity

- Preserve v547 and the default candidate manifest. Never reinject v548 into the process retaining v547.
- Use the ordinary Steam game and separately verified save backup. Do not change Steam/cloud settings or restore over shared saves.
- Record the exact v548 DLL hash, source provenance, game PID, loaded module path and launch arguments.
- Stop and preserve evidence if gameplay crashes, movement becomes unexpected, or save behavior regresses.

## Short test order

1. Enter VR and load the existing checkpoint. Check that accepted movement, turning and gun placement remain unchanged.
2. Behind a guard, wait for the actual native Choke hint. Release the controls, then hold right grip. Confirm a nonlethal choke completes, not a kill. No hint must mean no new choke, even when behind an NPC.
3. Check that a body grab/carry remains separate and cannot turn into a choke during the same grip hold.
4. At a native Talk offer, press Y to talk and enter a shop where available. Check the contextual Y glyph. Grip must not initiate Talk. Check ordinary non-Talk lean and wheel/menu controls separately.
5. Hold a bottle/glass in the left hand, then holster the pistol. If a gun duplicate appears, hold that exact pose for ownership capture and identify whether the prop is still held. Visibility instrumentation is observation-only; this defect is not yet fixed.
6. Reload once and report whether VR returns. This does not establish repeated death/reload stability.

## Evidence limits

Offline tests and native-arena hook checks do not establish real HUD execution, headset visuals, nonlethal outcomes or comfort. Projection snapshots omit compositor quad layers. Timing telemetry does not by itself prove headset FPS. Legacy hand collision remains active; pickup difficulty and intermittent door delay are not accepted as fixed.
