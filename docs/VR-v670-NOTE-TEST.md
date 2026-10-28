# v670 note repair test candidate

The user requested this build for their own headset test. This is a test candidate, not a claim that the rejected v662 feature now passes headset acceptance.

## Implemented repair

- Admit the observed native reader states16 (world pickup) and144 (journal Read). Keep native page ownership checks.
- Accept the observed native draw recording clock, including same-counter recording.
- Retain three immutable pose publications so completed draws from the preceding image survive publication of the next pose. Require executed receipts for both original hand surfaces and the completed eye pair before composition/contact.
- Reuse the existing native body and arm meshes, materials and shaders, with the shared calibrated body solver. No second hand color renderer.

## Evidence and limits

v662 was the actual loaded DLL in the failed headset test. It prepared242 palettes but bound zero native draws. This was a native consumer failure, not evidence that an old DLL was launched.

The repair was traced in the actual game with simulated tracking through v669: original IA/palette resources matched, both hand draws executed, completed poses were accepted, and final projections were captured for both eyes. That retained run was journal logic144; its changed final pixels and pose hashes do not establish headset visual quality or world-pickup success.

The final v670 DLL builds with /W4; ten focused /W4 /WX checks pass. The extra v670 changes are diagnostic capture scoping, a non-note fast gate and simulated-only test movement. Actual v670 gameplay was reached through Steam. World pickup, touch/scroll, close cleanup and headset appearance remain unverified. Full repository certification was not rerun.

Evidence: `artifacts/note-native-runtime-v669/evidence-summary.json`, `artifacts/note-native-runtime-v670/checks.log`, and the source-provenance file beside the numbered DLL.

## User test

1. Start the numbered v670 launcher with normal saved settings.
2. Pick up the map/note in the world, matching the rejected journey.
3. Check that the existing body and arms remain continuous, with one pair of hands. Move and rotate the right wrist and fingers; check that its visible surface follows.
4. Check page contact/scroll and the ordinary stick scroll. A static page alone is not a passing result.
5. Close with B, then check normal body visibility, walking and turning.

The diagnostic game is closed. The original game ini, game configuration and all save files were restored from the pre-test backups and hash-verified. Test snapshots remain under artifacts. No headset was launched for this handoff.
