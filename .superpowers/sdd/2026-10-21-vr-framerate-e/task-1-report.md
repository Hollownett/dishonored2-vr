# Plan E Task 1 report: frame split telemetry

## Changes
- src/frame_split.hpp (new): FrameStamps, FrameSplit (900-frame ring, feed O(1), sorted copies only in json()). Keys as in the brief.
  Stamps are ticks of one clock with an explicit freq. Missing: eye0 <0, others <=0, prev_present_enter 0 (the brief's test uses eye0 at 0, so eye0 uses <0 as "missing"; the probe passes -1).
  Eye frames that are missing or out of order count in split_skipped; their interval and hook samples still count. gpu_busy = p50 of gpu_elapsed/interval, null if no frame has both.
- src/dxgi_probe.cpp:
  - eye_begin_hook stamps eye 0/1 (index -1 ignored) under frame_split_mutex.
  - Present hook: enter = existing present_start_ns; exit stamped after the desktop Present; one feed per gameplay Present. Stamps are GameXr::cpu_time_ns (steady_clock ns, freq 1e9) rather than raw QPC, the same clock as the existing timing.
  - GPU elapsed = native_gpu_summary.last_elapsed_ms when native timing is on, else 0.
  - "frame_split" status block = {adapter, split}. frame_split_status_json() samples thread clocks at most once per second (own throttle, since status is written from several places).
  - ThreadBusyClock: OpenThread(THREAD_QUERY_LIMITED_INFORMATION) once per thread id; render = xr_present_thread, game = new NativeInteractionAim::game_thread_id() (recorded in the Stage0 stage<> hook); -1 -> null on first sample, no handle, or zero CPU delta across >=30 frames.
  - Adapter: no existing adapter field, so written once from chain->GetDevice->GetAdapter->GetDesc (name, vendor, vram_mb); null on failure.
  - Review minor 1: non-gameplay branch of the Present timing now resets perf_prev_start_ns, perf_prev_display and the split previous-present stamp; reset_perf_governor also resets the split stamp.
- src/perf_meter.hpp comment and docs/VR-PERF-LAPTOP.md: mod_cpu documented as "to XR submit" (key name unchanged), plus a frame_split section.
- tests/frame_split_check.cpp + tools/build-msvc.cmd registration beside perf_meter_check.

## Tests
- Full harness: 533 passed / 1 failed (fsr_taa_gpu_check --fsr4, env-only) / 27 skipped (baseline 531/1/27 + 2 new rows). DLL builds clean (only the existing world_ui_installed warning).
- Cases: split_known_values, missing_eye_frame_skipped, eye0 -1, out_of_order_skipped, gpu_busy null/non-null, coarse thread null (both threads), window_rolls_over (900), first-frame-after-gap (no interval).

## Mutation
- eye1_share numerator swapped to eye0 p50: frame_split_check failed (assert on eye1_share). Restored by copy; green again.

## Self-review / concerns
- Not run in the game (no sim run per instructions); thread busy values and adapter capture are untested at runtime.
- If xr timing.waited is false (no XR submit) the gap reset does not run, only when the frame is non-gameplay or display_period is 0.
- game_thread_id is the last thread that ran Stage0; if Stage0 ever runs on several threads it flips between them.
- feed uses the Present-thread clock for everything; eye stamps come from eye_begin_hook, assumed to run on the render thread before Present of the same frame.

## Fix round 1
- I1: any Present that does not feed (no XR wait/submit, menu, load, error path; DXGI_PRESENT_TEST excluded) now clears frame_split_prev_enter_ns, perf_prev_start_ns and perf_prev_display in the Present exit block. The in-branch else is kept. Early-return Presents (before present_start_ns) still do not clear.
- I2: FrameSplit accepts eye samples only when prev != 0 and prev < eye0 <= eye1 <= enter; otherwise split_skipped, interval/hook still counted. New tests: stale_eye0_skipped, multi_frame_percentiles (intervals 10..19 ms, p50 14, p95 19, one stale-eye frame, gpu_busy, hook), FrameSplitTrace row format.
- Trace: new FrameSplitTrace (fixed 4096 ring, 20000-row cap) in frame_split.hpp, knob build/frame-split-trace -> native-frame-split-trace.csv (frame,eye0,eye1,enter,exit,prev,accepted). Rows are queued on the render thread under the lock; the status writer formats and writes them. CarryTraceWriter is carry-specific, so not reused. Documented in docs/VR-PERF-LAPTOP.md. Not yet added to tools/default_features.json (that table is plan C; not touched).
- Minor 1: frame_split_prev_enter_ns is std::atomic. Minor 2: status copies FrameSplit under the lock and sorts outside it. Minor 3: game thread id recorded only in stage<0> (stage0_thread_id). Minor 6: header comment fixed. Minor 4: gpu_busy documented as native lists only, lagged 1-3 frames, lower bound.
- Harness: 533 passed / 1 failed (fsr_taa_gpu_check --fsr4, env-only) / 27 skipped.
- Concern: runtime check of accepted rate is left to Task 3 via the trace; the frame_split_trace knob is read at StartProbe only.
