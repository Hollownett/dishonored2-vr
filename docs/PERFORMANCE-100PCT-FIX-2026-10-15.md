# 100% VR resolution: measured FSR4 costs and correction

The 100% slowdown is real. A redundant FSR4 output copy has been removed without changing output pixels. This is a correctness-preserving cleanup, not a demonstrated cure for the sustained 39 FPS gameplay interval. The main native-game bottleneck is still not fully attributed because the archived game recording lacks native GPU durations and the headset is currently unavailable.

## What was measured on this machine

The existing production-wrapper GPU fixture was extended to replay the recorded 100% geometry: 2112 x 2304 output and 1626 x 1774 FSR input per eye, identical FSR4 4.1.1 provider, existing depth/reactive/motion/sharpening behavior, 8 warmup and 128 measured stereo pairs. Adapter identity is RX 9070 XT, LUID `00000000:00015bfc`, matching the adapter identifier in the captured resource series. This is a synthetic scene through the actual wrapper and real hardware, not a replay of the full native game or delivered headset FPS.

| Measured scope | Mean per stereo pair |
|---|---:|
| Entire FSR4 wrapper, with diagnostic enabled | 6.100 ms |
| FFX GPU intervals, left + right from the same 128 pairs | 1.989 + 1.813 = 3.803 ms |
| Outside those FFX intervals | 2.298 ms |
| CPU submission of both jobs | 0.586 ms |

The FFX timestamps bracket the SDK commands on its own D3D12 queue, excluding the surrounding interop transitions and waits. They are GPU elapsed intervals, not hardware GPU-busy counters; preemption can still affect them. The residual includes native depth preparation, copies, GPU queue handoffs, and CPU submission/queue-idle gaps. It is not all proven synchronization waste. CPU recording/handoff times are nested and must not be added to the table's total.

Evidence: [compute diagnostic](../artifacts/fsr4-100pct-fix-20261015/compute-diagnostic.json), [results and pixel hashes](../artifacts/fsr4-100pct-fix-20261015/results.json), benchmark logs in the same folder. The older recording's asynchronous per-eye latest durations were not added together to obtain this result.

## Fixed waste, preserving the reconstructed image

The exact archived v555 source copied shared FSR4 output into `e.upscaled`, then copied that entire texture again into the native eye target. The new path copies shared output directly into the native target. It removes two full-resolution RGBA8 copies per stereo pair: 38,928,384 bytes copied, roughly 77.9 MB of nominal read/write traffic per frame. No FSR parameters, render dimensions, quality presets, motion-vector handling, jitter, sharpening, masks, controls or calibration were reduced.

Output validation now permits the RGBA8 UNORM/sRGB/TYPELESS copy-compatible group while retaining exact dimensions, one mip, one slice, single sampling and the exact D3D11 device owner. The existing slot retains the final native output through its consumption fence. FSR3 fallback still uses its UAV intermediate and original final copy. [Microsoft CopyResource requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource).

Ten complete 19,464,192-byte patterned FSR4 output dumps match SHA256 `cbe5e4a8ceab1113ed8b3747f0cdacf0ba5374573f89a598fc9233e8cc69e18b`, including repeated original/candidate runs and UNORM, sRGB and TYPELESS output destinations. This demonstrates byte preservation for the tested spatial pattern and temporal sequence. It does not certify every dynamic game image or headset comfort.

The first matched 128-pair A/B runs were 5.893/6.018 ms for the original path and 5.931/5.938 ms for direct output. Variation exceeds the mean difference, so no material GPU speedup is established. Removing the copy is valid; claiming that it fixes the full slowdown would not be.

## Synchronization investigation

Both GPU waits and the explicit D3D11 submission Flush are retained: they order the D3D11 input copies, D3D12 FSR work and final D3D11 consumption. Removing them would risk stale images, resource reuse or a queue dependency on unsubmitted commands. The consumption signal remains the slot-reuse/retirement gate. [Microsoft interop Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-wait), [multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).

An isolated experiment reusing one D3D12 queue for both eyes measured about 6.103 ms versus 6.100 ms in the corresponding diagnostic run. It showed no useful saving and is not part of the shared-source patch. Context/device creation is cached, not per frame. Existing constant-input caching and cropped input copies were already present in v555; they were not counted as new optimizations.

The native stereo audit found two actual prepared views, not a third desktop scene render. In 207 consecutive archived Presents there are 207 left/right setups and begins, plus 207 null-view begins (`view=0, eye=-1`). The desktop mirror composes existing eye textures. Main hand/contact/body updates are guarded by eye zero. These observations do not establish that the engine shares all shadow/visibility preparation between eyes, but they do not justify deleting a supposed third scene pass or duplicate simulation update.

## Instrumentation ready for the missing native-game measurement

The optional FSR4 timing uses four fenced slots per eye, timestamp queries and small readback buffers. It reads them only after the existing consumption fence completes; it adds no CPU completion waits. Allocation/frequency/Map failures disable this diagnostic without selecting a lower-quality renderer. It is disabled by default, independent of the existing D3D11 disjoint timers. [Microsoft GPU timestamps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

For a coordinated diagnostic build/session, `build/fsr4-compute-timing` enables it. Status exposes `feedback_render.eyes[].fsr4_compute_gpu_ms`, `_total_ms` and `_samples`. Use advancing counters and differences of cumulative totals; counters reset on context rebuild. Pair it with the existing `build/native-gpu-timing` diagnostic, which disables independent D3D11 FSR disjoint brackets to avoid nesting them. The D3D12 FFX timestamps use a separate queue clock. Native-list sums still exclude immediate draws and intervening FSR, so retain engine CPU/elapsed spans and coverage.

The runtime probe returned `VirtualDesktopXR (1.0.10)` and `HEADSET_UNAVAILABLE`. No game was launched, no saved graphics/VR setting was changed, and no optimized default or delivered-FPS improvement has been declared. The remaining required run is the same scene at fixed 100% output, same FSR4 quality, no capture/compiler/resource-sampler load, with native GPU and FFX timing plus compositor delivery counters.

## Validation and integration boundary

The targeted C++ builds passed `/W4 /WX`. Real-GPU checks passed for FSR4 patterned 100% output, compatible output formats, genuine provider identities, missing-provider FSR3 fallback, native/Quality/Balanced wrapper paths, render-size context reuse/growth, standalone ownership/input rejection and in-flight retirement. Settings/config baseline hashes remained unchanged.

The repair chat began its one combined v556 build while this task was active. Its frozen source includes the direct-output correction and timing/status integration; the overlap hashes are recorded in `artifacts/fsr4-100pct-fix-20261015/combined-build-overlap.json`. Subsequent differences are the inactive-diagnostic early return and a test macro compatibility guard. Further shared edits/builds are held during that combined build. This task did not publish a second numbered DLL or launch an independently assembled candidate.

The scoped integration patch is [performance-only.patch](../artifacts/fsr4-100pct-fix-20261015/performance-only.patch). Synthetic checks pass, but the actual game-frame bottleneck and overall FPS repair remain unfinished pending the unavailable headset measurement.

Update: combined v556 retry2 passed the complete required suite and provenance checks for 763 compiler inputs and 500 external fixtures, including both late review fixes. The exported DLL was independently hash-verified as `43d9243e59da5f13df470f0c1bd4d6b9eaf6a079933bdcd66713f5126dca37ed`. It has not been launched. The build/provenance gate is complete; the native-game and headset performance gates remain pending. Evidence: `artifacts/combined-v556-retry2-build-20261015/combined-build-result.json` and `candidate-status.json`.
