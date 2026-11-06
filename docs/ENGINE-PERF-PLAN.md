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
| 123 (Context1) | PSSetConstantBuffers1 | `tests/context_slot_probe.cpp` v995, immediate + deferred |
| 131 (Context1) | SwapDeviceContextState | `tests/context_slot_probe.cpp` v995, immediate (deferred is a documented no-op) |

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

## v992 (2026-11-04): the ten-method registry, PS state census, elision behind `build/no-state-elision`

Landed:

- `GpuDispatchBindings` default is now ten methods: the seven context methods keep their indices (0-6: 44, 33, 34, 12, 20, 13,
  21), and 7-9 are the probe-verified PS setters (8 PSSetShaderResources, 10 PSSetSamplers, 16 PSSetConstantBuffers). The
  registry's static assert in `tests/gpu_dispatch_bindings_check.cpp` pins the count at ten.
- `src/ps_state_census.hpp`: per-context shadow of the three PS arrays (128 SRV / 16 sampler / 14 constant-buffer slots),
  hashed into 16 context records. `consider()` counts every call and, when every slot in the range is known and equal, counts
  a repeat; with elision on it returns "skip" and the hook does not forward the call. `repeats` therefore measures the
  elision opportunity in the same run that elision is on.
- Invalidation (`generation++`, every slot unknown): a render-target set change on that context (this is also how a
  `ClearState` and a swapped device-context state show up, because both unbind the targets), the UAV render-target form, a
  CPU phase change (pre/eye0/eye1/hook), the frame barrier at Present, and the mod's own `ContextState` scopes, whose calls
  are not offered to the census at all (the swap restores exactly the game state the shadow holds). The residual hole is
  documented in the header: a `ClearState` (or state swap) immediately followed by rebinding the identical targets and PS
  state with nothing observed in between would still look valid; no engine path has been seen doing that, and the census's
  repeats-per-method plus the image A/B is how a case would show.
- Status: `ps_state` = `{enabled, calls[3], repeats[3], skipped[3], barriers, contexts}`, next to `rt_binds`/`rt_bind_repeats`,
  in both status writers. Knob `build/no-state-elision` turns elision off but leaves the census counting.
- Lab checks: `tests/ps_state_census_check.cpp` (model: repeat/skip decision, barriers, phase, per-context records, hashed
  collisions cost elision but never an incorrect skip) and `tests/ps_state_census_warp_check.cpp` (WARP device: after every
  call the census skips, PSGetShaderResources/PSGetSamplers/PSGetConstantBuffers must return exactly what the skipped call
  would have set; barrier/phase/context rules; a deferred command list executed on the immediate context is the pass boundary
  the barrier exists for).

Next measurement, in this order: (1) run normal play with the census on and read `calls` vs `repeats` per method - that is
the ceiling for elision before any A/B; (2) `build/no-state-elision` present/absent A/B at 72 Hz, comparing fps and the same
frame_split/`draws_by_phase` numbers; (3) only if repeats are high and the image A/B is clean, consider the same treatment for
IASetVertexBuffers once the probe proves its slot.



## Review of v992 (Opus 5.5, 2026-11-04) - do this before any in-game run

Measured during the review: the user's RX 9070 XT reports `D3D11_FEATURE_THREADING` DriverConcurrentCreates=1,
**DriverCommandLists=0**. The D3D11 runtime emulates command lists: job threads record into d3d11's own buffer, then the whole
stream is replayed serially inside `ExecuteCommandList` on the render thread. That replay is the "amdxx64+d3d11 73% of a core".

Blocking:

0. **v992 poisons the GPU hook registry in game.** `GlyphSwap::install` (slot 8) and `SamplerLodBias::install` (slot 10, on by
   default) MinHook the same immediate PS setters, on deferred and immediate, before `ensure_game_gpu_hooks` runs
   (dxgi_probe.cpp ~12615-12618). The registry's `MH_CreateHook` on those targets returns MH_ERROR_ALREADY_CREATED, `bind` fails
   with "graphics hook creation failed", and none of the ten methods is enabled: viewports, targets and draws are all lost
   (paired target observation, HUD art capture, world-UI redirect, frame split). The WARP lab cannot see it. Rule: one hook per
   target. Put the census inside the existing slot-8/10 hooks, applied to the final forwarded arguments, and return the registry to
   seven methods. Those hooks already cover the deferred implementations too.
1. **v992 elides in the wrong place for the goal.** The three hooks sit on the immediate context's functions only
   (`gpu_deferred_methods_shared` all false; `object_motion.hpp:182-198` hooks the deferred methods separately for exactly this
   reason). The scene's PS binds are recorded on deferred contexts, so the census cannot see them and elision cannot shorten the
   replay. Whatever repeat rate it reports describes the immediate-context passes only.
2. **Elision is on by default, and the hole is the normal path, not an edge case.** Every `ExecuteCommandList` changes the
   immediate state (RestoreContextState=FALSE leaves it at default), and nothing emits a barrier. `note_rt_bind` skips the
   barrier when the same targets are rebound, which is exactly what follows a reset. `ClearState` (110) is unhooked.
   `CSSetUnorderedAccessViews` unbinds overlapping PS SRVs, and the mod binds CS UAVs on the immediate context itself
   (`fsr_taa.hpp:349,840`, `native_art_bounds.hpp:115`, `stale_eye_warp.hpp:792`). Failure: a needed PSSet is skipped, the slot
   stays NULL, and post/HUD/composite surfaces go black or wrong in the headset.
   Fix: make elision opt-in (`build/state-elision`), and call `ps_state.barrier(c)` before and after the execute path in the
   existing `TaaDispatchHook::execute_handler` (dxgi_probe.cpp, next to `native_hud_art_capture.executed`). Never elide while that
   hook is not installed.
3. (Corrected) DLLs build with the `build/compile-vNNN.cmd` pattern (`/W4` without `/WX`, as v989-v991 did). New code must still
   add no warnings, and every touched `tests/*_check.cpp` must run and pass.

Direction (user, 2026-11-04: no cheap wins; do the engine work):

- **Measure the replay first, as part of the work:** time `TaaDispatchHook::execute_original` with QPC in the execute lambda,
  summed per phase per frame, plus the ECL count and the restore TRUE/FALSE counts. This splits the 14-20 ms into serial replay
  vs engine work, and decides which engine change comes next.
- **Main track: a full redundant-state filter on the deferred (recording) contexts.** This covers the PS setters, then the other
  setters the census shows are redundant. Barriers go at FinishCommandList, ClearState, the deferred OM binds and CS UAV/SO output
  binds. Prove each slot on a WARP deferred context first. Elision stays opt-in until a sim image A/B is clean.
- If the replay is not the critical path, the next engine work is plan step 4 (second-view shadow reuse) or step 3 (eye-2 list reuse).

Order: (a) fixes 0 and 2 -> (b) ECL timing + deferred census build -> (c) one sim run, elision off -> (d) deferred elision A/B in
the sim with images -> (e) headset at 72 Hz.

## Hook ownership (v993)

One hook per target is a hard rule: a second `MH_CreateHook` on a code address returns `MH_ERROR_ALREADY_CREATED`, and
`GpuDispatchBindings::bind` treats that as failure and poisons every GPU hook for the session (the v992 failure, review item 0).
A slot with an owner below is therefore never hooked by another component. "Both" means the immediate and the deferred
implementation. The v993 probe measured the two as different functions for all eight slots it checked; the other "both" rows are
explicit two-vtable installs (`ObjectMotion::hook2`, `TaaDispatchHook::hook_clear`).

| slot | method | owner | context kind | role |
|---|---|---|---|---|
| 7 | VSSetConstantBuffers | `object_motion.hpp` (`hook2`) | both | per-eye MVP bookkeeping + wide-census observer (v997) |
| 8 | PSSetShaderResources | `glyph_swap.hpp` (`hook_table`) | both | glyph substitution + PS census |
| 9 | PSSetShader | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 10 | PSSetSamplers | `sampler_lod_bias.hpp` (`hook_table`) | both | mip bias clones + PS census |
| 11 | VSSetShader | `object_motion.hpp` | both | per-eye draw bookkeeping + wide-census observer (v997) |
| 12 / 20 / 13 / 21 | DrawIndexed / DrawIndexedInstanced / Draw / DrawInstanced | `GpuDispatchBindings` methods 3-6 | immediate + the deferred target set it binds | draw census, note/HUD draws |
| 14, 15 | Map / Unmap | `object_motion.hpp` | both | per-eye constant updates |
| 16 | PSSetConstantBuffers | `ps_constant_buffers.hpp` (v993) | both | PS census |
| 17 | IASetInputLayout | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 18 | IASetVertexBuffers | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 19 | IASetIndexBuffer | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 24 | IASetPrimitiveTopology | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 25 | VSSetShaderResources | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 26 | VSSetSamplers | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 33, 34 | OMSetRenderTargets / AndUnorderedAccessViews | `GpuDispatchBindings` methods 1, 2 | immediate + the deferred set (bound by `begin_native_hud_art`) | paired capture, HUD art, PS/wide barrier |
| 35 | OMSetBlendState | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 36 | OMSetDepthStencilState | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 37 | SOSetTargets | `context_barriers.hpp` (v993) | both | PS/wide barrier only |
| 41 | Dispatch | `taa_dispatch.hpp` | deferred | native TAA dispatch |
| 43 | RSSetState | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 44 | RSSetViewports | `GpuDispatchBindings` method 0 | immediate + the deferred set | paired capture, HUD art |
| 45 | RSSetScissorRects | `wide_state_census.hpp` (v997, opt-in) | both | wide census, count only |
| 48 | UpdateSubresource | `object_motion.hpp` | both | per-eye constants |
| 53 | ClearDepthStencilView | `taa_dispatch.hpp` | both | depth observation |
| 58 | ExecuteCommandList | `taa_dispatch.hpp` | immediate | replay handler + PS/wide barrier |
| 68 | CSSetUnorderedAccessViews | `context_barriers.hpp` (v993) | both | PS/wide barrier only |
| 110 | ClearState | `context_barriers.hpp` (v993) | both | PS/wide barrier only |
| 114 | FinishCommandList | `taa_dispatch.hpp` | deferred | list observation + PS/wide barrier |
| 116, 119 | UpdateSubresource1 / VSSetConstantBuffers1 (Context1) | `object_motion.hpp` | both | per-eye constants; 119 also has the wide-census observer (v997) |
| 123 | PSSetConstantBuffers1 (Context1) | `ps_constant_buffers.hpp` (v995) | both | count `cb1_calls`, invalidate the covered CB slots, never elided |
| 131 | SwapDeviceContextState (Context1) | `ps_constant_buffers.hpp` (v995) | immediate (deferred: documented no-op) | barrier the immediate context before/after a game-owned sighting; count and record its caller (v996) |
| swapchain 8, 13 | Present / ResizeBuffers | `dxgi_probe.cpp` | swapchain | frame boundary, output size |

Slots 33/34 on the deferred implementation are the one place a "barrier hook of its own" is deliberately not created: the
registry owns that target set. v996 binds it at `ensure_game_gpu_hooks` time from a deferred context of the verified game
device, before gameplay, using the same registry (`simultaneous_targets=true`, capacity 4 per method). `begin_native_hud_art`
still binds the game's own HUD deferred set when it runs; equal targets are reused, a different set is a third binding. The
barrier comes from the registry's own `targets_hook` (`note_rt_bind`, which barriers on every OM bind, including the
same-target case), and the census reports the deferred kind not ready only if that bind did not happen.

## Verified vtable slots (v993 probe, WARP, 2026-11-04)

`tests/context_slot_probe.cpp` calls each candidate slot through its raw vtable entry on an immediate AND a deferred context,
with the arguments of the method it claims, and reads the state back through the real interface method (whose own index cannot
be wrong). ClearState is proved by binding a PS SRV, calling slot 110 and reading null.

| slot | method | immediate | deferred |
|---|---|---|---|
| 8 | PSSetShaderResources | MATCH | MATCH |
| 10 | PSSetSamplers | MATCH | MATCH |
| 16 | PSSetConstantBuffers | MATCH | MATCH |
| 33 | OMSetRenderTargets | MATCH | MATCH |
| 34 | OMSetRenderTargetsAndUnorderedAccessViews | MATCH | MATCH |
| 37 | SOSetTargets | MATCH | MATCH |
| 68 | CSSetUnorderedAccessViews | MATCH | MATCH |
| 110 | ClearState | MATCH | MATCH |

For these eight slots the immediate and the deferred entry are different code addresses on WARP, so each is two hooks (the mod
still compares targets at install and never creates a second hook on one address).

v995 adds two ID3D11DeviceContext1 slots. Slot 123 is proved on both kinds with `PSGetConstantBuffers1` reading back the exact
buffer, `first=16` and `num=32`. Slot 131 is proved on the immediate context by swapping in a fresh default state (the bound PS
SRV reads null), then swapping the previous state back through the real method (the SRV reads again). The deferred context is
reported as not supported by design: Microsoft documents `SwapDeviceContextState` as immediate-only, so a deferred call is a
no-op. Note the header layout: after `PSSetConstantBuffers1` (123) there are `CSSetConstantBuffers1` (124) and six
`*GetConstantBuffers1` methods (125-130), so `SwapDeviceContextState` is **131**, not 125.

| slot | method | immediate | deferred |
|---|---|---|---|
| 123 | PSSetConstantBuffers1 | MATCH (first/num readback) | MATCH (first/num readback) |
| 131 | SwapDeviceContextState | MATCH | NOT SUPPORTED (by design) |

## v993 (2026-11-04): deferred PS census, opt-in elision, replay timing

Fixes from the v992 review:

- **Registry poisoning (item 0).** `GpuDispatchBindings` is back to seven methods; the static assert in
  `tests/gpu_dispatch_bindings_check.cpp` pins seven. The PS census now runs INSIDE the existing owners of slots 8/10, on the
  final arguments those hooks forward (after a glyph or biased-sampler substitution), for both context kinds. Slot 16 has a new
  single owner, `ps_constant_buffers.hpp`. `GlyphSwap::install(device,feature)` installs the hook as a passthrough when glyph
  remapping is off; `SamplerLodBias` already installs with a zero bias as a passthrough.
- **Elision opt-in (item 2).** Knob `build/state-elision` (file presence) enables it; absent by default. The census always
  counts. Status field `enabled` is gone: `ps_state.elision` reports the knob, `ps_state.barrier_ready.{immediate,deferred}`
  reports whether every barrier for that kind is installed, and `ps_state.reason.{immediate,deferred}` names the first missing
  one.
- **Barriers.** `context_barriers.hpp` owns 110/68/37 on both kinds (barrier before forwarding).
  `TaaDispatchHook::finish_hook` barriers the deferred context after FinishCommandList; `execute_hook` barriers the immediate
  context before and after ExecuteCommandList, whether a handler takes it over or not. `note_rt_bind` now barriers on every OM
  bind, dropping the same-target shortcut. The mod's own `ContextState` scopes and `D3D11TemporalOwner::isolate` suppress
  tracking (the swap restores the game state the shadow describes) via `overlay_scope_depth()`.
- **Deferred coverage.** 64 context records (several job threads record at once) and an `adopts` counter: a hash takeover
  drops the shadow and is counted, so hash thrash is visible instead of silent. Counters are split `immediate`/`deferred` per
  method: `ps_state.calls|repeats|skipped = {immediate:[3],deferred:[3]}`, plus `contexts`, `barriers`.
- **Replay timing.** `TaaDispatchHook::execute_original` is timed with `GameXr::cpu_time_ns()` in the execute lambda; per frame
  and per frame_split phase the ns, the list count and the restore TRUE/FALSE counts are accumulated and fed to
  `FrameSplit`'s 900-frame window at the existing feed point. Status `ecl_replay`:
  `{"ms_p50":[4],"ms_p90":[4],"lists_p50":[4],"total_ms_p50","total_ms_p90","restore_true","restore_false","frames"}`.

Labs (all run by `tools/build-msvc.cmd`): `tests/context_slot_probe.cpp` (the table above), `tests/ps_state_census_check.cpp`
(model: opt-in default, per-kind counters and barriers, adopts, collisions), `tests/ps_state_census_warp_check.cpp` (WARP:
deferred skips leave identical readable state, FinishCommandList / target bind / CS UAV / ClearState each re-arm, a recorded
list ending in a 1x1 draw renders the same pixel with elision on and off, immediate ExecuteCommandList re-arms),
`tests/gpu_dispatch_bindings_check.cpp` (seven methods).

## Sim measurement protocol for v993

Two runs at the same spot, same settings, same duration, 72 Hz; the only difference is the knob file. Use the v993 DLL, the
same save, the same load, and restart the game between runs so `ps_state` and `ecl_replay` start clean. `build/no-state-elision` is
obsolete in v993 and is ignored: remove it so it cannot confuse the run record.

