# VR v1457: performance improvements (release, 2026-11-21)

This release is about frame rate and smoothness. Everything below is new since v1392, the last release.

## Performance improvements

1. **Spacewarp: the headset keeps its full frame rate (new, on by default).**
   - When a frame takes longer than one headset frame, the game runs at half rate and the mod draws every frame in between itself.
     The headset gets 90 images a second while the game renders 45.
   - Each frame in between is built from the last real frame's depth, your head and controller motion, your walking and turning,
     and your hands, weapons, held objects, body and legs. The HUD, crosshair, prompts and markers are drawn again in their
     place.
   - Launcher > Settings > Spacewarp: Off, Auto (default), or Always (half rate). Set your headset to 90 Hz.

2. **SteamVR: Spacewarp's frames on time, and steady timing.**
   - On SteamVR the VR session runs on its own graphics device next to the game's. SteamVR's work for each frame never waits on
     the game's device, and each frame in between goes out in its own display slot, even while the game is still drawing.
   - If SteamVR's frame timing ever stops, the mod restarts the VR session within about two seconds instead of leaving you in
     SteamVR's waiting scene.

3. **DLSS on NVIDIA RTX works again, and costs half as much.**
   - DLSS failed to start on some RTX cards, and the mod then quietly rendered FSR 3 at full resolution. That was the low frame
     rate some players saw.
   - DLSS Quality now costs about 0.7 ms per eye instead of 1.4 ms (RTX 5070), and it is the first-start default on RTX.
   - If DLSS cannot start, the mod falls back to FSR 3 at the same reduced resolution, not at full resolution.

4. **Steam Link: no rendering outside the lenses.** SteamVR over Steam Link gives the game no lens mask, so the corners of each
   eye image that you never see were rendered anyway. The mod now masks them itself.

5. **No more half-second hitches on NVIDIA and Intel cards** when the upscaler restarts (a new level, a resolution change). The
   mod tried AMD's FSR 4 on every restart, and each try cost about 0.4 s. FSR 4 is now tried on AMD Radeon cards only.

6. **One frame generation at a time.**
   - With Spacewarp on, the launcher switches SteamVR's Motion Smoothing off for Dishonored 2. It can only do that while SteamVR
     is closed, so close SteamVR before you press Launch VR.
   - Spacewarp and performance mode do not run together.
   - Setup checks, Save and Launch list what else to switch off: Virtual Desktop's Synchronous Spacewarp, Meta's ASW, NVIDIA
     Smooth Motion and Lossless Scaling.

7. **Performance mode:** the rebuilt eye costs a little less GPU time (about 0.1 ms).

8. **A new performance guide.** It explains every setting that changes the frame rate. The launcher also warns that Save
   diagnostic logs costs frame time.

## Also new

- Steam Link: a short hiccup in a frame no longer restarts VR, so Steam Link stays connected where it used to freeze or drop.
- The VR settings menu is drawn with Dear ImGui, and PlayStation VR2 adaptive triggers work on SteamVR. Both are Indurhra's work,
  thank you.
- Quitting the game no longer ends in a crash report.

## Known issues

- Glass objects (drinking glasses, bottles) can show doubled while you move with Spacewarp on: glass has no depth to warp it with.
- Steam Link adds stutter of its own that Spacewarp cannot fill. A Quest 2 can decode frames more slowly than 90 Hz allows, and
  Wi-Fi drops pause the stream. If it stutters, set Steam Link's Target Bandwidth to a fixed value (about 150 Mbit/s). Virtual
  Desktop copes best.
- Sometimes the weapon wheel selects a different weapon than the one you pointed at. Choose again if the wrong one comes to the
  hand.

If something looks worse to you, an empty file in `%LOCALAPPDATA%\DishonoredVR\build` undoes it:
- `spacewarp-legacy-body`: the body and legs in Spacewarp;
- `spacewarp-hand-bubble`: the area around the hands and weapons;
- a file `xr-own-device` containing `off`: SteamVR's own graphics device;
- `legacy-xr-recovery`: the Steam Link hiccup handling.

Turning Spacewarp off on the Settings page undoes all of Spacewarp. Say what you saw.

Tested by the author in the headset over Virtual Desktop and SteamVR (Steam Link, Quest 2), with Spacewarp and DLSS Quality on an
RTX 5070. v1457 differs from the last build tested in the headset only in turning the SteamVR path on by default. Meta Link was not
retested for this release.
