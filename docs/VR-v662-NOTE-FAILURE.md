# v662 rejected headset test, 2026-10-28

The user tested the launcher and saw the previous note/map view without moving
hands. This is a failed feature test, not an accepted repair.

The actual installed v662 DLL matches SHA256
`54a2614c9698b918ca9fc7c4de4e8bcc989afa674ab26940f62a5812873b6912`.
Proxy log identifies PID 58004 and that exact numbered DLL. The note interval
entered native logic16 at 16:46:41 and returned to gameplay at16:46:45 in the
launcher's timestamps. Evidence is preserved under
`artifacts/note-v662-rejected-20261028/`.

Final counters:243 note frames,242 prepared palettes, **zero bound native
draws, zero ready frames, zero eye compositions**. The original controller page
fallback was therefore expected. No stale-launch explanation is supported.

v662's tests exercised the shared solver and a synthetic native-like graphics
draw. They did not establish that the actual paused game submitted a matching
original hand draw with the expected shader resource. The prior completion
report overstated that production connection.

The remaining dependency is now explicit:

1. Separate callback, frame/focus/epoch, IA geometry, palette resource and
   replacement-error rejection evidence during the actual native reader.
2. Verify the engine's selected geometry and skinning route. Pinned25e4d0 can
   select replacement geometry and a pooled identity palette; never substitute
   articulated skinning data into that shared identity view without proving
   the deformation producer. Static proof is in `native-cached-route.txt`.
3. Repair the observed native consumer route, keeping original embodiment,
   original page and pause. Preserve accepted movement, turning and settings.
4. Require actual-game note/map entry, continuously changing original hand
   pose consumption, same-pose contact, and close cleanup before publishing a
   replacement as a repair candidate. Headset appearance remains human acceptance.

A diagnostic build is an investigation artifact, not a working-feature release.
Do not promote another palette-only synthetic pass to headset success.
