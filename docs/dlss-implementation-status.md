# DLSS implementation status

Updated 2026-10-22. **DLSS and DLAA are not available rendering modes in this implementation.** The portable contract and typed SDK owner are implemented, but the native execution adapter and required input ownership are incomplete. Existing AA modes 0..6 and accepted FSR/native paths remain the production paths.

This is an implementation checkpoint for the [design](superpowers/specs/2026-10-22-dlss-design.md) and [task plan](superpowers/plans/2026-10-22-dlss.md). It does not establish runtime or headset acceptance.

## Completed source and offline evidence

| Stage | Implemented scope | Evidence and limit |
| --- | --- | --- |
| T1 | SDK-independent immutable render receipts, strong input/list leases, staged split/sealed execution envelopes, ordered fallback nodes, explicit busy/defer result and GPU-retirement boundary | Scoped source review and CPU checks passed. Portable scheduling/lifetime checks do not implement the native adapter. Initial commits `ad9d130`, `0dc5c24`; subsequent portable extensions belong to partial T3. |
| T2 | SDK-typed serialized NGX owner, independent eye feature/parameter histories, bounded retirement, partial-failure rollback, explicit provider validation and expected/observable loaded-module diagnostics | Scoped re-review passed at `81b0df8` after `97ef347`. Controller strict Windows-target CPU checks, typed SDK ABI compile/link and MinHook/production DLL cross compile/link passed without degraded allowances. Native MSVC and actual device/provider execution remain pending. |
| T3 | Completed-Setup view/pair/projection metadata, exact native counter/owner correspondence, retained clear-context/resource/view diagnostics, portable mixed-node order, copy compatibility, pair outcome and retirement/quarantine contracts | Partial source only, initially `4aee87c`, with corrective commits `f10d411`, `4365eeb`. Initial candidate CPU and production cross-link passed. Corrective CPU checks pass; final corrective review and combined source archive/production compile are pending at this checkpoint. |

T2 pins the NVIDIA SDK headers and static `/MD` library to commit `9981c6350dc58c0ce21f024fd65e31e851c31689`; [provenance](../third_party/dlss/source.json) and [notices](../THIRD_PARTY_NOTICES.md) are retained. The compile-only lane checks exact typed SDK APIs. Injected lifecycle tests make no real NGX/device calls. No provider was downloaded, bundled or initialized by this work.

T2's final controller DLL hash was `421af86715e28dadf775a88bf6abeeae9e673e48f97e2a3352a59abba88a68e8`. That evidence covers the T2 snapshot, not later T3 or aim changes. T3's initial successful production link likewise does not validate subsequent corrections or the final combined candidate.

## Partial T3 boundary

Optional source diagnostics use an **internal capture flag defaulting false**. ABI availability does not enable capture; no user opt-in or settings mode has been added. While disabled, added metadata readers, receipt locks and diagnostic COM snapshots are gated. Existing cheap depth discovery and accepted FSR scheduling remain active. Future opt-in lifecycle/configuration work belongs to T5.

The following native integration work is still unimplemented or unqualified:

- **Depth producer:** establish exact producing eye/frame/context/thread/resource/view and its ordered prefix. Heuristic depth selection and a captured clear are not a stereo producer certificate. Luma's DownsampleDepth resource binding is a research lead, not verified native stereo ownership.
- **Projection consumption:** qualify rendered current/previous projection/jitter and GPU constant correspondence. Observing submitted projection against native current projection after Setup establishes CPU correspondence only.
- **Callback ownership:** prove the immediate-owner, non-reentry and bounded deferred-retry contract. A portable busy result cannot safely become native-forward-on-miss. No configuration bypass certifies this boundary.
- **Native state isolation:** implement supported Device1/Context1 context-state creation/swap/restoration with the actual device feature level/interface/threading flags. Existing compute-only state saving is not certified for NGX.
- **Queue adapter:** connect real reserve/split/finished/execute operations through one ordered envelope for temporal and fallback-only nodes, preserving all native lists, restore flags, observer timing and existing FSR behavior. Production DLSS takeover remains unwired before any split or SDK evaluation.
- **GPU retirement:** preallocate bounded event-query/fence resources, signal/poll at owned immediate points, retain old owners/scratch through real completion and stop/reset, quarantine failures, and connect completion to T1/T2 retirement. CPU completion is not GPU retirement.
- **Native outputs and fallback:** allocate exact-format eye snapshots/intermediates, commit only successful results, implement supported crop/conversion where necessary, and connect ready FSR/spatial fallback plus coherent pair/history reset. Portable descriptor checks do not supply this adapter.

Production admission remains refused where these contracts are unknown. This is a partial source gate requiring corrective integration work, not runnable DLSS awaiting only visual testing.

## Remaining stages and acceptance

- **T4:** DLSS-only paired 32-phase jitter, coherent native current/previous constants and reset/transition handling. Existing jitter remains in use for other paths.
- **T5:** diagnostic fixed-dimension paired DLAA opt-in, qualified color/exposure/MV conventions, truthful requested/actual backend reporting and recovery. No opt-in exists yet.
- **T6:** Quality optimal-settings dimensions, paired resize transaction and compatible preference migration/selectable modes, only after the DLAA runtime input gate passes.
- **T7:** explicitly authorized Windows RTX input, provider-selection, recovery and GPU ownership validation; repeat for Quality. No such runtime validation has occurred.
- **T8:** explicitly authorized headset acceptance and separate actual Proton/DXVK-NVAPI verification for both staged modes, plus release/legal prerequisites. No visual quality, comfort, complete gameplay or Proton success is established.

Native MSVC `/W4 /WX` remains pending; cross-toolchain compile/link results do not replace it. Final combined review, strict CPU checks, production compile/link and source-archive matching remain controller gates. Game, VR, GPU/device/provider execution and headset validation remain restricted until explicitly authorized. Any future provider use follows the explicit user-supplied signed-provider lane; there is no provider download/copy automation or distribution authorization.
