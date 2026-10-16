# v562 repair and measured results

## Implemented

The only production change from v561 is in `render_perf_execute_scope`: retain the callback result inside `__try`, leave through `__finally`, then return. Ordinary true and false callbacks no longer invalidate their GPU timing frame. Microsoft documents that returning out of a guarded block counts as abnormal termination: https://learn.microsoft.com/en-us/windows/win32/debug/abnormaltermination . Genuine C++ and SEH exception cleanup remains unchanged.

The actual extracted MSVC/WARP regression now checks retained frame ownership/validity and zero aborts for normal true/false returns, as well as exactly-once exceptional abort, nesting, rejected contexts, original calls and HUD publication faults. The generated fixture's abort-site counter is disclosed; it is not production instrumentation. The extended regression fails against v561 and passes against the repaired source. Two independent read-only reviewers found no blocker.

## Build identity

- DLL: `build/d2vr_render_probe_arm_v562.dll`, 11,809,792 bytes, SHA256 `5f663ca48a0abc5a0d92e00a084165d612f94ed3bbce7f1be45bdf4178fbb942`.
- Full required build passed; source remained unchanged throughout. All 801 compiler inputs match the source ZIP and final checkout. Only `src/dxgi_probe.cpp` and `tests/render_perf_callback_check.py` differ from v561.
- All 500 external fixtures match the accepted baseline after compilation. This is a coordinated working-tree build, not an isolated-snapshot compilation. Upstream FidelityFX missing-PDB LNK4099 warnings remain nonfatal.
- Provenance: `artifacts/v562-headset-preparation/root-final-validation.json`; source pointer `build/d2vr_render_probe_arm_v562.dll.source.json`; candidate `artifacts/repair-v562-test-candidate.json`.
- Prior numbered DLLs remain preserved. No commit, branch, save replacement or quality reduction is made.

## Actual testing and controls

Fresh Steam PID9012 ran the same reviewed DLL with detailed profiling enabled. A ten-second readiness capture proved that GPU duration rows now publish; the earlier invalid-frame bug is absent. The subsequent user-confirmed 45-second still capture is archived at `artifacts/vr-v562-profile-20261016-0210/stationary-01`.

After the user authorized a same-checkpoint/same-view restart, fresh PID35408 ran the identical DLL with detailed profiling off. Its user-confirmed 45-second still capture is at `artifacts/vr-v562-profile-off-20261016-0230/stationary-01`. The normal lightweight per-eye timer remains enabled; this is not an all-instrumentation-off claim. Detailed native/HUD/FSR timings and standalone native/D12 compute timers are disabled. No screenshots, video, desktop inputs, compilation or helper analysis run inside either timed window. Paired QPC/tick clocks bound each window.

Both captures retain Native100, AA5, 2688x2880 per eye and settings SHA256 `c5ada63b43ca859277721d5633392032619b4f7ef5dbf4f47f6465200925127a`. Before v562 launched, the saved sharpening changed from0.25 to0.5 while v561 was running; root did not write it and preserved the latest setting. Comparisons with the older v561 readiness capture are therefore not byte-identical quality comparisons. The two v562 controls are byte-identical settings. Same checkpoint/view is human-confirmed, not pixel-verified scene equality.

The off capture began during FSR initialization despite the requested warmup. Analysis uses prespecified trailing warm cohorts, not the whole off window or arbitrarily removed slow frames. The off cohort starts more than12seconds after the first initialized FSR geometry/dispatch and generic GPU completion.

## Measurements

| Warm strict cohort | Detailed profiler on | Detailed profiler off |
| --- | ---: | ---: |
| Uninterrupted duration | 30.770s | 30.858s |
| Complete eligible frames | 1605 | 1901 |
| Application cadence | 52.128Hz | 61.571Hz |
| Mean frame interval | 19.183ms | 16.241ms |
| p95 frame interval | 25.226ms | 18.366ms |
| p99 frame interval | 33.161ms | 20.081ms |

The observed detailed-profiling difference is approximately2.942ms/frame, or18.1% throughput. Do not attribute the entire low-frame-rate issue to instrumentation or claim that this profiler-only repair improves normal play by that amount. The off control still does not reach90application frames/second.

The warm on cohort's native command-list GPU elapsed sum averages12.384ms and the complete GPU frame envelope17.511ms. Elapsed queries are not a GPU-busy proof. In the broader reconciled 1763-frame on cohort:

