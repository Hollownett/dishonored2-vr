# Left-hand reading tablet: launcher candidate, 2026-10-28

Status: repaired and enabled by the launcher default feature list in v654.
Focused geometry, GPU, native ownership and simulated XR
checks pass. Headset visual acceptance is still pending. The full repository
build currently stops at unrelated MSVC C4702 in dlss_mode_check.

## Implemented

- The left hand holds a thin page at its knuckles. The page inherits the whole
  submitted palm pose, including wrist roll, instead of facing the headset.
- The existing finger-contact solver fits the thumb against the front and the
  four fingers behind the same thin slab used for page placement. Hinges come
  from the native hand binding. Other fingers and the right arm stay tracked.
- Note-only arm admission checks native model/source/palette identity and
  freshness without publishing a weapon attachment. Gameplay attachment policy,
  locomotion and turning retain their existing gates.
- Right stick Y feeds native note scrolling. Holding the right trigger while
  pointing at the page drags the text. A fresh press on the page is required;
  tracking, focus, epoch, time or hit loss cancels the drag until release.
- Page placement comes from the accepted skinning palette, with its original
  world-to-XR mapping. Receipt frame, tracking/reference epoch, binding identity
  and freshness must match. Missing receipts fall back to a readable front panel.
  Alternating-eye frames deliberately use this fallback until separate eye
  receipts can be composed correctly.

## Native evidence

`artifacts/note-tablet-20261028/native-scroll.txt` records exact extracted
`notes.iggy` bytecode methods and asset hash. `AbsSimpleNote` binds
`LEFT_ANALOG_STICK`; right analog input controls native artwork tilt. The
note mode reserves right axes for scrolling and physical wrist pose replaces
artwork tilt. Level-map pages share this movie; the pinned native ShowNote/ShowMap
observer now separates them. Existing B/X close buttons remain native.

The existing native ownership/ABI evidence remains in
[world-ui-native-ownership.md](world-ui-native-ownership.md). No new engine entry
point, guessed ABI, physics actor or animation-memory write was introduced.
The primary [Dishonored VR menu implementation](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/menu_immersion.cpp)
was consulted; Dishonored 2 ownership still comes from this checkout's evidence.

## Hand/page visibility repair

The note is composed into each eye using its exact rendered camera. A private
hand depth buffer uses actual installed native meshes and the accepted native
skinning palette after its synchronous upload returns. Front hands preserve the
native eye pixels; back hands are covered by the page. The isolated native note
texture retains its source transfer function and alpha. No unrelated scene depth,
proxy spheres or replacement hand colors are used.

Native resource, surface counts, inverse binds, model frame, snapshot, pose and
reference epochs, current frame and note generation must agree. A verified
native world-depth listener update is required. Unknown ownership, alternating
eyes, missing meshes or a non-own camera slot use the readable front panel.
Maps retain their original controls: only a pinned, successful ShowNote event
can enable physical note holding and right-hand scrolling. Switching notes
requires a capture scope newer than the corresponding native ShowNote call.

The note-only grip reduces finger spread and prevents crossing the thin slab.
Full native triangle/slab checks find zero intersections for Corvo and Emily at
three HUD scales. The thumb still overlaps the front; the fingers sit behind.
The imported arm meshes stay private to the user's installed game.

## Remaining acceptance

Headset checks must still cover antialiased hand edges, native material effects,
readability and right-hand crossing. The geometry mask is not a claim of
pixel-identical native shading. A focus loss invalidates note ownership; reopening
the note restores the feature after focus returns. Conservative shared-rig
clearance leaves approximately 2.7-6.2 mm gaps for Emily's thinner finger skin.
## Earlier implementation checks

Passed focused checks: native-rig page/finger contact at HUD scales 50/65, 1 and
100/65; rigid translation/rotation; note drag, repeated timer tick, input loss,
epoch and frame rejection; actual GameXr simulated note pose/fallback/controller
input; native attachment admission; full-body driver and frame latch;
controller input and existing world-UI placement. Test executables and command
files are under `artifacts/note-tablet-20261028/`. The initial sampled-skin test
allowed penetration within its 4 mm tolerance. The v654 full-mesh check supersedes
that result: zero triangle/page-slab intersections at all six character/scale
combinations. This still does not establish headset visual acceptance.

The broader `game_xr_simulation_check` stops at its wrist-overlay
assertion (line 109), before note scenarios. The production translation-unit
syntax check passes with `/WX-` because unchanged paths emit warnings C4267,
C4456 and C4127. The GameXr fixture also needs C4706 disabled for existing
assignment-in-condition frame gates. This does not establish a clean release
build or prove the unrelated wrist-overlay failure predates this change.

Review the numbered candidate
and test: opening short and long notes with weapons equipped; raise, tilt and
roll the left hand; inspect thumb/front and four-finger/back contact; right-stick
and trigger-drag direction/speed; right hand passing in front/behind; B/X close;
held inputs on exit; tracking/focus loss and recenter; HUD scale extremes; maps,
journal and shop regression; subsequent movement, turning, weapons and grabs.
Launch only when requested, directly with `-SkipPreflightWait`.
