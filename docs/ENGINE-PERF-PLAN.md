# Engine-side performance plan (paired mode)

Goal: raise fps in the heavy spots by engine-level work, modelled on the id Tech VR mods. Alternate-eye is excluded by the user.
Every step needs a build knob and a lab check; image quality must not regress.

## Facts measured so far (do not re-derive)

- Heavy spot: engine ~14-20 ms for the two views against an 11.1 ms budget at 90 Hz (13.9 at 72), Present hook 1-4 ms, mod CPU
  0.6-1.0 ms. The engine cost is per-view draw submission; it does not move with shadows (`noShadow 1` + preset 0 + cull 15 +
  slice 2 all applied: 14.35 -> 14.03 ms), decals, culling (`r_useGPUCulling` was a test-spot artefact), resolution or quality.
- `object-motion.json`: 266 draws per Present, per-draw bookkeeping 84.6 us. `frame_split`: eye1 costs ~1.7x eye0 (e.g. 5.2 vs 9.1).
- id Tech reference (EternalVR, DOOM Eternal VR): stereo = 2.5x mono CPU, recording/submission is the serial critical path,
  shared culling between eyes measured slower (152.8 -> 143.1 ticks/s), eye 2 reuses eye 1's shadow atlas in Parallel Eyes,
  target 72 Hz at Quest-3 size.

## v990 measurement build (done)

Per-CPU-phase counters from the draw hooks: `draws_by_phase` = `[pre,eye0,eye1,hook]` for draws, elements, instances.
A normal play session fills it; no test protocol.

## Verified ID3D11DeviceContext vtable slots (use only these unless a lab proves another)

| slot | method | verified by |
|---|---|---|
| 7 | VSSetConstantBuffers | `object_motion.hpp` install (works in game) |
| 11 | VSSetShader | `object_motion.hpp` install |
| 12, 20, 13, 21 | DrawIndexed / DrawIndexedInstanced / Draw / DrawInstanced | `dxgi_probe.cpp` gpu bindings |
| 14, 15 | Map / Unmap | `object_motion.hpp` install |
| 33, 34 | OMSetRenderTargets / AndUnorderedAccessViews | `dxgi_probe.cpp` gpu bindings |
| 44 | RSSetViewports | `dxgi_probe.cpp` gpu bindings |
| 48 | UpdateSubresource | `object_motion.hpp` install |
| 116, 119 (Context1) | UpdateSubresource1 / VSSetConstantBuffers1 | `object_motion.hpp` install |

Unknown and therefore **not to be guessed**: `PSSetShaderResources`, `PSSetSamplers`, `IASetVertexBuffers`, `PSSetConstantBuffers`.
A wrong index does not corrupt memory (the registry validates each target against d3d11.dll code and refuses, poisoning itself), but
it disables the hooks for the session. Discover them with a WARP lab: create two devices, call the method normally, then call
vtable slot N with the same arguments and read the state back with the matching Get method; only a slot that both compiles the call
and leaves the readable state equal is accepted. Record the result in this table.

## Step 2 - redundant state elision

- Per-context shadow copy (deferred contexts are each their own recording stream; the immediate context is one more).
- For each hooked setter, compare the new argument list with the shadow; identical -> skip the original call and count it.
  Compare pointers and, for viewports/scissors, the full values.
- Start with the verified slots (7 VSSetConstantBuffers, 33 OMSetRenderTargets, 44 RSSetViewports); add the PS setters once the
  WARP lab proves their indices, since texture/PS-cbuffer binds are where redundancy should be highest.
- Knob `build/no-state-elision`; status counters `state_calls`, `state_skipped` per method; lab test against a WARP deferred
  context asserting the skipped call leaves identical readable state.

## Step 3 - command-list reuse/replay for eye 2

Record eye 1's lists and replay for eye 2 with the per-view constants patched. Constraints: per-view render targets differ, so only
passes that bind the same targets and differ solely in the view constant block can be reused; hazards across lists. High risk;
gate behind a knob and compare images.

## Step 4 - shadow atlas reuse for eye 2 (retry of v817/v818)

v817/v818 swapped atlases and broke shadows because the atlas is not kept between views and 2.3% of faces have per-view caster sets.
Retry: skip the atlas clear for view 2, keep view 1's contents, and render only the union difference of casters. Needs the shadow
pass identified per view (render-pass names exist: `ARK_RENDERPASS_SHADOWS_DYNAMIC/_PREPARE/_WORLD`).

## Step 5 - UI/post-process/exposure once per tick

Small, safe. Matches EternalVR's single exposure update. Identify the passes and skip them for the second view when their inputs
are view-independent.

## Explicitly not

Shared/merged culling between eyes (measured slower in id Tech). Multiview (DX11 has none; the Vulkan patcher still has artefacts).
Alternate-eye (user decision).

## Verified by the WARP vtable probe (build/context_slot_probe.cpp, 2026-11-04)

The probe calls a candidate vtable slot with one known method's arguments on a WARP device, then reads the state back with the
matching Get method. A slot is accepted only when the call did not fault and the readable state became exactly the value passed.

| slot | method | result |
|---|---|---|
| 8 | PSSetShaderResources | MATCH |
| 10 | PSSetSamplers | MATCH |
| 16 | PSSetConstantBuffers | MATCH |
| 22 | (not PSSetShaderResources) | rejected |

So the elision census can cover PS SRVs, PS samplers and PS constant buffers with proven indices; IASetVertexBuffers is still
unproven and stays out until the same probe proves it.


