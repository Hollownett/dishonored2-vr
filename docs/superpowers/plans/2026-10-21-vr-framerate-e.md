# VR Frame Rate (Plan E) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reach at least 72 fps, stable, on 8 GB-class GPUs (the RTX 3070 laptop) and 90 fps as the baseline on a gaming PC. Measure first, then cut the cost that the measurements show dominates.

**Architecture:**
- The user sees 30-45 fps on 8, 12 and 16 GB GPUs. Since the frame rate barely changes with the GPU, the bound is probably the engine and CPU: the engine prepares and renders two full views per frame.
- Tasks 1-3 build the tooling that separates CPU from GPU time, per eye and per thread, and run the measurement campaign on the laptop sim rig.
- Task 4 cuts the mod's own render-thread cost.
- Task 5 is a decision gate, ruled on numbers. If the second eye costs at least 40% of the engine frame and the frame is still over budget, the controller writes the alternate-eye-rendering (AER) plan from findings §5 and the Task 3 data. Otherwise plan E closes with plan D's GPU-side tasks.
- Task 6 is the headset proof.

**Tech Stack:** C++17 (MSVC and clang-cl), D3D11, OpenXR, Python 3 (the sim rig and the reports).

**Spec:**
- The user, 2026-10-21 10:39: "performance increases very poorly on 8gb, 12 gb and 16gb gpus. I got from 30 - 45 fps wich is not acceptable for VR, I need atleast 72 stable fps, 90 fps should be a base line for a gaming pc."
- Evidence and options: `.superpowers/sdd/plan-d-intake/framerate-findings.md`, cited below as "findings §N".

**Order with plan D:** D6 (perf block) → E1 → E2 → E3 → D7 → D8 → E4 → D9 → E3 re-run → E5 gate → (AER plan or close) → D10 → E6 together with D11.

## Global Constraints

- All of plan D's Global Constraints apply (`.superpowers/sdd/2026-10-20-comfort-performance-d/global-constraints.md`): the worktree and branch, the pinned exe SHA-256, no Bonelab material, a restore knob per behaviour, the harness and baseline, the sim rig rules, testing honesty, and the commit trailer.
- "72 stable" means: in the headset, `perf.missed_fraction` ≤ 0.01 and `perf.pace_ms_p95` ≤ 13.9 at 72 Hz over a 90 s gameplay window. 90 Hz uses the same rule with 11.1 ms.
- Sim-rig numbers are throughput without a compositor. They decide the gate and compare before against after; they are not headset fps. Only Task 6 declares the target met.
- No new engine RVA in tasks 1-4. Task 5's AER plan may add RVAs; that plan does its own Ghidra work.
- The sim rig only runs with the screen unlocked, with the game and SteamVR closed, and through `--candidate-dll`. The user's install, saves and knobs are restored and verified after every run.
- No more than 10 sim runs per task, each 120 s or less.

## Review Focus

1. **Hybrid laptop GPU selection:** a sim run where the game landed on the AMD iGPU instead of the NVIDIA GPU gives meaningless numbers. Expected: the report names the adapter, and the verdict refuses non-NVIDIA runs on this laptop (Task 2 test `rejects_wrong_adapter`).
2. **The screen locks mid-campaign:** runs stall at the main menu (`logic_mask` 64). Expected: the bound report marks a run with no gameplay frames as invalid; it is never averaged in (Task 2 test `run_without_gameplay_is_invalid`).
3. **Thread CPU counters under Wine:** `GetThreadTimes` may return coarse (15.6 ms) or zero deltas. Expected: when a sample window's delta is 0 across 30 or more frames, the busy fraction is reported as `null` rather than 0 (Task 1 test `coarse_thread_clock_reports_null`).
4. **Eye-begin hooks out of order or missing** (GUI view index -1, a dropped right eye): the per-eye split must not produce negative or huge spans. Expected: the frame is skipped and counted in `split_skipped` (Task 1 test `missing_eye_frame_skipped`).
5. **The knob leaks into the user's real game after a crashed run:** Expected: `--knob` files go through the rig's `FileGuard` and the dev-knob cleanup (Task 2 test `knob_removed_after_failed_run`).

---

## Task 1: Frame split telemetry (CPU per eye, per thread, GPU busy)

**Files:** Create `src/frame_split.hpp` and `tests/frame_split_check.cpp`. Modify `src/dxgi_probe.cpp`:
- `eye_begin_hook` (~1079): add QPC stamps.
- The Present hook entry and exit: add stamps.
- The thread-time sampling in the status writer.
- A `"frame_split"` status block next to `"perf"`.

Also modify `tools/build-msvc.cmd`.