1. **A (baseline).** No `build/state-elision` in the build directory (project root's `build\`). One sim session, hold the
   spot, 60 s. Capture one still at a fixed pose.
2. **B (elision).** Create an empty `build\state-elision`, repeat the same session at the same spot for the same 60 s, and
   capture the same still pose.
3. Read the status JSON of each run (`xr-live-status.json` while live, the same block in `dxgi-probe-result.json` at shutdown;
   `ps_state` and `ecl_replay` sit next to `rt_binds`).

Which field decides what:

- `ps_state.barrier_ready.deferred` / `barrier_ready.immediate`: whether elision could run at all on that kind. `false` means
  the run measured nothing there; `ps_state.reason.{immediate,deferred}` names the missing barrier (fix before reading A/B
  numbers). In particular, "deferred OM hooks not bound" means the game's HUD deferred target set has not been bound yet; keep
  one HUD child (crosshair/prompt/health) drawn in gameplay, or discard the run's deferred-elision numbers.
- `ps_state.repeats` / `ps_state.calls` per kind: the ceiling - the share of PS binds that were already redundant. If the
  deferred ratio is low, elision is not the engine lever and the next step is plan step 4 or 3.
- `ps_state.skipped` per kind: what elision actually removed (only nonzero in B).
- `ps_state.adopts`: if this is a large fraction of `calls`, recording threads are colliding in the 64-record table and the
  measured ceiling is understated.
- `ecl_replay.total_ms_p50` / `total_ms_p90`: serial replay time per frame (the emulated ExecuteCommandList). Compare with
  `frame_split.split.interval_ms_p50` for the share of the frame; `ecl_replay.ms_p50[phase]` says whether it lands in `pre`,
  `eye0`, `eye1` or `hook`, and `lists_p50[phase]` how many lists that is.
- `ecl_replay.restore_true` / `restore_false`: whether the engine asks D3D11 to restore the immediate state after a list (TRUE
  would make the barrier redundant but harmless; FALSE is the case the barrier exists for).
- `draws_by_phase`, `frame_split` and the two images: the unchanged measurement pair, to confirm B did not change what the
  engine draws (same draw counts, same eye split, identical frame capture).

## v994 review + sim (Opus, 2026-11-04 12:50)

- v993 reviewed and accepted as a measurement build (elision off by default). v994 = v993 plus one fix: `draw_phase_json` wrote three
  bare arrays, so the whole status JSON had been invalid since v990. It now writes `[[draws],[elements],[instances]]`.
- Sim (3 launches, title + load screen; the selftest cannot pass the unfocused load screen, see below): the registry accepted all 7
  methods, nothing was poisoned, no crash. PS census on the **deferred** contexts: calls SRV/sampler/CB 3.15M/3.20M/3.31M, repeats
  **22% / 54% / 48%**. Immediate-context PS calls: 0, so the game binds PS state only on recording contexts. `ecl_replay` was 0
  because the TaaDispatchHook installs only after stereo capture in gameplay.
- Launcher `Downloads\DishonoredVRLauncher-v994.exe` (manifest `artifacts/repair-v994-test-candidate.json`, source copy
  `build/v994src`). A normal headset session fills `ecl_replay` and `ps_state` at the heavy spots.
- Why an RTX 4080 Super holds 72 (user's video): NVIDIA's D3D11 driver builds command lists on the recording threads itself.
  Here `DriverCommandLists=0`, so the runtime replays every recorded call serially on the render thread. Fewer recorded calls
  (elision on deferred contexts) is the lever that matches this.
- Before any `build/state-elision` A/B: track `PSSetConstantBuffers1` (ID3D11DeviceContext1 slot 123), and fix the selftest
  load-screen hang. In the runs, the background-input cvars were missing from `dishonored2Config.cfg`, and command lines written
  before injection never reached `vr-command-result.txt`.

## v995 (2026-11-04): CB1 coverage, four output hazards, selftest command channel

Build `build/compile-v995.cmd` -> `build/d2vr_render_probe_arm_v995.dll`, `dll_exit=0`. Elision is still opt-in
(`build/state-elision`); nothing here is an in-game result.

### PSSetConstantBuffers1 (slot 123, both kinds) and SwapDeviceContextState (slot 131, immediate)

- `tests/context_slot_probe.cpp` proves slot 123 on WARP for the immediate and deferred contexts by calling the raw vtable entry
  with `(start=0,count=1,buffer,first=16,num=32)` and reading back with the real `PSGetConstantBuffers1`: buffer identical,
  first=16, num=32 (the probe prints the readback). It proves slot 131 on the immediate context by swapping in a fresh default
  state (PS SRV reads null) and swapping the previous state back through the real method (the SRV reads again). The deferred
  131 entry is reported as not supported, matching Microsoft's "not supported on a deferred context" note. The offset is 131,
  not 125: 124 is `CSSetConstantBuffers1` and 125-130 are the six `*GetConstantBuffers1` methods.
- `src/ps_constant_buffers.hpp` is the single owner of 16 and 123 on both vtables, and of 131 on the immediate vtable
  (deferred 131 is the documented no-op, so it is not hooked; one MinHook per code address, the existing target scan). The base
  16 hook keeps the census filter. The 123 hook is **never** elided: it increments
  `ps_state.cb1_calls.{immediate,deferred}` and calls `PsStateCensus::note_constant_buffers1`, which clears the known bits of
  the covered PS CB slots. A later base `PSSetConstantBuffers` to the same buffer pointer is therefore forwarded when the
  offsets differ. The 131 hook reports a **game-owned** sighting (`ps_tracking()` true) and the census then refuses elision for
  the rest of the session with reason `SwapDeviceContextState observed`; the mod's own `ContextState` /
  `D3D11TemporalOwner::isolate` swaps happen at `overlay_scope_depth()>0` and are ignored because they restore the game state.
- Audit of the other routes that can write PS SRV/sampler/CB state: `PSSetShaderResources` 8, `PSSetSamplers` 10,
  `PSSetConstantBuffers` 16 (covered by their single owners); `ClearState` 110, `CSSetUnorderedAccessViews` 68,
  `SOSetTargets` 37 (covered by `context_barriers.hpp`); `OMSetRenderTargets` 33 / `OMSetRenderTargetsAndUnorderedAccessViews`
  34 (covered by `GpuDispatchBindings`; deferred only after `gpu_deferred_targets_bound`); `ExecuteCommandList` 58 /
  `FinishCommandList` 114 (covered by `taa_dispatch.hpp`); `PSSetConstantBuffers1` 123 and `SwapDeviceContextState` 131 (v995).
  `ID3D11DeviceContext2` adds only tile/annotation methods, `ID3D11DeviceContext3` only `Flush1` / hardware-protection state, and
  `ID3D11DeviceContext4` only `Signal` / `Wait`; none writes a PS slot. Residual risk (documented, not covered): D3D11 unbinds a
  bound resource when its last reference is released, which the census cannot see; a pointer-identical reallocation could look
  like a repeat. The opt-in knob and the still-image A/B remain the gate.

### Four output-binding hazards, explicit

`PsStateCensus` now has four hazard bits: 33 `om_set_render_targets`, 34 `om_set_render_targets_and_uavs`,
68 `cs_set_unordered_access_views`, 37 `so_set_targets`, plus a separate finish/execute pass bit. `barrier_ready(kind)` is true
only when the pass bit and **all four** hazards are set; `reason` names the first missing one, and the status JSON adds
`hazard_ready` + `barrier_ready` + `reason` per kind. `refresh_ps_elision_state` sets 33/34 from the registry's
`gpu_immediate_targets_bound` / `gpu_deferred_targets_bound`, and 68/37 from `ContextBarriers::cs_uav_ready` /
`so_targets_ready`; `tests/ps_state_census_check.cpp` has a model loop that leaves each of the four missing in turn and asserts
no skip until the fourth is covered. Every path that binds an output on a deferred context goes through the context vtable
(the game's own calls and the mod's `native_hud_child_capture` / `world_ui` / `native_hud_fallback` calls); before the deferred
target set is bound, `barrier_ready.deferred` is false, so the missing hook cannot cause a skip.

### Selftest diagnosis (code only; no game was run for this)

- **What removes `artifacts/vr-command.txt`:** the mod's `poll_test_commands()` reads the file and then deletes it
  (`src/dxgi_probe.cpp:4803-4811`, `fs::remove` at 4809). It is the only remover/mover in the tree (`tools/inject_probe.py`
  `preserve_probe_outputs` copies only `PROBE_OUTPUTS`, which does not contain it). It polls only on the XR present path
  (`dxgi_probe.cpp:10786`), has no acknowledgement or command id, and a line appended between its read and its `fs::remove` is
  lost. Pre-injection lines can therefore be consumed by whichever mod instance polls first, and the selftest cannot tell.
- **What drops the config lines:** `game_keys.py --prepare-config` used to append only a missing name; it left a stale
  `win_pauseOnFocusLoss "1"` in place, and the engine rewrites `dishonored2Config.cfg` from its archived cvars on start/exit, so
  non-archived appended lines disappear. `vr_selftest.py` calls `--prepare-config` only when it launches the game itself
  (`vr_selftest.py:1191-1194`); a game already running at the title is never re-prepared, and a running process only reads the
  cfg at start. Evidence: the last run's cfg was written by the game at 12:44:34, 13 s before the injector started at 12:44:47,
  and contained `r_fullscreen "0"` but neither focus cvar.
- **Fix:** `prepare_config` now forces both values (and removes duplicate lines). The selftest clears a stale command file, and
  after injection sends `cvar in_controlInactiveWindow 1` and `cvar win_pauseOnFocusLoss 0` through the command channel, then
  waits for its own result offset in `vr-command-result.txt` (`vr_selftest.py:729-769`). `find_cvar` can reach both: its `.rdata`
  name search + `.data` pointer search (`dxgi_probe.cpp:4693-4713`) feeds `run_test_command`'s `cvar` write at +0x28/+0x2c
  (`dxgi_probe.cpp:4793-4799`), and earlier answers in `artifacts/vr-command-result.txt:5684-5685` are
  `cvar in_controlInactiveWindow=1 ok` / `cvar win_pauseOnFocusLoss=0 ok`. No focus stealing and no global input is added; the
  posted `pad` path is unchanged.

### Labs, build, and what to read in the sim

- `tests/ps_state_census_check.cpp`: adds the four-hazard completeness loop, the CB1 invalidation sequence, the never-elide
  CB1 counter, and the state-swap refusal.
- `tests/ps_state_census_warp_check.cpp`: adds the CB1 sequence base B -> CB1 B `first=16` -> base B again on a deferred WARP
  context. The CB1 bind reads buffer constant 16 (blue), the last base bind must be forwarded and read constant 0 (red), and
  the pixels are identical with elision on and off; `cb1_calls` is 3 in both runs.
- `tests/context_slot_probe.cpp`: 19 matched, 0 rejected (including 123 on both kinds and 131 immediate).
- Sim A/B is still: elision off baseline, then `build/state-elision`; read `ps_state.barrier_ready.deferred`/`reason`,
  `ps_state.cb1_calls`, `ps_state.state_swaps`, `hazard_ready`, `calls`/`repeats`/`skipped`, and the image pair. In v995 a
  `state_swap_seen=true` refused elision. v996 replaces that flag with `state_swaps` and only barriers the immediate context
  that swapped; a swap no longer invalidates the run for the deferred kind (the `reason` field says why if readiness is false).

## v996 (2026-11-04): swap sighting barriers its context, deferred OM bound from the first frame, load-screen pad fix

Build `build/compile-v996.cmd` -> `build/d2vr_render_probe_arm_v996.dll`, `dll_exit=0`. No in-game result is claimed.

### SwapDeviceContextState: barrier the immediate context, never refuse the session

- `PsConstantBufferHook::swap_state_hook` (`src/ps_constant_buffers.hpp:73-81`) now reports `(context, _ReturnAddress(),
  before/after)` around the original call. `ps_note_state_swap` (`src/dxgi_probe.cpp:2657-2683`) barriers before the call,
  and after it resolves the caller module with `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` + the module
  basename and RVA, then calls `PsStateCensus::note_device_context_state_swap` (`src/ps_state_census.hpp:162-183`), which
  barriers again, increments the count and keeps the first eight distinct `(module, rva)` caller sites.
- `barrier_ready()` (`src/ps_state_census.hpp:141-145`) no longer reads a session-wide swap flag: a swap changes only the
  swapping context's state, and only the immediate context can swap, so it cannot change a deferred recording stream or the
  deferred elision shadow. The status field is `ps_state.state_swaps = {count, callers:[{module, rva, count}]}`. The mod's own
  `ContextState` / `D3D11TemporalOwner::isolate` swaps are still invisible to the census (`ps_tracking()` is false inside
  those scopes), so the count is game-owned sightings only.
- Labs: `tests/ps_state_census_check.cpp` asserts the immediate first bind after a sighting is forwarded, the deferred shadow
  and both kinds' readiness survive, the caller list is distinct/counted, and the ninth distinct caller is counted but not
  listed. `tests/ps_state_census_warp_check.cpp` performs a real immediate `SwapDeviceContextState` on WARP: the immediate PS
  SRV is cleared, the first bind after it is forwarded, and an immediate-context repeat still skips after the swap.

### Deferred OM readiness from the first frame

- `bind_deferred_targets` (`src/dxgi_probe.cpp:2530-2559`) runs from `ensure_game_gpu_hooks` (`:2618`). It snapshots the seven
  targets (44/33/34/12/20/13/21) from a deferred context of the verified game device, validates each against d3d11.dll and
  against duplicate targets, caches the set, and binds it into the existing registry. `gpu_deferred_targets_bound` is therefore
  true before the first gameplay frame, and `refresh_ps_elision_state` (`:2697-2741`) reports the deferred OM hazards ready.
- Why this is safe: it is the same registry (`gpu_bindings`, `simultaneous_targets=true`, capacity 4 per method), `bind()` dedups
  by code address and never creates a second MinHook; the seven slots have no other owner; `begin_native_hud_art` still binds
  the game's actual HUD set later and reuses equal targets; and the wrappers already have to tolerate recording job threads once
  the HUD set is bound. Before that point they only touch atomics, reject non-game contexts in `trace_game_context`, short
  circuit `ObjectMotion::DrawScope` until both eye views exist, and return early from `fsr_taa.observe_depth` while temporal
  rendering is disabled. The one extra state read is `game_logic_mask()` for a 39-index draw, which is the validated,
  mutex-guarded per-frame cache (`logic_mask_cache.hpp`) and was already called from hooks on non-render threads.
- If the deferred snapshot/bind cannot be made, the flag stays false and the old HUD-time behavior applies; the status reason
  still names "deferred OM hooks not bound". `disable_gpu_hooks` and session start clear the cached target set so a recreated
  device is never bound with stale addresses.

### Selftest load-screen diagnosis and simulated-input fix

Code path for `pad a 200` at logic mask 64 (`views_valid=true`, `session_state=5`):

1. `src/dxgi_probe.cpp:10386` sets the simulated XR controller active for the frame, but the load-screen session's action
   sample is not active: the run's own status has `xr_controller_samples=1` and `action_sync_calls=1`, and the live status
   has `active_input_hands=0`. `sample_input` therefore leaves `active_input_hands` at 0 (`src/game_xr.hpp:1299,1305,1311`),
   and `controller_input.active=active_input_hands!=0` is false (`:1323`). That is the state the test command starts from.
2. The test pad block at `src/dxgi_probe.cpp:12725-12741` forces that copy `input.active=true` and A for 200 ms, and
   `controller_bridge.publish` at `:12790` composes it.
3. `PadComposer` had returned `{}` for the preceding inactive frames (`src/controller_input.hpp:86`), so `previous_active_`
   was false. The fresh-activation rule (`:111-120`) adds the held A to `blocked_buttons_` and clears it from `buttons`; it
   stays blocked for as long as A is held, i.e. the whole 200 ms window. `XInputBridge::get_state` returns that neutral
   `state_.Gamepad` and only increments `nonzero_reads` for a non-neutral pad (`:190-194`), so
   `native_virtual_controller_reads` rises while `native_nonzero_controller_reads` stays 0.
4. `native_pad_focus.publish` (`src/dxgi_probe.cpp:12793`) is true during the pad, but the window override
   (`src/native_pad_focus.hpp:42-56`) only runs when the native window getter returns false. In a focused sim session (and
   with the background-input cvars) it returns true, so `native_pad_window_overrides=0` is expected and is not the failure
   signal. The getter override (`:27-41`) additionally needs a currently valid native joystick root; the virtual XInput pad
   is the path the load screen has to receive A on.

Fix (simulated/test path only):

- When a new test-pad command starts, the block publishes one active, test-button-free frame first
  (`src/dxgi_probe.cpp:12726-12736`). That clears `previous_active_`/`blocked_buttons_`; the same frame's real publish then
  carries A, so the press is not swallowed even if the captured swapchain presents only once inside the 200 ms window. Real
  input never enters this block.
- `apply_simulated_controller_command` (`src/controller_input.hpp:36-40`) also preserves the live `active` bit when a
  scripted gameplay segment replaces the controller input, so the test path cannot drop the controller's presence in the
  first place; `src/dxgi_probe.cpp:10473` uses it instead of the wholesale assignment. Focus, OS input and the real
  controller path are untouched.
- `tests/controller_input_check.cpp` pins both: the old wholesale assignment plus an inactive frame still reproduces the
  mask, and an active button-free publish followed by the held button delivers A.

### Checks and build

- `build\ps_state_census_check.exe` -> PASS: "PS state census shadow, opt-in elision, per-kind counters, four output hazards,
  CB1 invalidation, state-swap caller accounting".
- `build\ps_state_census_warp_check.exe` -> PASS: "WARP deferred PS census - skips leave identical state, every pass barrier
  re-arms, CB1 window A/B equal, immediate swap keeps deferred elision".
- `build\controller_input_check.exe` -> PASS: "VR pad mapping, native window connection probe through patched XInput,
  menu/focus/stale release and import restoration, menu grip taps".
- `dll_exit=0`; changed files add no warnings. The remaining `/W4` warnings are pre-existing (`game_xr.hpp`,
  `native_hand_contact.hpp`, `hold_coupling.hpp`, `native_weapon_hands.hpp`, old `dxgi_probe.cpp` sites).
- Open risks: the deferred draw/target hooks now run on recording job threads from the first frame, earlier than the measured
  HUD-time install (cheap wrappers, but new timing); a fresh deferred context's target set differing from the game's UI
  deferred set would consume a third per-method binding (capacity 4) and a fourth would fail closed; swap caller RVAs are
  return addresses (call site + 5) and only game-owned sightings are counted; the load-screen fix is code-only until a sim run.



## v996 review (Opus, 2026-11-04)

Accepted offline. A swap now barriers only the context that swapped. The test-pad fix touches only the simulated path:
`apply_simulated_controller_command` sits under `simulated_gameplay_script_requested`, and the activation publish runs only for
test pad commands. Target budget: headset sessions show 2 code-address sets per method on the immediate context, and the deferred
implementation adds 1 (the startup bind and the HUD bind share it), so 3 of the 4 slots are used. A third immediate set would
poison the registry. Watch `gpu_dispatch_poisoned` in the first runs.
Pending: the sim run (gameplay reach, `ecl_replay`, `barrier_ready`), then the `build/state-elision` A/B. Postponed because the
user was playing another game.

## v996 headset run 1 (user, 2026-11-04 13:47, elision off, 72 Hz, canted, paired)

Status window (last 900 gameplay frames) and session counters, copied to artifacts/claude/perf-engine/headset-v996-run1/:
- **ecl_replay: total p50 10.0 ms, p90 11.5 ms per frame** (eye0 4.8 ms, eye1 5.0 ms; about 118 lists per frame; every list
  restore=FALSE). The serial runtime replay of the recorded command lists alone takes 72% of the 13.9 ms budget.
- frame_split: interval p50 27.7 ms (36 fps), eye0 9.1, eye1 13.0, hook 5.2 ms. Pacer locked/unlocked 18 times.
- PS census (session, deferred): calls SRV/sampler/CB 37.5M/36.6M/51.3M, repeats 34%/80%/51% (68M of 125M PS calls redundant);
  cb1 0.86M; immediate PS calls 0. Draws about 1,900 per frame.
- state_swaps 23,822, all from dxgi.dll (two call sites, one pair per Present). Since v996 they barrier only the immediate context.
- barrier_ready immediate and deferred both true; registry not poisoned.
Next: run 2 with `build/state-elision`, same spot, then compare ecl_replay.total and interval, and check the image.

## v996 headset run 2 (user, 2026-11-04 13:57, `build/state-elision` ON, same settings) - image OK per user

| last 900 gameplay frames | run 1 (off) | run 2 (on) |
|---|---|---|
| ecl_replay total p50 / p90 (ms) | 10.0 / 11.5 | **2.47** / 11.5 |
| eye0 / eye1 replay p50 (ms) | 4.79 / 5.03 | 1.28 / 1.19 |
| frame interval p50 / p95 (ms) | 27.7 / 31.4 | **16.0** / 29.9 |
| eye0 / eye1 / hook span p50 (ms) | 9.1 / 13.0 / 5.2 | 3.2 / 7.0 / 5.5 |
| session frames at half rate (pacer) | 69% (6642/9630), 18 locks | 42% (2567/6119), 8 locks |

Session: 30.7M of 59.4M deferred PS calls skipped (52%): SRV 5.5M, sampler 13.4M, CB 11.8M.
Caveats: the status window holds only the last ~25 s, and the two runs need not cover the same spot. The replay p90 is unchanged
(11.5 ms), so a share of frames still replays as heavily as before. A same-spot live toggle is needed before claiming a number.
Hook p50 5.5 ms is now a third of the frame.

## v997 (2026-11-04): live same-spot elision A/B, replay heavy tail, wide census

Build `build/compile-v997.cmd` -> `build/d2vr_render_probe_arm_v997.dll`, `dll_exit=0`. No in-game result is claimed; nothing
here was run against the game. Elision stays opt-in (`build/state-elision`), the wide census is a second opt-in
(`build/state-census-wide`).

### Live A/B toggle (`elision_ab`)

- `build/state-elision` is polled about once per second on the Present thread (`fs::exists` only; nothing in a hook or draw
  path). The file state is compared with the last applied state, and only a change calls `ps_state.set_elision`. The PS census
  shadow is maintained with elision off as well (`consider()` always records and compares; only the final "skip" is gated), so
  switching on mid-session needs no re-record. `tests/ps_state_census_check.cpp` proves off-repeat-forward,
  on-mid-session-skip and off-again-forward on one shadow; `tests/ps_state_census_warp_check.cpp` does the same on a real
  deferred WARP context and checks the skipped call left the same SRV bound.
- An unchanged file never overrides a test command. `elision on|off` in `run_test_command` changes the state immediately and
  reports the toggle count; a later file change still wins. A headset/sim test can therefore toggle without racing the game's
  own reads of the knob.
- Two independent 900-frame windows, one per state, selected at feed time from the state the frame ended in:
  `elision_ab = {"off":{"frames","ecl_ms_p50","ecl_ms_p90","interval_ms_p50","interval_ms_p95","eye0_ms_p50","eye1_ms_p50",
  "hook_ms_p50","skipped_per_frame_p50","barriers_p50","barriers_p90","max_list_ms_p90","ecl_hist"},"on":{...},"toggles":n}`.
  v998 changes this shape to four combined-state windows and makes `ecl_hist` a per-frame TOTAL replay histogram (see the v998
  section); the v997 `off`/`on` keys are historical.
  The span percentiles reuse the FrameSplit nearest-rank helper (`percentile_sample`, now shared); `skipped_per_frame` and
  `barriers` are per-frame deltas of the census session counters. At 72 Hz 900 frames is about the last 12.5 s per state
  (90 Hz about 10 s), not a whole 30 s segment.

### The replay heavy tail

- The execute handler now also records the largest single `ExecuteCommandList` replay of each frame (`elision_ab.<state>
  .max_list_ms_p90`, and `ecl_replay.max_list_ms_p50/p90/max`) and a per-list histogram (`ecl_hist`: buckets <1, 1-2, 2-4,
  4-6, 6-8, 8-12, 12-16, >16 ms). `ecl_replay.hist` carries the same per-list totals for the whole fed window. **v998 fixes
  `elision_ab.ecl_hist` to be a per-frame TOTAL replay histogram whose buckets sum to `frames`; only `ecl_replay.hist` is still
  per-list.**
- Why a frame can still replay heavily with elision on, and what to read:
  - **Shadow invalidated (barrier storm).** Every output bind (OM targets and the UAV form, CS UAV, SO targets), ClearState,
    Finish/ExecuteCommandList, CPU phase change and Present invalidates that context's shadow, and the first bind of every PS
    slot after it must be forwarded. `elision_ab.<state>.barriers_p50/p90` counts those events per frame. A high barrier p90
    with a heavy `ecl_hist` tail means the elision opportunity keeps being reset, not that elision is ineffective.
  - **Lists recorded before elision was ready.** A list recorded before the toggle (or before the barriers were ready) carries
    the full recorded call stream; executing it after the toggle cannot retroactively elide. The first frames after an
    off->on switch can still replay old lists; the 900-frame window washes that out, but `toggles` and the first seconds after
    the switch are the place to look.
  - **A different pass mix.** A frame that includes a heavy pass (shadow atlas, HUD/UI, a copy/clear, a different number of
    recorded lists) costs differently even with perfect elision. `ecl_replay.lists_p50[phase]` and `max_list_ms_p90` separate
    "many mid-size lists" from "one long list"; the histogram says whether the tail is one >16 ms list (a specific pass) or a
    spread of 4-12 ms lists.
  - The replay is CPU time inside `ExecuteCommandList`; a driver wait or a GPU-bound list can extend it, so a heavy frame is
    not proof of redundant state. Cross-check `interval_ms`, `eye0/eye1/hook` and, when enabled, the GPU timers.
- `elision_ab` in v997 has exactly two windows, so an off->on->off run compares the two states; the second off segment joins the
  first off window. (v998 replaces this with four windows keyed by PS x wide, so a wide-only toggle reads the same way.)

### Wide census (`state_census_wide`, COUNT ONLY)

- Opt-in: `build/state-census-wide` is read once at session start (off by default; every extra hook costs time on the
  recording threads). It never elides and never skips a call.
- New single owner `src/wide_state_census.hpp` for slots 25 VSSetShaderResources, 26 VSSetSamplers, 9 PSSetShader,
  17 IASetInputLayout, 18 IASetVertexBuffers, 19 IASetIndexBuffer, 24 IASetPrimitiveTopology, 35 OMSetBlendState,
  36 OMSetDepthStencilState, 43 RSSetState, 45 RSSetScissorRects, on both context kinds. VSSetConstantBuffers (7),
  VSSetConstantBuffers1 (119) and VSSetShader (11) stay owned by ObjectMotion; the census observes their final forwarded
  arguments through optional observers, so there is still one MinHook per code address.
- Exact-repeat rules include the parameters: VB buffers+strides+offsets, IB buffer+format+offset, CB1 buffers+first+num,
  shader+class-instance list, blend state+factor+mask (a NULL factor normalizes to ones), depth state+ref, full scissor rects.
  A base CB bind invalidates the CB1 shadow of its slots and a CB1 bind invalidates the base shadow, so a window change is
  never counted as a repeat. IA/VS SRV state is barriered on every output bind (a SO target also unbinds a vertex buffer).
- Counters are per-thread, cache-line-aligned shards summed on read, so many recording job threads do not fight over one cache
  line. `PsStateCensus` uses the same sharding and keeps its v993 JSON shape (`ps_state.calls|repeats|skipped =
  {immediate:[3],deferred:[3]}`).
- Status: `state_census_wide = {"enabled":bool,"hooked":[14],"deferred":{"<method>":{"calls":n,"repeats":n},...},
  "immediate":{...},"contexts":n,"adopts":n}`. `hooked` distinguishes "no calls" from "not observed" (the three ObjectMotion
  methods only count when its install succeeded).
- Every new slot was proved through its raw vtable entry on a WARP immediate AND deferred context with the matching Get
  readback before any hook was written: `tests/context_slot_probe.cpp` reports 47 matched, 0 rejected (7/11/119 re-proved too;
  119 with first/num). A wrong index only crashes the probe process.

### Labs and build

- `build\context_slot_probe.exe` -> PASS: "PASS: every required slot proved on the immediate and deferred contexts"
  (47 matched, 0 rejected).
- `build\ps_state_census_check.exe` -> PASS: "PASS: PS state census shadow, opt-in elision, per-kind counters, four output
  hazards, CB1 invalidation, state-swap caller accounting, live toggle, sharded counters, elision A/B windows,
  wide-census repeats".
- `build\ps_state_census_warp_check.exe` -> PASS: "PASS: WARP deferred PS census - skips leave identical state, every pass
  barrier re-arms, CB1 window A/B equal, immediate swap keeps deferred elision, live toggle, A/B windows and histogram,
  wide-census parameter comparisons".
- `build\frame_split_check.exe` -> PASS: "PASS: frame split" (now also pins the replay tail fields, histogram totals and
  `FrameSplit::latest`).
- `build\compile-v997.cmd` -> `build\d2vr_render_probe_arm_v997.dll`, `dll_exit=0`; the changed files add no warnings. The
  remaining /W4 warnings are the pre-existing `game_xr.hpp`, `hold_coupling.hpp`, `native_hand_contact.hpp`,
  `native_weapon_hands.hpp` and old `dxgi_probe.cpp` sites; the LNK4099 warnings come from the FFX static libraries.

### Same-spot headset protocol (v997)

1. Settings: 72 Hz, the same save, spot and settings as the v996 runs. Do **not** create `build/state-census-wide` for the
   A/B; a wide run is a separate measurement (it adds hooks on the recording threads).
2. Remove `build/state-elision` if present. Load the heavy spot, stand still (no locomotion, turning or hand motion) for 30 s.
   Copy `xr-live-status.json` (e.g. to a run-1 name): this is the clean off window.
3. Create an empty `build/state-elision`. Wait at least 2 s (1 s poll plus a few frames), stay still for 30 s. Copy the
   status. `elision_ab.on.frames` should be about 900 (the last ~12.5 s); `toggles` is 1.
4. Delete `build/state-elision`. Wait at least 2 s, stay still for 30 s. Copy the status. `toggles` should be 2; the `off`
   window now holds the last ~12.5 s of this third segment (the two off segments share the window; the step-2 copy is the
   clean off baseline).
5. Which fields decide:
   - `elision_ab.off.ecl_ms_p50/p90` vs `on`: the same-spot elision effect; read p50 and p90 separately.
   - `max_list_ms_p90` and `ecl_hist` (buckets >8 ms): whether the tail is one long list or many mid-size ones.
   - `barriers_p50/p90`: a storm explains a heavy tail with elision on.
   - `interval_ms_p50/p95`: the frame result; `eye0/eye1/hook_ms_p50` attributes it.
   - `skipped_per_frame_p50`: must be nonzero on `on`; zero means elision did not run (read `ps_state.barrier_ready` and
     `ps_state.reason`).
   - `toggles`: 2 for this protocol; any other value means a poll interval was skipped or the command channel was used.
   - Cross-check `ps_state.calls/repeats/skipped` (ceiling vs actual), `ecl_replay.restore_false`, `elision_ab.*.frames`, and
     `state_census_wide` only if that run enabled it.
   - Image: capture and compare the same still pose in each segment; the protocol does not automate the visual check.

## v997 same-spot A/B (user headset, 2026-11-04 14:24-14:33, 17 alternating 30 s segments, wide census on)

- **PS-only elision has no measurable effect.** Over 17 segments, ecl_replay p50 is 8.0-11.6 ms in both states and the interval
  p50 is 27.4-28.2 ms (pacer k=2, half rate) in both. Elision skipped about 9-10k PS calls per frame. Run 2's 2.47 ms came from
  a lighter end spot, not from elision.
- Per frame (17,751 frames over 481 s): about **335k recorded D3D11 calls per frame**, about 36.7k draws per frame (both eyes; from
  IASetIndexBuffer counts), replayed in about 10 ms, which is about 30 ns per call. PS elision removes about 8.6k calls (2.6%),
  about 0.3 ms: inside the noise.
- Exact repeats per frame (wide census): IASetPrimitiveTopology 36.2k of 36.6k (99%), PSSetShader 28.8k of 34.7k (83%),
  VSSetShader 26.8k of 34.7k (77%), VSSetShaderResources 10.9k of 13.0k (84%), VSSetConstantBuffers 10.8k of 36.6k (29%),
  IASetVertexBuffers / IndexBuffer 3.5k each of 36.7k (10%), PS SRV / sampler / CB 1.5k / 3.8k / 3.3k. In total about 129k
  repeated calls per frame, **about 38% of the recorded stream**.
- render_thread_busy 0.34-0.64; pacer work_ms_p50 16.4 ms against the 13.9 ms period.
- Defects: `ecl_hist` is wrong (eight values of about 100k each per 900-frame window; it looks cumulative or counts lists, not
  frames). The doc names `barriers_per_frame`; the status key is `barriers_p50/p90` (about 900 per frame). Both are fixed in
  v998 (per-frame total histogram; the protocol below uses `barriers_p50/p90`).
Next: elide the whole repeated set (topology, shaders, VS SRV/CB, IA) under the same barrier/hazard rules. If the replay cost per
call is about uniform, that is up to about 4 ms of the about 10 ms, which is the size needed to bring 16.4 ms of work under 13.9.

## v998 (2026-11-04): wide redundant-state filter, combined-state A/B, per-frame replay histogram

Build `build/compile-v998.cmd` -> `build/d2vr_render_probe_arm_v998.dll`, `dll_exit=0`. No in-game result is claimed; nothing
here was run against the game. Elision stays opt-in: `build/state-elision` (PS) and the new `build/state-elision-wide` (wide).
`build/state-census-wide` still means count-only wide.

### Knobs and live toggle

- `build/state-elision-wide` is present = wide elision on. It is polled at 1 Hz on the Present thread next to
  `build/state-elision` (`fs::exists` only); a file CHANGE calls `WideStateCensus::set_elision` and increments
  `elision_ab.toggles`. Test commands: `elision-wide on|off` (and the existing `elision on|off`) with the same rule - an
  unchanged knob file never overrides a test command, a later file change wins.
- The wide hooks are installed at session start when **either** `build/state-census-wide` or `build/state-elision-wide` is
  present (the knob that turns elision on also requires the census hooks). If neither was present, the 1 Hz check installs them
  as soon as either file appears, so a live A/B can also start with no wide knob and create `state-elision-wide` later. A test
  command sent before that install reports that the hooks are not installed yet.
- Elision on the IMMEDIATE context is allowed only when its PS-census readiness is proven, exactly as before. Wide readiness
  is a callback into the SAME `PsStateCensus::barrier_ready(kind)` / `barrier_reason(kind)`: finish/execute pass barrier,
  ClearState, the four output hazards (OM 33, OM+UAV 34, CS UAV 68, SO 37) and the state-swap rules are therefore identical
  for both filters and cannot drift. A device-context state swap now barriers the wide shadow of that (immediate) context too.

### Per-method rules (all parameters compared; anything not compared is never elided)

| method (slot) | owner | rule |
|---|---|---|
| IASetPrimitiveTopology (24) | WideStateCensus | topology identical |
| IASetInputLayout (17) | WideStateCensus | layout pointer identical |
| RSSetState (43) | WideStateCensus | rasterizer pointer identical |
| OMSetDepthStencilState (36) | WideStateCensus | state pointer + stencil ref identical |
| OMSetBlendState (35) | WideStateCensus | state + all 4 blend factors (NULL normalizes to ones) + sample mask |
| RSSetScissorRects (45) | WideStateCensus | count + every rect identical |
| PSSetShader (9) | WideStateCensus | shader pointer + full class list compared; **skip only when class count is 0** |
| VSSetShader (11) | ObjectMotion | same class-count-0 rule, filter asked AFTER ObjectMotion's shader/layout bookkeeping |
| VSSetShaderResources (25) / VSSetSamplers (26) | WideStateCensus | same array rules as the PS versions; every output bind (OM 33/34, CS UAV 68, SO 37) barriers SRV state |
| VSSetConstantBuffers (7) | ObjectMotion | ObjectMotion forwards the game's pointers unchanged; compare exactly those; its own pool bind/restore is reported to the census so it can never be mistaken for the game's state |
| VSSetConstantBuffers1 (119) | ObjectMotion | NEVER elided; counts and invalidates the covered base slots (PS CB1 rule) |
| IASetVertexBuffers (18) | WideStateCensus | every slot in range: buffer + stride + offset |
| IASetIndexBuffer (19) | WideStateCensus | buffer + format + offset |

SO targets and every other output bind are a full barrier for VB/IB/CB (and SRV/sampler/CB1) state, so a buffer bound as output
elsewhere cannot leave a stale VB/IB/CB shadow. The ElisionAb check paths and the Finish/Execute/ClearState/phase barriers are
the same owner call sites as the PS census.

### Status and A/B

- `state_census_wide` = `{enabled, elision, hooked:[14], barrier_ready:{immediate,deferred}, reason:{...},
  deferred:{<method>:{calls,repeats,skipped}}, immediate:{...}, contexts, adopts, barriers}`. The required per-method key is
  `state_census_wide.deferred.<method>.skipped` (and the immediate equivalent).
- `elision_ab` now has FOUR 900-frame windows keyed by the combined state:
  `ps_off_wide_off`, `ps_off_wide_on`, `ps_on_wide_off`, `ps_on_wide_on`, plus `toggles` (any PS or wide change). Each window
  has the v997 fields plus `frames`. `skipped_per_frame_p50` and `barriers_p50/p90` are per-frame deltas of BOTH censuses
  combined, so a PS-on/wide-off window can be compared directly with PS-on/wide-on at one spot.
- **v997 defect fix:** `elision_ab.<window>.ecl_hist` is the histogram of each frame's TOTAL replay, one frame in exactly one
  bucket (<1, 1-2, 2-4, 4-6, 6-8, 8-12, 12-16, >16 ms). The eight counts sum exactly to that window's `frames`. The old field
  summed per-list counts (about 100k per bucket per 900 frames) and is gone. `ecl_replay.hist` remains the per-list histogram
  and is now documented as such; the two must not be read as the same thing.
- The doc/protocol names the real status keys: `elision_ab.<window>.barriers_p50` and `.barriers_p90` (not
  `barriers_per_frame`).

### Same-spot headset protocol (v998)

1. Settings: 72 Hz, same save, spot and settings as v997. Keep `build/state-elision` ON for the whole run unless the PS
   dimension is also being measured.
2. Start the session with `build/state-census-wide` present (count-only) **or** with `build/state-elision-wide` present, so the
   wide hooks install. Load the heavy spot, stand still (no locomotion, turning or hand motion) for 30 s. Copy
   `xr-live-status.json` as the `ps_on_wide_off` (or `ps_on_wide_on`) baseline.
3. Create an empty `build/state-elision-wide`. Wait at least 2 s (1 s poll plus a few frames), stay still 30 s, copy the
   status. `elision_ab.ps_on_wide_on.frames` should be about 900 (about 12.5 s at 72 Hz), `toggles` increased by 1.
4. Delete `build/state-elision-wide`. Wait at least 2 s, stay still 30 s, copy the status. `toggles` increased again; the
   `ps_on_wide_off` window now holds the last ~12.5 s of this off segment (the step-2 copy is the clean baseline).
5. Repeat 3-4 for several 30 s alternations. Which fields decide:
   - `elision_ab.ps_on_wide_off.ecl_ms_p50/p90` vs `ps_on_wide_on`: the wide-filter effect at the same spot; read p50 and p90
     separately, and compare `interval_ms_p50/p95`, `eye0/eye1/hook_ms_p50`.
   - `ps_on_wide_on.skipped_per_frame_p50`: must be nonzero (PS+wide). If zero, read `ps_state.barrier_ready` /
     `ps_state.reason` and `state_census_wide.barrier_ready` / `reason`.
   - `state_census_wide.deferred.<method>.skipped`: which methods actually disappeared (topology/shader/VS SRV are the expected
     bulk; CB1 and class-instance shader binds must stay 0).
   - `state_census_wide.deferred.<method>.repeats` vs `skipped`: ceiling vs actual; a large repeat count with low skipped means
     the barrier readiness or the phase shadow is being reset (read `barriers_p50/p90`).
   - `ecl_hist` bucket sums equal `frames`; a heavy tail is a bucket >8 ms, not a list count. Cross-check
     `max_list_ms_p90` and `ecl_replay.restore_false`.
   - Image: capture and compare the same still pose in each segment; the protocol does not automate the visual check.
   - `toggles`: if it does not move on a knob change, the poll or the test command did not apply; if it moves without a change,
     a test command was used.

### Labs and build

- `build\ps_state_census_check.exe` -> PASS: model for every wide method rule and negative case (class instances, changed
  stride/offset/format, blend factor/mask, stencil ref, scissor rect, CB1 invalidation for VS, ObjectMotion direct substitution,
  output/barrier/phase readiness, immediate-not-ready), the ObjectMotion observe-then-filter ordering, and the four combined
  A/B windows with the per-frame total histogram.
- `build\ps_state_census_warp_check.exe` -> PASS: the existing PS/CB1/swap/toggle labs plus a deferred WARP pixel A/B with
  redundant topology/shader/VS SRV/VB/IB/blend/depth/raster/scissor/VS-CB binds (pixels identical with wide elision on and off)
  and a second sequence through every hazard type (Finish/Execute, ClearState, OM 33/34, CS UAV 68, SO 37, VS CB1, ObjectMotion
  substitution), also pixel-identical.
- `build\object_motion_check.exe` -> PASS (the changed ObjectMotion header still builds and its model is unchanged).
- `tests/context_slot_probe.cpp` was not changed: v998 introduces no new vtable slot.
- `build\compile-v998.cmd` -> `build\d2vr_render_probe_arm_v998.dll`, `dll_exit=0`. Changed files add no warnings; the
  remaining /W4 warnings are the pre-existing `game_xr.hpp`, `native_hand_contact.hpp`, `hold_coupling.hpp`,
  `native_weapon_hands.hpp` and old `dxgi_probe.cpp` sites, and LNK4099 comes from the FFX static libraries.

### Open risks (code-only; no game run)

- Wide hooks run on the recording job threads (as the v997 census already did). The filter adds one shadow lookup/lock per
  recorded call and the callback indirection for slots 7/11/119; the measured 38% repeat ceiling is not yet a measured saving.
- A class-instance shader bind is compared but never elided (by rule), so the shader repeat share above the class-count-0 subset
  is not recovered.
- ObjectMotion's substitution callbacks currently report only the VS b2 pool bind/restore; a future substitution at another slot
  must report it or invalidate the shadow.
- `state_census_wide` skipped counts are session totals; the per-frame deltas in `elision_ab` combine both censuses and cannot be
  split per filter after the fact.
- The wide hooks are installed at session start or, if no wide knob existed then, at the next 1 Hz check after a knob appears;
  a very early `elision-wide on` before that check reports the hooks are not installed yet.
- A resource released while bound can still leave a pointer-identical reallocation looking like a repeat (the same residual as
  the PS census); the opt-in knob and the still-image/headset A/B remain the gate.

## v998 same-spot wide A/B (user headset, 2026-11-04 15:05-15:12, 15 alternating 30 s segments, PS elision on throughout)

- **Wide elision has no measurable effect either.** It skipped 58k-149k calls per frame (vs 4-8k with PS only), with no reported
  image problem. Median of segments: ecl_replay p50 6.78 ms off vs 6.77 ms on; interval 27.77 vs 27.72 ms (pacer k=2 all along,
  work_ms_p50 19.3 ms).
- Conclusion: **replay time does not scale with recorded call count.** The "serial CPU replay" model is wrong, or at least not
  what limits this spot. The replay time is most likely spent blocked, for example the driver waiting for the GPU.
- Settings in that session: FSR4 native (temporal_aa 5, render_fraction 1), canted, 72 Hz. The v819 notes measured about 11-13 ms of
  native lists plus about 4.7 ms of FSR4 per frame at FSR4 native, which is over the 13.9 ms budget. So the heavy spot is probably
  GPU-bound. The GPU timers were off in every run today (`native_gpu.enabled false`).
- Next: one headset session with `build/native-gpu-timing` (status `native_gpu.last_list_sum_ms`, `frame_split.split.gpu_busy`,
  `gpu_phase_ms_p50`, `mod_gpu_ms_p50`). Compare FSR4 Native vs FSR4 Quality at the same spot. Elision knobs are removed from the
  user's build (no gain, nonzero risk). The elision code stays opt-in.

## Refresh target back to 90 Hz (user decision, 2026-11-04 15:35)

`C:\Program Files (x86)\Steam\config\steamvr.vrsettings`: `steam.app.403640.refreshRate` and `steamvr.preferredRefreshRate`
72 -> 90 (backup `steamvr.vrsettings.bak-20261104-72hz`; the pre-72 file is `.bak-20261104`). The runtime is Steam Link
(`ActualHMDDriver` vrlink). Budget is now 11.1 ms, with the half-rate floor at 45 fps. GPU-side numbers from the v998 session (AA off,
heavy spot: 13.8 ms native lists + 0.9 ms mod) mean the GPU work must drop by about 3.6 ms to hold 90 at that spot.

## Render pass map (v999)

Static analysis only: PE + capstone over `Dishonored2.exe` on disk (Steam build, sha256 `b671f824...6180`). No game was
launched, injected, attached to or read from in this phase; the pass table and the interception point below are code/static
evidence, and the numbers the new status reports are for the next headset run.

### Registration (0x34fe00 -> 0x1402c2960 -> 0x1402c29a0)

`0x34fe00` is called once from `0x36c619` with `rcx = rbx + 0xd7f0` on the render-system object, right after `0x34f6c0`.
It registers 52 passes with 52 calls to `0x1402c2960` between `0x35002a` and `0x35113a`:

- `rdx` is the `ARK_RENDERPASS_*` name string (contiguous in `.rdata` at `0x1b2e700..0x1b2edf0`);
- `rcx` is one 0x18-byte vector object at `render_system + 0xd7f0 + offset` (offsets step by 0x18);
- `r8d` is the item count (always 1), `r9d` is a constant passed through `0x140181a40` (which is just
  `or eax,-1; ret`), stack `+0x20` is flags, stack `+0x28` is the job callback.
- The append half `0x1402c29a0` grows the vector to 0x120-byte items and writes:
  `item+0xd8 = callback`, `item+0xf0 = 0xffffffff`, `item+0xf4 |= flags`, `item+0xf8 = index`,
  `item+0xfc = r8d`. The dispatcher's `[item+0xd8]` is therefore exactly the callback RVA in this table.
- `POSTPROC` (`0x327270`) and `POST_TONEMAP` (`0x323e70`) in the table are the same callbacks the existing
  `native_taa_history.hpp` and `post_tonemap_pass.hpp` already hook; v999 does not hook either of them again.

| id | name | name RVA | callback RVA | register call |
|---|---|---|---|---|
| 0 | `ARK_RENDERPASS_CLEAR` | 0x1B2E700 | 0x3110E0 | 0x35002A |
| 1 | `ARK_RENDERPASS_ASYNC_KICKOFF` | 0x1B2E718 | 0x34FBD0 | 0x35008A |
| 2 | `ARK_RENDERPASS_ASYNC_CLEAR` | 0x1B2E738 | 0x310FB0 | 0x3500ED |
| 3 | `ARK_RENDERPASS_UPDATE_NPC_DECALS` | 0x1B2E758 | 0x340F10 | 0x350150 |
| 4 | `ARK_RENDERPASS_CULL_OCEAN` | 0x1B2E780 | 0x31A320 | 0x3501BA |
| 5 | `ARK_RENDERPASS_GATHER_UMBRA` | 0x1B2E7A0 | 0x31C710 | 0x3501F6 |
| 6 | `ARK_RENDERPASS_GATHER_LIGHTS` | 0x1B2E7C0 | 0x31BCC0 | 0x350232 |
| 7 | `ARK_RENDERPASS_INDIRECT_LIGHTING_GATHER_LIGHTS` | 0x1B2E7E0 | 0x3101D0 | 0x35026E |
| 8 | `ARK_RENDERPASS_GATHER_DYN_MODELS` | 0x1B2E810 | 0x31AD30 | 0x3502AA |
| 9 | `ARK_RENDERPASS_GATHER_DYN_MODELS_FINALIZE` | 0x1B2E838 | 0x31A150 | 0x3502E6 |
| 10 | `ARK_RENDERPASS_SORT` | 0x1B2E868 | 0x33D320 | 0x350322 |
| 11 | `ARK_RENDERPASS_SHADOWS_PREPARE` | 0x1B2E880 | 0x333550 | 0x35035D |
| 12 | `ARK_RENDERPASS_COLLECT_DYNAMIC_CASTERS` | 0x1B2E8A0 | 0x330020 | 0x3503C7 |
| 13 | `ARK_RENDERPASS_GATHER_CASTERS` | 0x1B2E8C8 | 0x330C10 | 0x350431 |
| 14 | `ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_DYNAMIC_MODELS` | 0x1B2E8E8 | 0x310230 | 0x35046D |
| 15 | `ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_WORLD_STATE_AND_OCCLUDERS` | 0x1B2E920 | 0x310200 | 0x3504A9 |
| 16 | `ARK_RENDERPASS_GPUCULLING` | 0x1B2E968 | 0x2FC030 | 0x3504E1 |
| 17 | `ARK_RENDERPASS_DEPTHPASS` | 0x1B2E988 | 0x2FD8D0 | 0x350547 |
| 18 | `ARK_RENDERPASS_DEPTHMINMAX` | 0x1B2E9A8 | 0x2FC130 | 0x3505AD |
| 19 | `ARK_RENDERPASS_SSAO` | 0x1B2E9C8 | 0x305BE0 | 0x350613 |
| 20 | `ARK_RENDERPASS_SHADOWS_WORLD` | 0x1B2E9E0 | 0x32F390 | 0x350679 |
| 21 | `ARK_RENDERPASS_SHADOWS_DYNAMIC` | 0x1B2EA00 | 0x32F380 | 0x3506DF |
| 22 | `ARK_RENDERPASS_INDIRECT_LIGHTING_ACCUMULATE_FINAL_SH` | 0x1B2EA20 | 0x310190 | 0x350749 |
| 23 | `ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_IBL` | 0x1B2EA58 | 0x3102A0 | 0x350785 |
| 24 | `ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_PROBES` | 0x1B2EA88 | 0x310260 | 0x3507C1 |
| 25 | `ARK_RENDERPASS_SCREENSHADOWS` | 0x1B2EAB8 | 0x32C7F0 | 0x3507F9 |
| 26 | `ARK_RENDERPASS_TLFLIGHTCULL` | 0x1B2EAD8 | 0x2F7260 | 0x35085F |
| 27 | `ARK_RENDERPASS_FORWARD_WORLD` | 0x1B2EAF8 | 0x3133D0 | 0x3508C9 |
| 28 | `ARK_RENDERPASS_FORWARD_DYNAMIC` | 0x1B2EB18 | 0x3133B0 | 0x350905 |
| 29 | `ARK_RENDERPASS_FOG` | 0x1B2EB38 | 0x309430 | 0x35093D |
| 30 | `ARK_RENDERPASS_UPDATE_OCEAN_FFT` | 0x1B2EB50 | 0x3053B0 | 0x3509A3 |
| 31 | `ARK_RENDERPASS_UPDATE_OCEAN` | 0x1B2EB70 | 0x305370 | 0x350A09 |
| 32 | `ARK_RENDERPASS_RENDER_WATER` | 0x1B2EB90 | 0x3045A0 | 0x350A6F |
| 33 | `ARK_RENDERPASS_DEPTHALPHA_LIT` | 0x1B2EBB0 | 0x3222A0 | 0x350AD5 |
| 34 | `ARK_RENDERPASS_DOF_AUTOFOCUS` | 0x1B2EBD0 | 0x3150D0 | 0x350B3F |
| 35 | `ARK_RENDERPASS_DOF_ALPHA` | 0x1B2EBF0 | 0x314B10 | 0x350BA5 |
| 36 | `ARK_RENDERPASS_DOF` | 0x1B2EC10 | 0x315450 | 0x350C0B |
| 37 | `ARK_RENDERPASS_EMISSIVE` | 0x1B2EC28 | 0x31F220 | 0x350C71 |
| 38 | `ARK_RENDERPASS_ALPHA_LIT` | 0x1B2EC40 | 0x31DCE0 | 0x350CD7 |
| 39 | `ARK_RENDERPASS_EMISSIVE_ONLY` | 0x1B2EC60 | 0x323210 | 0x350D0F |
| 40 | `ARK_RENDERPASS_DARKVISION` | 0x1B2EC80 | 0x31F960 | 0x350D75 |
| 41 | `ARK_RENDERPASS_DISTORTION` | 0x1B2ECA0 | 0x3222F0 | 0x350DDB |
| 42 | `ARK_RENDERPASS_POSTPROC` | 0x1B2ECC0 | 0x327270 | 0x350E41 |
| 43 | `ARK_RENDERPASS_POST_TONEMAP` | 0x1B2ECD8 | 0x323E70 | 0x350E79 |
| 44 | `ARK_RENDERPASS_WAIT_FOR_ASYNC` | 0x1B2ECF8 | 0x34FBD0 | 0x350ED8 |
| 45 | `ARK_RENDERPASS_TEXTURE_STREAMER` | 0x1B2ED18 | 0x34FD00 | 0x350F3E |
| 46 | `ARK_RENDERPASS_IGGY_MANAGE_TEXTURES_PACKS` | 0x1B2ED38 | 0x31A690 | 0x350FA2 |
| 47 | `ARK_RENDERPASS_IGGY_3D` | 0x1B2ED68 | 0x31A710 | 0x350FF6 |
| 48 | `ARK_RENDERPASS_IGGY_FULLSCREEN` | 0x1B2ED80 | 0x31A8E0 | 0x351041 |
| 49 | `ARK_RENDERPASS_IGGY_TEXTURES_SUBSTITUTIONS` | 0x1B2EDA0 | 0x31AC40 | 0x351077 |
| 50 | `ARK_RENDERPASS_POST_IGGY` | 0x1B2EDD0 | 0x31AB10 | 0x351105 |
| 51 | `ARK_RENDERPASS_OCCLUSION_QUERIES` | 0x1B2EDF0 | 0x31D7E0 | 0x35113A |

`ASYNC_KICKOFF` (1) and `WAIT_FOR_ASYNC` (44) are the only duplicate callback (`0x34fbd0`). That callback is
`mov [deferred+0x12ad4],1; xor al,al; ret`; it never finishes a command list, so it contributes no list attribution.
The lookup takes the first match (id 1); the table still lists both names.

### Chosen interception point: 0x2c5d50, the deferred job-item executor

`0x2c5d50` is the one path that runs a render-pass callback and then finishes its deferred list:

```
0x2c5d50  40 53                    push rbx
0x2c5d52  48 83 ec 20              sub rsp,0x20
0x2c5d56  48 8b d9                 mov rbx,rcx                 ; item
0x2c5d59  48 8b 89 08 01 00 00     mov rcx,[rcx+0x108]         ; deferred context
...
0x2c5d6c  f7 83 f4 00 00 00 00 01 00 00  test dword [rbx+0xf4],0x100
0x2c5d7d  48 8b cb                 mov rcx,rbx
0x2c5d80  ff 93 d8 00 00 00        call qword ptr [rbx+0xd8]   ; ARK_RENDERPASS_* callback
0x2c5d86  0f b6 f8                 movzx edi,al
...
0x2c5da1  48 8b 8b 08 01 00 00     mov rcx,[rbx+0x108]
0x2c5dad  e8 ...                   call 0x2bba20               ; FinishCommandList(restore=FALSE)
0x2c5db2  48 89 83 18 01 00 00     mov [rbx+0x118],rax         ; the new command list
```

- Its only direct caller is `0x2c85b6` in the job scheduler loop (function around `0x2c8540`); a rel32 scan of
  `.text` finds no other call.
- `0x2bba20` finishes through the context vtable slot `+0x390` (slot 114, `FinishCommandList`) and returns the list. It first compares
  `[context]` with the global immediate-context vtable at `0x32d4ef8` and returns 0 for the immediate context, so the
  list at `[item+0x118]` is only produced for a real deferred context.
- The other callback path, `0x2c5f60` (only caller `0x2c739d`), is entered only for items whose scheduler flag
  `0x400` is set (`0x2c738e` test, set at `0x2c77fc`). That path compares `[item+0x108]`'s vtable with the same
  immediate-context vtable at `0x2c5f80`, then temporarily stores the global immediate context pointer
  `0x32d4ef0` at `item+0x108` (`0x2c5fbb`). `0x2bba20` then returns 0, so this path cannot create a command list.
  It is the "run the callback directly on the immediate context" fallback and needs no list attribution.
- The pass map is installed only when `build/native-gpu-timing` is present (`src/dxgi_probe.cpp` startup, next to
  the native timer). One MinHook on `0x2c5d50`; no new D3D11 vtable slot is used.

### Hook ownership

`src/` had no hook on `0x2c5d50` (or any `0x2c5*` address) before v999; a grep finds only the `0x327270` comment in
`native_taa_history.hpp`. The two pass callbacks the mod already owns remain owned by their existing headers:
`ARK_RENDERPASS_POSTPROC` `0x327270` (`native_taa_history.hpp`) and `ARK_RENDERPASS_POST_TONEMAP` `0x323e70`
(`post_tonemap_pass.hpp`). v999 hooks the dispatcher instead, so there is exactly one MinHook on the dispatcher address
and no second hook on either callback.

### Pass tagging, list map and status

- `render_pass_map.hpp`: the dispatcher detour reads `[item+0xd8]` (callback) and `[item+0x108]` (deferred context),
  maps the callback RVA through `render_pass_table.hpp`, and sets thread-local `(current_pass, current_context)`
  around `original(item)` (a `__try/__finally` restores the previous values, so nested passes work).
- The existing census-free `FinishCommandList` observer (`TaaDispatchHook::finish_observer`) calls
  `render_pass_map.finished(c,l)`. While the thread-local pass is set and `c == current_context`, it records
  `list -> pass` in `CommandListPassMap` (open addressing, 8192 slots, 16 probes, atomic key/pass, no mutex).
  A full probe window evicts its home slot and is counted; a list executed more than once keeps its pass; a list
  re-recorded at a reused address is overwritten before it is executed.
- The `ExecuteCommandList` path looks the pass up once per list and passes it to
  `NativeListGpuTimer::begin_list(context, phase, pass)`. The timer now stores a pass id next to the phase and
  publishes `Stats::last_pass` (per pass and phase, frame totals) with the existing lagged timer frame.
- `GpuPassWindow` keeps the last 900 unique completed timer frames. `gpu_passes` reports
  `{enabled, frames, window, passes:[{name, total_ms, eye0_ms_p50, eye1_ms_p50, eye0_ms_p90, eye1_ms_p90,
  lists_p50}], untagged:{...}, mod_gpu_ms_p50:{fsr_eye0, fsr_eye1, aa_sharpen, eye_copy, eye_overlay,
  layer_overlays, eye_capture, sum}}`. `passes` is sorted by `total_ms` (the mean per-frame
  eye0+eye1 ms over the window); `untagged` is separate and has the same fields. Zero frames are included in the
  percentiles, so a pass that runs on only some frames shows its per-frame cost and its p90. `lists_p50` is the
  total list count per frame across all four phases (normally eye0+eye1). `mod_gpu_ms_p50` comes
  from the same 900-frame feed, next to the pass table.
- Only `build/native-gpu-timing` installs the dispatcher hook and the window; without it there is no new hook, no
  per-list lookup, and the `gpu_passes` block is absent. The pass map is disabled and the window cleared at session
  teardown.

## v999 (2026-11-04): per-pass GPU attribution

Build: `build/compile-v999.cmd` (copy of v998 with `v998 -> v999`) -> `build\d2vr_render_probe_arm_v999.dll`,
`dll_exit=0`, 14,749,184 bytes, sha256 `d616ceced5655fa428bb2c4d68cb646fc6e18a6fa074f41c7a95193fc7712252`.
Changed files add no warnings. The remaining `/W4` warnings are the pre-existing `game_xr.hpp`, `native_hand_contact.hpp`,
`hold_coupling.hpp`, `native_weapon_hands.hpp` and old `dxgi_probe.cpp` sites; the LNK4099 warnings are the FFX static
libraries. No in-game behaviour is claimed; nothing here was run against the game.

Code:

- `src/render_pass_table.hpp`: the 52-name/callback table and the callback->id lookup.
- `src/command_list_pass_map.hpp`: bounded atomic list->pass map.
- `src/render_pass_map.hpp`: the single `0x2c5d50` MinHook and the thread-local pass scope.
- `src/gpu_pass_window.hpp`: the 900-frame per-pass window, nearest-rank percentiles and the status JSON.
- `src/native_list_gpu_timer.hpp`: `PassFrame`, per-list pass tags, `Stats::last_pass`; phase sums unchanged.
- `src/dxgi_probe.cpp`: install/disable with the native-gpu-timing knob, finish observer, execute lookup, status
  `gpu_passes` and `pass_map`, feed under `frame_split_mutex`.

Labs (run with the exact `tools/build-msvc.cmd` flags; `tools/build-msvc.cmd` contains both):

- `build\gpu_pass_attribution_check.exe` -> PASS: "52-pass table, thread-crossing list map (reuse, never executed,
  untagged, bounded, concurrent), per-pass 900-frame window percentiles and mod spans".
- `build\native_list_gpu_timer_check.exe` -> PASS (WARP): "native sum 0.503700 ms, elapsed 2.963300 ms; multilist,
  capacity, owner, stale ticket, async, cleanup, phase and pass tags".
- `tests/context_slot_probe.cpp` was not changed: v999 introduces no new D3D11 vtable slot, so no new WARP slot proof
  is needed. The existing FinishCommandList/ExecuteCommandList hooks are reused.

### Candidate GPU levers (to be confirmed by the first v999 pass numbers)

Budget: 11.1 ms at 90 Hz. At the heavy spot with AA off, native lists 13.8 ms + mod 0.9 ms = 14.7 ms, so the
AA-off gap is about 3.6 ms. FSR4 native adds about 4.7 ms for the two eyes; with FSR4 on the gap is about 8.3 ms.
The ranking below is by expected *recoverable* ms without an image regression; the v999 `gpu_passes` table decides
which passes actually carry the cost.

| rank | lever | what it removes or shares | rough saving at 90 Hz | what confirms it |
|---|---|---|---|---|
| 1 | Second-eye reuse of view-independent passes | Run `SHADOWS_PREPARE/WORLD/DYNAMIC`, `COLLECT_DYNAMIC_CASTERS`, `GATHER_CASTERS/GATHER_UMBRA/GATHER_LIGHTS`, `INDIRECT_LIGHTING_*`, `SORT`, `GPUCULLING`, `TLFLIGHTCULL`, `SCREENSHADOWS` once for the frame and let eye 1 consume the same result. These are the passes the prior evidence places in the eye-0 phase (eye0 6.57 vs eye1 7.25 ms; mono view saved 2.5 ms of list sum on the laptop). | 1.5-3.0 ms (could be larger if the table shows the shadow/probe group is >3 ms; cannot exceed the shared group's eye-0 cost) | `gpu_passes` eye0 vs eye1 for the shadow/probe/gather/cull rows; a second-eye run must keep the same image. |
| 2 | FSR4 cost | The mod timer reports `fsr_eye0`/`fsr_eye1` (about 4.7 ms for both eyes at native). A cheaper FSR4 mode/scale, or one dispatch that serves both eyes where the view difference permits, removes part of that pool. Removing FSR4 entirely recovers the full measured pool but is an image/AA decision, not a free optimization. | 1.5-2.5 ms with a cheaper mode/scale; up to 4.7 ms only if FSR4 is removed | `gpu_passes.mod_gpu_ms_p50.fsr_eye0/fsr_eye1`, `frame_split.mod_gpu_ms_p50`, and an image A/B of the chosen mode. |
| 3 | Per-eye passes at reduced resolution | Run screen-space per-eye work (`SSAO`, `FOG`, `DOF`, `DOF_AUTOFOCUS`, `DOF_ALPHA`, `DISTORTION`, `DEPTHALPHA_LIT`, `ALPHA_LIT`, `RENDER_WATER`) at half resolution in each axis and upscale/composite. This is a pixel-count lever, so it only pays on passes whose GPU time scales with pixels. | 1.0-2.0 ms (if the table shows the screen-space group is 4+ ms and half-res halves it) | `gpu_passes` eye0/eye1 for the screen-space rows; a still-image A/B for softness and edge artefacts. |

Ordering caveat: if the headset run has FSR4 native on, the FSR4 pool (4.7 ms) is the largest single measured pool,
but only the part that a cheaper mode or a shared dispatch can remove is a real lever without changing the user's AA
choice. If the run is AA-off (the 13.8 ms native case), the 3.6 ms gap is best addressed by lever 1 first, then lever 3.

### Open risks (code only; no game run)

- The dispatcher hook runs on job threads. It adds two reads, one linear table scan (52 entries), two thread-local
  writes and one atomic list record per pass job/list, only with `build/native-gpu-timing`. The lab proves the model,
  not the in-game timing.
- The pass table is build-specific; the diagnostic already rejects an unsupported `Dishonored2.exe` by sha256 before
  installing. `ASYNC_KICKOFF`/`WAIT_FOR_ASYNC` share a callback; if a future build makes that callback finish lists,
  both would map to id 1.
- A command-list pointer freed and reallocated without a new `FinishCommandList` could still be looked up with its old
  pass. The engine re-records a reused list before executing it; evictions are counted in `pass_map.list_evicted`.
- `pass_map.unmatched_finishes` counts every `FinishCommandList` outside a tagged pass (including non-render jobs), so
  a large value is expected; it is only a health counter.
- The pass map is installed at the native-gpu-timing startup point, but lists are only measured once
  `TaaDispatchHook` installs the Execute/Finish hooks (already required by the native timer). The first frames before
  that are not in the window.
- `gpu_pass_window` is fed once per new lagged timer sequence and clears on a sequence regression (timer
  clear/reprepare on a resolution/AA change). The first frame after such a reset is dropped, not mis-attributed.
- The mod's own GPU spans are the mod timer's latest completed frame and can lag the native timer by 1-3 frames, the
  same convention as `frame_split`. They are reported next to the pass table, not attributed to a native pass.
- A mod-created command list finished inside a pass callback on the same deferred context would be tagged with that
  pass; current mod lists are immediate-context or their own contexts, and this was not observed in static code.
- The window is ~0.6 MB of static storage and the status block is ~6 KB, written at the existing status cadence; only
  with `build/native-gpu-timing`.


### v999 review fix (Opus)
`item+0x108` is the engine's context wrapper. `0x2bba20` loads the D3D11 context from its first field (`mov rcx,[rcx]`, then
`call [rax+0x390]` = FinishCommandList), and the wrapper carries engine state at `+0x12ad4`. As built, the hook stored the wrapper,
and `finished()` compared it with the D3D11 context the FinishCommandList observer passes, so every list would have been "untagged".
The hook now stores `*(void**)wrapper`. Return type checked: 0x2c5d50 returns `movzx eax,dil` and the caller (0x2c85bb) reads `al`.

## v999 per-pass GPU (user headset, 2026-11-04 16:14-16:18, 90 Hz, FSR4 native, canted; artifacts/claude/perf-engine/headset-v999-passes)

Heavy spot (5 snapshots, interval p50 21-22 ms = 45 fps). GPU ms per frame, p50, both eyes (L/R):
SHADOWS_WORLD 3.36 (1.61/1.75), SHADOWS_DYNAMIC 1.73 (0.87/0.86), FORWARD_WORLD 1.03, DEPTHPASS 1.00, FORWARD_DYNAMIC 0.57,
FOG 0.54, POSTPROC 0.43, SCREENSHADOWS 0.41, EMISSIVE_ONLY 0.28, ALPHA_LIT 0.17, RENDER_WATER 0.15, POST_TONEMAP 0.13, ...
Tagged total 10.45 ms (untagged 0). Mod: FSR4 1.37 + 1.48 = 2.86 ms, sharpen 0.14, copies/overlays 0.12; mod sum 3.2 ms. Frame
GPU is about 13.65 ms against the 11.1 ms budget. Light spots (11 snapshots, about 90 fps): tagged 7.16 ms, shadows 2.39 ms.
**Shadows are 49% of the game's GPU work at the heavy spot, and both eyes render them at full cost.** Sharing the shadow maps
between the eyes saves about 2.5 ms there, most of the 2.5 ms gap. Next: phase 7, second-eye shadow reuse (plan step 4 retry).

## Shadow pipeline (phase 7)

Static analysis only: PE + capstone over `Dishonored2.exe` on disk (Steam build, sha256 `b671f824...6180`), no game launched,
injected into, attached to or read from. Every address below is an RVA. `0x2c5d50` is the deferred job-item executor described
in "Render pass map (v999)"; the pass ids are the rows of `render_pass_table.hpp`.

### Job context layout used by every shadow pass

The executor calls `callback(item)` with `item+0x70` = the view and `item+0x108` = the engine context wrapper (its first field is
the D3D11 deferred context). The shadow callbacks add:

| offset | meaning | evidence |
|---|---|---|
| `+0x48` / `+0x50` | second named-parameter table (16-byte entries `{u32 hash, u32 pad, u64 value}`), count at `+0x50 & 0xffffff` | `0x332de2` searches `[rbx+0x48]` for `parallelShadows` (`cmp [rax],hash`, value `[rax+8]`) |
| `+0x70` | view pointer | `0x333550`, `0x32d4b0`, `0x330020`, `0x330c10`, `0x32c7f0` all read it first |
| `+0xe0` / `+0xe8` | first named-parameter table (16-byte entries `{object*, u32 hash, pad}`, value at `object+0x10`), count at `+0xe8 & 0xffffff` | `0x332900` `mov r10,[rcx+0xe0]`; `0x330020` `mov r10,[rcx+0xe0]` |
| `+0xf4` | job flags; bit `0x100` = cached command list, bit `0x400` = immediate-context fallback | `0x2c5d6c`, `0x2c77fc`, `0x2c738e` |
| `+0xf8` / `+0xfc` | parallel-slice index / count | `0x33355a`, `0x32d4b0`, `0x32ee50`; the executor writes the finished list to `+0x118` |
| `+0x108` / `+0x118` | context wrapper / finished command list | `0x2c5d59`, `0x2c5da1`, `0x2c5db2` |
| `+0x12ad4` / `+0x12ad8` / `+0x12b68` | wrapper state: "no pass work" flag / pass flags / current `parallelShadows` object | `0x34fbd0`, `0x2bc445`, `0x2bc44d`, copied per list by `0x2c6b37/0x2c6b56` |

The two shadow render passes are thin wrappers over one function:

```
0x32f390  xor edx,edx                    ; ARK_RENDERPASS_SHADOWS_WORLD
0x32f392  jmp 0x14032d4b0
0x32f380  mov dl,1                       ; ARK_RENDERPASS_SHADOWS_DYNAMIC
0x32f382  jmp 0x14032d4b0
```

`0x32d4b0` reads `[ctx+0x70]`, loads `rdi = view+0x4590`, then reads the shadow bias cvars from that container through
`0x342640` (the 8 descriptors resolved by the registration table at `0x13732d0..0x1373620`, e.g.
`env/shadow/hwBiasSpot16`). It compares `[ctx+0xf8]` with the global parallel slice count at `0x397a2f8`: slices below the
count go to `0x32ee50` (directional/parallel), the rest go to `0x32d6e0` (spot/omni atlas). Both return `false`.

### What SHADOWS_PREPARE produces and where it lives

```
0x333550  48 83 79 70 00        cmp qword ptr [rcx+0x70],0
0x333555  75 03                 jne 0x14033355a
0x333557  32 c0                 xor al,al
0x333559  c3                    ret
0x33355a  83 b9 f8 00 00 00 00  cmp dword ptr [rcx+0xf8],0
0x333561  0f 85 59 e7 ff ff     jne 0x140331cc0
0x333567  e9 94 f3 ff ff        jmp 0x140332900
```

Slice 0 is the real prepare (`0x332900`); later slices take `0x331cc0`. `0x332900`:

1. Finds the job parameters whose FNV-1a name hashes are `0xa4719e7a` ("default") and `0xf750ce51` ("renderWorld") in the
   `ctx+0xe0` table. `r15` becomes the "default" object, `r12` the "renderWorld" object.
2. Reads the per-view camera/projection block: `view+0x73b0, 0x73b4, 0x73b8, 0x73bc, 0x73c0, 0x73c4, 0x73c8, 0x73cc`
   (near/far and projection scale, `0x332a3d..0x332b09`), `view+0x73f0..0x740c` (`0x332d76..0x332da2`) and the two matrices
   at `view+0x7390` / `view+0x7450` (loaded at `0x332eaa/0x332eb1`).
3. Runs the cascade-fit loop over the gathered parallel lights. `0x330630` (called at `0x332ddd`) writes four cascade far
   values to the caller's buffer (`0x330630` stores at `[rdi]`, `[rdi+4]`, ...; `0x330aac` returns the buffer) and the prepare
   stores the four values it got back at `[r15+0xb00..0xb0c]` (`0x332edd..0x332f0e`).
4. Calls `0x32b280` (called at `0x332ebc`) to build the world-to-shadow / shadow-to-world matrices. Arguments include
   `view+0x7390`, `view+0x7450`, `view+0x3a78`, the "parallelLightExtend" cvar, the cascade-far buffer, and outputs at
   `r15+0x100`, `r15+0x200`, `r15+0x300`, `r15+0xb10`, `r15+0xb20`; the entry stride is `0x100` (`0x333100` `add rbx,0x100`).
   The RTTI in `.rdata` names the type and its static list:
   `idStaticList < arkShadowMapInfo_t , 6 , TAG_IDLIST >` (`0x1d57eb8`), with member names `m_worldToShadow`,
   `m_shadowMapToWorld`, `m_shadowMapSidesInfo`, `m_shadowParallelSliceCnt`, `m_shadowCascadeFars` (`.rdata` reflection
   strings at `0x1d8fe20..0x1d9ffe8`). The six entries are the per-frame shadow map infos.
5. At `0x332de2` it finds the `parallelShadows` parameter (hash `0x98853501`) in the `ctx+0x48` table and reads the
   `parallelLightExtend` cvar from the **view shadow container** at `view+0x4590`
   (`lea rcx,[r14+0x4590]; call 0x342640`). That container is read-only in the whole image: a disassembly scan for
   `[reg+0x4590]` finds 26 references and no store, so it is a fixed per-view named-parameter table (cvars and named
   textures), not the per-frame matrix state.

So the produced state is: **which lights get a map, their world-to-shadow / shadow-to-world matrices, atlas/slice rectangles,
the per-slice texture handles, cascade splits and the count of active maps**. It is stored in the render-world shadow lists
referenced by the "default" object:

| storage | content | evidence |
|---|---|---|
| `default+0x264010` (data) / `+0x264018` (count, 24-bit) | atlas (spot/omni) shadow map infos | `0x32d7fa..0x32d876`, `0x32d8b9..0x32d90e` |
| `default+0x267020` / `+0x267028` | parallel/cascade list used for the last slice | `0x32d7fa`, `0x32d8ab` |
| `default+0x26a030` / `+0x26a038` | the other parallel list (slice fraction path) | `0x32d822..0x32d8ab` |
| `default+0xb00..0xb0c`, `+0x100/+0x200/+0x300/+0xb10/+0xb20` | counts, cascade fars, matrix arrays, slice arrays | `0x332edd..0x332f0e`, `0x32b280` arguments |
| `view+0x4590` | only the shadow cvars and named texture lookups (read-only) | 26 refs, zero stores |

The storage is therefore per-frame/render-world (one object), while its **content is per-view**: each view's
`SHADOWS_PREPARE` rebuilds it from that view's camera fields, so the last prepare wins. Nothing is stored in the view struct
itself. That is the fact the second-eye design uses. The same "default" object is also the model/flag source for caster
collection (`[default+0x3c010]` in `0x330020`, the per-model byte array), so it is a render-world object shared by the two
views, not a per-view render-target set.

### One atlas per render system, cleared per view

`0x371210` is called once from `0x36c852/0x36c85a` (render-system setup) and creates the named images through `0x2ecb20`
(image manager): `_parallelSplitShadowMap` -> `render_system+0xc860`, `_compressedParallelSplitShadowMap` -> `+0xc858`,
`_compressedParallelSliceCache_%i` -> vector at `+0xc868`, `_shadowMapAtlas` -> `+0xc808` via `0x2ceb20` and a target group
at `+0xd488` via `0x2cd690`. So there is **one atlas and one parallel map per render system/frame, shared by both views**; the
second view does not allocate its own. The atlas is a depth target: the clear helper `0x2bafc0` uses the image's clear value
at `+0x80` and flags at `+0x84` and calls `ClearDepthStencilView` through context vtable `+0x1a8` (`0x2bb035`).

Clears found in the shadow paths:

- `0x32d76e` in `0x32d6e0` (atlas path): when the relative slice index `r15d == 0` and the parent byte `[r14+0x21] == 0`, it
  walks the global atlas-info list object at `0x3308d88`/`0x3308d90` (count at `+8`) and calls `0x2bafc0` for every entry.
- `0x32f055` in `0x32ee50` (directional path): clears the current parallel slice depth before rendering it.

Both clears run inside `SHADOWS_WORLD`/`SHADOWS_DYNAMIC`, i.e. **once per eye per frame**. That is exactly the v817/v818
cause (1): the second view re-cleared the shared atlas and rendered it from its own camera, so the first eye's contents were
gone. The clear is guarded only by slice/parent flags, not by a "view already rendered" flag.

The atlas render target is fetched from the global at `0x3309b18` (`+0x80`/`+0x30`) and bound with `0x2bb470` at `0x32ddb7`;
the per-light depth targets are bound through `0x2bc340`/`0x2bc370`. Both paths use the images created by `0x371210`, so
nothing in `SHADOWS_WORLD`/`SHADOWS_DYNAMIC` creates a per-view copy.

`0x32db10` (`ShadowMapCopy`, marker string at `0x32db37`) copies a map with `CopySubresourceRegion` (context vtable `+0x170`
at `0x32dbf8`) when manual AFR management is enabled; `0x32db9e` reads the shadow flag `0x10` from `+0x12ad8` and the current
`parallelShadows` object from `+0x12b68`. This is the partial-update cache path; it does not survive the second view's clear.

### How FORWARD_* and SCREENSHADOWS read it

`0x32fd40(binder, unused, view+0x4590)` binds the shadow resources into the pass's parameter table:

- `0x310a80` / `0x310ad0` set float4 constants; the descriptors are the shadow slice/far arrays at `0x22b3930` and
  `0x22b3950` (registered from the `parallelLightSliceFars*` / `parallelLightSliceNears*` strings).
- `0x3107f0` sets a five-element texture array (up to four cascade SRVs plus a fallback) under the descriptors at
  `0x22b3750` and `0x22b3770`; the fallback image pointers come from the static table at `0x3308e20..`.
- `0x310780` sets the scalar `shadow/bias`, `shadow/normalOffset`, `shadow/slopeScaleBias` constants under
  `0x22b38d0/0x22b38f0/0x22b3910`.
- `0x342740` looks up the atlas texture in the view's shadow container by the descriptor at `0x22b3c70`; `0x310750` binds it
  (default image if the lookup is empty).

Call sites: `0x311e96` in `FORWARD_WORLD` (0x3133d0), `0x31e038` in `FORWARD_DYNAMIC` (0x3133b0), `0x32ca47` in
`SCREENSHADOWS` (0x32c7f0). `SCREENSHADOWS` additionally reads `ssaoScreenShadows` (hash `0x779feccc`) and
`decompressedDepth` (hash `0x21181874`) and calls `0x2bc340` with flag `0x18`.

The per-light matrix/rectangle buffer is bound by name `shadowMapsInfos` (FNV-1a `0x3719f109`): `0x311e54` looks it up in the
forward pass parameter table, converts it with `0x2c2b90/0x2d38c0`, and binds it with `0x310720`; the same happens at
`0x31e0c0`. `cpu_shadowMapsInfos` (`0x3b921636`) is the CPU-side producer/consumer stub at `0x2f7366`. The named parameter
descriptors `shadowProjections` / `shadowInvProjections` are registered at `0x13732f0` / `0x1373290`. `0x2bc340` stores the
current `parallelShadows` object at wrapper `+0x12b68` (`0x2bc44d`), converts up to four per-light image handles through
`0x2bc370`/`0x2bd230`, and issues the context call at `+0x108` (`0x2bc489`); the matrices/rects the shaders sample are the
`shadowMapsInfos` buffer. So the sampling data is (a) the bound atlas/slice textures, (b) `shadowMapsInfos` with matrices and
rects, (c) the slice far/near float constants, and (d) the bias scalars.

### Where caster culling depends on the view

- `ARK_RENDERPASS_COLLECT_DYNAMIC_CASTERS` `0x330020`: reads the view at `[ctx+0x70]`, the camera basis from
  `view+0x7490/0x74a0/0x74b0/0x74c0`, the model array at `[rbx+0x3c010]`, per-model flags, and the squared cull distance
  global at `0x32f899c`; the distance test is at `0x3302c2..0x3302e9`. `view+0x3a34` bit 1 gates the extra pass.
- `ARK_RENDERPASS_GATHER_CASTERS` `0x330c10`: checks `[view+0x3a20] == 0` and `[view+0x3a34] & 2` at `0x330cbb..0x330ccf`,
  builds the view position from `view+0xbcd8..0xbce0` (`0x330d8a..0x330dcf`), uses the render-world matrices at
  `+0x10800/+0x10810` (`0x330d18..0x330d85`), and appends to the caster list at `0x32f9b70` (`0x330f70..0x33101f`).

Both read the per-view camera, so the caster set is genuinely per-view. That is the v817/v818 cause (2), measured there as
2.3% of faces differing.

### Are the parallel cascades view-dependent?

Yes. The fit at `0x332900` is driven entirely by per-view fields (`view+0x73b0..0x73cc`, `view+0x73f0..0x740c`), the view
matrices at `view+0x7390`/`view+0x7450`, the camera position globals `0x32f9a80..0x32f9a88` written at `0x33084a..0x330862`,
and the cvars `r_shadowCascadeFars` / `r_tightenParallelShadows`. `0x330630` fits each slice's fars from the view's visible Z
range and writes the four fars (`0x330900..0x330ad3`). For a 6.5 cm eye offset the fit is nearly identical in the middle of
the view but can differ at the near/far edges; that is why the second view must reuse the first view's fitted state instead
of re-fitting.

### v817/v818 mapped to the static evidence

| recorded cause | static site | effect |
|---|---|---|
| atlas not kept between views; second view re-clears/reallocates | `0x32d76e` / `0x32f055` depth clears, run unconditionally inside each view's WORLD/DYNAMIC | second view's clear destroys the first eye's depth |
| 2.3% of faces have per-view caster sets | `0x330020` camera/distance test; `0x330c10` `view+0x3a20/0x3a34`, view position `+0xbcd8` | second view's caster list differs at frustum edges |

## v1000 design: render the shadows once per frame

### Rule

Within a frame, the first eye runs `SHADOWS_PREPARE`, `COLLECT_DYNAMIC_CASTERS`, `GATHER_CASTERS`, `SHADOWS_WORLD` and
`SHADOWS_DYNAMIC` normally. The second eye runs none of them. Its `FORWARD_WORLD`/`FORWARD_DYNAMIC` and `SCREENSHADOWS`
still run per eye and read the first eye's state and atlas contents. Nothing is copied and nothing is restored: the
render-world lists and the depth atlas are not view-owned, so leaving the writers unrun leaves exactly the first eye's
values in place (see the storage table above). This is the "first view's shadow state" the second view samples.

**Exact copy set: zero bytes.** The only data that crosses from the first view to the second is what was already in the
shared render-world lists and the shared depth images; the second view reads it in place. The "restore" for a fallback is
therefore also free: the next frame's first-view PREPARE rebuilds the lists normally.

The five suppressed passes are exactly the ones that mutate the state or the atlas:
`SHADOWS_PREPARE` (rebuilds the lists/matrices from the view), `COLLECT_DYNAMIC_CASTERS` and `GATHER_CASTERS` (rebuild the
caster lists from the view), `SHADOWS_WORLD` and `SHADOWS_DYNAMIC` (clear and render the atlas/slices). Skipping the two
caster passes is part of the reuse: they would otherwise overwrite the shared caster lists that the first eye's shadow
command lists reference, and their per-view result is precisely what is being given up.

### Data retained per frame (no copy)

| data | where | why it is safe to retain |
|---|---|---|
| atlas shadow map infos (up to 6, `0x100` bytes each), count | `default+0x264010/+0x264018` | written only by PREPARE/WORLD/DYNAMIC, all skipped for eye 2 |
| parallel/cascade lists and counts | `default+0x267020/+0x267028`, `+0x26a030/+0x26a038` | same |
| cascade fars, slice counts | `default+0xb00..0xb0c`, `+0xb10` | same |
| world-to-shadow / shadow-to-world matrices, rects | entries pointed to by the lists | same |
| slice/atlas depth image contents | `render_system+0xc808`, `+0xc858`, `+0xc860`, `+0xc868` | the clear and draws are in WORLD/DYNAMIC, skipped for eye 2 |
| view shadow cvar/texture container | `view+0x4590` | read-only; the second view's copy already holds the same cvars |

### Caster union versus the 2.3% loss

The first eye's caster set is used. Widening it to the union of both eye frusta would mean editing the inputs of
`GATHER_CASTERS` (the view position/frustum at `view+0xbcd8`, `view+0x3a20`, `view+0x3a34`, and the camera matrices at
`view+0x10800`) or running the second eye's collect/gather and merging, which is exactly the shared-culling path the earlier
id Tech measurements found slower. v1000 therefore **accepts the recorded tiny loss** (2.3% of faces in v817/v818) and
leaves union widening unimplemented. The image A/B protocol below is how that loss is judged; it is not a claimed result.

### Fallbacks

The runtime falls back to normal rendering for the frame (no suppression for the rest of that frame) when any of these is
seen before the first suppression:

- the pass-map or eye-begin hook prologue does not match (reported as `prologue_mismatch`);
- the eye observer cannot read the view/gates (`unpublished`);
- a second eye begins before a first eye, or a second eye begins twice, or a third view appears (`view_count`,
  `third_view`);
- a second-view writer job is submitted before any first-view writer job in the same frame (`ordering`);
- the two views have different shadow/gather gates: `view+0x3a20` or `view+0x3a34` bit 1 (`light_set_mismatch`);
- the engine offers a cached command list for a suppressed job (`item+0xf4 & 0x100`, `cached_job`).

The light-set check is the cheap per-view gate pair, not a gathered-light count: the second view's gather is itself skipped,
so its gathered count cannot be compared without running the penultimate work. This is stated as a limitation, not hidden.

### Hazards and how the design handles them

- **Async compute / command lists.** The suppression is a callback substitution inside `0x2c5d50`, not an early return: the
  executor still calls `FinishCommandList` on the same deferred context and stores a valid (empty) list at `item+0x118`.
  The scheduler's `0x2c77b0` therefore sees a normal job. Returning early from the hook would have left the previous list
  in `+0x118` and is explicitly not done. `WAIT_FOR_ASYNC` is not in the skip set; the first eye's waits still run.
- **Cached jobs.** If `item+0xf4 & 0x100` is set, the executor would replay the previous list instead of the substituted
  callback; the frame falls back rather than suppressing such a job.
- **TEXTURE_STREAMER.** `ARK_RENDERPASS_TEXTURE_STREAMER` (45, `0x34fd00`) runs once per frame. The shadow images are render
  targets created by `0x371210`, not streamed textures; a streamer-driven recreation of `_shadowMapAtlas` or the slice
  images in mid-frame would invalidate the retained contents and is not detectable from the pass hook. This is an open risk.
- **Partial slice updates (`r_shadowParallelPartial*`).** With partial updates, the first eye may update only a subset of
  parallel slices in a frame. The second eye reuses whatever is resident; it does not get its own slice update. That is the
  intended saving, but if the engine expected the second eye to advance a slice, the retained map can be one slice-update
  behind. Check temporal stability of sun shadows in the A/B.
- **Parallel slice count changes mid-frame.** `0x397a2f8` selects the slice path. If a cvar change is applied between the
  eyes, the first view's state and the second view's expected slice count disagree; the current check does not sample that
  global. Open risk; the A/B includes a cvar-change spot.
- **Other per-view passes after PREPARE.** `GATHER_LIGHTS`, `SORT` and the indirect-lighting passes are not skipped. If any
  of them recycles the shadow info entries or caster buffers for the second view, the retained state is invalidated. Static
  code shows the shadow lists are written by the five skipped passes, but this is not a whole-frame ownership proof.
- **Ordering.** The design assumes the first view's shadow jobs are recorded before the second view's forward passes, and
  that the two views are serialized (the frame loop `0x361d60` is eye begin + RenderSingleView). A late first-eye shadow job
  after the second view's forward would write the state too late. The engine's per-view job dependency handling was not
  proven here.
- **Mod TAA history.** The per-eye TAA history pair (`native_taa_history.hpp`) is separate and untouched. The only
  interaction is that the second eye's color now samples the first eye's shadow matrices; TAA will see a stable image as
  long as the shadow content is stable. The mod's alternate-eye path and the TAA twin/history logic do not run through the
  shadow pass hook.
- **Screenshadows.** `ARK_RENDERPASS_SCREENSHADOWS` remains per eye by design (it is screen-space and reads the eye's own
  depth); it reads the retained shadow state through `0x32fd40`, so it is covered by the same A/B.

### Implementation (opt-in `build/shadow-share`, OFF by default)

- `src/shadow_share_model.hpp`: the pure per-frame state machine (eye pair, signatures, five writer ids, per-frame
  fallbacks, counters). `src/shadow_share.hpp`: a mutex-guarded runtime around it plus the status JSON.
- `src/render_pass_map.hpp`: the existing single MinHook on `0x2c5d50` gains an optional `shadow_share::Runtime*`. Before
  calling the original, for a suppressed job it substitutes `skip_shadow_pass` into `item+0xd8`, calls the original (so the
  command list is still finished), and restores the callback in `__finally`. No new hook address is used.
- The eye boundary is the existing `0x351210` hook, already prologue-checked with 32 bytes and owned by this file; v1000
  extends its install condition to `build/shadow-share` and does not create a second hook on that address. Hook ownership is
  unchanged: one MinHook on `0x2c5d50`, one on `0x351210`.
- `src/dxgi_probe.cpp`: `build/shadow-share` is read next to `build/native-gpu-timing`; the pass map is installed when
  either knob is present. The existing eye-begin observer (`0x351210`, already prologue-checked with 32 bytes) publishes the
  eye index and reads `view+0x3a20`, `view+0x3a34`, `view+0x3a37` for the pair check; its CSV remains gated on the selftest
  knob. Status: `"shadow_share": {enabled, knob, installed, frames_shared, second_view_skipped_jobs, fallbacks, reason}`.
  `enabled` is true only when the knob is on and both hooks installed.
- Nothing is installed and no pass is touched when the knob is absent. When the knob is present, non-shadow pass jobs
  pay one relaxed atomic load and a pass-id range check; only the five shadow writer ids take the state mutex.

### Labs

- `tests/shadow_share_state_check.cpp`: model lab for OFF, a normal pair (five writers skipped, FORWARD/SCREENSHADOWS kept),
  view count (second before first, duplicate second, third view), a frame with one view, light-set mismatch, cached job,
  writer ordering, resize/view reallocation, per-frame fallback and recovery, unpublished view.
- `tests/shadow_share_consistency_check.cpp`: offline recorded-state lab. A canonical shadow blob is written only by the
  five writer passes; the real model decides suppression; the lab asserts both eyes' FORWARD read the same blob and that no
  writer ran between them in a shared frame, that a fallback frame keeps per-eye blobs (and they differ), that the next clean
  frame shares again, and that a third-view partial frame is counted.
- Both are in `tools/build-msvc.cmd` with the same `/W4 /WX /MT /Isrc` flags as the other model labs.

## v1000 (2026-11-04): second-eye shadow reuse behind `build/shadow-share`

Build: `build/compile-v1000.cmd` (copy of v999 with `v999 -> v1000`) -> `build\d2vr_render_probe_arm_v1000.dll`,
`dll_exit=0`, 14,754,304 bytes, sha256
`e32500b1a4ce13bea9a438f8c360c50d83c9ec37a9fb71787b7ab21596d3d4d4`. The changed files add no warnings; the 30 `/W4`/LNK4099
warnings in the log are the same set as v999 with shifted line numbers (pre-existing `game_xr.hpp`,
`native_hand_contact.hpp`, `hold_coupling.hpp`, `native_weapon_hands.hpp`, old `dxgi_probe.cpp` sites and the FFX PDBs).
No in-game behaviour is claimed; nothing here was run against the game.

Labs (exact `tools/build-msvc.cmd` flags):

- `build\shadow_share_state_check.exe` -> PASS: "shadow-share per-frame machine (off, pair, five writers skipped,
  forward/screen kept, view-count, single-view, light-set mismatch, cached job, writer ordering, resize, per-frame fallback,
  unpublished view)".
