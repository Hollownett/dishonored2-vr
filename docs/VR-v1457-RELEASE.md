# VR v1457 (release, 2026-11-21)

## SteamVR: steady, with Spacewarp's frames on time

- On SteamVR, the VR session now runs on its own graphics device next to the game's. SteamVR's work for each frame no longer
  runs on the game's device, so it can no longer stall or freeze the game.
- With Spacewarp on, each frame in between now goes out in its own display slot, even while the game is still drawing the
  next one. Before, about a quarter of them came too late on SteamVR and the headset repeated a frame instead.
- If SteamVR's frame timing ever stops advancing, the mod notices and restarts the VR session within about two seconds. Before,
  you were left in SteamVR's waiting scene.
- On by default on SteamVR. Virtual Desktop's own runtime (VDXR) works differently and is unchanged.
  - On another runtime (Meta Link, Pimax, WMR), an empty file named `xr-own-device` in `%LOCALAPPDATA%\DishonoredVR\build` turns
    it on. Say how it went.
  - If your PC cannot use it (an older driver, or a runtime that refuses it), the mod falls back to the previous way by itself.
  - The same file containing the word `off` switches it off.
- **Close SteamVR before you press Launch VR.** While Spacewarp is on, the launcher switches SteamVR's Motion Smoothing off for
  Dishonored 2, but it can only change SteamVR's settings while SteamVR is closed. Otherwise set it yourself: SteamVR >
  Settings > Video > Per-application video settings > Dishonored 2 > Motion Smoothing: Off. Two frame generations at once
  look worse than either alone.

## Spacewarp: your body and legs move like your hands

- In the frames in between, your legs now come from the frame that was actually shown, not the one before it (they were 3-6 cm
  behind while walking). The feet are covered down to the tip of the boot.
- Your chest, coat and upper sleeves follow your head the way the game's body does, so the upper body no longer smears when you
  lean or look around.
- A wall beside you no longer moves with your head when you lean.
- An empty file named `spacewarp-legacy-body` in `%LOCALAPPDATA%\DishonoredVR\build` brings back v1445's body rules.

## Settings and performance

The performance guide (`docs/PERFORMANCE.md`) now explains every setting that changes the frame rate: Spacewarp, performance
mode, resolution, each anti-aliasing mode, sharpening, the monitor view, the headset's refresh rate and the streaming apps.

## Known issues

- Glass objects (drinking glasses, bottles) can show doubled while you move with Spacewarp on: glass has no depth to warp it with.
- Steam Link (SteamVR on a Quest) adds stutter of its own that Spacewarp cannot fill. A Quest 2 can decode frames more slowly than
  90 Hz allows, and Wi-Fi drops pause the stream. If it stutters, set Steam Link's Target Bandwidth to a fixed value (about 150
  Mbit/s) instead of automatic. Virtual Desktop copes best.
- Sometimes the weapon wheel selects a different weapon than the one you pointed at. Choose again if the wrong one comes to the hand.

If a Spacewarp change looks worse to you, each one can be undone with an empty file in `%LOCALAPPDATA%\DishonoredVR\build`:
- `spacewarp-legacy-body`: the body and legs;
- `no-spacewarp-legs`: legs at all;
- `spacewarp-hand-bubble`: the area around the hands and weapons;
- `spacewarp-crack2`: close surfaces.

Turning Spacewarp off on the Settings page undoes all of it. Say what you saw.

Tested by the author in the headset on SteamVR over Steam Link (Quest 2), in three game sessions with v1456, two of them with
Spacewarp on:
- no frame was refused;
- SteamVR's own "timed out" share fell to 2-4 % of each session, from 11-17 % in v1451;
- the body and legs changes were included.

v1457 differs from the tested v1456 only in turning the SteamVR path on by default. Virtual Desktop and Meta Link were not
retested for this release.
