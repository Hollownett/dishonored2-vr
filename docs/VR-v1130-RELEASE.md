# VR v1130 (release, 2026-11-10)

The weapon wheel selects with dual wield on high-resolution headsets (Pimax Crystal / Crystal Light and others).

## Weapon wheel and menu pointer

| change | what you see |
|---|---|
| The mod passed the hand pointer to the game's menus only when the game rendered at most 4096 pixels per side. Headsets that ask for a larger eye image (a Pimax Crystal at 100% asks for about 4312x5104) silently lost the pointer, with no log line. The limit is now 16384 (the D3D11 maximum) | with dual wield, pointing at a wheel item moves the highlight and the trigger equips it; before, the highlight stayed on one item and every choice re-equipped it. Laser pointing in menus works on these headsets too |

Not headset-tested on a Pimax yet. If the wheel still freezes, turn on "Save diagnostic logs" in the launcher and share
`dual-wield-events.jsonl` (its `wheel_pointer` lines) and `xr-live-status.json` from `%LOCALAPPDATA%\DishonoredVR\artifacts`.

Includes v1129 (wheel pointer no longer turns itself off), v1127 (parry) and v1126 (tutorial fixes).

## Known issues

- **Crossbow aim in the tutorial's firing range looks wrong:** the tutorial puts an invisible wall in front of the range. Outside the
  tutorial the crossbow aims normally.
- As v1123: exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).