**Interfaces:**
- Consumes: plan D Task 6 `PerfMeter` and its `"perf"` block (unchanged). The `build/native-gpu-timing` knob output `native_gpu_elapsed_ns` (findings §2).
- Produces:
  - `struct FrameStamps {std::int64_t eye0_begin, eye1_begin, present_enter, present_exit;}` holds QPC ticks; 0 means missing.
  - `class FrameSplit { void feed(const FrameStamps&, std::int64_t prev_present_enter, std::int64_t qpc_freq, std::uint64_t gpu_elapsed_ns) noexcept; void thread_sample(double render_busy, double game_busy) noexcept; std::string json() const; }`
  - The 900-frame ring is the same as `PerfMeter`'s.
  - `json()` keys:
    - `interval_ms_p50` and `interval_ms_p95`: present enter to present enter.
    - `eye0_ms_p50`: eye0 begin to eye1 begin.
    - `eye1_ms_p50`: eye1 begin to present enter.
    - `hook_ms_p50`: present enter to present exit.
    - `eye1_share`: the eye1_ms p50 divided by the interval p50.
    - `gpu_busy`: gpu_elapsed over the interval, as a p50. It is `null` when gpu_elapsed is 0 for every frame.
    - `render_thread_busy` and `game_thread_busy`: the latest window. Each is `null` when the window is too coarse.
    - `frames` and `split_skipped`.
  - Status block `"adapter":{"name":...,"vendor":...,"vram_mb":...}` from the swap chain's `IDXGIAdapter::GetDesc` (written once). Reuse an existing adapter field if `grep -n adapter src/dxgi_probe.cpp` finds one.
  - Thread busy fraction: the `GetThreadTimes` kernel+user delta divided by the wall delta, sampled once per status write (about 1 s).
    - The render thread is the Present thread.
    - The game thread is the thread id recorded by the existing game-thread Stage0 hook (`grep -n Stage0 src/dxgi_probe.cpp`).
    - Open its handle once with `THREAD_QUERY_LIMITED_INFORMATION`.

- [ ] **Step 1: Failing tests** in `tests/frame_split_check.cpp`:
  - `split_known_values`: freq 1e7; eye0 at 0, eye1 at 60000, present_enter at 110000, exit at 125000, previous present_enter at -30000. Expect eye0 6.0 ms, eye1 5.0 ms, hook 1.5 ms, interval 14.0 ms, and `eye1_share` 5/14 ± 1e-6.
  - `missing_eye_frame_skipped`: eye1_begin=0, which gives `split_skipped`=1, no eye samples, and the interval still counted.
  - `out_of_order_skipped`: eye1 < eye0, which is skipped.
  - `gpu_busy_null_without_timer`: gpu 0 on every frame, so the JSON contains `"gpu_busy":null`.
  - `coarse_thread_clock_reports_null`: `thread_sample(-1, .5)` gives `"render_thread_busy":null`. The caller passes -1 when the window's CPU delta is 0 across ≥ 30 frames.
  - `window_rolls_over`: 1000 feeds, so `frames` is 900.
- [ ] **Step 2: Run, see fail.** Use `--only 'frame_split'`.
- [ ] **Step 3: Implement** `frame_split.hpp` (header-only, no allocation per feed; percentiles from a sorted copy in `json()` only). Wire the stamps:
  - eye 0 and eye 1 in `eye_begin_hook`; ignore index -1;
  - enter and exit in the Present hook;
  - one `feed` per Present;
  - `thread_sample` in the status writer.
  - Guard everything with a mutex, as `comfort_mutex` does. Use unique local names (-Wshadow is fatal).
- [ ] **Step 4: Green.** Run the full harness: no regression from 528/1(env)/27, plus the new check.
- [ ] **Step 5: Mutation.** Swap eye0 and eye1 in `eye1_share`; `split_known_values` must fail. Restore by copy.
- [ ] **Step 6: Commit** `feat: frame split telemetry (per-eye CPU, thread busy, GPU busy)`.

## Task 2: Sim rig knobs, probe modes and the bound report

**Files:**
- Modify `linux/d2vr_linux/sim.py`, `linux/d2vr_linux/cli.py`, `linux/tests/test_sim.py` and `linux/README.md`.
- Create `tools/perf_bound_report.py` and `tests/perf_bound_report_check.py`, registered like `perf_run_report_check.py`.

**Interfaces:**
- `d2vr-linux sim --knob NAME` (repeatable) writes an empty `build/NAME` through the rig's `FileGuard`. `NAME` must match `^[a-z0-9-]+$`. The file is removed with the other dev knobs whatever the outcome, and `--dry-run` lists it.
- `d2vr-linux sim --probe-mode xr|paired|single` (default `xr`):
  - `paired` and `single` set the existing diagnostic stereo modes 3 and 2 (`dxgi_probe.cpp:7553-7591`) in the rig's override config instead of the XR mode. Both are flat, with no XR.
  - Used only for measurement B (findings §3). If mode 2 fails under Proton, the rig reports it and the campaign records "B: not measurable".
