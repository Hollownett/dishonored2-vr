# v610 camera work: checklist (Tier C)

Status: not run. Read this first.

**Proved only in the sim (Linux harness, Tier A and B):** the unit logic of the recenter and the cutscene policy; a live mantle (state 8) enters and exits with no view jump; a live cutscene (save slot 22, "void_01") renders as a 3D stereo pair in VR comfort and VR full, with distinct eyes, a level view, a comfort vignette, clean exits, and the flat quad in Theater; the settings panel layout (rendered images, every row description fits).

**Needs the headset:** everything about comfort and feel. The sim could NOT exercise these: takedowns and chokes (the guard turns and the player dies in the sim), the 30 degree re-aim, an actual gaze-to-action ease (the mantle heading already equalled the gaze), skipping or pausing during a cutscene, the hand-off from an opening cutscene to the first gameplay frame (the sim reached a cutscene by loading a save, but never ran that hand-off), held frames on a bad render, and the true end tilt of VR full.

Known by design: the first ~0.3 s of a cutscene is black while the stereo pair forms. Entry and exit keep your direction (no snap back to a fixed forward).

If something misbehaves, note the item, the cutscene or action, and copy the `scripted_view` block from `xr-live-status.json`. Dev knobs (in `build/`): `no-scripted-recenter` turns the recenter off; `cutscene-mode` (text: `vr_comfort`, `vr_full` or `theater`) overrides the menu setting while the file exists. Remove them when done.

## Before you start

- Note the DLL hash and the runtime (Steam Link or Virtual Desktop).
- Open the VR settings panel. Check that the two new rows ("Recenter on scripted moments", "Cutscenes") and the description text under every row are readable, and nothing overlaps the footer.

## Scripted moments (recenter)

1. **Climb with the head turned away.** Turn your head well away from your body (at least 60 degrees) and climb a ledge. Expect: the climb plays in front of you, the turn is gentle, and nothing swings back afterwards.
2. **Takedown and choke with the head turned away.** Not provable in the sim. Expect the same as item 1. Watch for a one-frame jump at the start and the end.
3. **Look backwards (180 degrees) and climb.** Expect: it turns the short way, with no spin the long way.
4. **Two climbs in a row.** Expect: the second behaves like the first, with no leftover offset.
5. **Pause and Recenter view during a climb.** Expect: no jump beyond the recenter itself, and the ease resumes after the pause.
6. **A sharp game turn mid-climb** (the game turns the character sharply). Expect: the view eases, with no jump.
7. **Comfort vignette during the turn.** Expect: a soft vignette while the view turns, gone when it ends. Say whether the strength is right (too strong, too weak, fine).
8. **Recenter on scripted moments: Off.** Set the row to Off. Expect: the old behaviour (the action plays where the game put it). Set it back to On afterwards.

## Cutscenes

Do each item in each mode: VR comfort, VR full and Theater (the Cutscenes row).

10. **Watch a cutscene and a dialog.** Expect: 3D (not flat) in VR comfort and VR full, comfortable in VR comfort, no broken eye (no doubled or one-eyed image), and the flat screen in Theater. About 0.3 s of black at the start is expected; longer or repeated black is a fail.
11. **Skip the cutscene** (in each mode). Expect: it skips, the game returns to your view with no jump and no stuck black screen.
12. **Pause during a cutscene** (in each mode). Expect: the pause menu is usable, and the scene resumes in 3D afterwards.
13. **A cutscene right after loading a save** (the opening cutscene). Expect: the first gameplay frame after it has no jump.
14. **Entry and exit direction.** Expect: you keep the direction you were facing going in, and keep it going out.
15. **Comfort vignette strength while the cutscene camera moves** (VR comfort). Say whether it is right, too strong or too weak.
16. **VR full:** are the scene's motion and its end tilt comfortable? (VR full has no vignette.)
17. **Back to normal.** After each cutscene, the hands, HUD and aiming work as before.

## Report

For each item: pass or fail, the DLL hash, the runtime, and the `scripted_view` status block for any fail. For items 7 and 15, say whether the vignette strength is fine; for item 16, whether VR full is comfortable.