- `build\shadow_share_consistency_check.exe` -> PASS: "recorded shadow-state blob is identical for both eyes in shared
  frames, no writer between them; fallback frames keep per-eye state; third-view partial frame is counted".

### Sim/headset protocol

Purpose: decide (a) whether the second eye really stopped doing shadow GPU work, (b) whether the image is still right in
both eyes, and (c) whether the first eye's caster set is acceptable. No result is claimed until these are read.

0. Off run first: no `build/shadow-share` file, `build/native-gpu-timing` present, same spot/settings as the v999 run.
   Record the `gpu_passes` table (the rows `ARK_RENDERPASS_SHADOWS_WORLD`, `SHADOWS_DYNAMIC`, `SHADOWS_PREPARE`,
   `COLLECT_DYNAMIC_CASTERS`, `GATHER_CASTERS`, `SCREENSHADOWS`), `mod_gpu_ms_p50`, `native_gpu.last_list_sum_ms`
   and the frame interval.
1. On run: create `build/shadow-share` and restart. Read `shadow_share` from `xr-live-status.json`: expect `enabled:true`,
   `frames_shared` rising, `second_view_skipped_jobs ≈ 5 * frames_shared` (5 per shared frame), `fallbacks` small and
   `reason` not `prologue_mismatch`/`view_count` in normal play. A `fallbacks` value that grows every frame means the pair
   check is refusing; stop and report the reason instead of judging the image.
