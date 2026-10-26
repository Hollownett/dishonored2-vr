# v627 test release

This launcher includes the accumulated VR repairs since v620 and embeds the exact v627 DLL tested locally. It is a prerelease for continued headset testing.

## Changes

- Sprint hand synchronization: native collision replies retain their original request identity and geometry, with prediction accounting for native update cadence. The headset tester confirmed sprint fixed in v626; that path is unchanged in v627.
- Nearby pickups: a 25 cm sphere around each controller grip selects the nearest eligible prop. Coins, books and bottles now show native prompts before grip; the tester accepted those paths in v626.
- World controls: doors and other non-takeable usables outside pickup bounds retain native visibility checks, allowing native pre-action prompts. Retained corpse/NPC prompts restore body-relative heights. These latest v627 changes need further headset confirmation.
- Native labels and hold gauges retain their exact HUD entry/object ownership, rather than following an unrelated latest selection. Limbs drag; holding a torso grip for 500 ms offers native body carry.
- Crosshair placement uses the actual shot ray and full per-eye projection. Existing hand/prop depth repairs and temporal-AA recovery work are included.
- Performance mode renders one eye per frame by default, retaining the selected AA when per-eye ownership checks pass. FSR Native, DLAA and DLSS Quality are available according to hardware/provider support; failed temporal ownership falls back to spatial AA.

## Install and defaults

Exit Dishonored 2, download **DishonoredVRLauncher.exe**, and run it. It installs the mod, imports UI art from your own game and starts VR from **LAUNCH VR**. Steam game build 17576867 and the VC++ 2015–2022 x64 runtime are required. No game assets or DLSS provider are bundled; DLSS uses a compatible provider from your own installation.

Gameplay features are enabled by default except power gestures. Powers use the controller trigger. The diagnostic temporal census is also off. Existing saved AA/settings and explicit feature choices survive upgrades; review Settings → Features or restore defaults to adopt the new defaults.

Linux users can install **d2vr-v627-dll.zip** with `linux/d2vr-linux install --zip d2vr-v627-dll.zip` using the matching v627 checkout. The local headset test uses stable Proton 11.0; newer Proton variants previously showed mixed-layer OpenXR issues in this setup.

## Validation and remaining work

- 21 focused v627 native/prompt compile, execution and routing checks passed with no skips. Production DLL compilation passed clang-cl/xwin `/W4 /WX`; the Windows-only maintenance check was skipped.
- Launcher checks: 172 passed. Linux CLI/install tests: 239 passed.
- The shipped launcher bundle is checked for the exact DLL, manifest, provenance, feature list, proxy and dependency/license payloads.
- Full native Windows MSVC validation and native Windows launcher startup are not established by these Linux checks.
- Doors, typewriters, safe dials and corpse prompt visibility still need fresh headset acceptance. Prior low-resolution centre/right-eye distortion and alternate-eye continuity reports remain open. Headset quality and comfort are not inferred from compilation or simulated checks.

See **v627-validation.json** in the release assets for hashes and precise evidence. The previous public v620 release remains available for rollback; exit the game before changing launcher versions.
