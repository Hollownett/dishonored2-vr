# DLSS implementation status

Updated 2026-10-22. **DLSS and DLAA are not available rendering modes in the game.** The typed production NGX owner now passes a real RTX synthetic paired-DLAA check, and the standalone D3D11 state/query component passes real hardware checks. Native game input ownership and the execution adapter remain incomplete. Existing AA modes 0..6 and accepted FSR/native paths remain the production paths.

This checkpoint tracks the [design](superpowers/specs/2026-10-22-dlss-design.md) and [task plan](superpowers/plans/2026-10-22-dlss.md). Standalone hardware results do not establish game or headset acceptance.

## Completed source and hardware evidence

| Stage/component | Implemented scope | Evidence and limit |
| --- | --- | --- |
| T1 | Immutable render receipts, strong input/list leases, split/seal/execute phases, ordered fallback, explicit busy/defer and retirement contracts | Source review and CPU checks passed at `0dc5c24`, with later partial-T3 extensions. Native adapter integration remains open. |
| T2 | SDK-typed serialized NGX owner, independent eye histories, bounded retirement, rollback, signed-provider/canonical-path validation | Corrective review passed at `81b0df8`; strict typed SDK cross compile/link and CPU checks passed. Actual production owner/provider execution passed in the standalone synthetic check below. Native Windows MSVC remains pending. |
| Partial T3 metadata/contracts | Completed-Setup pair/projection metadata, exact counter/owner correspondence, optional clear diagnostics, ordered jobs, copy/pair/retirement contracts | Corrective reviews passed through `bcaee8c`; CPU/source gates passed. CPU metadata is not a GPU projection or depth-producer certificate. |
| Partial T3 state/query component | Actual Device1/Context1 state swap/restoration and bounded EVENT-backed submission leases | `7b419d7`, independent source review and strict cross compile/link passed; real RTX/DXVK checks passed with ordinary and SINGLETHREADED devices. Native queue/module-stop integration remains open. |
| Partial T3 native census | Default-off paired-Setup-armed binding/recording observer at the existing pinned POSTPROC/Dispatch seams | `9304fa7`, refusal/flush race corrected at `8a7af29`; independent source re-review, portable CPU, C# checks and Windows-target object/link passed. Final CPU-only Wine fixture passed at `8a7af29` (exit 0, 0.62s). Actual protected game captures now exist; initial runs exposed timing and retention limits described below. Native input certification remains open. |
| Partial T7 standalone NGX | Production validation and SDK calls with synthetic paired DLAA inputs, output readback and full-generation GPU drain | `9d39531`, independently reviewed, strict object/static-SDK link passed; actual RTX/DXVK run passed four pairs/eight SDK evaluations. Native D2 inputs, Quality and recovery matrix remain unqualified. |

The NVIDIA SDK headers and static `/MD` library remain pinned to `9981c6350dc58c0ce21f024fd65e31e851c31689`; [provenance](../third_party/dlss/source.json) and [notices](../THIRD_PARTY_NOTICES.md) are retained. No provider was downloaded, copied or bundled. The standalone check used the existing Helldivers 2 `bin/nvngx_dlss.dll` in place (SHA-256 `8707e53b26c68c606b98bf31c223485ff30d310a261b1a36d48b2eaabc1507ec`). Production WinVerifyTrust and canonical expected/observed loaded-provider checks passed without a validation bypass.

Hardware environment: RTX 3070 Laptop 8 GiB, Vendor 10de/Device 249d, feature level 11_1, NVIDIA driver 615.71.09, Fedora 44, private Proton 10.0-4b prefix and DXVK 2.6.2. These were real GPU executions of Windows-target binaries under Proton, not native Windows/MSVC results.

- State/query: both device flag variants passed six-stage/pipeline restoration after success, failure and exception, CB1 offsetting, EVENT completion, copy bytes, bounded leases/drain and callback facade teardown. Query-error/device-loss hardware injection remains untested. Evidence: `artifacts/dlss-implementation-20261022/rtx-hardware/state-query-7b419d7/result.json` (exit 0, 2.37s), with command and test log alongside.
- NGX: four synthetic 1024×768 paired DLAA frames produced eight SDKSuccess evaluations, each with 786432 finite pixels, zero untouched sentinels and distinct left-red/right-green output. Frame 3 changed source textures and explicitly reset both histories. All submitted uses completed EVENT queries and generation cleanup passed. Master prepare/shutdown report provider_verified=1 and matching paths; per-eye evaluation status has default provider fields and reports SDK/eye results only, not a second trust check. Evidence: `artifacts/dlss-implementation-20261022/rtx-hardware/ngx-9d39531/result.json` (exit 0, 3.07s), with planned command and test log alongside.
- Census CPU wrapper: `artifacts/dlss-implementation-20261022/native-temporal-census-cpu-wine-8a7af29-result.json`; no D3D device, GPU getters or provider initialization. Its invalid-output/open-failure/marker/reset/retry checks do not establish native capture.

