# Note reader: v662 rejected, native consumer repair in progress

The user's v662 headset test failed: the correct DLL prepared242 palettes but
bound zero native draws. The intended v662 design below is not working-feature
or headset acceptance evidence. See [the failure record](VR-v662-NOTE-FAILURE.md).

Actual journal -> Lore & Maps -> Read reproduction also exposes native logic144
(128|16). The original exact16 checks disabled page capture and pose publication
there. Both observed reader states now share a bounded classifier; ownership
still requires the native ShowNote/ShowMap receipt. Native draw consumption
remains a separate acceptance requirement.

## v662 intended body and arm reuse

The user's latest headset test was **v661**, PID 20576. Its separate hand/sleeve
composition was rejected: another pair appeared over the player body. The exact
DLL, launcher and feedback are preserved; see [the test record](VR-v661-NOTE-TEST.md).

v662 removes the `NoteHandArt` color draw from both eyes. The existing native
body and arm meshes, materials and shaders remain the rendering path. Left page
grip and right tracked palm/finger controls go through the same calibrated
`fullbody::Driver` that normally connects the arms to the torso. The wrist moves
through that solver, without an independent shoulder reconstruction.

The native note movie, original controller page anchor/content, pause, B/grip
controls and native scrolling remain. There is no animation scheduler call,
global unpause, native model write or gameplay movement/turning change.

Paused animation no longer needs to supply new upload callbacks. At existing
native indexed draws, a scoped D3D11 replacement supplies owned skinning buffers
to the original geometry. Admission requires the exact native vertex/index
buffers, strides, offsets, format and full draw range, plus the same model's
current/previous palette resources. Every modified SRV binding is restored
after its draw. Native buffers are equality tokens, never dereferenced as COM
objects. Both current and previous skinning views use the current paused pose,
avoiding old-animation motion vectors.

The original body and both arms use one frame-latched solve. A unique palette
serial follows deferred recording through Finish/ExecuteCommandList; recording
alone cannot admit touch. Page contact/occlusion uses that arm palette only after
both original hand-surface draws have been submitted. A culled torso does not
disable the hands. Focus, tracking, model, generation, frame or reference changes
revoke admission. Missing evidence retains the original controller page and
stick scrolling, without another hand renderer.

Native route evidence: pinned executable SHA256 b671f824...6180; `25f930` uploads
48-byte joint rows, model buffer wrappers are at `+2008/+2040`, `25e4d0/25da80`
provide current/previous skin views, and `2d3aa0` creates their D3D buffers.
`24c340/12e1d00` establishes animated-affine times inverse-bind packing with
eight-joint padding. Existing `inspect_native_surface_buffers.py` and
`compare_native_surface_draws.py` establish exact geometry ownership. Calling
convention reference: [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).

Checks cover read-only source/geometry identity and rejection; affine packing;
WARP pixel movement using the original IA/shaders/material and restored state;
one shared solve per frame, connected chest roots, moving/rotating wrists,
nondefault torso-width calibration and same-palette fingertip; explicit native
draw receipts, touch/focus/release behavior; pixel equality proving no extra hand
color composition. Evidence: `artifacts/native-note-reuse-v662/`.

Headset acceptance is pending. The GPU route preserves native visibility/culling;
it cannot create a native draw that the paused renderer culled. Exact geometry
admission currently supports the single full zero-based ranges observed in the
retained native fixtures. Submitted matching draws do not prove surviving pixels
or comfort. Test note/map entry and exit, both wrists, wide reaches, page contact,
scrolling, focus/recenter and return to normal gameplay. Full repository
certification is distinct from the focused checks and DLL-only build.

## v661: tested separate-hand approach (replaced)

The custom text/map reader is removed. v659 and v660 are rejected approaches:
they copied/relaid-out content and automatically closed the native UI, contrary
to the requested reuse of the existing controller-attached game interface.

Current behavior:
- The game's original note/map movie remains open, owns its content and scroll
  state, and provides the only page texture. Original crop, left-controller
  placement, B/close and native menu pause remain. No automatic Back input,
  copied text, custom parchment, map importer, holstering or gameplay-input filter.
- Existing Corvo/Emily native hand models and diffuse textures are imported from
  the installed game. Owned copies of their bind skeletons are posed from both
  controllers while reading. This requires no native animation scheduler call,
  model-state write, global unpause or change to gameplay arm publication.
  Existing arm posing supplies the native sleeves, preserving the exact wrist
  and finger pose rather than exposing the model's authored wrist seams.
- The existing native edge-grip layout is fitted to the actual page's bottom
  edge, keeping the left wrist below the sheet without changing the page anchor.
  The left finger solver contacts that page. The right index
  stays extended for touch; its displayed distal transform supplies the contact
  point. A 12 mm contact slab starts a swipe; 22 mm retains it. Losing contact,
  tracking/focus, page generation or XR epoch requires a physical lift/reentry.
