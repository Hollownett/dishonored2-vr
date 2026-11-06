# VR v1018 (2026-11-05)

Held weapon: two binding defects fixed. Built from the worktree at `bf1ea1a` + the
uncommitted weapon work. `build/d2vr_render_probe_arm_v1018.dll` (14,898,688 bytes), compiled with
`build/compile-v1018.cmd` (`dll_exit=0`); `weapon_draw_place_check` and `object_motion_check` pass
(the only files touched are `src/dxgi_probe.cpp` and the two new counters), no headset run yet.

## What the last three sessions show

The user ran v1017 twice (16:33 and 16:46) and reported a right-hand pistol that
"floats" and always lags the hand, renders black in the left eye and flickers in the right.
The live `xr-live-status.json` of the 16:46 session has two hard facts about the binding:

- `weapon_draw.binding[hand]` can end a session **bound with a wrong G**: the last pistol binding
  reported `sanity {n:4, gap_mm:227.6, angle_deg:133.0}` - the drawn weapon sat 22.7 cm from the
  engine frame and 133 degrees off. `sanity` is report-only, so that G kept being used.
- Before that binding the same hand was **unbound for 25,970 frames** whose only candidates were
  `rejects` (invalid G), i.e. `weapon_draw.skip_reasons[hand][4]`. On those frames the engine's
  tick rows stay, and the engine's weapon frame was measured 0.3-0.5 m from the drawn hand
  (`engine_dev_mm [309, 518]`). That is a weapon drawn away from the hand for over half the
  session, then snapped into the hand on the frames that published.

The cause is the learning rule, not the placement arithmetic: `candidate_from_read(note.read, F)`
accepted **any** writer note within 3 frames. A note records the hand pose the engine composed the
item from; when the hand has moved since (or the note is the other arm's), `inv(read)*F` bakes that
motion into the grip. Two such notes can agree by chance (a still hand makes a wrong pairing look
perfectly consistent), so the two-sample confirmation binds a wrong G; and when no note ever
matches F, `valid_grip` rejects every candidate and the hand never binds.

## Fix

- `publish_weapon_draw` builds `image` (the drawn hand) **before** the candidate, and only a note
  whose read is within **3 cm and 8 degrees** of that drawn hand can become a G candidate. The
  newest such note is used; otherwise G is kept and no candidate is made. A stale/other-hand note
  can never rewrite the grip any more. New status counter `weapon_draw.stale_note`.
- One copy entity published for both physical hands made two sides share one engine model frame,
  so every draw of it matched both sides within the `origin_tie` and was dropped as `ambiguous`,
  leaving that weapon at the engine's tick frame. The publisher now detects
  `copy_draw(0).entity == copy_draw(1).entity` and publishes that copy only for the hand it was
  actually placed from (the copy's recorded read, compared with each hand's image grip).

## Install and test

1. Close the game (nothing running now).
2. Run `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1018.exe` (built from
   `launcher/dist/DishonoredVRLauncher.exe`; its publish log printed
   `Embedding d2vr_render_probe_arm_v1018.dll from repair-v1018-test-candidate.json`).
   The standalone launcher writes the DLL into `%LOCALAPPDATA%\DishonoredVR\build`, verifies its
   sha256 against `artifacts/repair-v1018-test-candidate.json` and removes the older probe DLLs.
3. LAUNCH VR, hold the right-hand pistol and move it. Report whether the float/lag is gone; the
   black left eye / right flicker is the second item below.

## What is not fixed yet

The **black weapon in the left eye and the slight flicker in the right** are not explained by the
binding: in the 16:46 session the pistol hand was bound with `G` translation 0.000, `placed_dev`
0.000 and every matched draw placed, yet the report was made on that session. FSR 4 Native is
active (`fsr4_dispatches`, `temporal_mode 6`), and the weapon is the only object whose motion
vectors and depth the mod authors. The next diagnostic is one short session with
`%LOCALAPPDATA%\DishonoredVR\build\fsr-depth-dump-now` (dumps both eyes' FSR depth, motion and
colour at the next dispatch) and `build\weapon-draw-probe` (per publish: hand, source, F, B,
distance/angle), with the pistol held in view. That decides whether the left eye's FSR depth,
motion or reactive mask is the other eye's (cross-eye near-field ghosting) or whether the weapon's
own previous rows are wrong.

## Evidence

- `%LOCALAPPDATA%\DishonoredVR\artifacts\xr-live-status.json` (16:46-17:05 session):
  `binding[0] {bound:1, source:"engine", learns:5, rejects:25988, fallbacks:25977,
  sanity:{n:4,gap_mm:227.609,angle_deg:133.028}}`, `skip_reasons[0] [370,0,0,7,25970,0]`,
  `engine_dev_mm [309.66,518.38]`, `placed_dev_mm [0,0.000194]`.
- 16:33 session: `source [{engine:898,held:9695},{cross:4937,held:8398}]`,
  `place.skip [{"ambiguous":9697},{"placed_pass":61191}]` (two sides sharing one copy frame).
- `xr-menu-transfer-left/right.json`: `temporal_mode 6` (FSR 4), `antialiasing "native"`.