- HUD publication GPU median0.064ms, p950.191ms. Aggregated per-frame CPU validation median1.510ms, p952.035ms; this is not a measured critical-thread execution duration.
- FSR stereo workload sum averages4.181ms, including approximately3.210ms shader compute. These sums are not automatically the frame critical path.
- GPU bridge-minus-shader residual averages0.483ms per stereo pair; it does not isolate queue idle or a fence wait. CPU Flush averages0.0057ms per stereo pair. CPU handoff averages1.086ms, nested within the1.233ms dispatch scope; do not add nested scopes.
- One selected frame lacks an observed second-eye job. One cumulative drop/failure is recorded in the longer run; no frame-capacity loss occurs. Sparse HUD capture samples and asynchronous completion coverage are explicitly limited in the underlying reports.

The largest remaining wall-clock component is outside Present:14.219ms mean in the off warm cohort. It includes engine work and possible waiting, rather than proving native CPU execution. HUD GPU cost, Flush and measured bridge residual do not explain the whole frame interval. Critical CPU feeding versus native GPU workload is not yet conclusively separated. No explicit third full-scene render was found in the bounded source review; the complete native render census remains unproven.

Twelve supplementary one-second OS samples, taken after the clean windows, show game 3D-engine utilization median81.14% (range76.31–93.29), process CPU median19.00% of16logical processors, about3.04core-equivalents, working set approximately4.07GiB and private committed bytes11.61GiB. The engine LUID matches the RX9070XT build-log adapter identity; CPU is Ryzen79800X3D. These samples are not a matched scene/pose control, do not identify a saturated critical thread, and do not measure VRAM usage or establish a leak. GPU engines are not summed into a misleading single utilization percentage. Raw collector and errors are retained in `system-sample.json`; no counter errors occurred.

## Shutdown and outstanding evidence

The on session stopped through exact-PID StopProbe, exited0 and reported complete core/body restoration, no queued manual equip and zero first-chance/recoverable faults. Its flat game then closed through the authorized graceful restart. Archive: `artifacts/runs/20261016T002712.433039Z-pid9012-arm-v562`.

The user intentionally closed the off game and confirmed testing finished. The launcher consequently cannot verify a native restoration report and exits1; this is not classified as a crash. Archive: `artifacts/runs/20261016T003502.382100Z-pid35408-arm-v562`. Diagnostic flags restored, current settings unchanged, no game remains. A proposed per-thread counter capture refused the missing PID before collecting anything; there is no20second thread result. No relaunch follows the user's test completion.

The earlier v561 session's six guarded cached-sensor read faults are separate from seven startup OpenXR availability recoveries. Audit: `artifacts/profiler-normal-return-20261016/v561-fault-audit.md`. Preserve OpenXR recovery and do not suppress faults or extend stale ownership to make counts look clean.

## Next repair gates

1. Keep the known numbered baseline and quality settings. Use detailed profiling only for bounded diagnostic sessions; a critical-thread/GPU counter window with correlated XR timing remains the next performance classification gate. Do not lower quality or optimize an unproved third scene.
2. The user rejected v561 tank stability and clarified that the tank moves relative to the hand. v562 contains no carry repair. Exact state49/50, full-spawn/entity/RF/model and final admitted hand palette still need correlation with a real tank-visible interval.
3. A narrow default-off, finite observer is approved for isolated artifact implementation/testing only: existing held publication/bind multiplexer, final admitted arm selection and one verified outer753c10 boundary. Verify executable ABI/prologue/calls, original-once recursive/C++/SEH behavior, ownership before/after, nonblocking completed-slot ring and stop/drain lifetime. No shared integration or replacement DLL until root review and tests pass. See `artifacts/oil-tank-reopened-20261016/BOUNDED-CAPTURE-PROPOSAL.md`.
4. Only the demonstrated target/model/render boundary determines the carry fix. Stable native model with unstable image requires a later exact-instance current/previous/temporal capture, not guessed smoothing or native-physics disabling. Preserve pickup/release/throw, corpse transitions, gun hip/draw and accepted arm replay.
5. Separately qualify current cached sensor/player-machine ownership before native takedown lookups. Preserve native stealth eligibility; require an actual stale-owner failing regression and reviewed fix rather than broadening eligibility or swallowing exceptions globally.
6. One combined reviewed build follows source freeze. A new qualified tank still/moving trial, weapon/corpse transitions, native-hint Choke, sword damage and the remaining Y prompt/Shadow Walk/hand-physics/comfort checks remain required. Procedural/query hands are not accepted full native physical hand actors. Build and simulation checks never establish headset acceptance or a bug-free release.