2. `gpu_passes`: the intended result is `SHADOWS_WORLD` and `SHADOWS_DYNAMIC` eye1 p50 near 0 while eye0 keeps the v999
   values (about 1.6-1.8 / 0.86 ms per eye at the heavy spot). `eye0_ms_p50` should not move much; `eye1_ms_p50` for the
   two rows is the direct saving. `SCREENSHADOWS` stays per eye. Total tagged should fall by about 2.5 ms at the heavy spot.
3. Image A/B, both eyes, still captures at a few spots: near the player (a wall or crate casting a hard edge within 1-2 m),
   far (a building or fence edge at 10-30 m), and a mixed sun+local-light spot. Use the same pose and time of day for off
   and on (save/load or a fixed standing spot). Compare at least: the shadow edge position on the near surface, the far
   cascade boundary, contact shadows under the player/held object, and the shadow direction on a curved surface. Also watch
   one moving dynamic caster (a patrolling guard or a dropped body) for several seconds: its shadow must stay attached and
   not stutter, because the dynamic caster list is part of the retained state. Look specifically at the right eye for a
   one-eye shift or a doubled edge (the first eye should be unchanged), at the join between two cascade slices for a seam,
   and at any flicker when turning slowly (partial slice update / stale slice).
4. Fallback-frame check: force a mismatch if possible (e.g. a view flag change such as a cutscene/zoom transition) and
   confirm `shadow_share.fallbacks` increments and the image matches the off run on that frame. This is a code-path check,
   not a quality claim.
