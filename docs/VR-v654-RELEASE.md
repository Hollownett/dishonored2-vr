# v654 test release

The launcher includes the updated illustrated controls guide and enables hand-held notes by default. This is a prerelease; headset acceptance is pending.

## Controls guide

- Controller artwork based on a Touch Plus reference, with button indicators and 22 gesture illustrations for Corvo and Emily.
- Seven guide pages cover sword throwing, grabbing enemies, movement, weapons, bodies, menus and settings.
- Open Controls in the launcher, Controls & gestures in VR Settings, or squeeze the right grip at your right temple. Flick the right stick to turn pages.

## Hand-held notes

- Notes follow the left hand and wrist. Scroll with the right stick, or point at the page and hold the right trigger to drag.
- The repaired grip places the thumb in front and fingers behind the page. Full native mesh checks found no page intersections for either character at three HUD scales.
- Per-eye page composition uses the accepted native hand meshes to preserve hand/page ordering. The launcher imports these meshes from your installed game; game meshes are not distributed.
- Maps retain their native controls. Missing or stale hand/camera data, alternating-eye rendering and unverified ownership use the readable front panel. After focus loss, close and reopen the note.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe**, and run it. Allow the launcher to import guide artwork and note hand meshes from your installed game, then select **LAUNCH VR**. Existing launcher settings are retained.

The standalone DLL package is also available, but physical note occlusion requires the meshes imported by the launcher. Without them, notes use the front panel. v652 remains available for rollback.

## Validation and limitations

- The actual packaged launcher passed installation into a fresh folder, including the embedded DLL hash, controller artwork and enabled note feature.
- The release executable generated all 22 guide figures; dimensions, opacity, hashes and generation markers passed.
- Focused note geometry, GPU occlusion, simulated GameXr composition/fallback, native subtype/ownership, settings, guide and launcher checks passed.
- **Not headset-tested.** Readability, comfort, hand crossings, antialiased edges, material effects and gameplay transitions still need headset acceptance. Emily's thinner fingers retain a small conservative clearance from the page.
- The full strict build stops on existing MSVC C4702 in `src/dlss_mode.hpp:163`. The native DLL was built with `/W4` without `/WX`. An earlier broad GameXr simulation also stopped at its wrist-overlay assertion; focused note and guide GameXr checks pass.

The binary was built from the recorded working-tree snapshot before the release commit. Its manifest retains that original base commit and dirty-source status. `release.json` and `SHA256SUMS.txt` identify the published files.
