# v637 test release

This launcher embeds the exact v637 DLL tested locally. It is a prerelease that fixes the two remaining problems from the v636 headset run. The corpse carry, thumb and wrist changes from v636 are unchanged.

## Changes

- **Hands no longer vanish.** Whenever the game briefly took over the camera (landing after a jump, slides, some combat moments), the mod handed both arms to the game's animation for 0.5-2 s, so the hands left your controllers. Now the arms go to the game's animation only for scripted scenes (takedowns, finishers, mantles) and cutscenes. The knob `build/legacy-authored-hands` restores the old behaviour.
- **Performance mode is off by default.** Performance mode draws one eye per frame, so the two eyes see the world a frame apart. At headset frame rates this reads as a double image in one eye when you move or turn. This was the "right eye shows two images for the first minutes" effect: in that session the mode switched itself off after about three minutes. The pose and image of each eye were verified in the simulated game (0 mismatches over 14,000 one-eye frames).
  - **If you are upgrading,** your previous choice is kept: turn performance mode off in the launcher under Settings → Features.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled.

The DLL alone, with its manifest and notices, is in **d2vr-v637-dll.zip**.

## Validation and open items

- Focused checks pass: action-camera hand rule, full-body ownership, arm flicker, attachment pose, alternate-eye routing, and the launcher's performance-mode default.
- Simulated game:
  - the alternate-eye pose trace has 0 mismatches at 105 and 138 fps;
  - a jump and landing on v637 refused no tracked arm frames.
- The camera mode that took the hands in the headset (mode 14) and a guard striking the block were not reproduced in the simulation, so the block case still needs the headset.
- The DLL was built with Windows MSVC (DLL only). The full required build was not run. **Not headset-tested.**
- The checklist is in `VR-v637-TEST-CHECKLIST.md` inside the zip.

The v636 release remains available for rollback.