5. Acceptance: only if the right-eye image is indistinguishable from off at the still spots, both-eye shadows persist for
   several seconds, `frames_shared` is high with negligible `fallbacks`, and the `gpu_passes` eye1 rows drop by the expected
   amount. Report the actual `gpu_passes` numbers and the capture paths; do not infer from this document.


## v1001 (Opus, 2026-11-04): v1000 + live build/shadow-share

Reviewed v1000. The skip swaps the job item's callback (`item+0xd8`) for a no-op returning false around the original executor call,
then restores it. Items are per view (`item+0x70`) and per slice, so the first eye's jobs are untouched. The sim still cannot pass the
load screen (pad reads now non-zero, 66, but the screen stays), so validation is a headset A/B. `Runtime::set_live` lets the 1 Hz
knob poll turn sharing on and off mid-session when the session started with the knob (hooks installed). Turning it on resets the model.

## v1001 headset A/B (17:40-17:44) did not test sharing; v1002 fix

`shadow_share.installed=false` and `pass_map.installed=false` in every snapshot. `pass_jobs` was 20,103 (a few seconds), then
an XR close/recovery ran the path at dxgi_probe.cpp ~10241, which called `shadow_share.stop()` and `render_pass_map.disable()`.
Nothing re-installs either within the session, so both on and off segments rendered normally. v1002 keeps the hook across an XR
close/recovery (it only resets the shadow model and the GPU window). The full-session teardowns still disable it.

