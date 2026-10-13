# Native child artwork capture

The pinned D2 executable's `0x1037930` native display-node renderer is the
ownership boundary. The v206 live observer identified native prompt nodes;
`native-hud-art-geometry.jsonl` reports all geometry fields and the original
stage transform `[1,0,0,1,960,540]`. This establishes node identity, not captured
pixels or headset acceptance.

Primary executable exports:

- `artifacts/hud-re/native-display-node-path.txt`: `100BA70` with a null name
  resolves the bound AS3 value. `100D280` proves DisplayObject tag24 and native
  node pointer at tagged value+27. `1037100` proves parent80 and root iggy+26B0.
- `artifacts/hud-re/native-display-draw-abi.txt`: `1037930` arguments are
  `(iggy,matrix,node,aux,pointer5,pointer6,uint32 clip,float depth)`, returning
  the original EAX value. Matrix is six floats; the hook preserves arguments,
  result and native node metadata by default.
- `artifacts/hud-re/d3.txt`: `2E3840` stores the native RTV at renderer+B90 and
  renderer context comes from renderer+8. This setter does not flush draws.
- `artifacts/hud-re/world-ui-feasibility.txt`: `2E2ED0` is the movie end/reset
  path. Its use as an arbitrary child batch flush has not been established.

## Parent integration

Install `NativeHudChildDraw` only after verified executable ownership. Publish
the exact child from the owned native UI Call observer; never invoke AS3 getters
on Present or an arbitrary worker. Probe paths `_interactionWindow` and the
public MasterHUD slots `_masterHUD._healthMeter`, `_masterHUD._manaMeter`,
`_masterHUD._equipmentIcon`. Individual wrist paths still require live binding
proof. Bindings expire250ms; a500ms diagnostic probe is insufficient for a
continuous producer. Clear prompt bindings on native Remove caller3906A9 and
owner replacement.

`NativeHudChildCapture` has no engine hooks of its own. In the child Begin
callback read renderer from base+32E8F38, context from renderer+8 and root RTV
from renderer+B90. Validate COM dispatch/device identity using the existing
core guards. The context can be null outside native draw. Never substitute
Present's immediate context for a worker's deferred context.

Call `begin(context,rootRTV,scope,slot,eye,serial,presentThread)`; slots0..11 and
eyes0/1 have independent surfaces. Begin requires current sole OM target equal
to the verified root. It binds a distinct transparent target of the same size
and format. A declined Begin leaves the original native draw unchanged.

The existing OMSetRenderTargets hook should consult
`replacement(context,count,targets)`. Only exact root rebinds route to the
capture. Native internal filter targets pass through. The child End callback
calls `end(scope)`; it disarms routing before restoring the original target.

Wire `finished(context,list)` after successful native FinishCommandList and
`executed(originalList)` after complete native execution, including any FSR
split remainder. `snapshot(slot,eye)` remains empty before execution. The
returned source is retained native artwork with original owner/epoch metadata.
This is ordered CPU submission proof; subsequent copying must use that same
immediate queue, rather than treating it as a GPU-fence completion signal.

Completed RGBA readback can use `alpha_bounds(bytes,width,height,pitch,length)`
to obtain the measured native artwork crop. Native world-placement consumers
must use this or otherwise verified actual pixel bounds, not assume local
Flash width/height imply a registration origin.

## Verification and remaining gates

`tests/native_hud_child_capture_check.cpp` passes WARP `/W4 /WX`: native shader
pixels and alpha preserved, original scene unchanged, original root restored,
internal targets passed through, deferred snapshots withheld until execution,
separate eyes and measured alpha bounds. Existing native child hook and geometry
fixtures also pass. These are GPU/ABI fixtures, not live visual acceptance.

The live producer must additionally show actual D3D draws inside the exact child
scope and a nonempty completed alpha crop. If Iggy emits a batch after that
scope, the capture cannot yet be relied upon; do not force an unverified flush.
Per-instance goal/NPC marker node-to-worldpoint bindings remain separate work.

The pinned [D1 native HUD reference](https://raw.githubusercontent.com/VR-Stereo-Hub/Dishonored-VR/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/core/gfx/hud_layout.cpp)
uses measured owned native draws, dedicated sinks, and separate full-screen
effect treatment. Its rectangles and engine layouts are not D2 ownership proof.

## Wheel crop correction

The actual native `world-ui-1.rgba` is2496x2688, with selector controls and potion
shortcuts above/below the central16:9 band. GameXr now shows the full native
wheel image and uses that same full-image mapping for its cursor. Its aspect
is preserved on an eye-height world surface. The underlying scene projection's
avatar must be suppressed by the separately owned body renderer for logic256;
the capture itself does not contain the character. Simulation verifies full
source extent and aspect, but full-selector headset acceptance remains pending.