## Partial T3 boundary

The new **Capture graphics diagnostics** feature (`native-temporal-census`) defaults false. It is separate from the older internal temporal capture flag and does not enable FSR ClearSnapshot capture or a DLAA mode. With census disabled its observer callbacks are null and optional metadata/getter/retention work is inactive. When enabled, two completed, distinct paired-world Setup receipts must agree on counter/allocation/camera ownership before capture arms.

Capture is bounded to 120 admitted native passes, 4096 events, 128 canonical COM objects and 16 contexts. Admitted scopes drain after job admission closes; actual losses/capacity exhaustion mark output incomplete rather than overwriting lineage. Retained objects survive until controlled-thread output publication succeeds. Failed output blocks reset and permits retry; CSV files publish before the JSON completion marker. Recording/binding observations are not shader bytecode identity, depth-writer certification or GPU execution ordering. Luma's DownsampleDepth hash 27BD5265 remains a research lead only.

The following native integration boundaries remain open:

- **Depth producer and projection:** exact producing eye/frame/context/thread/resource/subresource and prefix ordering; rendered current/previous projection, jitter and GPU constant consumption. A heuristic depth selection, clear or CPU Setup correspondence cannot certify these.
- **Callback and queue ownership:** immediate-owner/non-reentry/deferred-retry contract, real reserve/split/finished/execute envelope, restore flags, all native lists and FSR observer behavior. Production NGX takeover remains unwired.
- **Native state and retirement integration:** wire the hardware-tested state/query primitive at the actual immediate seam, join completion to aggregate T1/T2 generations, and implement stop/reset/module-drain ownership. Standalone restoration/drain does not supply module-unload integration.
- **Outputs and fallback:** exact-format eye-private snapshots/intermediates, commit-on-success, supported crop/conversion, ready FSR/spatial fallback, coherent pair/history resets and error recovery.

Production admission remains refused where these contracts are unknown. Whole T3 remains partial.

## Build provenance

Historical T2 DLL SHA-256 `421af86715e28dadf775a88bf6abeeae9e673e48f97e2a3352a59abba88a68e8` covers its snapshot. Historical combined aim/T3 candidate `6e4f59e` passed eight CPU groups/sixteen build-run steps, affected cleanup checks and typed SDK/MinHook/production cross compile/link (`degraded=false`). Its DLL SHA-256 was `8d1f5232a1fa462ae83f8f8e77052fbd153310da946a093af33c1a1247bfe368`, archive `20261022T192531.125906Z-229152b8`, source unchanged during build. Historical provenance is retained at `build/d2vr_render_probe_aim_candidate.dll.source.json`. These older artifacts are not the new RTX/census checkpoint.

Final combined census checkpoint at source `8a7af29`: MinHook and SDK-free production DLL cross compile/link both passed, `degraded=false`; production step 44.49s. Diagnostics were the three existing unused-global warnings and vendor LNK4099 missing-PDB warning. No SDK was linked and no NGX symbols were found in the map.

- DLL: `build/d2vr_render_probe_temporal_census.dll`, SHA-256 `17561005344e4d4e1465cf6f79020f1b12ce161cec5bdf43fdaa973f5a7c2820`.
- Provenance: `build/d2vr_render_probe_temporal_census.dll.source.json`, state complete, source_unchanged_during_build=true. It was built from the unchanged checkout matched to archive `20261022T210912.064417Z-dac98716`, not compiled directly from the archived snapshot (`compiled_from_frozen_snapshot=false`). Manifest SHA-256 `71c8d236eef1878978bd202e92d41d2596638ca5ddc1543a4c1cd39a2c4db9e2`.
- Commands/results: `artifacts/dlss-implementation-20261022/census-build/production-command.json` and `build-result.json`.

The final feature list has 33 entries and census defaults false. That older production DLL is a historical checkpoint. Later diagnostic candidates executed in the protected game runs below; separately built state/query and NGX binaries supply the standalone hardware evidence above.

## Actual native diagnostic captures

The first protected 120-second capture (`20261022T232944`) returned 0 with gameplay_at_capture=true, 7305 XR gameplay layers and no callback/TAA failures. Its census completed 120 scopes / 240 events but recorded zero Dispatch calls/contexts: it exhausted before hook installation. The post-pass B1 recorder also consumed its budget early. This was lossless scope bookkeeping, not binding/input proof. Analysis: `artifacts/dlss-implementation-20261022/game-census/first-run-analysis.json`.

