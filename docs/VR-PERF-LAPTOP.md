# VR performance on the laptop (sim rig numbers)

Sim-rig numbers are GPU and CPU cost without a real HMD. They are valid for before/after comparison on the same scenario and
save, not for absolute headset fps.

## Comfort baseline

Command: `linux/d2vr-linux sim --scenario idle --lead 28 --seconds 120 --candidate-dll <current build, named like the installed DLL> --status-field comfort`
(the 120 s session cap leaves 28 s of lead-in for 90 s of idle; the save may still be loading when the lead ends).

Run 2026-10-21 08:22 (fix round 1 rig; candidate = Task 1 source built under the installed name d2vr_render_probe_arm_v600.dll, sha256
860971e56ada5c7764490952a1161092045e2305931c78ff251d34c073c038b7). Result OK, gameplay true. An earlier attempt at 07:57 had been
refused by Steam (`AppError_5 "FamilySharing"`); after the account switch it launched.

`comfort` block (the plan-D "before" numbers):

```
{"authored_ms":0,"authored_entries":0,"anchor_v_peak":0,"anchor_jerk_events":0,"view_yaw_rate_peak":1.99371e-05,
 "view_yaw_events":0,"unrequested_yaw_rate_peak":0,"unrequested_yaw_events":0,"samples":6328,"skipped":0}
```

Reading these numbers: `view_yaw_*` measures the engine anchor (body) yaw. Scripted HMD yaw does not rotate the anchor, so
about 0 is the correct reading while standing still; it is not a sign that the scripted yaw failed. The idle scenario therefore
cannot show wobble. Later before/after comparisons of the comfort meter need a scenario where the player moves (oil_tank_drop at
least); idle is only good for the cost (GPU/CPU) baseline and as a floor.

## Comfort changes (plan D)

| Task | Change | Sim result (idle) |
| --- | --- | --- |
| 2 | The headset owns view rotation in authored scenes (`authored_view.hpp`; knob `legacy-authored-camera`). Status: `comfort.authored_snaps`. | NOT MEASURED. 2026-10-21 08:54-09:04 (two candidate runs, one control run with the unchanged installed DLL): all ended `StartProbe returned 4` because the GNOME screen lock was active (since 08:30), which throttles the game window to about 1 fps, so the menu Enter presses never landed. `logic_mask` 64 means Main Menu, not loading; the save was never selected. The control failing the same way ruled out this change. `comfort` stayed at `samples:0`. The installed DLL's SHA-256 was unchanged and no restore marker remained. `d2vr-linux sim` now refuses to start while the session is locked. Retry with the session unlocked. |

Unit-level proof stands in until then: `tests/authored_view_check.cpp` (heading hold/snap, HMD-owned basis, IPD) and
`tests/authored_view_routing_check.py` (wiring and knob). An idle run cannot exercise an authored scene in any case; the
expected idle reading is `authored_ms` 0, `authored_snaps` 0.

## Performance baseline

This is the "before" for Tasks 7-10. Status: **baseline outstanding: session locked.** On 2026-10-21 the first sim run was refused
by the rig (`d2vr-linux: error: session is locked; unlock before running`), so no run was made and no number below is measured.
Nothing was started or changed: the installed DLL's SHA-256 stayed `ff0daf6566a62a44a31cef372a93114fc42790f2a13eb9887f3dfb3de63bc927`
and no restore marker exists.

What is measured: `xr-live-status.json` now carries a `"perf"` block (`src/perf_meter.hpp`), a rolling window of the last 900
gameplay frames: `pace_ms_p50`, `pace_ms_p95` (present-to-present), `missed_fraction`, `mod_cpu_ms_mean` and `mod_cpu_ms_p95`
(`mod_cpu` = time in our Present hook "to XR submit" only, i.e. up to the end of the XR submit, minus `xrWaitFrame`; the JSON key name is kept stable for `tools/perf_run_report.py`. The full-hook number is `frame_split.split.hook_ms_p50`), `fraction` (render fraction), `period_ms`, `frames`.
`python3 tools/perf_run_report.py <xr-live-status.json> [--before <earlier>]` prints the one-line summary and the delta.

