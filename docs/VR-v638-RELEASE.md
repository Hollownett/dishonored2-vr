# v638 test release

This launcher embeds the exact v638 DLL tested locally. It is a prerelease that fixes the problems from the v637 headset run. Everything else is unchanged from v637.

## Changes

- **Severed limbs and body halves pick up like props.** A severed arm, leg, head or body half is a carryable like a bottle, but it draws a skinned model. The hand's proximity check accepted only static models, so a part was selected only when the game's own gaze or hand ray landed on it. That is why parts were picked up reluctantly and showed no prompt. Now the hand selects a part it is near, the game's pickup prompt appears over it, and grip picks it up.
- **No "Carry body" prompt.** Bodies are carried with your hands only, so the game's carry prompt is no longer shown over a dead or unconscious body. The knob `build/legacy-corpse-carry` brings it back.
- **The free hand no longer catches on a dragged body.** The hand holding a body already passed through it, but the other hand was stopped by the body's swinging legs and stayed behind your controller. Now a body that either hand holds never stops your hands.
- **Smaller kill and choke prompts.** The takedown prompt is now 0.16 m wide at kill range, down from 0.25 m. HUD size still scales it: at 85% it is 0.21 m, at 55% 0.14 m.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled.

The DLL alone, with its manifest and notices, is in **d2vr-v638-dll.zip**.

## Validation and open items

- Simulated game, severed head next to the player:
  - with the head level (not looking at the part), v637 never selected it, showed no prompt and did not pick it up; v638 selected it, showed the prompt for 193 frames and picked it up on the first squeeze (HoldObject);
  - looking down at the part, both v637 and v638 picked it up.
- Severed parts in the test save draw model type 0x1b0d628 (skinned), measured live.
- Focused checks pass: hand sphere, native Use range, LOS anchor, two-hand selection and takedown prompt size.
- Not tested in the simulation: the body prompt (no hand rested on a body in these runs) and the dragged body's legs against the free hand.
- The DLL was built with Windows MSVC (DLL only). The full required build was not run. **Not headset-tested.**
- The checklist is in `VR-v638-TEST-CHECKLIST.md` inside the zip.

The v637 release remains available for rollback.