- `d2vr-linux sim --vr-setting KEY=VALUE` (repeatable; keys only `temporal_aa`, `resolution`, `mirror`, with values validated like `vr_preferences.hpp` parses them) makes the run use those VR settings. Today `sim_probe_config` forces `saved_settings 0`, so `vr-settings.cfg` is never read (plan D Task 6 finding). Either write a run-only settings file through `FileGuard` and let the override enable it, or pass the values through the override config, whichever the probe already supports (`grep -n saved_settings src/dxgi_probe.cpp`). The user's own `vr-settings.cfg` is never edited. Test: `vr_setting_applied_and_user_cfg_untouched`.
- `--env KEY=VALUE` (repeatable; allowed keys only `DXVK_HUD`, `DXVK_LOG_LEVEL`, `__GL_THREADED_OPTIMIZATIONS`) is added to the launch environment.
- Every run's artifacts include `adapter.txt`, copied from the status block `"adapter"` (Task 1).
- `tools/perf_bound_report.py <run-dir> [<run-dir> ...] [--before <json>]` prints one row per run, then a verdict line. Each row shows `mode`, `adapter`, `interval p50/p95`, `eye0/eye1 ms`, `eye1_share`, `hook ms`, `gpu_busy`, and `render/game thread busy`.
  - The verdict is `GPU-BOUND` when gpu_busy ≥ .90.
  - It is `CPU-BOUND(render)` or `CPU-BOUND(game)` when gpu_busy < .70 and that thread's busy is ≥ .85.
  - Otherwise it is `MIXED`.
  - A run with `gameplay` false, or with 0 `frame_split.frames`, is `INVALID` and excluded.
  - On this laptop, an adapter whose name lacks "NVIDIA" is `INVALID (wrong adapter)`.
  - `--json` writes the table for `--before` comparisons.

- [ ] **Step 1: Failing tests:**
  - Linux: `knob_written_and_removed`, `knob_removed_after_failed_run`, `knob_name_rejected` (`../x`), `probe_mode_paired_sets_flag_3`, `probe_mode_single_sets_flag_2`, `env_key_not_allowed_refused`. Every test injects the fake launcher; the real launcher guard stays.
  - Python check: `verdict_gpu_bound` (gpu .95), `verdict_cpu_render` (gpu .5, render .92), `verdict_cpu_game` (gpu .5, game .9, render .6), `verdict_mixed`, `run_without_gameplay_is_invalid`, `rejects_wrong_adapter` ("AMD Radeon(TM) Graphics"), `before_after_delta`.
- [ ] **Step 2: Implement.** Make the tests green. Run the full Linux suite (175 OK before) and `--only 'perf_bound_report'`.
- [ ] **Step 3: Commit** `feat(linux): sim knobs, probe modes, env; perf bound report`.

## Task 3: Measurement campaign on the laptop (the decision data)

**Files:** `docs/VR-PERF-LAPTOP.md` gets a new section, "Bound analysis (plan E)". No code changes.

**Conditions:**
- Mains power, with asusd in Performance (`asusctl profile -p`; record it).
- The NVIDIA GPU is free (`nvidia-smi` shows no other compute process).
- The screen is unlocked; the game and SteamVR are closed.
- The candidate is built from HEAD under the installed DLL name.
- Scenario `oil_tank_drop` (moving), `--lead 28 --seconds 120`.