- A right-hand up/down touch swipe feeds the native note's left-analog Y input;
  the right stick remains an alternative. Neither trigger nor ray pointing is
  required. Native B/grip controls remain untouched.
- Original game page pixels and textured hands are composed into both eyes with
  hand/page depth occlusion. If hand assets/tracking are unavailable, the original
  controller-attached page remains usable. Model replacement revokes old hands
  and touch state before a new validated binding is admitted.
  A temporary loss of XR focus suspends hands/touch; the same still-valid native
  reader resumes on focus return without requiring another ShowNote call.

Why hands are drawn separately while the native menu is open: the paused native
lane skips animation phases. The earlier render-gate hook cannot create absent
callbacks. Calling the native refresh manually lacks proven worker/lifetime
exclusion. This implementation uses owned model data only and leaves that native
pipeline alone. All menu hand resources retire with the existing GameXr owner.

Validation: real installed model export/parse (four textured packs); /W4 /WX hand
skinning/contact/scale and D3D11 depth/state/UV tests; native ShowNote/ShowMap
ownership fixtures; GameXr test using actual native meshes with gameplay=false,
original page anchor, two-eye composition, moving geometry, zero-trigger physical
swipe, unchanged B/grips and cleanup. The right wrist pose is checked against the
ordinary gameplay retarget, and left edge contact is checked at nine combinations
of hand/text scale. A synthetic source texture in the offline
fixture is only a test surface, not game content or headset evidence.

Remaining acceptance: actual headset pickup/read/map transitions, finger contact,
scroll direction/speed and visual comfort. Hand shading uses native diffuse maps
without native scene lighting/specular. Native menu pause is retained; this change
does not add walking inside that paused screen. Full repository certification has
the recorded unrelated DLSS /WX blocker. Candidate/build evidence lives under
artifacts/native-note-v661 and artifacts/launcher-native-note-v661.

## Earlier rejected implementation history

# v660: restore controller attachment and hand input after rejected v659

v659 is rejected and launch-blocked in the workspace. The user's real XR run
(PID 41240, installed proxy log naming v659) copied two documents but recorded
1702 reader frames, zero ready frames, zero eye draws and zero hand receipts.
The recorded tracking reason reached ready. Gameplay attachment admission was
allowed, but the note branch incorrectly called validate_visual, which rejects
allowed gameplay hands. This rejected the normal tracked palettes. At the same
time the portable flag suppressed the existing controller-attached quad, and
the invisible session consumed grips/triggers until dismissal.

v660 repairs those dependencies:

- Native tracked hand palettes use ordinary attachment publication, regardless
  of optional note skin receipts. Fingers cannot veto controller-driven arms.
- The existing left-controller page placement remains available whenever the
  left controller is tracked. Missing/stale skin receipts cannot hide the page.
- Right-hand pointing intersects the page actually displayed, including the
  original controller quad. Text and map legends keep right-stick/drag scrolling.
- No automatic holster requests, global grip/button suppression or disabled
  weapon grip state machines. A fresh ordinary hand action dismisses the page
  and reaches gameplay unchanged; walking and turning remain available.
- Only a fresh right-trigger press over the visible page belongs to scrolling.
  That trigger remains consumed until physical release after pointing/focus loss
  or B dismissal, preventing an accidental attack. B closes the page.
- Owned document content and normal native Back remain to allow walking while
  reading. There is no new native engine hook or ABI in this repair.

Checks: focused capture/session/scroll/GameXr tests passed, including missing
receipts, a moving controller quad, pointing at that quad, gameplay action
passthrough and held trigger ownership. Native attachment fixture reproduces
visual-only rejection under gameplay policy and verifies ordinary publication.
Source and build/package evidence are under artifacts/note-repair-v660 and
artifacts/launcher-note-v660. These checks are not headset visual acceptance.

Limitations: finger contact enhancement requires observed empty hands; equipped
weapons retain their existing pose and behavior. Map grip+stick zoom is removed
so the grip remains a gameplay control. The full imported map and scrollable
legend remain readable. Visible finger alignment, pickup transitions and walking
must still be validated in a real headset. Full repository certification retains
the recorded unrelated DLSS /WX blocker.

## Historical v659 implementation (rejected)

# Portable notes and maps: gameplay reader, 2026-10-28

The v657 headset result was rejected: the note stayed on a front panel. Its
recorded reading interval had 379 frames, zero hand uploads and zero eye draws.
The render-gate workaround made zero overrides. It did not solve the feature.

v659 automatically puts a picked-up note or map into the left-hand reader. No extra button or mode is required. Opening a saved document uses the same reader.

The implementation replaces menu-dependent reading with owned documents:

- Copy full localized note title/body from the verified native string setter,
  then match its successful ShowNote call. Native pointers are never retained.
