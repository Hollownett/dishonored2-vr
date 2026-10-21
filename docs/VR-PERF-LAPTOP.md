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

## Frame split block (plan E task 1)

`xr-live-status.json` also carries `"frame_split":{"adapter":{name,vendor,vram_mb},"split":{...}}` over the same 900-frame gameplay window:
`interval_ms_p50/p95` (Present enter to Present enter), `eye0_ms_p50` (eye 0 begin to eye 1 begin), `eye1_ms_p50` (eye 1 begin to Present enter),
`hook_ms_p50` (Present enter to exit, the full hook including the desktop Present), `eye1_share` (eye1 p50 / interval p50), `gpu_busy`
(native GPU elapsed over the interval, p50; native command lists only, excluding FSR and immediate draws, from a timer that lags 1-3 frames, so read it as a lagged lower bound; `null` without the `native-gpu-timing` knob), `render_thread_busy` and `game_thread_busy`
(GetThreadTimes delta over wall delta, about 1 s window; `null` when the thread clock is coarse or unknown), `frames`, `split_skipped`
(cumulative; frames whose eye stamps were missing, out of order or stale, i.e. not `prev present < eye0 <= eye1 <= present`). The first gameplay frame after a menu or load feeds no interval sample.

Debug trace: create `build/frame-split-trace` before launch to get `native-frame-split-trace.csv` (columns `frame,eye0,eye1,enter,exit,prev,accepted`,
steady_clock ns; one row per fed frame, capped at 20000 rows, written by the status writer, not the render thread). Plan E task 3 checks that
`accepted` is 1 on nearly every frame and `eye1 < enter`; many 0 rows mean the eye hooks do not run between two Presents as assumed.