Hardware: NVIDIA GeForce RTX 3070 Laptop GPU, 8192 MiB; AMD Ryzen 9 5900HX (8 cores / 16 threads), hybrid laptop.
Sim headset: the scripted XR backend, no real HMD. Its render resolution is to be recorded from the first run (the rig config
uses `resolution 1.0`, `mode 6`, `saved_settings 0`); the numbers are relative, for before/after comparison, not headset fps.

Procedure, with the session unlocked and the game and `vrserver` not running:

```
linux/d2vr-linux sim --scenario idle --lead 28 --seconds 120 --candidate-dll <dll built as d2vr_render_probe_arm_v600.dll> --status-field perf
linux/d2vr-linux sim --scenario oil_tank_drop --lead 28 --seconds 120 --candidate-dll <same> --status-field perf
```

There is no "walking" scenario; `oil_tank_drop` is the moving one (it needs a tank save for a meaningful run, see linux/README.md).

| Run | Scenario | p50 / p95 / missed% / mod cpu / fraction |
| --- | --- | --- |
| 1 | idle | NOT MEASURED (session locked) |
| 2 | oil_tank_drop | NOT MEASURED (session locked) |

A/B rows. The rig's guarded per-run override (`linux-probe-override.cfg`, see `sim_probe_config` in `linux/d2vr_linux/sim.py`) fixes
`saved_settings 0`, so `vr-settings.cfg` is not read, and it has no hook for the `governor` knob. None of these can be set without
editing the user's real files, so they are not measured even once the session is unlocked, unless the rig gains such an override:

| Row | Result |
| --- | --- |
| aa mode Native vs FSR 3 Quality vs Balanced | not measured: rig cannot set aa mode safely |
| mirror both eyes vs one eye | not measured: rig cannot set mirror safely |
| governor on vs off | not measured: rig cannot set the governor knob safely |

## Headset checklist (Task 11)

- [ ] Task 2: choke a guard from behind; turn your head during the animation; the view follows your head; no spin.
- [ ] Task 3: mantle onto a ledge and drop off a roof: no lag swoop and no pop. Also crouch and stand with the tracked arms on: arms keep their length.

## Frame split block (plan E task 1)

`xr-live-status.json` also carries `"frame_split":{"adapter":{name,vendor,vram_mb},"split":{...}}` over the same 900-frame gameplay window:
`interval_ms_p50/p95` (Present enter to Present enter), `eye0_ms_p50` (eye 0 begin to eye 1 begin), `eye1_ms_p50` (eye 1 begin to Present enter),
`hook_ms_p50` (Present enter to exit, the full hook including the desktop Present), `eye1_share` (eye1 p50 / interval p50), `gpu_busy`
(the SUM of the native command-list durations over the interval, p50; native command lists only, excluding FSR and immediate draws, from a timer that lags 1-3 frames, so read it as a lagged lower bound; `null` without the `native-gpu-timing` knob), `gpu_span`
(the same ratio from the elapsed span, first list start to last list end, which also counts idle gaps between lists, so `gpu_span >= gpu_busy`; diagnostic only, the verdict uses `gpu_busy`), `render_thread_busy` and `game_thread_busy`
(GetThreadTimes delta over wall delta, about 1 s window; `null` when the thread clock is coarse or unknown), `frames`, `split_skipped`
(cumulative; frames whose eye stamps were missing, out of order or stale, i.e. not `prev present < eye0 <= eye1 <= present`). The first gameplay frame after a menu or load feeds no interval sample.