## v1003 (user request 2026-11-04 17:55: "normal launcher with the new improvements")

Shadow reuse is ON by default (hooks installed every session); `build/no-shadow-share` turns it off live. Elision stays off
(no measured gain). Measurement left on in the user's home: `build/native-gpu-timing`, so normal play reports `gpu_passes` and
`shadow_share` (frames_shared, fallbacks) and the gain can be confirmed without A/B scripts. Visual correctness is still unverified
in game; v1002/v1003 are the first builds whose hook survives an XR recovery.

## v1003 headset (user, 18:25) and the v1004 fix

The user still saw 45 fps at the heavy spot. Status: `shadow_share` enabled and installed, but `frames_shared` 0, one fallback,
reason `view_count`. The right eye still rendered SHADOWS_WORLD 2.08 + SHADOWS_DYNAMIC 1.13 ms. Heavy spot GPU: eye0 6.11 + eye1
6.13 ms of native lists + 3.13 ms mod, about 15.4 ms against 11.1. Elision was on (PS + wide); replay p50 8.3 ms.
Cause: the model required the raw engine eye index to be 0 then 1. frame_split.hpp already records that the raw index is not
the eye in paired mode, and the null GUI view also calls 0x351210. The first frame failed `view_count` and no pair ever formed.
v1004 passes the begin order of non-null views since the last Present (the frame-split counter, reset every Present).

## v1004 headset (user): much faster, but no shadows in the right eye -> v1005

Status: frames_shared 5644, fallbacks 0. Eye1 GPU 2.60 ms vs eye0 5.29 ms (SHADOWS_WORLD/DYNAMIC right eye 0.00), interval p50
11.42 ms (about 88 fps), FSR4 native. But the right eye had NO shadows. The phase-7 claim that the shadow setup (light list,
matrices, atlas rects) lives in shared storage is wrong: with SHADOWS_PREPARE / COLLECT_DYNAMIC_CASTERS / GATHER_CASTERS
skipped, the second view had no shadow setup. v1005 skips only SHADOWS_WORLD + SHADOWS_DYNAMIC (the GPU atlas draws, which also
hold the only atlas clears). The second view's setup passes run, so it builds its own lists and samples the first view's atlas.
Expected risk: local-light matrices come from the light and should match exactly. The sun cascades are fitted per view (canted
eyes), so right-eye sun shadows may be offset. If they are, the next step is to make the second view use the first view's
cascade fit, or to skip only the local-atlas part. The lab now models a per-view setup with a shared atlas.

## Shadow setup ownership (phase 8)

Static only: PE + capstone over `Dishonored2.exe` on disk (Steam build, sha256 `b671f824...6180`), plus the v1004/v1005
headset results already in this document. Every address is an RVA. This section corrects the phase-7 conclusion: the
object named `default` is shared, but the setup content written into it and the render-graph resource the forward/fog
passes bind are per view. That split explains both headset results.

### a. Which object is per view

The named parameter `default` (FNV-1a `0xa4719e7a`) is read from the job item's first parameter table
`item+0xe0`, count at `item+0xe8 & 0xffffff`. Entries are 16 bytes `{descriptor*, 0}`; the descriptor has the hash at
`+8` and the value at `+0x10` (`0x34b0b0`: `0x34b0f6` loads `[rcx+0xe0]`, `0x34b0f9` compares `[rax+8]`, `0x34b116`
stores `[rax+0x10]=rdx`). PREPARE resolves it at `0x332941..0x332963` and uses the returned value as `r15`.

The per-view render-job builder is `0x351780`. Its caller is the per-view render path immediately after the eye
boundary:

```
003668b2  lea rcx,[r14+0xd7f0]      ; the pass manager
003668c0  mov r8,[r14+0x90]
003668c7  mov rdx,[r13+0x20]        ; view
003668d1  call 0x140351210          ; eye begin
003668e5  call 0x140351780          ; build this view's jobs
```

Inside `0x351780`, the shadow/light job item gets `default` from a **manager-relative** address:

```
0035369a  lea rax,[r12+0x1c8ac0]    ; r12 = pass manager
003536a6  mov r8d,0xa4719e7a        ; "default"
003536ac  mov rdx,rax
003536af  mov rcx,[rbp]             ; the job item
003536b3  call 0x14034b0b0          ; set parameter
```

`r12+0x1c8ac0` is the `default` subobject constructed by `0x35a3d0` (`0x35a78a` calls it from the pass-manager
constructor `0x35a4b0`, which `0x35b94c` calls at outer `+0xd7f0`; the outer constructor `0x35b410` is called once from
`0x1376f7b`). Both views enter `0x351780` with the same `r14+0xd7f0`, so **`default` is the same object for both
views** and is not per view.

The constructor lays out the setup storage inline: `0x35a422..0x35a48c` resets the three lists at `+0x264010`,
`+0x267020`, `+0x26a030` and points their data at `+0x264020`, `+0x267030`, `+0x26a040` in the same object (capacity
`0x60040`, six `arkShadowMapInfo_t` entries of `0x100`). PREPARE writes the per-view content into that shared object:

- four cascade fars at `[r15+0xb00..0xb0c]` (`0x332edd..0x332efe`);
- matrices and side/slice arrays at `r15+0x100`, `+0x200`, `+0x300`, `+0xb10`, `+0xb20` via `0x32b280`
  (arguments at `0x332e47..0x332ebc`; `0x100` entry stride at `0x32bf37`/`0x32c1d7`).

The second view's PREPARE therefore overwrites the first view's matrices/fars in the shared object before the second
view's forward records. That is the v1005 "miscalculated at some spots" mechanism.

The per-view part sits in the job item and the view struct, not in `default`:

| per-view field | evidence |
|---|---|
| job item `+0x70` = view, so every item is view-specific | `0x32d4b6`, `0x3329c5`, `0x3301...` |
| job item `+0x58`/`+0x60` and `+0x80`/`+0x88`: render-graph resource tables, 16-byte `{u32 hash, u32 pad, u64 value}` | `0x2b08c0` reads `+0x58`/`+0x60` (`0x2b08c3`, `0x2b08d3`) and returns `entry+8` (`0x2b08ea`) |
| job item `+0x48`/`+0x50`: `parallelShadows` table; value bound from the global shadow manager `0x3308dd8+0x98` | `0x332e18` compares `0x98853501`; `0x3535da..0x3535f2` binds the global |
| view `+0x4590`: shadow cvar/texture container, read-only | 26 refs, no stores; `0x342640`/`0x342740` lookups |
| view `+0x3a20`, `+0x3a34`: shadow/gather gates | `0x32ee50`, `0x330c10`, `0x32d6e0` |
| view `+0x7390`/`+0x7450`, `+0x73b0..0x740c`: camera/projection, cascade fit input | `0x332a3d..0x332da2`, `0x332eaa/0x332eb1` |
| wrapper `+0x12ad4`/`+0x12ad8`/`+0x12b68`: per-command-list flag/pass flags/current `parallelShadows` | set at `0x2bc445/0x2bc44d`, copied per list by `0x2c6b37/0x2c6b56` |

`default` is an overloaded parameter name: on another item in the same builder, `0x353662..0x353676` sets `default` to
the static fallback-image array `0x3308e20` (the array `0x32fd40` binds when a slice lookup is empty). Only the
`manager+0x1c8ac0` value is the shadow setup object. This is why v1006 does not redirect `default` at all.

### b. The read path

