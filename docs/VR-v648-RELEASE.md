# v648 test release

This launcher embeds the exact v648 DLL tested locally. It is a prerelease: v646 plus the VR controls and gestures page.

## New: the VR controls and gestures page

Every control and gesture on one page: movement, hands, the sword, the left hand's pistol, crossbow and powers, stealth takedowns, grabbing and yanking a live enemy, bodies, the wheel, the HUD and menus.

- **In the game:** squeeze the right grip at your right temple to show the page in front of you; squeeze there again to hide it. The journal stays on the left temple. Reaching over the right shoulder still draws the sword: where the two places meet, the sword wins.
- **In VR Settings:** the **Controls & gestures** row (press right) shows the page over the pause menu. It goes away when you return to the game. This row replaces the old "Controls hint" switch, and the page replaces the small card on the left wrist, which showed outdated buttons.
- **In the launcher:** the new **Controls** page.
- **In the README** and in `docs/VR-CONTROLS.md`. All four come from that one file.

## Also in this build (from v646, not yet headset-tested)

- The free hand picks things up while the sword is held.
- Stealth kills from behind.
- Heavy objects lift.
- The thumb rests on the grip ball.
- Cutscenes keep your hands and body when the scene uses your character.

See `VR-v646-RELEASE.md`.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod (including the controls page) and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled.

The DLL alone, with its manifest and notices, is in **d2vr-v648-dll.zip**. A DLL-only install also needs `VR-CONTROLS.md` in the mod folder; without it the page says so.

## Validation

- Simulated game: the right grip at the right temple opened the page; a second squeeze closed it. The page was submitted for 371 frames between the two squeezes, and the sword stayed sheathed. The simulator's eye images do not include floating panels, so the page itself is seen in the unit-check render.
- `controls_guide_image_check`: the page parses into 8 sections and 42 entries, and all 42 are drawn on the 1920x1200 card.
- `vr_settings_native_check`: the Controls & gestures row opens the page.
- `vr_controls_guide_check.py`: the README section and the launcher's embedded file match `docs/VR-CONTROLS.md`.
- The launcher's Controls page was captured live.
- **Not headset-tested.**
