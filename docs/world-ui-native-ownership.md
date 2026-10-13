# Native world UI ownership and draw isolation

The requested wheel, notes and journal must retain the stereo world behind the
UI and use separately rendered UI artwork. A crop of the complete game image
does not isolate the UI.

## Primary reference

Dishonored 1 VR is pinned to `b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec`.
Its [menu immersion implementation](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/menu_immersion.cpp#L24-L80)
uses render-only head tracking with validated camera, controller and pawn owners.
The same file's [UI effect handling](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/menu_immersion.cpp#L258-L328)
targets the UI blend while preserving the game's effect timelines. Its README
describes the wheel as a world dial, but describes books and ordinary menus as
flat screens and its F10 settings as a separate panel. Native pause submenu
settings and world text surfaces here are the current user's requirements.

## Pinned Dishonored 2 evidence

Executable SHA-256:
`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
All addresses below are RVAs for that executable. They are not Dishonored 1 ABIs.

- `artifacts/hud-re/d2.txt` and `raw1.txt`: `0x2DD580` is the per-movie draw
  function. Its five arguments are render context, Iggy render object, color
  descriptor, depth descriptor and an unsigned-byte HUD-rectangle flag. The
  prologue bytes are `48895c240848896c24104889742418574883ec40807c247000`.
  The wrapper forwards all arguments to the original once.
- `0x2BFB0` is exactly `MOV RAX,[RCX+0x1608]; RET` in the pinned executable.
  This is the **render** Iggy object. `0x2BEF0` returns `[file+0x1618]`, the
  player used by input. These identities must not be interchanged.
- Read-only Ghidra export `artifacts/hud-re/world-ui-feasibility.txt`:
  `0x2C880` receives the requested path in argument 2 and copies it as an
  `idStr` at `file+0x1658` after successful creation. `0x1D9090`, the string
  accessor, returns the first pointer of its object. Consequently
  `[file+0x1658]` supplies the movie path. The path is read with a 256-byte
  terminator bound; names that are absent, unknown or unterminated are refused.
- The installed game's `base/game1_patch.index`, read with the existing bounded
  `tools/extract_arm_assets.py::index_entries`, names
  `ui/quick_access_wheel.iggy`, `ui/notes.iggy` and `ui/journal.iggy`.
  The wheel belongs to `ui/powerwheel.iggyfile`; it is a distinct movie, while
  `ui/lib_wheel.iggy` is also a HUD library. Classify only the three exact main
  movie paths, never the shared library or the entire HUD.
- `native_menu_state.hpp` verifies the native manager root, callback owner,
  latest batch, wrapper bounds, file/player/dispatch identity and cursor cache.
  World UI lookup additionally compares `file+0x1608` with the draw argument and
  checks the movie name and player again after reading. Unknown owners stay on
  the original render path. This bounded lookup covers the latest batch; an
  older batch's movie is deliberately not inferred to be current.

## Render target and recording boundaries

`artifacts/hud-re/d3.txt` shows `0x2DD580` resolving the color descriptor's
cached RTV at `descriptor+0x60` through `0x2BD230` and the depth descriptor's
cached view at `descriptor+0x38` through `0x2BD070`. Color resource creation uses
the underlying image's resource at `[*descriptor+0x130]`. These are COM views
and resources, so the D3D integration must validate their type, dimensions and
matching identity through COM before redirecting a render target.

The cached color field is directly the COM RTV pointer, not a table row:
`0x2BD230` returns `*param3` immediately when populated, with `param3` supplied
as `color_descriptor+0x60` by the caller's `LEA R8,[RSI+0x60]`. Otherwise it
calls the device's vtable slot `0x48` (`CreateRenderTargetView`) using the
resource at `[*color_descriptor+0x130]`, stores the resulting view in that
cache field and returns it. `0x2E3840` calls the returned view's slot `0x40`
(`ID3D11RenderTargetView::GetDesc`) and compares its dimension with 6
(`D3D11_RTV_DIMENSION_TEXTURE2DMS`). The slot identities and descriptor use
corroborate the pointer's type; runtime COM validation remains required.

`0x2E3840` stores these views in the Iggy renderer; it does not itself bind them
to a D3D context. Per-movie render goes through `0x100C7C0` and `0x10363A0`.
`0x10363A0` calls the Iggy graphics callback at `[DAT_143EC0A58+0xF8]` before
returning. `world-ui-flush.txt` also shows this callback inside `0x1035F40`.
Native callbacks may encode work for later execution. Capture intercepts the
scoped `0x2E3840` target setter; actual D3D binds and draws need not occur inside
the CPU movie scope.

There is also a final loop ClearState: `0x31A5F0` is a null-checked tail jump to
`0x2E2ED0`, whose argument is unused and which calls the backend vtable slot
`0x370` then resets renderer bookkeeping. Extending a single movie's scope
across that final ClearState cleanup could capture unrelated movies. The implementation
therefore scopes only the original per-movie call and makes no descriptor
writes or repeated native draw calls.

## Required runtime evidence

Static ownership and synthetic tests establish the lookup and hook signature,
not the final image. D3D capture must demonstrate isolated native movie pixels
after matching `FinishCommandList` / `ExecuteCommandList` completion, retain any
deferred context involved, and verify alpha and absence of scene pixels. A valid owner
without captured native draws must not hide the original UI. The native scene
must continue stereo rendering/head tracking during the identified UI state,
while gameplay actions and locomotion retain their existing gameplay gate.

Headset checks still need to cover readable notes/journal, wheel placement and
selection, menu-to-game heading continuity, UI disappearance on close, and
loading/focus/owner replacement. Automated checks do not establish visual
quality, comfort or complete gameplay.

The subsequent export `artifacts/hud-re/world-ui-target-encoding.txt` identifies
the backend as ID3D11DeviceContext: slots 33/43 are `OMSetRenderTargets` and
`RSSetState`; `RSSetViewports` is slot 44. Slot 110 is `ClearState`; `Flush` is
slot 111. Descriptor caches remain native. Retired capture RTVs are retained
until process exit because Iggy stores a raw pointer. This intentionally retains
small capture resources across resolution changes; it is not complete GPU cleanup.

`0x2DF140 -> 0x2E31C0` installs the context at
`[*(base+0x32E8F38)+8]`. Observed native contexts have inline heap vtables at
`context+8`, so a module-owned vtable check alone incorrectly rejects them.
The integration safely reads slots 0/3/112/114 and verifies their function
addresses against executable sections of the loaded system `d3d11.dll`, then
checks COM type and device identity. Capture clears on that owned native
deferred context. Immediate contexts additionally require the Present owner
thread; the immediate Present context must never be called from the native
UI worker.

## v199 observed result

The wheel and journal runs produced isolated native art and completed XR world
layers over stereo projection. The settings run clicked the actual native VR
Settings item and changed only the measured menu content rectangle; outside
pixels matched exactly. Evidence and limitations are recorded in
[the feedback report](feedback-2026-10-10.md). Notes use the same verified capture
pipeline but were not opened in the final live trial. Headset placement and
readability remain acceptance checks.
