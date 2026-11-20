# VR v1441 (release, 2026-11-20)

## Spacewarp: the headset stays at its full frame rate, on by default

- When a frame takes longer than one headset frame, the game runs at half rate and the mod draws every frame in between
  itself. It uses each real frame's depth, your head and controller motion, your walking and turning, and your hands, weapons,
  held objects, arms and legs. The crosshair, prompts, markers and the HUD are drawn again at the right place in every frame.
- On by default (Auto). The launcher's Settings page has Spacewarp Off / Auto / Always (half rate).
- **Set your headset to 90 Hz.** Spacewarp renders every second frame, so the game needs 45 fps at 90 Hz. At 120 Hz it would
  need 60, and 72 Hz leaves longer gaps to fill. Virtual Desktop: Settings > Streaming > Frame rate. Meta Link: Meta Quest
  Link app > Devices > Graphics Preferences > Refresh rate. SteamVR: Settings > Video > Refresh rate. The launcher tells you
  when your last session ran at another rate.
- **Only one frame generation may run at a time.** With Spacewarp on, switch these off yourself:
  - Virtual Desktop: Synchronous Spacewarp → Disabled (headset app > Settings > Streaming).
  - Meta Link: Asynchronous Spacewarp off (Oculus Debug Tool).
  - NVIDIA Smooth Motion and Lossless Scaling: off for Dishonored 2.
  The launcher switches SteamVR's Motion Smoothing off for Dishonored 2 by itself, and puts it back when you turn Spacewarp
  off. The Setup checks, Save and Launch remind you of the rest.
- Spacewarp works only with an FSR or DLSS anti-aliasing mode. That is the default on every GPU; Setup checks turn red if you
  pick another one.
- Spacewarp and performance mode do not run together: with Spacewarp on, performance mode stays off.
- Best with Virtual Desktop's own runtime (VDXR). SteamVR and Meta Link work too: there, far fewer generated frames are now dropped
  when the game hitches, but Virtual Desktop still copes best with a heavy scene.

## Faster and steadier

- DLSS on RTX works again: the foveated DLSS feature failed to start and the mod fell back to FSR 3 at full resolution (the
  low frame rate some players saw in Virtual Desktop). DLSS Quality is the first-start default on RTX.

## Also new

- The VR settings menu is drawn with Dear ImGui.
- Performance mode can use PureDark's AFW plugin (your own `PDAFWPlugin.dll`, picked in the launcher; not included).
- PlayStation VR2 adaptive triggers on SteamVR (thanks to Indurhra).

## Known issues

- Glass objects (drinking glasses, bottles) can show doubled while you move with Spacewarp on: glass has no depth to warp it with.
- On SteamVR, some generated frames can still come late when the game hitches; Virtual Desktop copes best.
- Sometimes the weapon wheel selects a different weapon than the one you pointed at. Choose again if the wrong one comes to
  the hand.

If a Spacewarp change looks worse to you, each one can be undone with an empty file in `%LOCALAPPDATA%\DishonoredVR\build`:
`no-spacewarp-legs` (legs), `spacewarp-hand-bubble` (the area around the hands and weapons), `spacewarp-crack2` (close
surfaces). Turning Spacewarp off on the Settings page undoes all of it. Say what you saw.

Tested by the author in the headset over Virtual Desktop and SteamVR up to v1438 (Spacewarp, hands, weapons, HUD). The leg changes
(v1439) and the SteamVR timing (v1440-v1441) are tested in the lab, the simulator and one SteamVR session.