Reviewed corrections: `6851c82` requires the actual Dispatch hook+handler before census paired-Setup arming; `20a85ac` reserves B1 capture for structural Dispatch observations in census sessions; `43b6be9` skips Dispatch calls outside admitted native POSTPROC TLS scopes before readers/getters/QI/COM retention. All received independent source-review PASS. The generic Ledger and renderer/AA/FSR paths remain unchanged; JSON now explicitly declares POSTPROC-scoped hooked-implementation coverage, not all-process coverage.

The second protected 120-second capture (`20261022T234330`) returned 0, gameplay_at_capture=true, 6604 XR gameplay layers and no callback/TAA failures. It recorded 5 inside-POSTPROC and 16 outside dispatches before 128-object capacity: 24 attempted / 23 accepted / 1 dropped, one native pass, complete=false/incomplete=true. B1 now contains 128 paired samples over 64 Present-labeled frames (64 per eye), showing the native eight-phase jitter after the initial zero. This structural-classifier/delayed-readback evidence is not exact shader identity, native eye lineage or GPU ordering. B2 remained empty: the observed 160-byte buffer is below the existing 192-byte minimum. Source UCD42 variants use 208 bytes, but no exact shader hash links the observed buffer to that source variant; no guard was relaxed or layout inferred. Analysis: `artifacts/dlss-implementation-20261022/game-census-hook-ready/second-run-analysis.json`.

The `43b6be9` Windows CPU wrapper passed under root-owned CPU Wine (5.02s), including an armed out-of-scope invalid-context pointer proving the guard precedes D3D access. Its final SDK-free production cross-link passed (`degraded=false`, 46.24s production step). DLL `build/d2vr_render_probe_temporal_census_postproc_scoped.dll` SHA-256 `5c98a4fd86e70ace016e7d6f31141bf315f7124203336bc9cc2e7ec08889073b`; provenance `build/d2vr_render_probe_temporal_census_postproc_scoped.dll.source.json`. Archive `20261022T214900.305377Z-0456f28b` matches unchanged checkout, source_unchanged_during_build=true and compiled_from_frozen_snapshot=false. The third protected 120-second capture (`20261022T235018`) using this DLL returned 0/OK on the verified RTX adapter under Proton Experimental/DXVK. It reached gameplay_at_capture=true with 7268 XR gameplay layers, callback_failed=false, taa_failures=0 and 7357 right-eye swaps. Census reached its 120-pass bound with 840 accepted events (240 scope events plus 600 inside-POSTPROC dispatches), zero drops, 49 retained objects and 8 contexts. All 840 events had stable native view/counter observations; two distinct native views contributed 60 passes each over counters5605–5664. Five retained compute-shader object identities each appeared in 120 dispatches. These are object identities, not bytecode identities.

All 840 events remained unknown with zero resolved Setup receipts. Thus complete=true means a lossless bounded recording/binding trace; it does not certify paired native inputs. Shader-bytecode/depth-producer/GPU-order flags remain false and production_admission=false. B1 retained 1152 rows / 128 jitter samples over 64 frames, 64 samples per eye; B2 remained empty with the 160-byte variant unresolved. Analysis: `artifacts/dlss-implementation-20261022/game-census-postproc-scoped/third-run-analysis.json`.

All three protected runs restored saves/config and the original installed DLL with verification (original SHA-256 `ff0daf6566a62a44a31cef372a93114fc42790f2a13eb9887f3dfb3de63bc927`). No headset acceptance or performance comparison is inferred. The next gate is to establish why completed Setup receipts do not match the native POSTPROC observations, without inventing a past-counter association, and qualify exact shader bytecode identity; actual producer/resource lineage and native queue integration follow.

## Remaining stages and acceptance

- **T4:** paired 32-phase DLSS jitter and actual native current/previous constants/reset handling.
- **T5:** fixed-dimension diagnostic paired DLAA in the renderer, qualified color/exposure/MV conventions and truthful requested/actual reporting. The census opt-in is not this mode.
- **T6:** Quality optimal-settings dimensions, paired resize and compatible selectable preferences after the native DLAA input/recovery gate.
- **T7:** native Windows RTX input, provider/failure/recovery and ownership matrix for DLAA, then Quality. Standalone synthetic DLAA is a completed subgate only; whole T7 remains unchecked.
- **T8:** actual game Proton/DXVK-NVAPI verification and headset acceptance for both staged modes, including visual quality, comfort, performance and gameplay. No headset result is established.

The user has explicitly authorized GPU tests and game launch. The earlier protected v605 baseline attempt was blocked by Steam AppError 5 / Family Sharing before StartProbe; game access subsequently recovered. Actual protected XR game captures below now supersede that availability block. They do not establish headset visual quality, comfort or complete gameplay acceptance. v605 remains a prior release, not a new DLSS release.

Native MSVC `/W4 /WX` and the native integration/headset gates remain pending. Cross-toolchain and standalone Proton results do not replace them. Provider publication, SDK/license/notice/trademark obligations and any future distribution scope remain separate release prerequisites.
