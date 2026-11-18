# VR v1393 (release, 2026-11-19)

## Steam Link: no more freezes and dropped connections

- A short hiccup in a frame (a late or rejected frame from the headset app) no longer restarts VR. The mod drops that
  one frame and keeps going, so Steam Link stays connected where it used to freeze or drop.
- Only a real, lasting problem (about 3 seconds of rejected frames, or a lost session) still restarts VR.
- If anything is worse than before, create the empty file `build\legacy-xr-recovery` in
  `%LOCALAPPDATA%\DishonoredVR` to get the old behaviour back, and say what you saw.

## Everything from v1392

- Hold the trigger on a body lying on the ground to carry it on your shoulder; the grip still drags.
- Whiter crosshair, and a white ring crosshair for the dual-wield second gun.

## Known issues

- Sometimes the weapon wheel selects a different weapon than the one you pointed at. Choose again if the wrong one
  comes to the hand.

Tested by the author in the headset over Steam Link (normal play and a forced frame fault: VR and Steam Link kept
running) and over Virtual Desktop. Say what you see if something is off.
