# Native interaction prompt reuse: verified ownership and bounded runtime probe

The requested prompt is the game's existing `InteractionWindow`, including its
localized action label, native controller glyphs, hold gauge and animated ink
art. It is part of `ui/hud.iggy`, which also draws damage effects and other HUD
widgets. Capturing or hiding the whole movie cannot isolate the prompt safely.
No static asset patch or generic HUD suppression is proposed.

## Primary local evidence

Pinned Steam executable SHA256:
`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
All addresses below are RVAs unless the leading `0x14...` full address is shown.

- `artifacts/hud-re/interaction-bindings.txt`: `0x3901E0` binds the HUD file at
  element+0x90 and `_interactionsManager` at element+0x188. Add/RemoveAll/Remove/
  UpdateGauge method caches are +0x98/+0xC8/+0xF8/+0x128. This receiver is a
  plain AS3 Object, not a display object; assigning its x/y cannot move the art.
- `interaction-placement-native.txt`: update `0x391650` preserves native state
  selection, removes obsolete entries, adds pending entries and updates gauges.
  Add return address `0x38F963` passes six native arguments. Gauge return
  `0x38F743` passes two. Remove return `0x3906A9` passes one. These call bytes
  were checked directly against the pinned executable.
- `interaction-manager-as3.txt`: `_C_AddInteraction` routes to `_window` or
  `_icons` according to the native interaction type. Native arguments and this
  routing define eligibility; an arbitrary ray hit alone is not eligibility.
- `interaction-window-as3.txt`: `InteractionWindow` extends
  `AbsInteractionHUDElement -> HUDElement -> UIElement -> MovieClip`. `add`
  retains the action type, item label and native art; group layout is set by
  alignment/origin logic. Its placement is independent of the reticle.
- `hud-scene-interaction-as3.txt`: public getter `_interactionWindow` (AS3
  method5907) returns `container.interactionWindow`. This is the exact child
  binding path. HUDContainer is initialized at x960/y540. Consequently child
  coordinates are relative to this centered parent; do not pass absolute
  render pixels straight into child x/y.

## Verified binding and getter ABI

`interaction-binding-lifecycle.txt` and `interaction-value-get-rs.txt` prove:

- `0x28E50`: AS3 object constructor `(object_storage, native_file, path)`;
  initializes the inline path list and binds through `0x29A60`.
- `0x28FD0`: object destructor `(object_storage)`; releases any retained Iggy
  reference and the path list. Temporary wrappers must be destroyed on the
  same native UI thread even when a later getter fails.
- Object layout has cached Iggy at +8, optional retained reference at +0x10,
  path table at +0x18, packed one-based path index at +0x20, and packed capacity
  at +0x24. Inline table starts +0x28 with16 records of0x28 bytes; wrapper
  storage is0x2A8 bytes. This is verified by constructor capacity0x1040,
  record stride and existing embedded native receivers0x2A8 apart.
- `0x100BA70`: read property
  `(path_record, fast_name_or_zero, name_or_null, optional_flags_out,
  diagnostic_label, status_out)` returns a tagged native value. A null
  optional flag pointer is used by native typed getter callers. Status zero
  and value other than7 are required before decoding.
- `0x1009BF0` (`IggyValueGetS32RS`) proves numeric decoding: an even tag is a
  signed inline integer shifted right one; a tag whose low two bits equal1
  can be a boxed number if the byte at value-1 is7, with a double at value+7.
  The read-only probe preserves fractional values; it does not round original
  x/y to integers. Boxed pointers are decoded immediately and never retained.
- `0x1009B70` (`IggyValueGetBooleanRS`) compares exact relocated singleton
  tags base+0x2424049 (true) and base+0x2424041 (false).
- `0x296C0`: member resolver `(object, two_word_handle, name)` returns a
  fast-name handle and object pointer. Native crosshair binding `0x672BF0`
  resolves literal x/y this way. Float setter `0x2A2C0` takes
  `(object_field_address, const float*, member_handle_address)` and is verified
  by the existing native crosshair and the explicit decompile export.
- The `0x998B0` calls in native `0x28A90` are a three-byte no-op, so the
  original argument block count remains readable immediately after Call.

## Implemented probe and integration contract

`src/native_interaction_prompt.hpp` installs no hooks and writes no properties.
`configure(base, read_self)` checks the exact constructor/destructor/GetRS live
code prefixes. `observe(caller_rva, method, receiver, block, tick_ms)` validates
one of the three exact native callers, receiver offsets, argument count,
current native menu-manager ownership, exact `ui/hud.iggy` path, and render-Iggy
identity. It is limited to one probe every500ms.

Call it from the AFTER side of the existing shared `NativeWristHud::Observer`,
when the original result is1. Never install another hook at0x28A90. This
provides the native UI thread that already updates this exact prompt.
Observation fields are x/y/width/height/visible, validity, decoded-field mask,
last getter status, and file/Iggy/element ownership. Field mask31 means all five
properties decoded. Each probe constructs a temporary `_interactionWindow`
wrapper and destroys it before returning. It never keeps object-property
pointers or invokes Add/Remove/Show/Hide again.

`tests/native_interaction_prompt_check.cpp` passes `/W4 /WX`. Its injected native
API fixture checks exact child path, fractional and signed coordinates, geometry
and visibility, getter failure cleanup, unknown/invalid numeric tags and wrapper
destruction. This does not prove a real Iggy instance accepts the binding.

## Required subsequent world rendering

The user's latest clarification requires the native interaction prompt beside
the object inside the game scene, rather than repositioned on the default HUD
or submitted as a separate OpenXR overlay. Collect real prompt observations
while the game itself selects an eligible item, then establish individual
`_interactionWindow` draw ownership. Reuse only that native artwork on a
world-positioned surface rendered into each game eye, and suppress only its
duplicate HUD draw. The whole `hud.iggy` surface also contains unrelated UI and
damage art and cannot be isolated wholesale for this purpose.

The target should be the retained native interaction object used for this
prompt. Do not independently advertise every ray-hit surface. Render-pixel to
parent-stage conversion must use the actual native stage and parent's transform;
HUDContainer's x960/y540 alone does not prove scale during portrait rendering.
A getter probe on the UI update thread does not establish that AS3 writes on an
Iggy renderer worker are safe. Native x/y setter research remains diagnostic;
changing those properties alone does not meet the world-rendering requirement.
Individual prompt capture, scene rendering and duplicate suppression remain
unimplemented and unverified.

## Reading-interface assessment

The extracted `ui/notes.iggy` owns `NotesScene._C_ShowNote`,
`AbsSimpleNote.text/title`, and the native scrolling SimpleNoteScreen. It also
owns `_C_ShowMap`. `ui/narrative.iggy` owns conversation choices/cinematic skip;
it must not be classified as a reading page merely because of its name.

Current exact movie classifier and logic gates are notes=16, journal=128,
wheel=256. v199 `world-ui-2.json` proves notes movie capture at2496x2688;
alpha107..255 with no zero-alpha pixels reflects the notes scene's native halo,
noise, screenFX and vignette backdrop. Its opaque backdrop is not proof of an
uncaptured UI. v200 recorded world_ui_layers732, equal to wheel_frames732, and
its native-menu-input rows observed logic4. That run provides no evidence that
notes was opened or captured. A real reading attempt needs a native logic/movie
trace before changing capture ownership or crop classification.

## Dishonored1 reference

Pinned D1 source [HUD layout](https://raw.githubusercontent.com/VR-Stereo-Hub/Dishonored-VR/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/core/gfx/hud_layout.cpp)
lines35–67 distinguishes measured native prompt draws, unmeasured fallbacks,
full-screen vignette draws and owned reading contexts. Its prompt claiming
rectangle is measured for D1; it is not a verified D2 crop. The
[menu immersion source](https://raw.githubusercontent.com/VR-Stereo-Hub/Dishonored-VR/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/menu_immersion.cpp)
provides reference practices for render-only head tracking and owned menu state.
Use the ownership and lifecycle practice rather than transplanting D1's memory
layouts or screen coordinates into D2.
