# VR v1109 (test candidate, 2026-11-08)

Paired mode. Headset-tested by the user on v1106-v1108 (Quest 3, Virtual Desktop, RX 9070 XT, FSR 4 Quality); v1109 adds an engine
cvar default and launcher fixes on top. Everything since v989 is in this build.

## 1. Rendering performance (v1105-v1109)

Techniques taken from a study of the Metro Exodus VR mod (`docs/METRO-EXODUS-VR-PERF-RESEARCH.md`).

| change | measured |
|---|---|
| Lens mask stamped into depth (v1106, `XR_KHR_visibility_mask`): pixels the lenses never show are not shaded | image OK in every headset run |
| Foveated FSR 4 / DLSS (v1107/v1108): the upscaler runs on a 30 deg centre box, FSR 3 upscales the rest | FSR 4 2.65 -> 0.65 ms in the GPU lab; no visible seam in the headset |
| Together, heavy spot | pacer work p50 9.95 -> 8.33 ms, no 45 Hz locks, 90 Hz held |
| Sun-shadow cascade time slicing on by default (v1109): the engine's own `r_shadowParallelPartialSliceUpdate` and `r_shadowParallelPartialDynaUpdate` | headset-accepted through `build/engine-cvars` |

Knobs: `build/no-visibility-stamp`, `build/no-foveated-upscale`, `build/foveated-upscale-deg`; live commands `vstamp on|off`,
`fovea off|<deg>`. A `build/engine-cvars` file replaces the cascade defaults (a file with only comments turns them off).

Not tested: foveated DLSS on an RTX GPU.

## 2. Launcher

- Reads and writes settings file version 11. The game writes 11 (it adds the upscaler render scale); the launcher only accepted
  1-10, rejected the file and showed defaults.
- Page switches 135-195 -> 15-33 ms: the window backdrop is cached per page and size instead of re-rendered on every click.
- Features list: three rows per wheel notch, one repaint per scroll, row windows built at start.
- Icon: an original badge on the exe; the window and taskbar icon is cut from the key art imported from the player's own install.

## 3. Native hands (v1050-v1092)

Grabs with mass-scaled haptics, weight slowdown and fist punches; two-handed holds; grips from the props' real Havok shapes;
held props, corpse limbs and severed parts stick to the hand like a carried weapon (snap turns, running); the weapon wheel moves a
power to the hand that chose it; a copied pistol or crossbow carries the player's upgrades. Details per version in the commit log
and `docs/superpowers/plans` notes.

## Known issues

- Pre-existing check failures (also on v1092): `object_motion_check` build, `game_xr_simulation_check` line 66.
- One Virtual Desktop runtime crash seen on v1108 at a menu transition (inside `VirtualDesktop.LibOVRRT64_1.dll`).
