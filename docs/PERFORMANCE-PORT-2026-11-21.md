# Performance port from the Cosmic Tangerine fork: what applied (2026-11-21)

Source reviewed: Indurhra/dishonored-2-vr `docs/PERFORMANCE-REFERENCE.md`, `docs/RELEASE-0.7.0.md` and the commits behind them
(the old fork is a different codebase: Game.cpp / GpuTrace.cpp / DlssLive.cpp / XRSession.cpp, per-draw transform rewriting, PureDark
AFW). Every item was checked against this mod's own code and against a profile of it, not ported by name.

Target: branch `perf/ct-port-v1300` from `26aebd5` (v1299). Renderer under test: AFW (the user's saved choice), unchanged.
All numbers are from the OpenXR simulator in this repo (`--simulated-vr`), not a headset.

## Method

- Scene: `idle` scenario (fixed view, both simulated controllers tracked), the user's saved VR settings, AFW.
- The simulator's `xrWaitFrame` never blocks: in AFW mode the only cap is the engine's frame limiter (30 x `com_timeScaler`; the
  user's config has 3 = 90 engine frames/s). Throughput runs lift it in memory only with the test command `cvar com_timeScaler 10`
  (the config file hash was verified unchanged after every run). Metric: engine Presents/s. In AFW mode one Present carries one
  fresh eye; the other eye is rebuilt. These are not complete stereo pairs and not headset-displayed FPS.
- Windows exclude loading: the engine compiles shaders (its own `d3dcompiler_47_64.dll`) for ~33 s after the gameplay flag,
  before gameplay frames flow; timing starts after it.
- Interleaved A/B (v1299, v1300, v1299, v1300...), same scene/settings/session conditions. No builds or other heavy work during runs.
- Profiling: `tools/sample_threads.py` (external stack sampler; now without the pefile dependency and with caller attribution), a
  PDB symbolizer, and two diagnostic knobs added here (`build/region-cache-stats`, `build/compile-census`). All off by default.

## Results

| Optimization (reference item) | Status | Reason | Validation and measured result |
|---|---|---|---|
| A. Repeated validation syscalls behind player/singleton lookups | **Newly implemented (adapted)** | This mod already reads the live singleton/player values in-process (`read_self`, guarded memcpy) with a per-thread VirtualQuery cache, and caches no game object. The profile still showed VirtualQuery at 6-10% of the engine main thread: the direct-mapped cache missed 36 k lookups/s, 91% on a slot holding another still-valid region. Now 4-way set associative; same TTLs, readability rules, fault-forget; no object caching. | VirtualQuery 36,238/s -> 5,444/s. Main-thread busy time per frame (waits/spins excluded) 6.24/6.49 -> 5.66/5.58 ms at the 90 cap. Uncapped engine Presents/s, diagnostics on: v1299 66.8/136.6/126.5/120.1 vs v1300 176.0/176.2/175.0/175.5; p99 interval 10.8-20.4 -> 9.1-9.5 ms. Diagnostics off (normal play; rate from the brief status's submission counter over five 5 s steady windows): v1299 118.2/148.0 vs v1300 176.4/169.2. The baseline varies by session because the thrash depends on where heap regions land; v1300 removes that swing. Zero AFW refusals in all runs. Unit test extended. |
| B. Transform-readiness witness | Skipped (not applicable) | The old witness skipped a lock taken per draw just to ask "is this view's hand transform ready". This mod has no such per-draw transform rewrite: hands are the engine's own skinned draws (the rig palette is copied once per engine frame, rig_mask.hpp v1179). Some per-draw scopes take mutexes (rig mask, object motion), but lock acquire/wait was ~0.2% of main-thread samples. | Profile only. |
| C. Per-thread shader identity caches | Skipped (no bottleneck) | No shared shader-hash map is looked up under a lock on the draw path; no shader-lookup frames appeared in the steady profile. | Profile only. |
| D. Pooled temporary GPU resources | Skipped (no bottleneck) | Steady-state GPU allocations (`NtGdiDdDDICreateAllocation`) were 5 hits in 2,500 sample rounds, almost all from the engine/driver. The old pool was for DLSS colour snapshots this mod does not make. AFW-retained resources were not pooled. | Profile only. |
| E. SteamVR frame dispatch worker | Skipped (bottleneck absent) | `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` run inline on the Present hook; no `ID3D11Multithread` lock is held while waiting; v835 headset logs had SteamVR `xrWaitFrame` p50 0.01 ms. Desktop Present sync 0 is already in place. | Code review. A simulator cannot validate a SteamVR/VD wait change. |
| F. Native-stereo engine cap | Checked, not changed | Paired native stereo draws both eyes in ONE engine frame, so the old 120-engine-frames = 60-pairs relation does not exist. In AFW/alternate mode one engine frame is one eye, and the frame pacer is off there: the engine limiter (90 for this config) is what paces it. It matches a 90 Hz headset exactly; a 120 Hz headset mode would be capped at 90 eye-frames/s. Lifting the limiter without a pacer would run past the display rate, so no default override was added. | Simulator: capped 90 vs uncapped ~175 Presents/s confirms the limiter binds. Recommendation below. |
| G. Persistent AFW/DLSS fallback recovery | Root cause fixed (DLSS); recovery not added | AFW (VSH) refusals fall back per frame only; refusals were 0 in every run. Alternate eye falls back for the session after 30 consecutive view-count mismatches; DLSS parks the session on FSR 3 after any NGX failure, including a pending feature waiting more than 240 dispatches. With the user's nvngx_dlss.dll (310.9.16, NVIDIA-signed) in the dev setup, DLSS failed on its FIRST evaluate in every session (NGX FAIL_InvalidParameter: output subrects not enabled at creation) and the sticky fallback parked it on FSR 3, so DLSS had never run. Fixed (fad3704). After the fix no transient failure occurred (idle, menu pages, sword swing), so no retry policy was added. | DLSS active with 0 evaluate failures / 0 fallbacks across 9 runs, both eyes correct, AFW 0 refusals. |
| H. Normal-play diagnostics | Already present | Diagnostics off by default writes only a ~100-byte status once per second; per-draw tracing exits early; read-self profiling, census and the new knobs are opt-in. The submission diagnostic does not exist here. | Code review; diagnostics-off runs measured. |
| Rejected experiments (layout cache, matrix-factor cache, snapshot reuse, single-pass stereo, VRS, async submission) | Not ported | Not accepted results in the reference. | n/a |

## DLSS after the fix: performance (simulator, AFW, uncapped engine Presents/s)

| Eye size | FSR 4 Quality (user's mode; FSR 3 Quality 77% on NVIDIA) | DLSS Quality (67%) | DLAA (100%) |
|---|---|---|---|
| 2048 x 2048 | 160.7 / 161.9 | 153.9 / 151.3 | 159.0 / 158.7 |
| 4096 x 4096 | 91.8 | 88.6 | 74.2 |

At 2048 the engine CPU (~5.4 ms per eye-frame) is the limit: DLAA at full resolution matches FSR 3 at 77%. At 4096 the GPU binds, DLSS Quality
is 19% faster than DLAA, but its evaluate costs slightly more than FSR 3's at 77%. DLSS is therefore an image-quality option on this GPU
(RTX-class, device 0x2B85), not a frame-rate gain over the user's current FSR 3 Quality.

## Correctness runs (v1300, simulator)

swing (moving hands, sword), guidepages (VR menu and guide pages), pistol, idle: gameplay reached, alternate eye on with 0 fallbacks,
AFW 0 refusals, Present p95 11.2-11.3 ms at the 90 cap. The two FAIL lines in these runs (injection cleanup timeout; HUD edge
coverage 0.022-0.023 on swing) are identical on v1299. Not verified: death-and-reload (its 70 s script cannot fit the harness's 120 s cap
with a lead long enough to load), renderer switches through the VR menu, real headset presentation, comfort and latency.

## Remaining bottlenecks and risks

- Uncapped in this scene the engine main thread is the limit (~5.7 ms per eye-frame); the mod's own share of its samples is now ~5%.
  The rest is the engine itself.
- Engine limiter vs headset refresh in AFW mode: at 120 Hz (PSVR2's 120 Hz mode) the 90 cap would bind. A follow-up could set the
  limiter to the runtime's display rate in alternate/AFW mode only (in memory, restoring the user's value, never writing the config),
  but it needs headset pacing validation.
- Session-permanent fallbacks (alternate eye on view-count mismatch, DLSS on any NGX failure) have no recovery. DLSS's systematic
  failure is fixed; a bounded, settled-gameplay retry is worth adding once a transient failure is reproduced.
- A simulator result is not a headset result; it does not establish stable 90 (or 120) FPS in crowded scenes.
