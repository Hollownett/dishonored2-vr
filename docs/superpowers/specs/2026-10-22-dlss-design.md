# H6: paired-eye DLSS design

2026-10-22. Design only; implementation and runtime acceptance remain gated by the [implementation plan](../plans/2026-10-22-dlss.md).
Goal: optional paired-eye DLAA first, then DLSS Quality, preserving accepted locomotion, turning, AA modes 0..6, and existing fallback rendering.
No implementation, builds, GPU probes, simulations, game sessions, or headset claims accompany this document.

## Integration and distribution boundary

Pin NVIDIA DLSS SDK `9981c6350dc58c0ce21f024fd65e31e851c31689` (310.9.16), with a hashed source/license/header/library manifest.
Use typed public D3D11 SDK functions and helper structures (`nvsdk_ngx_helpers.h` delegates D3D helpers to `nvsdk_ngx_helpers_d3d.h`); link x64 `nvsdk_ngx_d.lib` for the mod's `/MD` runtime, as specified in [guide §5.1.1][guide].
The `_d` suffix selects the runtime library; it does not select a debug provider.
Do not cast provider/Core exports to SDK prototypes or build a header-only D3D11 adapter: the pinned [SDK header][sdk] and [loader][loader] distinguish SDK/Core signatures and standalone CUDA support.
The loader discovers NGX Core; explicit feature search paths discover the separate signed `nvngx_dlss.dll` provider.
Initial lane accepts only an already user-supplied signed provider in an explicitly configured directory, using the official SDK/Core validation contract; no provider download, bundling, copying, or launcher installation. Windows/Proton discovery and signature-validation behavior require runtime evidence; do not assume Wine Authenticode support.
Missing/invalid provider, absent Core, non-NVIDIA adapter, unsupported capability, driver mismatch, and feature-create failures produce distinct unavailable reasons without disabling existing rendering.
SDK use/linkage still has terms. SDK/provider publication terms, notices, license compatibility, attribution/trademarks, and applicable release obligations are separate release prerequisites under the [pinned license and RTX supplement][license]; user supply is no legal exemption or approval.
This design authorizes no contact with NVIDIA or anyone else and makes no shipping-license determination.

## Retained execution contract

Extend `FsrTaa`, rather than adding a Present-time temporal dispatch. Stable anchors are `replace`, `finished`, `post_tonemap_end`, `execute`, and `dispatch` in `src/fsr_taa.hpp`.
Native resolution: `replace` reserves a slot and seals the deferred prefix with original `FinishCommandList(TRUE)` before suppressing native TAA; refusal before split leaves native TAA intact.
Upscale: capture MV at TAA, bypass TAA with the existing copy, and split after the post-tonemap bilinear draw; that spatial output remains valid if final replacement fails.
`finished` retains the exact remainder-list object. `execute` runs each retained prefix, immediate dispatch, then the next prefix/remainder in recorded order, including both eyes chained in one context.
Keep the existing `TaaDispatchHook::execute_handler` wrapper, caller restore flags, timing, and world-UI completion ordering. Already-immediate work dispatches at that same seam.
Never drop a prefix/remainder because a temporal job is stale. `frame_split.hpp` is telemetry, not a replacement scheduler.

Each immutable job owns COM references plus the following receipt, captured before split:

| Receipt | Exact identity and generation |
| --- | --- |
| Eye/frame | Logical eye 0/1, stereo pair ID, same-eye rendered-frame sequence, reset epoch and history-valid flag |
| Recording | Deferred context identity, retained prefix and remainder, chain order, original restore flags |
| Resources | Retained color, MV, native output, native depth resource and SRV; resource identity/selection generation for each |
| Depth/MV | Producing eye/frame, producing boundary/sequence and active rectangle; upscale MV receipt must come from this frame's TAA capture |
| Shape | Full descriptors/formats/sample counts, active input/output rectangles and origins, render/output dimensions, dimension generation |
| Backend/device | Requested mode, actual selected backend, backend generation, device identity/generation, feature generation, immutable owner lease |
| Temporal inputs | Actual rendered jitter current/previous in pixels, jitter phase/sequence generation, MV scale/jitter convention, color/exposure contract |

Reject missing, mismatched, or stale receipts before evaluation; a resource address or eye index alone is insufficient.
The current greatest-clear-count depth selection is heuristic; retaining its view establishes lifetime only. Prove that each executed prefix produces that eye's depth and snapshot it before any later prefix overwrites the shared native resource.
Current upscale `motion_` slots lack a frame stamp; add one and require exact eye/frame/dimension correspondence.
Retire old owners only after recorded jobs and their GPU work are safe; device loss invalidates all associated jobs/features. Backend/resize/reset changes reject old temporal evaluation while preserving command execution and safe fallback.

## Device owner and per-eye backend