`0x32fd40(binder, viewPositionStruct, view+0x4590)` is the per-pass shadow material binder (`rcx` is the binder from
`0x2c5c70`, `r8` is the view's shadow container):

- `0x310a80` at `0x32fd7d`, `0x32fdf8`, `0x32fe0b` binds the slice near/far float arrays. Their sources are the
  `.bss` globals `0x3309920`/`0x3309940..0x330995c`; a full-text scan finds no writes to them (only reads at
  `0x32fd6e..` and `0x3733d4`), so they are static zero constants, not per-view setup.
- `0x3107f0` at `0x32fe67`/`0x32fec3` binds two five-element texture arrays under descriptors `0x22b3750`/`0x22b3770`,
  with the static fallback images `0x3308e20/60/a0/e0` and one slice slot filled from the global slice count
  (`0x397a2f8`).
- `0x342740(view+0x4590, descriptor 0x22b3c70)` at `0x32ffa3` looks up the atlas texture (or uses the default image
  `0x3308e20+0x98`), then `0x310750` binds it at `0x32fffb`.
- `0x342640(view+0x4590, 0x22b38d0/0x22b38f0/0x22b3910)` at `0x32ff6b/0x32ff7e/0x32ff91` reads the bias scalars and
  binds them through `0x310780`.

Call sites (full-text xref of `0x32fd40`): `0x311e96` FORWARD_WORLD (27), `0x31e046` FORWARD_DYNAMIC (28),
`0x32ca55` SCREENSHADOWS (25), `0x304244` in RENDER_WATER (32; helper `0x303ae0`, called from `0x304a51`), and
`0x307c6d` in FOG (29; helper `0x307530`, called from `0x30993f`).

`shadowMapsInfos` (FNV-1a `0x3719f109`) is the per-view matrices/rects buffer and is looked up in the job item's
`+0x58` table:

```
00311e54  ba 09f11937          mov edx,0x3719f109      ; FORWARD_WORLD
00311e59  498bcd              mov rcx,r13             ; the job item
00311e5c  e8 5feaf9ff          call 0x1402b08c0        ; -> [item+0x58 entry + 8]
00311e91  e8 8ae8ffff          call 0x140310720        ; bind it
```

The same lookup is at `0x31e0c0..0x31e0c2` (FORWARD_DYNAMIC), `0x3095fa..0x309602` (FOG) and in the parameter
flattener at `0x2b5850..0x2b5856` (which copies the value from a source item table to a target table and binds it at
`0x2b5893`). `cpu_shadowMapsInfos` (`0x3b921636`) is read at `0x2f7366` by TLFLIGHTCULL (`0x2f7260`) from `+0xe0`
(`0x2f737d` reads descriptor `+0x10`). TLFLIGHTCULL is a **producer**, not a reader: `0x2f75c0..0x2f7811` fills
0x70-byte entries into the object's CPU array (`[obj+8] + index*0x70`) from the per-view light nodes. No other pass
looks up `cpu_shadowMapsInfos` in the image.

Every pass that reads shadow setup, from the xrefs above plus the `default`/`parallelShadows` scans:

| pass | id | shadow setup it reads |
|---|---|---|
| SHADOWS_PREPARE | 11 | shared `default` (writes it), `parallelShadows` (+0x48), view camera |
| COLLECT_DYNAMIC_CASTERS | 12 | `default+0x3c010` model flags; writes the per-view caster list in `surfaces` |
| GATHER_CASTERS | 13 | view camera/position; appends to the per-view caster list |
| SHADOWS_WORLD / DYNAMIC | 20/21 | shared `default` lists at +0x264010/+0x267020/+0x26a030; clear/draw the atlas |
| SCREENSHADOWS | 25 | `0x32fd40` (atlas, slices, biases); `ssaoScreenShadows`, `decompressedDepth` |
| TLFLIGHTCULL | 26 | `default` light nodes; writes `cpu_shadowMapsInfos` |
| FORWARD_WORLD | 27 | `shadowMapsInfos` (+0x58) and `0x32fd40` |
| FORWARD_DYNAMIC | 28 | `shadowMapsInfos` (+0x58) and `0x32fd40` |
| FOG | 29 | `shadowMapsInfos` (+0x58) and `0x32fd40` (helper `0x307530`) |
| RENDER_WATER | 32 | `0x32fd40` (helper `0x303ae0`) |

ALPHA_LIT, EMISSIVE, DARKVISION, DOF and the indirect-lighting passes have no `shadowMapsInfos`/`0x32fd40` hit and
therefore do not bind the shadow setup.

### What v1004 and v1005 actually did

- v1004 skipped all five writers. The shared `default` kept the first view's matrices/fars (nothing overwrote them) and
  the atlas kept the first view's depth, but the second view's own `shadowMapsInfos` render-graph resource was never
  produced, so its forward/fog sampled an empty setup. Hence no right-eye shadows despite the correct shared state.
- v1005 ran the second view's PREPARE again. Its `shadowMapsInfos` resource was produced, but from the second view's
  cascade fit, while the atlas had been drawn with the first view's fit. Hence the mismatch at some spots.
- The object identity is not the problem; the per-view resource is. v1006 skips the five writers again (restoring the
  v1004 saving and letting the shared setup and atlas stay the first view's) and redirects each second-view reading
  job's `shadowMapsInfos` value to the first view's captured resource.

### Consequence for the implementation

`src/shadow_share_model.hpp` now has two modes. `setup` suppresses all five shadow writers for the second view and
answers a redirect for every reading pass (FORWARD_WORLD, FORWARD_DYNAMIC, FOG, SCREENSHADOWS, RENDER_WATER). The
redirect value is the first view's value from the same pass when it was captured, otherwise the first captured value
(the canonical one). Therefore a setup frame can never be half redirected: every reader is redirected or the frame is
not in setup mode. If the first view's resource was not captured before the second view's first suppressed job, the
frame uses `atlas` (the v1005 behaviour: run the second view's setup, skip only WORLD/DYNAMIC) and reports
`setup_unavailable`.

Skipping the second view's PREPARE again is safe **because** the only reader-visible per-view output is the
`shadowMapsInfos` resource, and that is redirected. TLFLIGHTCULL still runs for the second view and still writes its
own `cpu_shadowMapsInfos` CPU array, but no pass outside TLFLIGHTCULL reads that array (the GPU `shadowMapsInfos`
lookups above are the only consumers), so it does not affect the shadow sampling. The v1004 headset run already
skipped these five passes and reported only the missing right-eye shadows, not a lighting regression, which is the
in-game check for this static claim.

Runtime mutation: one 8-byte value in the second view's own job-item `+0x58` table, written on the thread that runs
that job and restored in `__finally` before the executor returns. No view camera field, no `default` content, no
shared render-world list and no atlas image is written by the mod. The first view's capture is read under the same
mutex as the model.

## v1006 (2026-11-04): the second view reads the first view's shadow setup

Build: `build/compile-v1006.cmd` (copy of v1005 with `v1005 -> v1006`) -> `build\d2vr_render_probe_arm_v1006.dll`,
`dll_exit=0`, 14,756,352 bytes, sha256
`7f8f9c9f907ebb62c45b49a529b91b97564c6ce9789d7e1c1832680c6cff381b`. The changed files add no warnings: the 30
warnings in `build\v1006-build.log` are the same pre-existing set (game_xr.hpp, native_hand_contact.hpp,
hold_coupling.hpp, native_weapon_hands.hpp, old dxgi_probe.cpp lines and the FFX PDBs). No in-game behaviour is
claimed; nothing here was run against the game.

Labs (exact `tools/build-msvc.cmd` flags):

- `build\shadow_share_state_check.exe` -> PASS: "shadow-share per-frame machine (off, atlas-only, setup pair with
  five writers skipped and five readers redirected, canonical redirect, setup fallback, single view, third view,
  writer ordering, cached job, resize, reason clear, status)".
- `build\shadow_share_consistency_check.exe` -> PASS: "recorded per-view setup resource is identical for every
  second-view reader in setup frames, no mixed redirect, no writer between the views; atlas-only frames keep
  per-eye setup; resize and third-view partial frames are counted".

### What changed

- `src/shadow_share_model.hpp`: two modes. `setup` (default) suppresses the second view's five shadow writers and
  redirects every shadow-reading pass to the first view's captured `shadowMapsInfos` value; `atlas` is the v1005
  behaviour. A missing first-view capture selects `atlas` for the frame and records `setup_unavailable`.
- `src/shadow_share.hpp` / `src/render_pass_map.hpp`: the existing one hook on `0x2c5d50` now reads the job item's
  `+0x58` table for `0x3719f109`, and for a redirected job writes that entry's 8-byte value to the first view's
  resource for the duration of `original_(item)`, restoring it in `__finally`. No new hook address.
- `src/dxgi_probe.cpp`: `build/shadow-share-atlas-only` forces atlas mode; `build/no-shadow-share` stays all-off.
  Status adds `mode`, `atlas_only`, `setup_redirects`, `setup_fallbacks`.

### What to look at in the headset

0. No `build/no-shadow-share` and no `build/shadow-share-atlas-only`; start the reviewed candidate. Read
   `shadow_share` from `xr-live-status.json`: expect `mode:"setup"`, `enabled:true`, `setup_redirects` rising by
   about 3-5 per `frames_shared` (FORWARD_WORLD, FORWARD_DYNAMIC and FOG always, plus SCREENSHADOWS/RENDER_WATER
   when they run and their `+0x58` entry exists), `second_view_skipped_jobs` rising by 5 per `frames_shared`,
   `setup_fallbacks` and `fallbacks` small and not growing every frame, and `reason` not
   `prologue_mismatch`/`order`/`view_count` in normal play.
1. Right eye near: a hard shadow edge at 1-2 m (wall, crate, held object) in sun and under a lamp. It must sit in
   the same place as the left eye's shadow, not just be present.
2. Right eye far: a building/fence edge at 10-30 m and the cascade boundary; look for a doubled edge or a shadow
   that points the wrong way. This is the v1005 failure spot.
3. The specific spots that looked wrong in v1005: revisit them and compare with the left eye; also check a mixed
   sun+lamp spot, a moving caster (patrolling guard/dropped body) for several seconds, and a slow turn for
   flicker/seams.
4. A/B on the same spot: create `build/shadow-share-atlas-only`, restart, and confirm `mode:"atlas"`,
   `setup_redirects:0` and the v1005 image; remove the file and restart for setup mode. This is the only
   image-quality comparison that separates the redirect from other changes.
5. If the right eye has no shadows: read whether `mode` is `atlas` and `setup_fallbacks` is high (capture missing;
   report the counters) or `setup` with redirects (then the redirected resource is not the one the pass samples;
   report the pass and the status). If shadows are correct in some spots and wrong in others, report which pass was
   redirected and whether `setup_fallbacks` moved that frame.

### Open risks (code-only; no image claimed)

- **Cross-view resource lifetime.** The redirect hands the second view's job a resource object captured from the
  first view's job. If the first view's render graph released or recycled that transient before the second view's
  command list records, the right eye can sample a dangling/reused buffer. This is the main reason the feature has
  a per-frame fallback and the atlas-only A/B knob.
- **Resource-object side effects.** The binding path masks a format tag on the resource object (`0x2c2b90`:
  `and dword ptr [rcx],0xffe00000`) and resolves the handle through the descriptor cache (`0x2d38c0`). Both are
  idempotent/cache operations the engine performs for every binding, but the second view now performs them on the
  first view's object while the first view's jobs may still be recording. A race in that cache would show as a wrong
  shadow binding, not a crash.
- **Light shadow indices.** TLFLIGHTCULL still runs for the second view and builds its own `cpu_shadowMapsInfos`
  and per-tile light lists from the second view's light nodes. We redirect the buffer the forward shader binds, not
  the light data that indexes it. If a light's shadow index differs between the eyes, the redirected buffer can be
  indexed with the wrong entry.
- **Passes outside the static read set.** The redirect list is the five passes with a `shadowMapsInfos`/`0x32fd40`
  hit. A pass that samples shadow data through a path not in that scan (a new cvar, a mod-added pass) would keep the
  second view's stale setup.
- **`default` is still shared.** We suppress the second view's PREPARE so the shared object stays the first view's,
  but any non-shadow consumer of those matrices/fars in the second view now reads the first view's values. That is
  intended for shadow sampling; it is not a whole-frame ownership proof for every consumer.
- **Capture timing.** The mode is chosen at the second view's first suppressed job, not at the eye boundary. If the
  first view's reader jobs lag behind that point, the frame falls back to atlas mode (`setup_fallbacks`); if they
  are interleaved with the second view's, the captured resource may be from the wrong pass (the canonical value is
  used only when the exact pass was not seen).
- **Cached/partial jobs.** A cached job or a third view still falls back for the rest of the frame; jobs already
  redirected before the fallback cannot be undone.
- **Partial slice updates.** With `r_shadowParallelPartial*`, the first view may update only some parallel slices;
  the second view reuses whatever is resident. The second view no longer advances a slice (v1004 saving), so a
  temporal slice lag remains possible.
- **Status truth.** `setup_redirects` counts writes the hook actually applied (a redirect to the same pointer, or a
  job whose `+0x58` entry is absent, is not counted). A count of 0 with `mode:"setup"` therefore means the table
  entry was missing or already equal, not that the model refused. Compare with the atlas-only run.



## v1007 = v1006 (shadow setup sharing) + weapon worker W1 (Opus merge, 2026-11-04 19:50)

Weapon fix (docs/WEAPON-DRAW-PLACE-TEMPORAL.md): the black flash under temporal AA came from the weapon's DEPTHPASS draw. That
draw was never placed: there is no same-frame eye certificate and no previous rows, so `gather` rejected it. FORWARD was placed,
so depth landed at the tick frame and colour at the hand. Now every eye view pass places the weapon with the pass's eye, taken
from RenderPassMap's item+0x70 view, with correct previous rows, and the v970 temporal skip is removed. The dual-wield copy
publishes a per-hand CopyDraw and is placed from its own hand. 3-way merge with phase 8. Conflict in render_pass_map.hpp resolved:
the view is read once at the top of the hook, and the shadow redirect branch now also sets/restores `current_view_`. Otherwise
redirected second-eye FORWARD jobs would give the weapon placement a stale view. weapon_draw_place_check was missing from
tools/build-msvc.cmd (a stale 01:39 exe had "passed"); it is now built and run there.

## v1007 headset (user) and v1008 (live writer mask)

User: right eye misses the shadows of a chair and a table that the left eye shows (earlier: a clock). Status: frames_shared 15059,
setup_redirects 0, setup_fallbacks 0. decide_mode never fell back, so setup mode ran. The redirect never wrote because the
second view's shadowMapsInfos value already equals the first view's: the resource handle is shared. The missing furniture shadows
therefore come from one of the five skipped second-view writers (a per-view part of caster collection or dynamic drawing).
v1008: `build/shadow-share-skip` (letters p c g w d; absent = all five, live 1 Hz; status shadow_share.skip_mask) sets which
writers the second view skips, so a same-spot A/B at the chair can show which pass the right eye needs.

## Why the right eye lost dynamic shadows (phase 9)

Static only: PE + capstone over `Dishonored2.exe` on disk (Steam build, sha256 `b671f824...6180`), plus the v1007/v1008
headset and v1008 mask artifacts already in this document. The game was **not running** for this phase, so no live read was
possible and nothing below is an in-game observation except where it cites the earlier runs. Every address is an RVA.

### What the v1008 mask A/B had already narrowed to

The live mask at the chair: right-eye chair/table shadows appeared only with mask 0 (nothing skipped). Mask `pwd`
(second view runs COLLECT 12 + GATHER 13, skips PREPARE 11 / WORLD 20 / DYNAMIC 21) still lost them. Mask `pcgw` (second
view runs only DYNAMIC 21) still lost them; its eye1 `SHADOWS_DYNAMIC` GPU time was 0.057 ms against eye0's 0.344 ms, i.e.
it drew almost nothing because its caster list was empty. The artifact snapshots (`headset-v1008-mask`) show the same split:
mask 31 eye1 `SHADOWS_WORLD`/`SHADOWS_DYNAMIC` = 0; mask 25 eye1 `TLFLIGHTCULL` is *slower* than eye0 (0.154 vs 0.064 ms)
and eye1 `SHADOWS_DYNAMIC` is still 0; mask 15 eye1 `SHADOWS_DYNAMIC` = 0.057 ms. Passing only COLLECT+GATHER, or only
DYNAMIC, does not restore the missing shadow data; only PREPARE does.

That rules out the atlas *contents* as the cause: eye0 always runs WORLD+DYNAMIC and its atlas is the one eye1 samples
(0x3309b18 / `_shadowMapAtlas`, one image per render system created by 0x371210). The missing state is what the second view
*publishes to its own forward*, not what is in the shared depth image.

### The per-light link PREPARE owns and GATHER_LIGHTS clears

Three instructions carry the whole failure:

```
; 1. SHADOWS_PREPARE, atlas-slice path 0x331cc0: after building the arkShadowMapInfo_t for a light,
;    store that entry in the light node.
003328a9  488b03                 mov     rax,qword ptr [rbx]          ; rax = light node stored in the entry
003328ac  488998a8010000         mov     qword ptr [rax+0x1a8],rbx    ; light+0x1a8 = the 0x660-byte entry

; 2. GATHER_LIGHTS 0x31bcc0: every view clears the link before its own PREPARE can run.
0031bd39  bf00000000             mov     edi,0
...
0031c067  488b1cc8               mov     rbx,qword ptr [rax+rcx*8]    ; rbx = light node from the view's light list
0031c074  4889bba8010000         mov     qword ptr [rbx+0x1a8],rdi    ; light+0x1a8 = 0
0031c088  837b5c04               cmp     dword ptr [rbx+0x5c],4        ; [rbx+0x5c] is the light type: rbx is the light node

; 3. TLFLIGHTCULL 0x2f7260: copy the entry's per-slice records into cpu_shadowMapsInfos.
002f75ca  4d8baea8010000         mov     r13,qword ptr [r14+0x1a8]    ; r13 = the entry, or null
002f7641  4d85ed                 test    r13,r13
002f7644  7466                   je      0x1402f76ac                  ; null: skip the shadow-record copy
002f76e5  40387d50               cmp     byte ptr [rbp+0x50],dil      ; view+0x3a34 bit 1 gate
...
002f7701  488b5c2468             mov     rbx,qword ptr [rsp+0x68]    ; cpu_shadowMapsInfos object
002f7710  4d8b4510               mov     r8,qword ptr [r13+0x10]      ; entry's record array
002f771e  488d14c0               lea     rdx,[rax+rax*8]              ; entry stride 0x90 bytes
...
002f772a  8904d1                 mov     dword ptr [rcx+rdx*8],eax    ; first record word
...
002f78ae  0f855cfeffff           jne     0x1402f7710                  ; next light record
```

`r14` in the TLFLIGHTCULL loop is the light pointer from the job's light list. That list is the job item's `+0x40` field:
`[item+0x40]` is a list object, `[list]` its data (8-byte light pointers) and `[list+8] & 0xffffff` its count
(0x2f758d..0x2f75a6). `cpu_shadowMapsInfos` is `[job parameter value]`, and `[[value]+8]` is the mapped CPU record array
(0x2f7381..0x2f738b, 0x2f7717).

The chain is per view, not per frame:

| step | pass | what it does | scope |
|---|---|---|---|
| GATHER_LIGHTS | 6 | `[light+0x1a8] = 0` for every light in the view's list (0x31c074) | runs for both views |
| SHADOWS_PREPARE | 11 | builds the `arkShadowMapInfo_t` in the shared `default` pool and links it at `light+0x1a8` (0x3328ac) | per view (writes the link) |
| TLFLIGHTCULL | 26 | reads `light+0x1a8` and copies the entry's records into the view's `cpu_shadowMapsInfos` | per view |
| FORWARD_WORLD / FORWARD_DYNAMIC / FOG / SCREENSHADOWS / RENDER_WATER | 27/28/29/25/32 | bind and sample that view's `shadowMapsInfos` (0x311e54, 0x31e0c0, 0x309602, 0x32fd40) | per view |

With the second view's PREPARE suppressed, the order inside a frame is:

1. eye0 GATHER_LIGHTS clears the links; eye0 PREPARE sets them to eye0's entries; eye0 TLFLIGHTCULL publishes eye0's
   records; eye0 forward samples them. Correct in the left eye.
2. eye1 GATHER_LIGHTS clears the links again; eye1 PREPARE is suppressed, so nothing re-links the lights; eye1
   TLFLIGHTCULL sees `r13 == 0` for every light, skips the record copy, and writes only the per-tile light entries
   (`lightToPutInTiles`, 0x70-byte entries at 0x2f75f7). The second view's `cpu_shadowMapsInfos` therefore carries no
   per-light atlas records, and eye1 forward/fog/screenshadows sample an empty/degenerate `shadowMapsInfos`.

This is exactly why the mask results are what they are: COLLECT and GATHER do not touch `light+0x1a8`; DYNAMIC does not
write it; only PREPARE (11) does. Mask `pwd` clears the link in GATHER_LIGHTS and never restores it; mask `pcgw` does the
same and then draws almost nothing; mask 0 lets PREPARE restore it.

### Ownership of every output of the five writers

| writer (pass) | memory / resource written | scope | consumers in the second view's remaining passes |
|---|---|---|---|
| SHADOWS_PREPARE (11) | 0x600 `arkShadowMapInfo_t` entries (0x660 each) in `default`'s pool; pointer lists at `default+0x264010/+0x267020/+0x26a030`; cascade fars `default+0xb00..0xb0c`; matrices at `default+0x100/+0x200/+0x300/+0xb10/+0xb20`; atlas tile allocations through the global 0x3308d88; **`light+0x1a8` = entry** (0x3328ac) | entries/lists/matrices = per frame, shared `default`; the light link = per light node, cleared per view by GATHER_LIGHTS; tiles = per frame, shared atlas | WORLD 20 / DYNAMIC 21 draw from the lists (0x32d7fa..0x32d90e, 0x32f10e); TLFLIGHTCULL 26 copies `light+0x1a8` into `cpu_shadowMapsInfos` (0x2f75ca..); FORWARD 27/28, FOG 29, SCREENSHADOWS 25 and RENDER_WATER 32 bind the resulting `shadowMapsInfos`/atlas via 0x311e54, 0x31e0c0, 0x309602, 0x32ca47, 0x304244, 0x32fd40 |
| COLLECT_DYNAMIC_CASTERS (12) | clears and appends three caster lists on the `surfaces` object: `surfaces+0`, `surfaces+0xb4010`, `surfaces+0x168020` (clear 0x3300f8/0x33010d/0x330114, append 0x33053e); reads the per-model byte array at `[0x37f17877 object +0x3c010]` (0x330290) and the view frustum/cull distance | per frame shared scratch, rebuilt by each view | WORLD 20 / DYNAMIC 21 only: atlas dynamic list 0x32d2a4, parallel dynamic list 0x32f10e, draw helpers 0x3285a0/0x329120. No forward reader was found; the second view's forward does not read the caster lists. |
| GATHER_CASTERS (13) | clears and refills the global gathered-caster position list 0x32f9b70/0x32f9b78 (0x330f41..0x330faf); appends the per-view caster records to `surfaces`; clears per-model bytes in the per-view object at item+0x68 (0x331150..0x331190) | global scratch + per-view caster object | WORLD 20 / DYNAMIC 21 only (same draw helpers). No forward reader. |
| SHADOWS_WORLD (20) | clears the shared atlas (0x32d76e) and each parallel slice (0x32f055); draws the world/caster geometry; fills the per-slice job objects from 0x331250; sets wrapper `+0x12ad8` flags | shared GPU images (one `_shadowMapAtlas` 0x371210 and one `_parallelSplitShadowMap` per render system), per frame | FORWARD/FOG/SCREENSHADOWS/WATER sample the images through 0x32fd40; the second view must not clear or redraw them |
| SHADOWS_DYNAMIC (21) | same atlas/slice draws for the dynamic caster lists, no atlas clear; 0x3285a0/0x329120; wrapper flags | shared GPU images, per frame | same; in the v1008 "pcgw" mask its eye1 GPU time was 0.057 ms against eye0's 0.344 ms because it had no caster list |

The only per-view *CPU* output that the remaining passes read through this set is the light link consumed by TLFLIGHTCULL.
The `shadowMapsInfos` job-table value is per view but is the same handle in both views (v1007 `setup_redirects: 0`), the
atlas images are shared, and the caster lists are consumed only by the two atlas/slice writers that the second view skips.
That is why the redirect of `shadowMapsInfos` alone was not enough and why the missing data showed up in the forward, not
in the atlas contents.

### Answer to "why dynamic objects"

The movable props were the visible atlas-shadowed objects at the tested spot, not a separate per-model shadow path.
`light+0x1a8` is written in the **atlas-slice** branch of PREPARE (0x331cc0; the dispatcher sends slice indices at or
above the parallel slice count there, 0x32d4b0 `cmp r8d,eax` / `call 0x32d6e0`). The records TLFLIGHTCULL publishes from
`light+0x1a8` are the spot/omni (atlas) per-light matrices/rects/tile data; the sun cascades are a separate
`_parallelSplitShadowMap` texture array plus the five-element static image arrays and slice constants bound by 0x32fd40.
A lamp-lit chair/table/clock loses its shadow in the right eye because that view's TLFLIGHTCULL published no per-light
atlas record for the lamp; the static world's sun shadows are unaffected. The same loss would appear on any static
surface whose only shadow was an atlas shadow at that spot; the headset report named the props that were visible.

### The fix (v1009)

`src/shadow_share_model.hpp` gains a bounded per-frame capture table and a `LightAction {none, capture, restore}`:

- The first view's TLFLIGHTCULL job is a `capture`: the hook walks `[item+0x40]` (`[list]`, `[list+8] & 0xffffff`) and
  records `(light, [light+0x1a8])` for every non-null link.
- The second view's TLFLIGHTCULL job is a `restore` only in setup mode (and never in a hard fallback or atlas-only frame):
  for every light in the same list, the captured value is written to `light+0x1a8` before `original_(item)` runs, and the
  previous value (normally the null GATHER_LIGHTS left) is written back in `__finally`.
- In the shadow/light pass range (0x2f0000..0x340000) the only read of the field as a pointer is 0x2f75ca; the write is
  put back before the executor returns, so anything outside that one job sees the original value. The write is SEH-guarded,
  bounded to 1024 lights, and counted (`light_captures`, `light_restores`, `light_misses` in the status JSON).
- The five writers' skip mask is unchanged: the second view still skips PREPARE/WORLD/DYNAMIC (and, by default,
  COLLECT/GATHER), so the atlas is never cleared or redrawn by the second view.
- The captured entries are valid as is: they live in the shared `default` pool built by the first view's PREPARE, no
  second-view pass rewrites them (its PREPARE is suppressed), and a spot/omni shadow matrix and atlas rect are light-space
  data, not eye-space. This is the same reuse the atlas itself already relies on.

`src/render_pass_map.hpp` performs the capture/restore on the existing single executor hook (0x2c5d50); no new hook address
is used. `src/shadow_share.hpp` exposes the actions and status counters. The labs model the real flow: GATHER_LIGHTS clears,
PREPARE links, TLFLIGHTCULL publishes, the second view restores, and the published record blobs are compared.
In the next headset run, `shadow_share` should show `light_captures` and `light_restores` rising by roughly the number of
shadowed lights per frame and `light_misses` staying near 0; a `light_captures` of 0 with a non-zero frame count means the
first view's TLFLIGHTCULL ran before PREPARE set the links (the fix then does nothing and must not be judged by the image).

### Tests and build (v1009)

Build: `build/compile-v1009.cmd` (copy of v1008 with `v1008 -> v1009`) -> `build\d2vr_render_probe_arm_v1009.dll`,
`dll_exit=0`, 14,801,408 bytes, sha256 `3962858e4fe8a7e0222640a65d4dfa4485c08c1e31a8f7d2c19a4501902da97e`. The log's 30
warnings are the same pre-existing set (`dxgi_probe.cpp` line-number shifts, `game_xr.hpp`, `native_weapon_hands.hpp`,
`native_hand_contact.hpp`, `hold_coupling.hpp`, and the FFX LNK4099 PDBs); the changed shadow files add none.

