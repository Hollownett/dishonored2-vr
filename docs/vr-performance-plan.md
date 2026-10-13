# VR performance plan

Evidence: `artifacts/runs/*/xr-frame-timing.csv` and `present-boundaries.csv` (ad hoc Python over the CSV columns; `tools/analyze_frame_timing.py <run>` gives the same family of numbers). Real headset runs are the ones with pace >= 1 period and nonzero `wait_cpu_ns`: v233 (FSR Quality 0.77, 14.4k gameplay frames), v300 (16k, includes the governor's descent to 0.60), v323, v249, v247. Most other recent runs are simulated XR (pace about 0.5 period) and say nothing about headset timing. The display period is 11.111 ms (90 Hz) throughout. Only `fsr_gpu_*` are GPU timestamps; everything else is CPU wall time, including driver blocking.

## 1. Where the frame time goes (v233, FSR Quality 0.77, gameplay)

| Stage | Mean | Notes |
|---|---|---|
| Frame interval (present to present) | 12.28 ms = 81.5 fps (p50 11.8, p95 15.7, p99 18.7) | target 11.11 ms |
| Missed slots (`display_time` advanced by more than 1.5 periods) | 2.6% of frames | 97.3% advance one slot, 2.5% two |
| Engine, outside our Present hook (`engine_cpu_gap_ns`) | 9.4 ms (p50 8.1, p95 13.8) | game CPU plus GPU back-pressure; about 76% of the interval |
| Inside our Present hook (`present_total_cpu_ns`) | 3.9 ms; 3.3 ms excluding xrWaitFrame (p95 7.2) | serial on the game's render thread |
| of which: desktop mirror Present | 1.5 ms mean, p50 0.1, p95 5.8 | `desktop_sync_interval=0`, so this is driver queueing, not vsync |
| of which: logic discovery / pre-XR / post-XR | 0.51 / 0.49 / 0.36 ms | |
| of which: xrEndFrame submit | 0.73 ms, p95 7.2 | spikes are runtime/compositor back-pressure |
| of which: xrWaitFrame | 0.56 ms, p50 0 | the runtime rarely throttles us: the app is late, not early |
| of which: controls, lean, grab, housekeeping, GPU-hook maintenance | about 0.4 ms total | |
| FSR4 GPU, both eyes | 3.55 ms (1.80 + 1.75) | 32% of the frame budget, serial with game GPU work |

Summary: the engine's CPU+GPU path is the largest share. The mod holds about 3.3 ms of render-thread time per frame, and FSR4 adds about 3.5 ms GPU. The compositor/Virtual Desktop stage is not directly visible. Its footprint is the p95 submit spikes and the two-slot advances. In v300 at native 1.0 without AA (taa=0) the work was 0.55-0.9 of a period with almost no misses. FSR Quality at 0.60 was 0.78-0.95. Those are different scenes, so treat this as a lead for an A/B test, not a result. The FSR path's fixed costs (3.5 ms GPU, copies, two contexts) may outweigh the pixel saving.

## 2. Finding: every render-fraction change costs a ~0.5 s stall

v300, frame by frame after entering gameplay at 0.77: the governor stepped to 0.65 (frame 49) and then 0.60 (frame 98). Each change is followed by a 471 ms / 561 ms frame and a display-time jump of 33-40 slots. The cause is in `src/fsr_taa.hpp` (~lines 200-237): when `fraction` changes, both eyes' FSR3 and FSR4 upscalers are destroyed and recreated. `Fsr4EyeUpscaler::create` (`src/fsr4_eye_upscaler.hpp` ~145) keys the context on the exact render size, and each rebuild does LoadLibrary, Authenticode verification, `D3D12CreateDevice`, `ffxCreateContext`, a queue, a shared fence and shared textures. Dispatch (~231) rejects any size other than the creation size.

Consequences: any dynamic resolution freezes the view for about half a second per step (comfort problem). The post-stall frame was counted as a missed slot, which fed further down steps (a spiral to the floor). The governor now ignores `settle` (30) samples after each change; this fixes the spiral but not the freeze.

## 3. Ranked optimizations

| # | Change | Expected gain | Risk |
|---|---|---|---|
| 1 | **Make fraction changes cheap**: create the FSR3/FSR4 contexts and their textures once at the maximum render size (`max_scale` x display), pass the current size per dispatch (`ffxDispatchDescUpscale.renderSize`; `maxRenderSize` is a maximum by FfxAPI design), and copy only the rw x rh region. In `fsr_taa.hpp`, key `ready_*` on the max fraction and carry `render_w/h` per job; in `fsr4_eye_upscaler.hpp` ~231 change `rw!=s.desc.render_w` to `rw>s.desc.render_w`. | Removes the 0.5 s freeze per step; prerequisite for any governor | Medium: GPU validation of sub-rect copies, depth/reactive passes and history reset |
| 2 | Dynamic resolution governor (`src/vr_perf_governor.hpp`, integrated behind `build/governor`) | Holds 90 Hz in heavy scenes by trading resolution; at 0.77 the work was 1.05-1.10 of a period, so down steps are warranted | Low once #1 lands; until then each step freezes |
| 3 | Cut render-thread mod time (3.3 ms) | Desktop mirror Present 1.5 ms mean (p95 5.8): mirror off or single eye during gameplay. Logic discovery 0.5 ms: run every Nth frame. | Low (mirror is already a setting) |
| 4 | FSR4 single D3D12 submission for both eyes (one Wait, one Execute with two lists, one Signal; today it is Signal/Wait/Execute/Signal/Wait/Signal per eye, `fsr4_eye_upscaler.hpp` ~291-300) | 0.1-0.4 ms CPU and one fewer cross-API sync; GPU time unchanged | Medium |
| 5 | A/B the FSR path against native + cheap AA in the same scene | Possibly several ms; see the v300 note above | Low (measurement only) |
| 6 | Cvar presets (shadows, volumetrics) | Unknown: `native_cvars.hpp` notes that shadow/tessellation cvars showed no change; SSAO is already off (-15%) | Medium; measure first |
| 7 | Fixed foveated scaling | Engine exposes a single scalar fraction (`rs_forceFractionX/Y`); not feasible without render-pass ownership | High |

## 4. Governor (`src/vr_perf_governor.hpp`)

- `feed({work_ns, period_ns, missed})` once per submitted gameplay frame. The field is `pace_ns` but holds frame work: present-to-present minus xrWaitFrame. Raw pace is display-locked (always >= 1 period) and cannot express headroom.
- `missed_slot(display, prev_display, period)` flags an advance of more than 1.5 periods.
- Every 45 frames: step down 0.06 (0.12 if the miss rate exceeds 3x `miss_high`) when the miss rate exceeds 4%. Busy alone never steps down (headset v307: display-locked frames always look busy). Step up 0.02 after 4 calm windows (miss rate <= 0.5% and work below `busy_low`), never within 2 windows of a down step. Output is quantised to 0.01 and clamped to [min, max]. Stalls over 250 ms are dropped, and `settle` samples after each change are ignored.
- `apply_upscale` coerces f < 0.5 or f >= 0.999 to 1 (FSR off), so keep min >= 0.5 and max < 0.999.
- Test: `tests/vr_perf_governor_check.cpp`, including a settle regression case (it fails with `settle=0`).

## 5. Integration (already present in `src/dxgi_probe.cpp`, owned by the main session)

- State and `reset_perf_governor(mode)` next to `aa_mode_fraction`: active only in scaled FSR modes; start = mode fraction, max = min(0.95, f + 0.15), min = min(0.6, f). Enabled by the `build/governor` knob file.
- Feed in the `if(game_xr->timing.waited)` block before `frame_timing_record.xr=`: work = (start - previous start) - wait; then `upscale_fraction=perf_governor.scale()`, committed by the existing 100 ms `apply_upscale` tick.
- Status JSON: `perf_governor {active, scale, changes}`.
- Remaining: land #1 before enabling the governor by default. Until then, consider `step_down=.1`, so fewer steps (and freezes) reach a sustainable fraction. Log `settle` drops if headset tuning needs them.

Headset visual quality and comfort of resolution changes are not established by these checks.
