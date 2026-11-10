# VR v1132 (release, 2026-11-10)

Controllers work on PlayStation VR2 Sense, Valve Index and HTC Vive wands through SteamVR.

## Controller bindings

| change | what you see |
|---|---|
| The mod suggested bindings only for Oculus Touch. SteamVR binds a controller only to a profile the game suggested, and it presents PSVR2 Sense (and Index, and WMR controllers under SteamVR) as Index or Vive controllers, so those players had no hands and no input. The mod now also suggests the same actions for `valve/index_controller` and `htc/vive_controller`; Touch is unchanged | hands, sticks, triggers, grips and buttons respond on PSVR2 Sense, Index and Vive; before, the game ignored the controllers |

Button mapping on the new profiles: on Index-style controllers (PSVR2 Sense under SteamVR) left B opens the pause menu, since
Index has no menu button. On Vive wands the trackpad is the stick, right menu is A, left menu is the pause menu, and there is no B/Y.

Not headset-tested on PSVR2 yet. If controllers still do nothing, turn on "Save diagnostic logs" in the launcher and share
`xr-live-status.json` from `%LOCALAPPDATA%\DishonoredVR\artifacts`.

Includes v1130 (weapon wheel on high-resolution headsets), v1129, v1127 and v1126.

## Known issues

- **Crossbow aim in the tutorial's firing range looks wrong:** the tutorial puts an invisible wall in front of the range. Outside the
  tutorial the crossbow aims normally.
- As v1123: exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).