- [ ] **Step 1: Runs.** Each run gets one row from `perf_bound_report.py`:
  - A1: `--knob native-gpu-timing`, xr mode, today's defaults (FSR 3 Native, both-eye mirror).
  - A2: A1 plus `--env DXVK_HUD=fps,frametimes,gpuload,drawcalls,submissions` (screenshot `gpuload` and `drawcalls` from the rig's capture, if any).
  - B1: `--probe-mode paired`. B2: `--probe-mode single`, same scenario. Record interval p50, and the B2/B1 ratio as the per-view share.
  - C: `--knob render-perf`, xr mode. Per-eye GPU from `render-perf-gpu.csv`, summarised as eye 0 vs eye 1 GPU ms.
  - E: the mod-cost control: A1 plus mirror one eye (if plan D Task 8 has landed; otherwise "not measurable yet") and FSR 3 Quality via the rig's guarded `vr-settings.cfg` override. If the rig has no such override, record "not measured" instead of editing the user's config.
- [ ] **Step 2: Write the analysis:**
  - the verdict per run;
  - eye1_share;
  - the B2/B1 ratio;
  - the thread that is the long pole;
  - the mod's share (hook ms over interval);
  - the frame budget gap to 13.9 ms;
  - one paragraph, "What the data says". Every claim cites a run row.
- [ ] **Step 3: Gate inputs.** Write the three numbers Task 5 needs:
  - `engine_ms` = interval p50 − hook p50;
  - `eye2_share` = 1 − B2/B1 (fallback eye1_share);
  - `over_budget` = interval p50 > 13.9.
- [ ] **Step 4: Verify** that the installed DLL hash is unchanged and that no pending marker or knob is left. Commit `docs: plan E bound analysis on the laptop`.

## Task 4: Cut the mod's own render-thread cost

**Files:**
- Modify `src/dxgi_probe.cpp`: discovery cadence (`CpuSpan discovery_span` ~3112), the mirror path.
- Modify `src/desktop_mirror.hpp` and `src/vr_preferences.hpp`.
- Create `src/discovery_cadence.hpp` and `tests/discovery_cadence_check.cpp`.
- Update docs.

**Interfaces:**
- `class DiscoveryCadence { bool due(std::uint64_t frame, bool stable) noexcept; void invalidate() noexcept; }`:
  - Every frame until 120 consecutive stable frames.
  - Then every 8th frame.
  - Back to every frame after `invalidate()` (level load, epoch change, a failed validation).
  - Knob `no-discovery-throttle`.
- `MirrorMode` gains `off=3` (the desktop window gets no copy and no Present, but the window message pump stays alive). The launcher and `vr-settings.cfg` accept it; the default stays as plan D Task 8 sets it.
- Measure with Task 3's A1 setup. A change ships only if `hook_ms_p50` drops by ≥ 0.2 ms in two runs. Otherwise, revert it and record the result.

- [ ] **Step 1: Failing tests:** `every_frame_until_stable`, `throttles_after_120_stable`, `invalidate_returns_to_every_frame`, `unstable_frame_resets_count`. In the existing `vr_preferences` check: `mirror_off_round_trips`.
- [ ] **Step 2: Implement and wire.** Make the tests green, then run the full harness.
- [ ] **Step 3: Measure** before and after (≤ 4 runs) and add the rows to `docs/VR-PERF-LAPTOP.md`.
- [ ] **Step 4: Mutation.** With `throttles_after_120_stable` set to fire at 1, a test must fail. Restore. Commit `perf: throttle discovery once stable; mirror off option`.

## Task 5: Decision gate: alternate-eye rendering or close (controller ruling)

**Files:** `.superpowers/sdd/2026-10-21-vr-framerate-e/progress.md` (the ruling). If the gate passes, also `docs/superpowers/plans/2026-10-2x-alternate-eye-f.md`.

- [ ] **Step 1: Re-run** Task 3 A1 on HEAD, after D7, D8, E4 and D9 have landed.
- [ ] **Step 2: Rule.**
  - **The gate passes** when `over_budget` holds on the re-run AND `eye2_share` ≥ 0.40 AND the verdict is CPU-BOUND(render) or MIXED. Then write plan F, "alternate-eye rendering as a knob-gated performance mode", from findings §5. Its scope:
    - explicit eye parity;
    - per-eye staleness in `GameXr::present` (`rendered_mask_` per eye with a max age of 1 frame);
    - first-view-of-frame keying for every `eye==0` site;
    - a per-eye jitter counter;
    - per-eye previous matrices;
    - a persistent per-eye image;
    - knob `alternate-eye` (opt-in), sim-rig proof first, then a headset comfort check.
    Execute plan F with the same SDD workflow.
  - **The verdict is CPU-BOUND(game):** AER cannot help. Write the finding and propose sim-side options to the user (cvars for AI and physics density, if any).
  - **Otherwise** (GPU-bound or under budget): close plan E with plan D's GPU tasks and the governor. Record why.
- [ ] **Step 3: Tell the user** the ruling and its numbers in plain words.

## Task 6: Headset proof (72 on the laptop; 90 on a gaming PC)

**Files:** `docs/VR-PERF-LAPTOP.md` (a headset section) and the plan D Task 11 checklist (rows added).

- [ ] **Step 1: Checklist rows for the user.** Steam Link at 72 Hz, then 90 Hz. Play 90 s of the same mission area. The controller reads `xr-live-status.json` afterwards with `tools/perf_run_report.py`. Pass at 72 Hz: `missed_fraction` ≤ 0.01 and `pace_ms_p95` ≤ 13.9. Pass at 90 Hz: `missed_fraction` ≤ 0.01 and `pace_ms_p95` ≤ 11.1.
- [ ] **Step 2: Gaming PC row.** The user runs the same 90 s on their 12 or 16 GB machine and sends `xr-live-status.json`. The controller reports it against the 90 Hz rule.
- [ ] **Step 3:** Record pass or fail per machine. A fail reopens Task 5 with the headset numbers.