One device owner serializes NGX init/capability/create/evaluate/release/parameter destruction/shutdown, including teardown against in-flight work.
Create features at an explicit owned immediate-context lifecycle point; current `prepare(device)` alone cannot create a D3D11 NGX feature. Deferred recording never calls NGX.
Use SDK-owned capability parameters and separate evaluation/create parameter blocks plus a feature handle for each eye; never share stereo histories. [Guide §§3.17, 5.2.4–5][guide], [typed helpers][helpers].
Lifecycle: disabled → initializing → capable → both features ready → active; any error records its reason and resets/quarantines the failed generation.
Create both eyes transactionally for one mode/dimension generation; partial creation releases the partial pair and remains unavailable. Retry only after explicit retry or a relevant configuration/device change.
Immediate dispatch validates the receipt, snapshots native depth to the eye-private R32_FLOAT texture, crops color if required, then evaluates into a separate UAV-capable output intermediate.
Only NGX success permits committing that intermediate into the native output. `CopyResource` requires matching copy-compatible descriptors; it neither converts formats nor crops. Use a validated explicit conversion/crop pass when needed (for example RGBA16F → R11G11B10); publish only after successful validation/recording. Never publish partial/failed DLSS output.
Reset on first use, load/cut/teleport, lost frame continuity, resource-owner change, backend/mode/device/format/rect/dimension/jitter generation change, and evaluate failure; retire/recreate features when their create contract changes.

## Input contract and jitter

Existing live jitter is the engine's eight-phase `engine_taa_jitter`, not `Fsr3EyeUpscaler::jitter()`.
DLSS-only projection jitter must use its own deterministic 32-phase sequence (at least 16 phases per [guide §3.7][guide]); maintain current/previous jitter for each eye and stamp the actual sample rendered into the receipt.
Paired eyes use the same pair phase, advanced once per rendered stereo pair; reset sequence/history together on discontinuity. Changing backends restores existing jitter behavior.
Patch both projection and associated engine current/previous constants coherently; supplying a longer sequence only to NGX is invalid. Unsupported projection ABI rejects takeover.
Keep alternate-eye DLSS disabled until same-eye history, phase advancement, resets and MV/depth continuity are proven. The separate `nativeGameTAA` alternate-eye candidate is outside this feature.
Map padded/full-size resources using explicit descriptors and subrects; enable output-subrect behavior if used. Convert MVs once to documented pixel units, with explicit full/active-resolution scale and measured previous-frame direction/jitter convention. Do not reuse FSR signs by assumption.
Use reversed-depth flag only after verifying the captured convention. Verify R32_FLOAT depth, R16G16_FLOAT MV and output format acceptance against the actual NGX create/evaluate contract.
Upscale currently consumes post-tonemap R8G8B8A8_UNORM perceptual/gamma bytes; native-resolution color has a different HDR/exposure seam. Represent and validate each independently, including pre-exposure/exposure source and HDR/auto-exposure flags. [Guide §§3.1.2, 3.6–3.9][guide].
Do not reuse one blanket FSR flag tuple or implicitly enable a preset with different exposure requirements.
Keep sharpening and sampler LOD bias independently controlled; make no global tuning changes. FSR's near-field reactive mask is not automatically a DLSS transparency mask; tracked-avatar ghosting remains an explicit acceptance risk.

## Modes, failure behavior and publication

Stage 1 is diagnostic opt-in paired DLAA at fixed native dimensions. Stage 2 adds paired DLSS Quality using `NGX_DLSS_GET_OPTIMAL_SETTINGS`, validating returned recommended/min/max dimensions rather than hard-coded percentages. [Helpers][helpers].
Disable dynamic-resolution/governor stepping for the DLSS request only until its contract is separately proven; leave existing modes and global settings unchanged.
Preserve stored meanings of AA 0..6. Append 7 = DLAA and 8 = DLSS Quality with a compatible versioned preference migration; Balanced/Performance/Ultra modes are deferred.
Internal pending requests may initialize capability/features, but no selectable DLSS/DLAA option is advertised as available before capability and both feature creations succeed for the current device/mode/dimensions.
Expose requested mode, actual backend per eye/pair, generation and precise unavailable/failure reason; a loaded provider or capable adapter alone is insufficient.
After takeover, DLSS failure tries an already-prepared FSR route with reset history; if unavailable/failed, use existing native-resolution color-copy or upscale bilinear fallback. Do not describe this as replaying native TAA.
Mixed successful temporal backends cannot publish a successful DLSS stereo pair; preserve pair-generation checks, invalidate temporal pair status and reset both histories for the next coherent pair. Keep SMAA and eye postprocessing functional.
Record DLSS timings/failures accurately; do not label NGX work as FSR.

## Evidence gates

CPU contract tests establish ordering, lifetime/state transitions and failure containment only. Native MSVC compilation/linking establishes typed ABI/runtime-library integration only.
After the user lifts the current no-game/no-GPU restriction: prove real Core/provider discovery, immediate-context create/evaluate, per-eye resource contents, jitter/MV/depth/color conventions, failure containment, resize/device recovery and stereo publication on Windows RTX.
Then assess paired-eye headset shimmer/ghosting, avatar/weapon trails, HUD/text clarity, comfort and GPU cost with user feedback; automated results do not establish headset quality or complete gameplay.
Proton is a later independent compatibility gate: [DXVK-NVAPI][nvapi] forwards D3D11 DLSS but does not implement it; current Proton 9+ enables it with exceptions, while `PROTON_ENABLE_NVAPI=1` is legacy setup, not proof. Record actual Proton/DXVK/NVAPI/driver versions, NVIDIA adapter and provider capability; test fallback there separately.

[sdk]: https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx.h
[loader]: https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_loader.h
[helpers]: https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_helpers_d3d.h
[guide]: https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/doc/DLSS_Programming_Guide_Release.pdf
[license]: https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/LICENSE.txt
[nvapi]: https://github.com/jp7677/dxvk-nvapi/blob/master/README.md