Debug trace: create `build/frame-split-trace` before launch to get `native-frame-split-trace.csv` (columns `frame,eye0,eye1,enter,exit,prev,accepted,begins,idx_a,idx_b`; the last three are diagnostics: the number of non-null begin-view calls this frame and the raw index argument of the first and second one, -9 = none,
steady_clock ns; one row per fed frame, capped at 20000 rows, written by the status writer, not the render thread). Plan E task 3 checks that
`accepted` is 1 on nearly every frame and `eye1 < enter`; many 0 rows mean the eye hooks do not run between two Presents as assumed. The eye is named by view address against the published per-eye views (not by the raw index; `begins` should be 2 in paired XR gameplay).

## Bound analysis (plan E)

Measured 2026-10-21 on the sim rig (throughput without a compositor, not headset fps). Candidate: HEAD (9ad49cc) built as `d2vr_render_probe_arm_v601.dll`, run under the installed name; candidate SHA-256 `835ddbec32c458e8de39fadc48d631932287b6aad5441d88d75db64c3fd794d3`. The installed DLL (`ff0daf6566a62a44a31cef372a93114fc42790f2a13eb9887f3dfb3de63bc927`) was verified unchanged after every run, with no restore-pending marker and no rig knob left.

Conditions: NVIDIA GeForce RTX 3070 Laptop GPU, 8192 MiB, driver 615.71.09; AMD Ryzen 9 5900HX (8 cores, 16 threads); AC online; no other compute process on the GPU. **Deviation:** `powerprofilesctl get` and `asusctl profile get` both reported Balanced (asusd's AC profile is Performance, but it only applies on a power event), not Performance as the plan wants. I did not change it. All runs share this condition, so the comparison between rows holds, but the absolute numbers may be a little pessimistic. The mod home also held the user's own knobs `fsr4.off`, `carry-trace` and `full-body` in every run. Scenario `oil_tank_drop`, `--lead 28 --seconds 120`. All runs used the NVIDIA adapter.

| Run | Setup | Run dir | interval p50 / p95 (ms) | hook p50 | gpu_busy | render / game thread busy | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | xr, `native-gpu-timing`, `frame-split-trace`, FSR 3 Native, both-eye mirror | 20261021T120930 | 14.10 / 15.33 | 0.81 | 0.97 | 0.71 / 0.54 | GPU-BOUND |
| A2 | DXVK_HUD | not run | | | | | A2: HUD not captured (the rig takes no screenshot) |
| B1 | `--probe-mode paired` | 20261021T121243 | 7.54 / 9.42 (menu) | 4.52 | n/a | n/a | INVALID: StartProbe returned 4, never left the main menu (logic_mask 0) |
| B2 | `--probe-mode single` | 20261021T121602 | 3.95 / 6.76 (menu) | 2.52 | n/a | n/a | not measurable: started and exited 0, but stayed in the menu (logic_mask 0); the rig's MIXED verdict counts menu frames |
| C | xr, `render-perf` | 20261021T121908 | 14.57 / 15.77 | 0.85 | n/a (no timing knob) | 0.69 / n/a | MIXED (no gpu_busy) |
| E1 | A1 plus FSR 3 Quality (`temporal_aa=3`, 77% render scale) | 20261021T122223 | 14.63 / 15.85 | 0.78 | 0.96 | 0.68 / 0.53 | GPU-BOUND |
| E2 | A1 plus mirror one eye | not measurable yet | | | | | the mirror default changes in a later task |

Per-run perf block (`pace_ms`, same frames): A1 p50 14.09, p95 15.35, missed_fraction 0, mod_cpu mean 0.78 ms; C p50 14.59; E1 p50 14.64, fraction 0.77 (confirms the Quality setting was applied). Every xr row had `missed_fraction` 0 against a 11.1 ms period, because the sim has no compositor; ignore that field here.

### Correction: gpu_busy in the table above was the elapsed span

The `gpu_busy` column (0.97, 0.96) was computed from `native_gpu_elapsed_ns`, the span from the first list's start to the last list's end, which counts idle gaps between lists. The sum of the list durations (`native_gpu_list_sum_ns`, A1 median about 11.6 ms of a 14.10 ms interval, 0.82) is the busy figure, and `gpu_busy` is now that sum; the old value is reported as `gpu_span`. By the sum, A1 and E1 are about 0.8 busy, which the verdict rule calls MIXED (below .90, and the render thread at .71 is not saturated), not GPU-BOUND. Read "GPU-BOUND" and "the GPU fills 97%" below as span-based. The per-eye cause: the begin hook stamped by the raw index argument, and eye 0 was never stamped that way in paired XR; the eye is now named by view address (`begins`, `idx_a`, `idx_b` in the trace record the raw indices of the first two non-null begins, so one sim run with `frame-split-trace` shows what the engine passes).

### Per-eye numbers are not trustworthy

The A1 trace (`native-frame-split-trace.csv`, 6817 rows) has **accepted=1 on 0% of frames (0 of 6817)**. Every row has eye0 = -1 (the eye 0 begin hook was never stamped) and a valid eye1, so the frame-split rule `prev < eye0 <= eye1 <= present` fails on all of them. `split_skipped` is 6763 in A1, 6592 in E1 and 6680 in C. The `eye0_ms_p50`, `eye1_ms_p50` and `eye1_share` values (all 0) in A1, C and E1 are therefore empty, not measured zeros. This is below the 50% line: per-eye CPU numbers from this build cannot be used. Interval, hook, gpu_busy and thread busy do not depend on the eye stamps and are still usable.

Run C's `render-perf-gpu.csv` has no usable per-eye GPU time either: only 55 rows carry eye 0 (median 0.09 ms) and none carry eye 1. Its category 5 row (eye -1, median 13.8 ms, 6574 samples) matches the whole-frame GPU time that A1's gpu_busy gives (0.97 x 14.10 = 13.7 ms), so it is a per-frame total, not a per-eye one. Eye 0 against eye 1 GPU ms: not measured.

### The numbers

- **Verdict:** A1 and E1 are GPU-BOUND; C is MIXED only because it had no gpu_busy.
- **eye1_share:** not measurable on this build (trace accepted 0%). The reported 0.00 in A1 is not a measurement.
- **B2/B1 ratio:** not measurable. B1 and B2 both stayed in the main menu, so their intervals (7.54 and 3.95 ms) are menu frames and must not be used. Neither cross-checks A1.
- **Long pole:** the GPU. In A1 the native GPU work fills 97% of the interval (about 13.7 of 14.10 ms), while the render thread is 71% busy and the game thread 54% busy. Neither CPU thread is saturated. E1 agrees (gpu_busy 0.96; render 0.68, game 0.53). gpu_busy is a lagged lower bound that counts native command lists only, so the true GPU load is at least this high.
- **The mod's share:** hook p50 0.81 ms over a 14.10 ms interval is 5.7% (A1). The mod's own CPU is about 0.78 ms per frame (A1 perf block). The mod is not the cost.
- **Budget gap:** the interval p50 is 14.10 ms against 13.9 ms (72 Hz), so A1 is 0.20 ms over, about 1.4%. p95 is 15.33 ms, 1.43 ms over. Against 90 Hz (11.1 ms) the gap is 3.0 ms at p50.

### What the data says

On the laptop, the sim frame is GPU-bound and only just over the 72 Hz budget. In A1 the GPU is busy for 97% of a 14.10 ms interval, and neither the render thread (71%) nor the game thread (54%) is close to full, so extra CPU work removed would not shorten the frame; the mod itself costs 0.81 ms (5.7%). The control run E1 gives a result that does not fit a simple pixel-bound GPU: dropping the render scale to 77% (FSR 3 Quality) did not shorten the frame (14.63 ms against 14.10 ms, within run-to-run noise, with gpu_busy still 0.96). So the GPU time is mostly not in pixel work that the FSR upscale scale controls, or it is held at that interval by something else, so lowering resolution is not a way to reach 72 Hz here. I cannot say how much of the frame is the second eye, because the eye hooks never produced a valid pair (0% accepted) and the single-view runs B1 and B2 never reached gameplay. This means the data neither supports nor rules out alternate-eye rendering; the engine frame is about 13.3 ms and over budget, and the second-eye share is unknown. Two limits apply: the rig has no compositor, and asusd was in Balanced, not Performance.

### Gate inputs for Task 5

- `engine_ms` = interval p50 - hook p50 = 14.0954 - 0.8057 = **13.29** (A1).
- `eye2_share` = A1 `eye1_share`: **not measurable** (the value is 0 only because the trace accepted 0 of 6817 frames; do not read it as zero). B2/B1 is not available either.
- `over_budget` = interval p50 > 13.9: **true** (14.10 ms, A1).

The Task 5 gate says "second eye costs at least 40% of the engine frame and still over budget". Over budget is true; the second-eye share cannot be tested with this build. The frame-split eye0 stamp (eye 0 begin never recorded) needs fixing and A1 needs to be re-run before the gate can be ruled on.

## Mono-view diagnostic (plan E Task 5a): what one engine view per frame saves

Diagnostic only: knob `perf-mono-view` (never in `default_features.json`). In XR the engine is given the single-eye descriptor (one
full-viewport view, separation written as 0 so the view sits at the camera). The mod places that view at the midpoint of the two eye
poses with the mean orientation and the union FOV, copies the one image to both eye swapchains, and records it for both eyes
(`rendered_mask_` 3). The image is flat (no stereo depth); it exists to time the engine, not to be looked at. Any identification
failure (the engine still prepares two views for 30 setups, or the one view is not matched for 180 gameplay frames) returns to the
paired descriptor for the rest of the session and the status shows `"mono_view":"fallback"`. Status: `"mono_view":"off"|"on"|"fallback"`
and `"mono_view_frames"`. Routing is pinned by `tests/mono_view_routing_check.py`.

Measured 2026-10-21 on the sim rig (throughput without a compositor, not headset fps), same build for both rows (commit 32e5a77, built as
`d2vr_render_probe_arm_v600.dll`, sha256 bd6354ec8fcc04c312b971be6e084d49e0ffd3b8570f249db199d2ff9e577432), `oil_tank_drop --lead 28 --seconds 120 --knob native-gpu-timing`, NVIDIA RTX 3070 mobile, default FSR/mirror
settings as in A1. Both runs reached gameplay (`gameplay` true); M1 status: `mono_view` "on", `mono_view_frames` 8848. The installed DLL
stayed ff0daf65...bc927 and no knob or marker was left after either run.

| Run | Setup | Run dir | interval p50 / p95 (ms) | hook p50 | gpu_busy (list sum) | render thread busy |
| --- | --- | --- | --- | --- | --- | --- |
| M0 | paired, no knob | 20261021T143645 | 14.43 / 15.72 | 0.81 | 0.82 (11.9 ms) | 0.67 |
| M1 | `perf-mono-view` | 20261021T142525 | 10.70 / 11.56 | 0.76 | 0.88 (9.4 ms) | 0.65 |

- `mono_saving` = 1 - 10.6959 / 14.4324 = **0.259** (26% of the interval p50, 3.74 ms). Against the engine frame (interval - hook, 13.62 ms) the second view costs about 27%, not 50%.
- The GPU list sum fell only 2.5 ms (11.9 to 9.4 ms, 21%), so about 60% of the per-view GPU cost is shared or fixed; the interval fell more than the list sum (inferred, not measured) because per-eye work outside the native lists (FSR, post, gaps) went too.
- M1 is still GPU-heavy (0.88 of the interval), so even one view is near the 90 Hz budget: p50 10.70 ms is under 11.1 ms, p95 11.56 ms is over it. At 72 Hz (13.9 ms) it has 3.2 ms of room.
- M0 here (14.43 ms) matches the same-day reference on HEAD 1309f51 (14.31 ms, run 20261021T131156).
- Caveat: this is an upper bound for alternate-eye rendering. AER would still pay the engine's view-independent work every frame and adds eye-desync, TAA/FSR history and previous-matrix work that the mono diagnostic skips (the native TAA history twins are not driven in mono). Mono timing needs no stale-eye handling.
