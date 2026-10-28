# v672 test release

Fixes from the v670 headset test, plus grenades, mines and hands-only body carrying. This is a prerelease and has not been tested in a headset yet.

## Notes
- A picked-up note is held in the left hand like a tablet: thumb in front, fingers behind its left edge. Turning the wrist tilts and rolls it. Read it with the left controller across your chest, palm toward you.
- Note colours match the game. The page was gamma-encoded twice, which made it lighter and turned its faint dark-red ink bright red over the blue scene.
- Touch the page with the right index finger to scroll. B closes the note.

## Hands
- The pointing hand keeps the index straight and lays the thumb on the folded fingers instead of the palm.
- The thumb no longer sinks into the palm when the fingers lie flat on a big object, like an oil tank or a body. A replay of 3,106 recorded hand poses went from 319 thumb-in-palm poses to 11 (small held items) and found no new ones.

## Cutscenes
- Cutscenes show the scene camera without your body or tracked arms. The `build/cutscene-avatar` knob restores the v643 behaviour.

## Crosshair
- The in-eye reticle uses the game's own HUD crosshair dot. The launcher imports it from your installed game, re-running the art import once. The reticle logic is unchanged.

## Grenades and mines (left hand)
- **Grenade:** make a fist to hold the throw aim, swing, and open the hand to throw. The grenade leaves with your hand's speed. The trigger alone still works.
- **Stun mine / spring razor:** make a fist, press the palm to a wall, floor or ceiling, and open the hand. The mine goes on the surface in front of the palm. The `build/mine-point-aim` knob restores the pointing ray.
- While a grenade or mine is in the left hand, the left fist does not grab objects.

## Bodies
- Bodies are carried and dragged with your hands only. The shoulder dock is off (knob `build/shoulder-dock`). If the game's shoulder carry starts anyway, the mod puts the body down at once.

## Controls guide
- Three pages: **Body** (where the sword, pistol, guide, journal and wrist watch are), **Controllers** (every Touch button labelled) and **Tips** (throwing the sword, grabbing guards, dragging bodies). Open it by squeezing the right grip at your right temple, or from **Controls & gestures** in VR Settings. In the launcher, the Body and Controllers pages show as text only.

## Install
Exit Dishonored 2, download **DishonoredVRLauncher.exe**, run it and select **LAUNCH VR**. Let the launcher re-import the game art once, for the crosshair. v654 remains available for rollback.

## Validation and limitations
- Focused offline checks pass:
  - notes: page colour transfer, palm-attached page and wrist roll, hand pose, native body solve, XR composition;
  - fingers: finger solver and hand lab;
  - grenades and mines: throw gesture, gadget aim, equip-name classifier;
  - controls: grip decisions, guide layout and the README/guide sync;
  - crosshair: reticle art.
- The native DLL was built DLL-only with `/W4`. Full repository certification was not rerun.
- **Not headset- or sim-tested:**
  - the tablet angle and its comfort;
  - grenade release timing (the game launches about 0.3 s after the hand opens, because of its throw animation);
  - mine placement following the palm;
  - the size of the game crosshair;
  - the cutscene avatar hiding;
  - the native-carry auto-drop.
