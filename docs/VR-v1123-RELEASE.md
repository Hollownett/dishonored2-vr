# VR v1123 (release, 2026-11-09)

The first full release. Everything since v1109 is the dual-wield work: a second pistol or crossbow in the other hand is a real,
upgraded, animated weapon with its own ammo and sounds. Headset-tested by the user through v1122 (Quest 3, Virtual Desktop,
RX 9070 XT); v1123 adds the weapon-body animation fix on top, proven in the simulated-input runs.

## 1. Two weapons, one per hand (v1093-v1123)

| change | what you see |
|---|---|
| A weapon chosen a second time on the Quick Access Wheel goes to the other hand (v1093-v1101) | pistol + crossbow, or two of the same, each with its own ammo type and count |
| Every instance carries the player's upgrades, held or lying on the floor (v1093, v1111) | scope, magazine and the rest of the parts on both guns; the chosen bolt is shown |
| A held trigger fires the copy again as soon as it is ready (v1112) | the same fire rate as the original |
| Hand-over between hands without a frame of flicker (v1114) | |
| The copy runs on the game's own fire and reload path (v1114-v1122): the weapon's event handlers are registered on it, its fire and reload scenes run, dry pulls play the out-of-ammo scene, a hidden stand-in arm carries the arm-clip sounds | fire, reload and empty-click sounds; upgrade animations, bolt and magazine refill at the native times |
| The copy's animation component is resumed the way the game's equip does it and suspended when it leaves the hand (v1123) | the weapon body plays the full fire, reload and empty animations, every shot |

Kill switches (empty files in `%LOCALAPPDATA%\DishonoredVR\build`): `no-copy-scenes`, `no-copy-arm-actor`, `no-copy-anim`.

## 2. Logs are off by default

Nothing is written to disk during play unless **Save diagnostic logs (for bug reports)** is on in the launcher's Features
list. The mod keeps a one-line status file and the settings file; traces, captures and the dual-wield journal only exist with the
switch on. Guarded first-chance faults no longer write a minidump.

## 3. Since v989 (already in v1109)

Lens-mask depth stamp, foveated FSR 4 / DLSS, sun-shadow cascade time slicing; native hands v1050-v1092 (real-shape grips,
two-handed holds, props and severed parts that stay in the hand, punches with weight); launcher settings version 11 and faster
pages. See `docs/VR-v1109-RELEASE.md`.

## Known issues

- Exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (after the game has closed its window).
  Harmless in play; saves are written before it.
- Pre-existing check failures unchanged: `native_weapon_visibility_check` (C4459 in the test), `object_motion_check` build,
  `game_xr_simulation_check` line 66.
- Not re-tested in the headset on v1123 itself: the copy's visible body motion and its timing against the native weapon
  (sim-proven), foveated DLSS on an RTX GPU.
