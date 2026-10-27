# v652 test release

This launcher embeds the exact v652 DLL tested locally. It is a prerelease: v648 plus the illustrated controls guide. The design came from GPT-6.1 Sol, at the user's request, and is in `docs/VR-CONTROLS-DESIGN.md`.

## New: the illustrated controls guide

- **Seven pages in the game's journal style.** The pages use the dark leather page, gold headings and the brush highlight on the active page, with larger text (32 px).
  - Page 1: **Throw & grab** (the sword throw and grabbing a live enemy).
  - Then: Sword & stealth, Moving, Hands, Bodies & weapons, Wheel & journal, Menus & settings.
- **Pictures of every gesture.** The launcher renders them on your PC from the real Corvo and Emily player models, as sepia ink sketches on parchment.
  - Guards and bodies are ink silhouettes made from the same model.
  - Nothing from the game ships with the mod: the pictures are made on first start (or the next start of an existing install) from your own Dishonored 2 install.
  - Without the game art the guide still shows every line, with a short description in place of each picture.
- **In the game:** squeeze the right grip at your right temple; flick the **right stick left or right** to turn pages. While the guide is open the right stick never turns you, crouches or zooms. Squeeze the right temple again to close it. VR Settings > Controls & gestures opens it over the pause menu.
- **In the launcher:** the Controls page has the same pages and pictures, a Corvo/Emily switch, and an "All controls" text view.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. On its first start it makes the guide's pictures from your install (a few seconds); then **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime.

The DLL alone is in **d2vr-v652-dll.zip**. A DLL-only install shows the guide without pictures.

## Validation

- `controls_guide_image_check` renders all 7 pages: plain, with art and with partial art, for both characters. Every line is drawn exactly once, nothing is clipped, and the output is deterministic.
- `launcher_guide_figures_check` checks all 22 pictures: sizes, opaque, hashes.
- `controls_guide_navigation_check`: one flick turns one page, a held stick does not repeat, the pages stop at both ends, and closing gives the stick back only when it is neutral.
- Simulated game (`guidepages`): the right temple opened the guide; three flicks forward and one back changed the page (5 page uploads); no snap turn or view turn happened while it was open; the temple closed it.
- The launcher pages were captured with and without art.
- **Not headset-tested.** Readability at 0.85 m and the page flick feel are headset checks.

v648 and v646 remain available for rollback.