Labs (exact `tools/build-msvc.cmd` flags, `/W4 /WX /MT /Isrc`):

- `build\shadow_share_state_check.exe` -> PASS: "... v1009 TLFLIGHTCULL per-light link capture/restore, atlas-mode and
  fallback exclusions".
- `build\shadow_share_consistency_check.exe` -> PASS: "... the second view restores the first view's per-light
  shadow-map-info links so its TLFLIGHTCULL publishes identical local-shadow records; atlas-only frames keep per-eye setup
  and per-eye links ...".
- The consistency lab also asserts the counterfactual: with the links left cleared, the second view's published blob
  differs from the first view's; with the model's restore it is identical. Atlas-only frames publish per-eye blobs and use
  no restore, matching the v1005 baseline.

### Open risks (code only; no in-game behaviour claimed)

- **Capture coverage.** Only lights in the first view's TLFLIGHTCULL list are captured. A light present only in the second
  view's list is left null (`light_misses` counts it); if such a light also had a first-view atlas entry, its shadow stays
  missing in the right eye. A 6.5 cm eye offset should not add lights, but this is not proven for every cvar/cutscene state.
- **Entry lifetime.** The captured value points into the shared `default` pool. No pass between the two TLFLIGHTCULL jobs
  was found to rewrite that pool (the second view's PREPARE is the only per-view writer and is skipped), but a future cvar
  path that rebuilds it mid-frame would invalidate the pointer. The per-frame recapture makes the next frame self-correcting.
- **Concurrent TLFLIGHTCULL jobs.** If the engine records more than one TLFLIGHTCULL job for a view on different threads,
  their restore windows overlap. They all write the same captured values, so the visible state is consistent, but the
  static scan does not prove the scheduler never runs a TLFLIGHTCULL job for the *first* view after a second-view restore.
  The serial per-view order assumed by v1000 and the 0-fallback v1007 run are the evidence for the order used here.
- **Producer-to-bind gap.** The static chain proves TLFLIGHTCULL writes the per-light records from `light+0x1a8` into
  `cpu_shadowMapsInfos`; the exact copy into the GPU `shadowMapsInfos` resource was not traced to a single instruction.
  The fix restores the producer's input, which is the correct point for both a direct mapped write and a later upload, but
  only a headset A/B can confirm the image.
- **Atlas-only fallback.** In atlas mode the second view's PREPARE runs and writes its own links, so no restore is used and
  the v1005 behaviour (per-eye cascade fit against the shared atlas) is unchanged.
- **No performance claim.** The added work is one light-list walk and one 8-byte write per light on the TLFLIGHTCULL jobs
  (~100 writes per frame in the measured spot) plus one mutex per light through the existing runtime. The atlas draws and
  their clears remain skipped for the second view. Nothing here was measured in the game.

## v1010 = phase 9 (light-link carry) + weapon rounds 2/3 (Opus merge, 2026-11-04 22:10)

Phase 9 cause, verified by Opus in the binary: GATHER_LIGHTS 0x31c074 clears light+0x1a8 for every view, only SHADOWS_PREPARE
0x3328ac sets it, and TLFLIGHTCULL 0x2f75ca reads it (null skips the light's shadow records at 0x2f7641). With the second view's
PREPARE skipped, its local-light (atlas) shadows had no records. That is why the chair, table and clock lost their shadows in the
right eye while sun shadows stayed. v1009 captures the first view's links at its TLFLIGHTCULL and writes them around the second
view's TLFLIGHTCULL (restored after).
Weapons round 2: WriteLog pairs for every mod writer (cross, mirror copy, held copy) and physical-hand side mapping. Round 3: the
weapon's colour VS (arkDefault forward permutation) has cb_mvpmatrix only. The dinput8 proxy (linux/proxy/vs_layouts.hpp) now
records current-only layouts, so the colour pass is placed with the depth pass's transform. Motion vectors come from the placed
Z-prepass (rw_pspreviousposition). The launcher must ship the rebuilt build/dinput8.dll; ProxyInstall replaces ours when the
bytes differ.
Labs from the merged source: shadow_share_state/consistency, weapon_draw_place, object_motion, gpu_pass_attribution,
native_attachment_pose PASS; body_ik_check: the new grip check PASS, "reverse grip keeps the hilt" FAIL (pre-existing since v612).

## Phase 10: why the right eye still had no local-light shadows (v1011)

Static only: PE + capstone over `Dishonored2.exe` on disk (Steam build, sha256 `b671f824...6180`). The game was **not**
running for this phase, so every statement below is either a static listing or a re-reading of the v1010 headset counters
already in this document. No new in-game behaviour is claimed. All addresses are RVAs. The headset input is: v1010
`frames_shared 26215`, `light_captures 600333` (~23/frame), `light_restores 500161` (~19/frame), `light_misses 623048`
(~24/frame), and the user's report "still no shadows from the light sources in the right eye".

### The counters, read correctly

`light_captures` counts lights with a **non-null** first-view link. `light_restores` counts links the second view actually
wrote. `light_misses` counts every light in the second view's TLFLIGHTCULL list for which the first view had no captured
link - including every light that has no atlas shadow entry at all. It is not an error counter and its ~24/frame does not
mean half the lights failed. The 23 vs 19 difference is explained by per-view culling: four shadowed lights in the first
view's list are not in the second view's list in that frame. That is the v1000 "2.3% caster loss" class, not a total-loss
mechanism. The counters prove the link carry ran; they say nothing about whether TLFLIGHTCULL could **use** the links.

### Q1 - cpu_shadowMapsInfos to the GPU shadowMapsInfos resource

The job builder installs both sides from the same global object:

```
00354fd2  lea rdx,[rip+...]              ; "cpu_shadowMapsInfos"
00354fe8  lea rdx,[rip+...]              ; value = &0x3308cf0
00354ff2  call 0x14034d220               ; add the object parameter to every job item

0035507f  lea rcx,[rip+...]              ; "shadowMapsInfos"
003550ad  mov rcx,[rip+...]              ; rcx = [0x3308cf0]
003550a6  movsxd rdx,[0x3308d08]         ; slot index
003550b7  mov rdx,[rcx+rdx*8+0x18]       ; the GPU buffer wrapper for this slot
003550bf  call 0x1402c2470               ; add the resource parameter to every job item
```

So `cpu_shadowMapsInfos` is the global bookkeeping object `0x3308cf0`; `[[0x3308cf0]]` is the current per-view state
object `S`, `[S+8]` is the CPU record array, and `[S + slot*8 + 0x18]` is the GPU buffer. The forward and FOG passes
look up exactly that `shadowMapsInfos` handle in `item+0x58` (0x311e54..0x311e91, 0x31e0c0, 0x309602). The upload is
**not** in SHADOWS_PREPARE or SHADOWS_WORLD: TLFLIGHTCULL does it itself, after copying the records:

```
002f7ffc  4585ed                 test    r13d,r13d              ; r13d = records published by this job
002f7fff  0f84c5000000           je      0x1402f80ca            ; zero -> no upload at all
002f80a7  4c8b742460             mov     r14,qword ptr [rsp+0x60] ; job context wrapper
002f80be  488b4cc818             mov     rcx,qword ptr [rax+rcx*8+0x18] ; [S + slot*8 + 0x18]
002f80ba  4c8b4008               mov     r8,qword ptr [rax+8]  ; [S+8], CPU records
002f80c3  e828befdff             call    0x1402d3ef0            ; upload
```

`0x2d3ef0` is a buffer-update helper: when `[rcx+0x18] == 3` it Maps the buffer through context vtable `+0x70`, copies
the wrapper's `[rcx+0xc]` byte count (`0x11ad810`), and Unmaps (`+0x78`); otherwise it calls `UpdateSubresource`
(vtable `+0x180`) with `r9d` as the size. The same tail also uploads the per-tile arrays: `0x2f810c` (0x30-byte
`cpu_lightToPutInTiles`) and `0x2f8148` (0x70-byte `cpu_lightToPutInTiles_cb`). Therefore the normal per-view order is:
PREPARE writes the entries; TLFLIGHTCULL copies the entries into the CPU array and uploads them; FORWARD/FOG bind the
same handle and sample them.

The upload is gated on **the number of records TLFLIGHTCULL published**. If that number is zero, the GPU buffer is left
as it was (first view's records, in the first view's order) while the second view's per-tile lists index their own order.
That is the phase-9 open risk "producer-to-bind gap" realized: the producer is TLFLIGHTCULL, and it published nothing.

### Q2 - how a per-tile entry references a shadow record

TLFLIGHTCULL walks the view light list at `item+0x40` (`[list]` = light pointers, `[list+8] & 0xffffff` = count,
0x2f758d..0x2f75a6). For each light it writes a 0x70-byte tile entry into `cpu_lightToPutInTiles_cb` (hash 0x98042008)
at `[S_cb + 8] + index*0x70`:

```
002f76ef  44897660               mov     dword ptr [rsi+0x60],r14d ; running base record index
002f7714  8b4660                 mov     eax,dword ptr [rsi+0x60]
002f771e  488d14c0               lea     rdx,[rax+rax*8]           ; index*0x90
002f772a  8904d1                 mov     dword ptr [rcx+rdx*8],eax ; copy record 0
...
002f78cf  41898520060000         mov     dword ptr [r13+0x620],eax ; cache the same base in the entry
```

The index is **view-local**: it is the running count of records published so far in this TLFLIGHTCULL job, starting at
0 (0x2f7539 clears the counters). The source records are `entry+0x10` + i*0x100 (six 0x100-byte faces); the destination
stride is 0x90 and 0x84 bytes are copied per record. The entry's `+0x620` caches the base index. The forward binds the
0x70 array as `tlf/lightToPutInTiles_cb` (0x311c6f, descriptor 0x22ad6e0) and the head texture
`perTileLightListHeads` (0x311ce0), so the tile entry's `+0x60` indexes the same view's uploaded record array. The pair
is consistent **only if that view's records were actually uploaded**. There is no cross-view record-index mapping.

### Q3 - the ~24 misses and the job structure

The pass registration asks for one TLFLIGHTCULL job per view (`r8d = 1`, item count), the callback has no
`item+0xf8`/`item+0xfc` slice branch, and the per-tile output index `r15d` starts at 0 for the job (0x2f7539). The probe
still allows up to 8 jobs per view so a cloned/sliced job would not be hidden. The
`~24 misses` are therefore lights in the second view's whole light list that have no first-view link: non-shadow lights
plus four shadowed lights culled from the first view's list. They do not matter by themselves; a light without an atlas
entry has no local-light shadow to lose. The total loss is not explained by four missing lights: it is explained by the
**published record count being zero for every light**, which is the Q4 reset.

### Q4 - the other per-view writer: the job builder zeroes the shared entry counts

The per-view job builder `0x351780` is called immediately after the eye boundary for **each** view:

```
003668b2  lea rcx,[r14+0xd7f0]       ; pass manager
003668c7  mov rdx,[r13+0x20]         ; view
003668d1  call 0x140351210           ; eye begin
003668e5  call 0x140351780           ; build THIS view's jobs, before its passes
```

Inside `0x351780`, after `default = [manager+0x1c8ac0]`, the active entries of the shared
`arkShadowMapInfo_t` pool are reset (`default+8` is the active count; the first view's PREPARE appended one entry per
atlas light, ~23 in the v1010 scene):

```
00351dec  418b8424c88a1c00         mov     eax,[r12+0x1c8ac8]   ; default->entry_count
00351df4  25ffffff00               and     eax,0xffffff
00351df9  4c63f8                   movsxd  r15,eax
00351e10  4d8bb424c08a1c00         mov     r14,[r12+0x1c8ac0]  ; default
00351e18  4a8d1c36                 lea     rbx,[rsi+r14]        ; entry = default + i*0x660
00351e3b  816318000000ff           and     dword ptr [rbx+0x18],0xff000000
00351e42  85c0                     test    eax,eax
00351e44  410f4fc5                 cmovg   eax,r13d             ; r13d = 0
00351e48  25ffffff00               and     eax,0xffffff
00351e4d  094318                   or      dword ptr [rbx+0x18],eax ; entry+0x18 low24 = 0
00351e50  42c7843620060000ffffffff mov     dword ptr [rsi+r14+0x620],0xffffffff
00351e5c  4881c660060000           add     rsi,0x660
00351e63  49ffcf                   dec     r15
00351e66  75a8                     jne     0x140351e10
```

`entry+0x18` is the record count. PREPARE sets it after building the six faces (0x332f7d..0x332f94:
`entry+0x18 = min(capacity,6)`), and TLFLIGHTCULL reads it at 0x2f7646:

```
002f7641  4d85ed                 test    r13,r13               ; link non-null?
002f7644  7466                   je      0x1402f76ac
002f7646  418b5d18               mov     ebx,dword ptr [r13+0x18]
002f764a  81e3ffffff00           and     ebx,0xffffff          ; count = entry+0x18 & 0xffffff
...
002f76ca  3bd8                   cmp     ebx,eax
002f76cc  0f8cee010000           jl      0x1402f78c0            ; no records -> no publish
```

The v1009 fix captured and restored `light+0x1a8` only. In the second view the per-view builder has already zeroed the
counts of the very entries the first view's TLFLIGHTCULL captured. So the restored link points at an entry whose count is
zero; TLFLIGHTCULL skips the copy for every light, `r13d` stays 0, and the upload at 0x2f7ffc is skipped. The second
view's forward binds the record buffer with no records for its own order (or the first view's stale order), hence no
local-light shadows while the atlas (drawn by the first view) and the sun cascades remain correct. Eye 0 is unaffected:
its builder reset runs before its PREPARE, and its forward commands execute before the second view's builder.

### The fix (v1011, option a)

`src/shadow_share_model.hpp` / `src/render_pass_map.hpp` now carry the per-light **record count** with the link:

- First view's TLFLIGHTCULL: `collect_light_links` reads `entry+0x18 & 0xffffff` and `capture_light` stores
  `(light, entry, records)`.
- Second view's TLFLIGHTCULL (setup mode only, after its PREPARE was suppressed): for each captured light write
  `light+0x1a8 = entry` **and** `entry+0x18 = (old & 0xff000000) | records`. Save the previous link and previous count;
  restore both in `__finally`.
- The count write preserves the builder's top byte, exactly as `0x351e3b/0x351e4d` does. The old value is normally 0
  (the second view's builder), so the second view's TLFLIGHTCULL sees the first view's six records and publishes them.
- `light_restores` still counts link writes; the new `light_record_restores` counts count writes. A job with only a count
  change still runs through the same restore window. No new hook address is used (still the one on `0x2c5d50`), and no
  atlas image, shader, or job table is written.
- Atlas-only mode and hard fallbacks do not restore: the second view's own PREPARE writes its own links and counts, which
  is the v1005 baseline.

### Opt-in probe (`build/shadow-share-probe`, OFF by default)

`src/dxgi_probe.cpp` reads `build/shadow-share-probe` at session start and in the existing 1 Hz knob poll. When on,
`artifacts/shadow-share-probe.jsonl` is truncated and each second's first frame logs one line per TLFLIGHTCULL job
(up to 8 jobs per view, so slices are not hidden), with:

- `frame` (the model's per-frame serial), `view` 0/1, `pass` 26, `lightList`/`lightListData` for item+0x40;
- `shadowMapsInfos` handle for that job (item+0x58, hash 0x3719f109);
- `lights`: up to 64 `{light, info, records, published}` triples, where `info` is `light+0x1a8`, `records` is
  `entry+0x18 & 0xffffff` before the callback, and `published` is `entry+0x620` after it (-1 = not published);
- `records_published`: max(`published + records`) over the entries, i.e. the number of records TLFLIGHTCULL uploaded;
- `records`: the first 32 dwords of the CPU array (four 0x90-byte records, first 0x20 bytes of each).

The probe is read-only, bounded (64 lights, 32 dwords, 8 MiB file cap), and costs one atomic load per TLFLIGHTCULL job
when off. It does not run when the runtime is disabled. The next headset run can confirm the cause directly: for view 1
the shadowed lights must show `info != 0` and `records > 0` (6 for the six-face atlas path) instead of 0,
`records_published > 0`, and the same `shadowMapsInfos` handle as view 0. If `records` is still 0 with a restored link, the restore did not run (check `mode`,
`second_prepare_suppressed`, `light_record_restores`); if `records_published` is 0 with non-zero counts, another gate
stopped the copy (`view+0x3a34` bit 1, `light[0]` bit 1, or the 0x100-record cap at 0x2f76da).

### Tests and build (v1011)

Build: `build/compile-v1011.cmd` (copy of v1010 with `v1010 -> v1011`) -> `build\d2vr_render_probe_arm_v1011.dll`,
`dll_exit=0`, 14,843,392 bytes, sha256
`e913128edcfce9567fffcc8271430923820a7c6128fe522a97ac0726a4862645`. The log has the same 30 warnings as v1009
(`game_xr.hpp`, `native_hand_contact.hpp`, `hold_coupling.hpp`, `native_weapon_hands.hpp`, old `dxgi_probe.cpp` sites and
the FFX LNK4099 PDBs); a line-number-normalized comparison of the warning sets is empty. The changed shadow files add no
warnings.

Labs (exact `tools/build-msvc.cmd` flags, `/W4 /WX /MT /Isrc`):

- `build\shadow_share_state_check.exe` -> PASS: "... v1009 TLFLIGHTCULL per-light link capture/restore and v1011
  entry+0x18 record-count carry, probe off by default, atlas-mode and fallback exclusions".
- `build\shadow_share_consistency_check.exe` -> PASS: "... the second view restores the first view's per-light
  shadow-map-info links AND entry+0x18 record counts (v1011) so its TLFLIGHTCULL publishes identical local-shadow records
  after the per-view job builder reset; the v1009 link-only counterfactual publishes nothing ...".

The consistency lab now models the per-view builder reset (`build_jobs` zeroes every entry count before each view), the
PREPARE count write, and the hook's link+count restore. It asserts the fixed second view's published blob equals the
first view's, and that the v1009 link-only carry publishes a different (empty-record) blob even with every link restored.
The state lab exercises the probe file format (one JSON line with the view, frame, handle, record count and record words)
and the new status keys.

### Open risks (code only; no in-game image claimed)

- **The count is shared state.** The restore window writes `entry+0x18` for the duration of the second view's
  TLFLIGHTCULL job and puts the builder's zero back afterwards. A concurrent first-view job that reads the entry count in
  that window sees the first view's value, not zero; that is the same value the first view wrote. A first-view job that
  reads it after the `__finally` sees zero, but the per-view order already requires the first view's shadow work to be
  finished before the second view's builder (which zeroed it) runs. This is the same serialization assumption as v1009.
- **Per-view light lists differ.** Four shadowed lights per frame are in the first view's list but not the second
  view's. The second view cannot restore a link for a light it does not list; those lights keep the v1000 accepted loss.
- **Other entry fields are not carried.** Only `entry+0x18` is read by TLFLIGHTCULL besides `entry+0x10` (the record
  array, which the builder does not touch) and `entry+0x620` (which TLFLIGHTCULL rewrites). `entry+0x1c` is not read by
  the pass. If a future cvar path changes the entry layout or uses the capacity field, the carry may need widening.
- **Record-array lifetime.** The first view's record arrays live in the shared `default` pool and are freed only when
  PREPARE reuses an entry. The second view's PREPARE is suppressed, so they stay valid; a future mid-frame rebuild would
  invalidate them (the next frame is self-correcting).
- **GPU buffer sharing.** The v1007 run showed the second view's `shadowMapsInfos` handle already equals the first
  view's (`setup_redirects: 0`), so both views upload into the same buffer object at different command-list positions.
  v1005 (the second view's own PREPARE + TLFLIGHTCULL) is the precedent that this per-view upload ordering works; v1011
  restores the same second-view publish path. If a future scheduler reorders the two views' command lists, the count carry
  cannot fix a shared-buffer overwrite.
- **The 0x100-record cap.** TLFLIGHTCULL skips the copy when `base + count > 0x100` (0x2f76da). A dense local-light
  scene can hit that for one view and not the other; the probe's `records_published` shows it.
- **Probe scope.** It logs the first frame of each second, up to 8 TLFLIGHTCULL jobs per view, and only the first 32
  dwords of the record array. It is a diagnostic, not a general trace; it does not prove the image.
- **No in-game result.** The fix is proven against the static producer/consumer and the model lab, not against a headset
  run. The next run must read `light_record_restores` and the probe before judging the right-eye image.

## v1012 = phase 10 (record-count carry) + weapon round 4 (Opus merge, 2026-11-04 23:50)

Phase 10, verified by Opus in the binary: the per-view job builder 0x351780 resets every active shadow entry's record count
(`and [rbx+0x18],0xff000000` at 0x351e3b, then `or` with 0 at 0x351e4d). TLFLIGHTCULL reads the count at 0x2f7646 and uploads
shadowMapsInfos itself, gated at 0x2f7ffc on records published. v1009 restored only light+0x1a8, so the second view's
restored links pointed at entries with count 0: no records, no upload. v1011 carries and restores the count together with the link.
Weapon round 4: per-type grip clips (component RTTI: pistol vs crossbow; baked engine idle frames as fallback before procedural),
writer-note ring 16, and gate frames matched (crossed native: origin hand joint without the y180 correction; copy: controller grip).
Labs from the merged source: all PASS except the pre-existing body_ik_check reverse grip (v612). full_body_driver_check PASS.