- Copy map resource identity and ordered localized legend labels. The launcher
  imports all 16 complete 2048x1168 images from the user's installed game into
  `artifacts/game-art/maps/`; game assets are not embedded in the launcher.
- Prepare the entire document before issuing ordinary native Back input. Wait
  for the reader state before accepting gameplay; dismiss a remaining journal
  only when logic128 and its exact journal movie owner agree. Never change
  global pause clocks or pretend a menu is gameplay.
- In ordinary gameplay, keep movement and turning and reuse native rendered
  arms, the existing finger contact solver, and exact-frame hand/page receipts.
  Holster equipment through existing native queues, confirm empty hands from
  observed state, and bound retries. The prior paused-render hook is unused.
- Right stick Y or right-trigger pointed drag scrolls notes. Drag pans maps;
  right grip plus right stick Y zooms. Point at the legend to scroll its labels.
  Right stick X remains turning. B dismisses the held document; held triggers
  and grips remain consumed until release so closing cannot cause an attack.
- Missing current hand receipts hide a portable document instead of placing a
  new front-facing fallback panel. Unrecognized/failed content acquisition keeps
  the original native reader available and does not auto-close it.

Notes use independent UTF-8/markup layout with paragraph breaks and bounded
128 KiB input. Maps preserve their full image aspect and numbered legend; zoom
is clamped to 1-8x. GPU textures are reused and document pixels regenerate at
most about 30 Hz while scrolling input accumulates every frame. This does not
change game rendering resolution or frame rate. Owned textures retire on the
render thread with XR resources.

Checks: native ABI/hash/copy and ownership fixtures; delayed native-reader
registration and journal-only close; movement/turn preservation and held-input
release; long/Cyrillic note text through its last line; full map import,
corruption detection, pan/zoom and long legend; simulated two-eye document
composition during gameplay and no front fallback. Production syntax passes.
These are offline checks. Full repository certification still has the recorded
unrelated DLSS /WX blocker; actual headset behavior, visual finger contact,
scrolling comfort and simultaneous walking remain acceptance work.

Native evidence uses pinned Steam executable hash b671f824...6180. The content
setter is 0x2a720 (RCX wrapper, RDX text, R8 member, canonical EAX bool); title/body
returns are 0x790708/0x790723. Map legend setter returns are 0x7904c3/0x790508;
ShowMap/ShowNote returns are 0x790574/0x790755. The extracted native functions and
AS3 methods establish the producers and ordinary close flow. Public ABI reference:
[Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).

## Earlier candidates and evidence
# Left-hand reading tablet: launcher candidate, 2026-10-28

Status: v654 is enabled by the launcher default feature list, but the user
reported that physical note holding does not activate. The installed DLL hash,
enabled knob, four imported meshes and captured native note artwork were checked;
the launcher setting reaches the runtime. The v655 reproduction confirmed two
ShowNote calls, four call-owner rejections, zero accepted shows, 158 reading
frames and zero arm uploads. Do not treat the earlier offline repair as headset
acceptance.

The v657 repair candidate retains a verified paired ShowNote identity for up to
two seconds while drawable registration completes. It still requires the exact
native movie root and current screen owner before enabling note controls or
hand placement. Its capture barrier uses the observed event generation, so a
delayed owner does not permanently discard that note's artwork. Maps and owner
loss still invalidate the note.

Native render function 0x541270 asks animation gate 0x54b170 whether the clock
advanced or the palette is dirty before uploading the hands. A paused clock can
therefore suppress controller updates. The candidate permits this exact render
caller (return 0x5412d1) to refresh only a currently owned, freshly tracked note
arm and its verified body pose dependency. The body refresh allows recovery
after tracking is cleared while paused. Simulation caller 0x541194 retains the original result; no native clocks or
dirty flags are written. Both function bodies and the animation vtable slot are
pinned to the installed executable. The gate passes clock bits in EDX and returns
AL; this follows the extracted call sites and the
[Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).

The runtime has not yet proved that this render function runs while reading.
`note_tablet.arm_refresh` records render checks, native declines and permitted
refreshes to distinguish an absent callback from a blocked gate. v657 remains
a headset-test candidate, not visual evidence of working hand attachment.

The v655 diagnostic candidate preserves activation rules and adds a `note_tablet`
section to `artifacts/xr-live-status.json`: native ShowNote signature/call/owner
status, tracking prerequisite, last composition fallback, note frame counts,
per-hand upload/admission/publication counts and eye draws. Counters survive
closing a note, so one reproduction can distinguish opening-time ownership loss
from missing hand updates or camera receipts. Open the same note, leave it open
for a few seconds, close it and exit normally; retain the run's status file.
This is a diagnostic candidate, not an activation fix.

Earlier implementation evidence:
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
[world-ui-native-ownership.md](world-ui-native-ownership.md). The v657 gate audit
is retained in `artifacts/note-activation-20261028/native-arm-update*.txt`.
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
