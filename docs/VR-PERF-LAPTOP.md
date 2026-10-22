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

- Early VR start: write `8` into `build/linux-settle-seconds`, launch, and use the controller at the title and menu. Does the menu accept input, and does Continue load? (The sim's posted Enter stalls at starts ≤14 s; a real controller may not. If it works, the default can drop to about 8 s.) Delete the file afterwards.

- [ ] Windows: launcher installs the proxy; VR starts ~20 s after the window; Uninstall proxy removes it (Windows run outstanding).
- [ ] Task 2: choke a guard from behind; turn your head during the animation; the view follows your head; no spin.
- [ ] Task 3: mantle onto a ledge and drop off a roof: no lag swoop and no pop. Also crouch and stand with the tracked arms on: arms keep their length.
- [ ] Plan G task 3: from behind on an unaware guard, a fast swing kills and a crouched stab kills (watch `attack_intent` in `xr-live-status.json`: `honoured` rises).
- [ ] Plan G task 3: an aware guard: a swing is a normal attack, never a kill.
- [ ] Plan G task 3: no accidental attacks while walking, turning your head or resting the sword hand (`attack_intent.presses` stays 0).
- [ ] Plan G task 4: the takedown prompt appears behind an unaware guard (at least 25 cm wide, near eye height, readable at 1.5 m); `takedown_hints` rises in `xr-live-status.json`. An oil tank or door still shows its pick-up prompt.
- [ ] Plan G task 5: throw the oil tank at the floor and at a wall: it bounces and stays in the level (it leaves at the game's own throw speed along your hand's direction; `held_object.thrown` rises). Put `legacy-throw` in the build folder for the old hand-speed throw.
- [ ] Plan F: performance mode on: fps at 72 Hz vs off (same area, 90 s).
- [ ] Plan F: performance mode: turn your head and strafe past a guard: shimmer acceptable?
- [ ] Plan F: performance mode: fast hand swings: hands look solid?
- [ ] Plan F: performance mode: SMAA edges acceptable?

### Bonelab feel, Part 1: corpse and prop grab (plan A, Tier C)

Save the `corpse` block of `xr-live-status.json` after each item (`binds`, `releases`, `lag_m_max`, `peak_body_speed`, `peak_bone_speed_all17`, `runaway`, `fault_latch`, `event_posts_b9`, `wake_attempts`, `wake_refused`, `body_near_player`, `drawn_anchor_gap_m`, `bones`, `tuning`) plus `prop_seat_gap_m`. The first run is a tuning session, not a validation.

- [ ] Tuning session: drag a dead guard by the ankle, then lift and swing him (items 1 and 2). Edit `build/grab-tuning.cfg` (`axial_n`, `lateral_n`, `lift_fraction`, `tow`, `strength_scale`), restart, repeat; write down the values you prefer.
- [ ] Bonelab 1: grab a dead guard's limb and walk away: he follows with weight and the hand sits on the grasp point after about a quarter second, never jumping to it; open the fist and he drops where he is (no throw).
- [ ] Bonelab 2: lift a whole body with one hand and swing it: forgiving, nothing flies off (`peak_bone_speed_all17` under 12, `runaway` 0). Walk the body around for two minutes.
- [ ] Bonelab 3: hold the collar with `build/collar-grab` present: the chest and neck come up (full fist only; `bones.collar` rises).
- [ ] Bonelab 4: two hands on one body: both hold, each takes half the load, and letting go of one keeps the other.
- [ ] Bonelab 5: pull the body against a wall or the floor: the drawn hand stays within 0.35 m of your real hand (`drawn_anchor_gap_m`), and when the body lags more than 0.5 m for 0.25 s it lets go (`releases.lag`), with a 0.3 s wait before regrabbing.
- [ ] Bonelab 6: a living guard grabbed from behind is pulled and staggers and is alerted; he is never ragdolled by the grab (`event_posts_b9` stays within 2 x `wake_attempts`).
- [ ] Bonelab 7: an unconscious guard wakes while you hold him: the hold ends (`releases.state_changed` rises), no fling. Also reload a save mid-hold: no crash (`releases.lifecycle`).
- [ ] Bonelab 8: for every prop (bottle, crate, book, apple): the visible hand sits on the object's grasp point, easing in and out; then create `build/no-prop-grasp-point` and restart: the old drawing is back (`prop_grasp_point` false). Save `prop_seat_gap_m`. A native item (the oil tank) is unchanged either way.

### Bonelab feel, Tier B scenarios (rig)

Run with `linux/d2vr-linux sim --candidate-dll ... --scenario NAME --status-field corpse`, then `python3 tools/linux/bonelab_run_check.py ~/d2vr-test-builds/sim-runs/<stamp> --scenario NAME` (several run dirs for `corpse_stress`: four sessions of 50 cycles, `--lead 45`).

| Scenario | Needs |
| --- | --- |
| `oil_tank_drop`, `oil_tank_throw` | the current save (a tank is reachable); native hold, so the corpse counters stay 0 and only crash safety is checked |
| `corpse_drag`, `corpse_lift`, `corpse_stress`, `corpse_prompt`, `corpse_probe`, `corpse_reload` | needs test save: a `savegame` folder (`~/d2vr-test-saves/corpse-dead`) that starts the player standing and facing a dead guard lying on the floor, a limb about 0.5 m ahead at knee height, no other guard awake nearby |
| `corpse_collar` | the same save, run with `--knob collar-grab` |
| `corpse_two_hands` | the same save, with both ankles (or wrists) reachable at once |
| `corpse_reload` | UNVERIFIED: it presses menu then A on the first pause entry; if that does not reload, use headset item 7 |

Outstanding until that save exists: every corpse scenario run (the scripts compile and fit the session; the checker is tested on fixtures only).

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

## Measured VR cvar preset (plan D Task 9): nothing qualified

Ruling: **no cvar enters a VR preset; Task 9 ships only this table and the rig option `--game-cvar NAME=VALUE`** (appends `NAME "VALUE"` to the prefix game config for one run; the existing verified restore puts the original back). The rule was a saving of at least 0.3 ms interval p50 in two runs; no candidate reached 0.3 ms even once against the same-day baseline mean, so no second runs were made and no `VrCvarPreset` code, RVAs or `no-vr-cvar-preset` knob were added.

Measured 2026-10-21 on the sim rig (throughput without a compositor, not headset fps), candidate HEAD 022d051 built as `d2vr_render_probe_arm_v601.dll`, sha256 `8c069e909788a0343c9986217fd2b80d48bd3fac8c435bfb737344de7918c6cc`, run under the installed name. `oil_tank_drop --lead 28 --seconds 120 --knob native-gpu-timing`, RTX 3070 mobile, same conditions as the Bound analysis (asusd Balanced; user knobs `fsr4.off`, `carry-trace`, `full-body` present in every run). The user's config already had `r_skipGodRays 1`, `r_skipLensFlares 1`, `r_skipBloom 1`, `r_shadowQualityPreset 2`, `r_shadowsRats 0`, `r_shadowsBloodflies 0`, `r_oceanQualityPreset 1`, `r_maxDynamicDecalsPreset 1`, so those were not retested. After every run the installed DLL was `ff0daf65...bc927`, the game config matched its pre-run SHA-256, and no knob or restore-pending marker was left.

| Run (dir) | Config cvar appended | interval p50 / p95 (ms) | vs baseline mean 14.44 | gpu_busy | Kept |
| --- | --- | --- | --- | --- | --- |
| base1 (160532) | none | 14.18 / 15.23 | | 0.82 | |
| base2 (163350) | none | 14.69 / 15.97 | | 0.81 | |
| shdiv1 (160841) | `r_shadowParallelResolutionDividerLevel 1` | 14.44 / 15.73 | 0.00 | 0.81 | no |
| spot1 (161150) | `r_shadowSpotAndOmniLowResolutionDividerEnabled 1` | 14.62 / 16.35 | +0.18 | 0.82 | no |
| cull5 (161459) | `r_shadowCullDistance 5` | 14.45 / 15.71 | +0.01 | 0.81 | no |
| lod1 (161803) | `r_lodZoomMultiplicator 0.5` | 14.70 / 16.04 | +0.26 | 0.81 | no |
| lodd (162121) | `lod_dist_level0..3` 1 / 2 / 4 / 7 (defaults 3 / 6 / 12 / 20) | 16.32 / 18.17 | +1.88 | 0.82 | no (slower) |
| scat (162425) | `r_skipLightScattering 1` | 14.63 / 15.74 | +0.19 | 0.81 | no |
| dec (162740) | `r_maxDynamicDecals 4` | 14.55 / 15.87 | +0.11 | 0.81 | no |
| ssr (163044) | `r_skipOceanSSR 1` | 14.70 / 15.96 | +0.26 | 0.81 | no |
| cnt2 (163705) | `r_shadowParallelSliceCnt 2` | 14.73 / 16.61 | +0.29 | 0.81 | no |
| shall (164009) | shadow stack: divider level 2, spot/omni divider 1, cull distance 5, slice size 512 | 14.39 / 15.50 | -0.05 | 0.81 | no |
| lod4 (164315) | `r_lodZoomMultiplicator 4` | 14.53 / 15.77 | +0.09 | 0.81 | no |

- Run-to-run spread of the two baselines is 0.51 ms (14.18 to 14.69), larger than the 0.3 ms threshold, and the same-day M0 reference was 14.43. Every candidate is inside that spread except `lodd`, which is 1.9 ms slower.
- Even the stacked shadow run (the most aggressive shadow settings on the list) saved nothing, and `gpu_busy` stayed at 0.81 to 0.82 in every run. In this scene shadows, decals, light scattering and ocean SSR are not where the GPU time goes. That agrees with the Bound analysis: resolution did not move the frame either.
- Boot-only versus live was not separated: the cvars were set through the game config, so boot-time allocations (`r_shadowParallelSliceCnt`, `r_shadowParallelSliceSize`) took effect, which a runtime write could not reproduce. They saved nothing either way. `lodd` changed the frame, so config values are being read.
- Not tested (budget 13 of 14 runs, rig cannot pass a value with spaces): `r_shadowCascadeFars "4 10 24 50"`, `r_downsampleQuality 2`, texture streaming cvars, particle cvars, `r_shadowsRats/Bloodflies` (already 0 in this config), god rays (already skipped). No screenshot comparison was made because nothing was a candidate to keep.
- Consequence for plan D: the frame is not reduced by this class of cvar on this scene. The mono-view diagnostic (3.74 ms) remains the only measured lever.

## GPU phase attribution (plan E Task 3b): where the fixed GPU time goes

Run `20261021T174007` (sim rig, `oil_tank_drop`, lead 28, 120 s, `native-gpu-timing`, RTX 3070 laptop, 900-frame window, build knobs carry-trace / fsr4.off / full-body). Each native command list is tagged by the CPU phase in which it is *executed* (stamps from the eye-begin hook and the Present hook); its GPU time is the timestamp pair. The phase GPU times are the lagged per-frame sums, p50 over the window.

| phase | GPU ms p50 | lists p50 |
|---|---|---|
| pre (previous Present exit to eye-0 begin) | 0.00 | 0 |
| eye0 (eye-0 begin to eye-1 begin) | 7.21 | 45 |
| eye1 (eye-1 begin to Present enter) | 3.93 | 45 |
| hook (inside the Present hook) | 0.00 | 0 |
| sum of the p50s | 11.14 | 90 |

Context: CPU interval p50 13.73 ms, eye0 span 3.12 ms, eye1 span 9.56 ms, hook 0.77 ms, `gpu_busy` 0.81, `gpu_span` 0.97.

**Reading.** Every native list is executed after the eye-0 begin: the engine does all its list submission inside the two view passes, so nothing is left in "pre" or "hook" on this scene. The dominant phase is eye0 at 7.2 ms of 11.1, against 3.9 ms for eye1. The CPU spans show the opposite split (eye0 3.1 ms, eye1 9.6 ms): the render thread submits the eye-0 lists quickly while the GPU is still behind, and then waits. Because the engine is one frame deep (Setup for frame N is consumed by Begin in frame N+1, findings §1), "eye0" lists are not only the left eye: they include the view-independent work the engine does when the first view begins (shadow maps, culling-dependent uploads, particle and animation updates), which the eye-1 phase does not repeat. That is consistent with the 3.2 ms eye0-minus-eye1 difference and with the earlier finding that mono view (one view) saves only 2.5 ms. So roughly 3.9 ms is one view's worth of scene passes and about 3.3 ms is work that runs once per frame inside the first view. Exactly 45 lists per phase is a property of the engine's pass structure, not a measurement artefact (the capacity is 128). Which individual passes make up the 3.3 ms is not resolved here; that needs the per-list `render-perf` lane or a capture.

**Edge cases found.** (1) A list executed before the eye stamps of its own frame exist is tagged "pre"; the stamps reset at Present exit, so the first lists after a Present are "pre". Here none were. (2) With the eye-begin hook off (no `eye_boundary`), every list outside the Present hook is "pre". (3) A GUI/null view begin stamps nothing, so its lists stay in the running phase. (4) The timer frame closes inside the Present hook, so lists executed in the hook after that point are counted in the next timer frame, tagged "hook". (5) The p50 of each phase is taken separately, so the phase p50s need not add to the p50 of the frame sum (11.14 here against a 11.25 ms last frame).

### The mod's own GPU work (p50 per frame, same run)

| span | GPU ms |
|---|---|
| FSR dispatch, eye 0 | 1.037 |
| FSR dispatch, eye 1 | 1.046 |
| AA / sharpen | 0.090 |
| eye copy (CopyResource to the XR swapchain) | 0.056 |
| eye overlay draws (native object art, desktop mirror) | 0.033 |
| HUD / wrist / vignette layer updates | 0.000 |
| `native_eye_capture` copy | 0.025 |
| sum | 2.285 |

The mod's GPU work is about 2.3 ms per frame, of which FSR is 2.08 ms. It is not the 9 ms fixed cost. It sits outside the list sum, so list sum plus mod is about 13.4 ms of a 13.7 ms interval, which is why the laptop is GPU-bound at 72 Hz-class intervals. Caveats: the sim's render size is smaller than the headset's (FSR here is 1.04 ms per eye; see the headset data below), and a timestamp pair measures GPU execution between the two points, not queue wait.

### Headset session, same day (context, not from this rig)

Interval p50 45.4 ms; engine gap p50 19.7 ms at 2020x2160 per eye; xrEndFrame submit 4.4 ms; transfer 1.1 ms (p95 10); FSR3 native GPU 2.84 ms per eye; SteamVR display period 33-44 ms, i.e. throttled to 1/3 to 1/4 of 90 Hz. In the headset FSR costs 5.7 ms per frame (2.7x the sim), so the mod GPU work is no longer small there and scales with render size. The engine lists were not timed in that session (`native-gpu-timing` was off).

### What the three probe diagnostics in the real `dxgi-probe.cfg` cost

Counters from the same run (6901 frames):

| diagnostic | per-frame CPU | per-frame GPU | needed for normal play |
|---|---|---|---|
| `eye_boundary` (hook on the engine begin-view function; match under `gpu_mutex`; CSV only for the first 1500 begins) | 4.0 calls/frame, 37.6 us/call, **0.15 ms/frame** | none itself | yes: `native_eye_capture` and per-eye TAA history depend on it |
| `native_eye_capture` (one full-resolution backbuffer `CopyResource` into `paired_left` at the eye-1 begin) | included in the eye_boundary time (encode only) | **0.025 ms/frame** | yes: it is how the left eye image is captured for the XR submit |
| `arm_upload_observer` (hook on the native arm palette consumer; 45.5 uploads/frame, reads rig memory under `arm_log_mutex`; CSV of 100 samples per side then stops) | 3.5 us/call, **0.16 ms/frame** | none | yes: tracked arms and the body avatar use the same hook and need it |

None of the three is a meaningful share of the frame: about 0.3 ms of render-thread CPU and 0.03 ms of GPU together. They are not the cause of the fixed GPU time and removing them is not a frame-rate lever. The CPU numbers are wall time inside the hook on a loaded render thread, so treat them as upper bounds.

## Anti-aliasing modes compared (plan G Task 2): the default stays FSR 3 Native

Measured 2026-10-21 on the sim rig (throughput without a compositor, at the sim's own 1912x1046 resolution, not headset fps). Candidate: HEAD 9868024 as `d2vr_render_probe_arm_v600.dll` (SHA-256 `93c76bd5907f91e271190097358ba44c3b8415c639c3fca4a18827b064ec66c4`), scenario `oil_tank_drop`, `--lead 28 --seconds 120`, `--knob native-gpu-timing --vr-setting temporal_aa=N`. NVIDIA RTX 3070 Laptop, `fsr4.off`, `carry-trace`, `full-body` in `build/` for every run. The installed DLL (`ff0daf65...bc927`) was verified unchanged after every run. FSR 4 (modes 5, 6) is RDNA4-only and was not run.

"List sum" is the sum of the p50 per-phase native command-list times; "total GPU" adds the mod's own GPU work (FSR plus sharpen, copies, overlays), which is where the FSR cost sits.

| Mode | Run | interval p50 / p95 (ms) | FSR GPU per eye (ms) | list sum (ms) | total GPU (ms) | Image |
| --- | --- | --- | --- | --- | --- | --- |
| 0 off (SMAA row only) | 20261021T180453 | 13.88 / 14.97 | 0 | 13.31 | 13.52 | stair-stepped edges, otherwise clean |
| 1 game TAA | 20261021T180757 | 13.79 / 15.14 | 0 | 13.21 | 13.42 | slightly smoother than 0, diagonals still stepped |
| 1 game TAA (repeat) | 20261021T181924 | 13.81 / 14.94 | 0 | 13.27 | 13.48 | same |
| 2 FSR 3 Native (default) | 20261021T180143 | 14.00 / 17.39 | 1.03 / 1.05 | 11.49 | 13.77 | smoothest edges, clean |
| 2 FSR 3 Native (repeat) | 20261021T182230 | 13.84 / 15.20 | 1.04 / 1.05 | 11.24 | 13.53 | same |
| 3 FSR 3 Quality (77%) | 20261021T181101 | 14.33 / 15.68 | 0.79 / 0.79 | 12.25 | 14.03 | grainy speckle on flat surfaces, coloured fringes on dark edges |
| 4 FSR 3 Balanced (67%) | 20261021T181405 | 14.08 / 15.24 | 0.72 / 0.72 | 12.11 | 13.76 | grainy speckle, soft and smeared edges |

What it shows:

- The sim interval is about 14 ms for every mode (13.8 to 14.3 p50). The sim GPU is saturated near 14 ms whatever the mode, so the interval cannot rank them, and the repeats show a run-to-run spread of about 0.2 ms p50 (and one 17.4 ms p95 outlier in mode 2 that did not repeat).
- The mod's FSR pass costs 1.04 ms per eye here, but FSR 3 Native replaces the game's own TAA passes, so the list sum falls by 1.7 ms. Net, game TAA saves only 0.1 to 0.35 ms of total GPU per frame in the sim (13.45 against 13.65 averaged over the two runs each), inside the noise. At headset resolution (2.84 ms per eye measured for FSR) both sides scale up, but the sim cannot say by how much the net changes; only the headset can.
- Quality and Balanced render fewer pixels but cost more total GPU in the sim (14.03 and 13.76 ms) and look visibly worse (grain and fringing), so they are not a cheaper default.
- Off and game TAA are not clearly cheaper than FSR 3 Native and leave visibly stepped edges.

Choice: the default stays FSR 3 Native (`temporal_aa=2`). By the rule (the cheapest mode whose image is not visibly broken, and game TAA wins only if it is clearly cheaper) no mode beat it by more than the noise. No source default was changed.

Caveats: one still frame per mode cannot show temporal shimmer or ghosting, which is what AA is for in VR; the stills only judge edge quality. The viewpoint was identical across the five runs. To test game TAA in the headset (the only place its real saving shows), pick it in the VR settings panel (see PLAYING.md); a one-run A/B of modes 1 and 2 there is the open question.

## Performance mode (alternate eye)

Plan F task 1, knob `build/alternate-eye` (opt-in; it also turns on the mono-view path). The engine renders one eye per displayed frame
(parity of the frame), the other eye is resubmitted from its kept image with the pose it was rendered from. Sim rig, RTX 3070 mobile,
`oil_tank_drop --lead 28 --seconds 120`, knob `native-gpu-timing` (build/ also held `fsr4.off`, `carry-trace`, `full-body` in both runs),
HEAD 9e50b37. Run folders: `~/d2vr-test-builds/sim-runs/20261021T221957` (M0) and `20261021T222320` (M2).

| | M0 paired | M2 alternate-eye |
|---|---|---|
| interval p50 / p95 | 13.51 / 14.65 ms (74 fps) | 10.37 / 11.24 ms (96 fps) |
| gpu_busy / gpu_span | 0.82 / 0.97 | 0.81 / 0.95 |
| render thread busy | 0.70 | 0.67 |
| GPU lists per frame p50 | eye0 7.20 ms (45) + eye1 3.78 ms (45) | 8.38 ms (45), no second view |
| mod GPU p50 (sum) | 2.19 ms (fsr_eye0 0.99, fsr_eye1 1.00) | 1.30 ms (fsr 1.18, kept/stale eye copy 0.03) |
| verdict | MIXED | MIXED |

- Interval drops 23% (3.1 ms), the same order as the mono-view diagnostic (about 10.7 ms), so the second view really is gone.
- Status after M2: `"alternate_eye":{"state":"on","alternated":9240,"paired":1,"fallbacks":0}`, `mono_view_frames` 9240. In 120 s the mode
  alternated on every tracked free-gameplay frame, had one paired refresh frame (the start), and never fell back. M0 reports state off.
- The paired view's `eye0`/`eye1` split is not defined in M2 (one view): `split_skipped` counts every frame by design.
- Image: `dxgi-stereo.rgba` of M2 (1912x1046) converted to PNG and viewed: a sane gameplay frame (oil tank pipe, hand). The rig captures
  this file once per run, so two consecutive frames showing one fresh eye each could not be produced; the evidence for alternation is the
  counters (9240 alternated frames, one view per frame, the two layers submitted every frame). A per-frame eye capture would need a new probe hook.
- Not measured: the comfort of a one-frame interocular delay (Pulfrich-style shimmer). Only the headset decides.

### Double vision in performance mode: the engine pipeline is one frame deep (fixed 2026-10-22)

Headset session 2 (2026-10-22, performance mode on, 3576 alternated frames, 0 fallbacks): "у меня в глазах двоилось" (double vision).
Root cause: the engine renders a view one Present after the mod's Setup hook wrote it (findings §1; `NativeEyeSources` matches a Begin only
against the Setup published one Present earlier, which is why `mono_eye_frame`/`alt_head_stamp` could both equal the Present number while
describing different views). Task 1 stamped the Setup made while the counter read F for Present F+1, but its image is on screen at Present
F+2. With the eye flipping every frame, the image of eye k always landed in swapchain 1-k, and the kept copy inherited the swap: a
pseudo-stereo pair, consistently reversed, which the eyes cannot fuse. The pose submitted with it was that other eye's own pose, so nothing in
the per-frame checks could see it; only the images could.

Evidence from the existing sim runs (`tools`-free check, `xr-t<sec>-left/right.rgba` pairs, central band, best horizontal alignment of the
right image against the left): M0 paired `20261021T221957` +88 px at every sample from t=50 s; M2 alternate `20261021T222320` and
`20261021T230548` -96 px at the same samples. Same magnitude, opposite sign: swapped eyes.

Fix (`src/alternate_eye.hpp`, `src/dxgi_probe.cpp`, `src/game_xr.hpp`): everything is keyed to the DISPLAY frame of an image
(`alternate_display_frame(F) = F+2`). A Setup's view is recorded in `PendingViews` for that frame and promoted into GameXr at that Present
(`record_rendered_view`), so the layer carries exactly the pose and FOV the image was rendered with; the Present-time readiness comes from the
slot recorded for the Present (`alternate_present_readiness`), never from the view count the engine prepared since (that is the next frame's);
the planner plans the display frame two Presents ahead and counts the Setup already in flight (`AlternateEye::eye_for_frame`). A side effect:
the descriptor switch no longer drops a frame on each paired/single transition. Diagnostic knob `build/alt-pose-trace` writes
`alt-pose-trace.csv` (per Setup: the view written and its display frame; per Begin: the pose in the view's render copy; per Present: the
layers submitted); `python3 tools/alt_pose_trace_check.py alt-pose-trace.csv` joins them. Tests: `tests/alternate_eye_check.cpp` drives the
real helpers against an engine model that delivers Setup(F) at Present F+2 and shows that the task 1 stamping puts the other eye's image in
every alternated frame; `tests/alt_pose_trace_check.py` does the same for the trace checker.

Sim proof (`oil_tank_drop --lead 28 --seconds 120`, knobs `alternate-eye`, `native-gpu-timing`, `alt-pose-trace`; full report with the trace
join sample in `.superpowers/sdd/2026-10-21-alternate-eye-f/double-vision-report.md`, scripts and anaglyph in
`~/d2vr-test-builds/double-vision-2026-10-22/`):

| | fixed build, run `20261022T093102` | control: `alternate_pipeline_depth=0` (the task 1 stamping), run `20261022T093710` |
|---|---|---|
| alternate_eye | on, alternated 11771, paired 2, fallbacks 0 | on, alternated 12155, paired 1, fallbacks 0 (as healthy-looking as the headset session) |
| interval p50 / p95 | 7.89 / 8.69 ms (gpu_busy 0.95) | 7.57 / 8.21 ms (gpu_busy 0.94) |
| `tools/alt_pose_trace_check.py` | 11781 tracked one-eye Presents, 0 mismatches | 12150 checked, 12149 with the Begin rendering another Setup whose eye is the other one |
| right-vs-left disparity t=50..110 s | +68 px (the paired run's sign) | -96 px (reversed: swapped eyes) |

The pose match holds per Present: the fresh layer carries the Setup's pose and FOV, the Begin's render copy carries that Setup's game
position (eye-specific), the stale layer carries its own Setup's pose from exactly one Present earlier. The disparity magnitude is not
comparable across runs (different viewpoint at the sample time); the sign is.

### Paired path: the layer pose was one frame ahead of its image (layer-pose sync, fixed 2026-10-22)

The same one-frame engine pipeline bit the knob-off (paired) path, which `rendered_views_` had always recorded at Setup time
(`record_rendered_eye`): the Setup made while the counter reads F wrote the views located at Present F, the engine rendered them in the next
interval, and the image showed at Present F+2; but by then `rendered_views_` already held the Setup of counter F+1, so every paired layer
went out with the pose of the NEXT image. The compositor reprojects from the submitted pose, so during a head turn the world lagged the head
by one frame and jittered with the frame-time variance (headset session 1, paired at 30 fps: the submitted pose was located 29.5 ms p50
before the Present, the image 59 ms; `rendered_display - display_time` 0.001 ms, i.e. the layer claimed the pose of its own display time
while its image was a frame old). User reports: "head turn still provides unstable image", "same for walking".

Fix (`src/alternate_eye.hpp` `paired_views_promotable` / `paired_slot_to_promote`, `src/dxgi_probe.cpp`): the paired and perf-mono Setups
record their view(s) in the same `PendingViews` alternate mode uses, keyed to display frame F+2, and the Present promotes the slot recorded
for it (`record_rendered_view`). A Present without its own slot (a Setup that bailed, a recentre since) promotes the latest Setup's slot,
exactly the old submission for that frame, counted in status `layer_pose_sync.fallbacks`; with neither it is untracked as before. Knob
`build/no-layer-pose-sync` (launcher row `layer-pose-sync`, kind disable) restores the Setup-time record line for line. Alternate mode is
untouched. Tests: `tests/layer_pose_sync_check.cpp` (both glues against the F+2 engine model: the Setup-time record is one frame ahead on
every Present, sync is exact; 8/8 header mutants killed: depth 0/2, promotable mask/epoch/slot, latest-before-own, no fallback, take ignoring
the frame), `tests/layer_pose_sync_routing_check.py`, `tests/alt_pose_trace_check.py` (the trace checker's paired join). The pose trace
(`build/alt-pose-trace`) now works in every eye mode and `tools/alt_pose_trace_check.py` names a layer carrying the next Setup's pose
`layer_pose_is_the_next_setup`, with the layer-vs-image pose error in degrees and mm; scenario `head_turn_walk` moves the head every frame
(a static head cannot show the skew). Sim numbers: `.superpowers/sdd/2026-10-21-alternate-eye-f/paired-skew-report.md`.

Next candidate, not changed: the prediction horizon. The Setup locates for `predictedDisplayTime + period` but its image shows two
periods later (paired: one period short; alternate: fresh eye one, stale eye two; headset session 3 `rendered_display - display_time`
p50 -22 ms). Rotation is now corrected by the compositor; translation (walking 1.5 m/s x 30 ms = 4.5 cm) is not.

### Alternate eye and temporal AA (plan F task 3, path B)

Decision: alternate-eye mode forces temporal AA off (the "off, SMAA row only" mode 0), whatever the saved preference or settings panel asks
for, and the SMAA pass still runs. A correct but jagged-edged image beats ghosting. There is no launcher row for the knob yet; when one is
added it must say "performance mode (may shimmer on fast motion; temporal AA off, SMAA only)".

Evidence (sim run 20261021T230548, `build/alt-taa-trace`, 100 gameplay frames, frames 4217-4316, alternate-eye on):
- The engine keeps ONE view object: a single address (0x82c15cc0) on all 100 frames, while the logical eye alternates 0/1.
- Setup rolls the view's jitter and previous-matrix block in place every call: the roll counter `+0x77b4` rose by exactly 1 per frame, and
  `+0x7858` (previous jitter) equalled the previous frame's `+0x7850` on 99 of 99 rows. So the "previous" state of every frame is the OTHER
  eye's (one frame old), never the same eye's (two frames old). The sampled matrix words (`+0x7390` / `+0x7410`, first row x) were equal for both
  eyes on most rows, so they cannot show the pose difference; the structural result above is the evidence.
- Path A would need per-eye previous matrices written into a block whose layout (`+0x7390..0x75cc` rolled to `+0x7410..`, overlapping ranges)
  is only known from static reading, plus per-eye FSR history and dt. That is guesswork about engine state, so it was not done.
- `fsr_vfov`, the jitter counter and `eye_jitter` are indexed by the logical eye in either path.
- M2 image with temporal AA forced off (run above, one still frame): clean edges, no doubled outline. One frame cannot show temporal
  ghosting, and the removed history means there is none to ghost; judging the SMAA-only edge quality and the shimmer needs the headset.

## Cutting the mod's own per-frame cost (plan E Task 4)

Three changes, each with a knob that restores the old behaviour:

1. **Per-frame logic mask** (`src/logic_mask_cache.hpp`, knob `no-discovery-throttle`; fix round 1 replaced the 120-stable/every-8th cadence). `game_logic_mask()` re-read the engine's manager table on every call; the render path and the native hooks make about 90 calls a frame. Now the first call of each displayed frame (any thread) reads live and every later call of that frame gets that value: an answer is at most one frame old, a one-frame state such as the reload mask `1<<15` is always seen, and the game object and its state (`state==3`) are validated live on every call, so a load is never answered from the cache. A cached value also expires after 100 ms if the frame counter stalls. The knob reads on every call as before.
2. **Overlay swapchains** (`src/overlay_dirty.hpp`, knob `no-overlay-skip`). The HUD window, wrists, controls card, settings panel, world UI, wheel dot and comfort vignette acquire, draw and release an image only when their content changed; otherwise the quad re-submits its last released image (allowed by OpenXR, and the same mechanism the stale world UI / HUD hold already used). Each kind has its own dirty key:

   | Kind | Key |
   | --- | --- |
   | HUD window | capture texture, the engine's last draw tick of that surface (`WorldUiCapture::Snapshot::recorded_tick`), crop box |
   | World UI | capture texture, draw tick, panel kind, full-height flag, copied width and rows |
   | Wrist (each side) | art texture, its capture tick and owner, crop, alpha mode, the mana half's same facts, canvas size |
   | Controls card, wheel dot | constant (the pixels never change; one fill per runtime image) |
   | Settings panel | a counter that moves only when the generated 640x560 pixels differ from the previous ones |
   | Comfort vignette | constant with colour-scale; otherwise the number of alpha bakes (at most 10 a second) |

   Never skipped: the first frame of a (re)created swapchain (every chain made through `create_overlay_chain` gets a new generation), a pose-epoch change, a session close, any render that did not finish (the gate is invalid from the moment a render starts until its release), and an image older than 500 ms for the tick-keyed kinds (a draw inside the same millisecond is invisible to a tick). Spaces, sizes and poses of the layers are unchanged, so every layer still passes `xr_quad_validation.hpp`.
3. **Monitor view `off`** (`MirrorMode::off=3`, in `vr-settings.cfg`, the launcher and the native options page). No layout, canvas or desktop copy. The game's own Present still runs: its measured cost is 0.02 ms (headset `desktop_present_cpu_ns` p50), so skipping it would risk the window and swapchain for nothing. Default stays right eye.

Status (`xr-live-status.json`): `overlay_releases_per_frame` (p50 per overlay frame), `overlay_skipped`, `overlay_released`, `overlay_frames`, `overlay_skips_per_frame`, `overlay_by_kind` (`[released, skipped]` per kind), `discovery` (`reads`, `served_from_cache`, `resets`) and `xr_cpu_spans_ms` (measurement only: `[p50, p95]` ms per frame of the acquire+wait, AA/sharpen, eye copy, overlay draws, transfer observer, release and overlay-layer steps, both eyes summed).

Measured 2026-10-22 on the sim rig (throughput without a compositor, not headset fps; wineopenxr is not in the path, so the xr-call saving is NOT visible here). `oil_tank_drop --lead 28 --seconds 120 --knob native-gpu-timing`, NVIDIA RTX 3070 mobile, user knobs `fsr4.off`, `carry-trace`, `full-body` present in every run. Before = HEAD 9d00077 built as `d2vr_render_probe_arm_v600.dll` (sha256 41fdc12c84e4f2923d103388ec881d9e0604d3c6e0c9a6f8c71e67e91cfdf1d1); after = the Task 4 change. Installed DLL ff0daf65...bc927 verified after every run, no knob or marker left.

| Run | Build | Run dir | hook p50 (ms) | interval p50 / p95 (ms) | eye1 p50 | releases / frame p50 | overlay_skipped |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | before | 20261022T050544 | 0.793 | 13.93 / 16.08 | 9.69 | n/a | n/a |
| 2 | after | 20261022T051745 | 0.445 | 11.04 / 14.58 | 7.06 | 1 | 707 |
| 3 | after | 20261022T052103 | 0.414 | 11.30 / 14.74 | 7.49 | 1 | 690 |
| 4 | before | 20261022T052349 | 0.809 | 14.25 / 15.56 | 9.93 | n/a | n/a |

Hook p50 drops 0.35-0.39 ms in both after runs against both before runs (gate: at least 0.2 ms in two runs). The interval drops 2.6-3.2 ms: the 86 % of `game_logic_mask` calls now answered from the cache (`discovery`: about 150 000 reads against 910 000-927 000 cached calls per run) were costing more than the hook shows; the extra is probably calls made from the engine's own eye-begin and hook paths, which are outside the hook span (not separated by a measurement). Overlays in the sim: the HUD window releases once per frame (7415-7523 releases, 0 skips) because the engine redraws that surface every frame, so its tick changes every frame; only the left wrist is held (177-181 releases, 690-707 skips). So in the sim `overlay_releases_per_frame` is 1 and the HUD window is the whole overlay cost. Separating the discovery and overlay shares would take another pair of runs with one knob each; it was not done (4-run limit).

Other xr-facing work per frame (measured, not changed; sim p50 / p95 ms, both eyes summed, `xr_cpu_spans_ms`): acquire+wait 0.001 / 0.0015, AA+sharpen CPU 0.035 / 0.060, eye copy (`CopyResource`) 0.001 / 0.002, overlay draws 0.065 / 0.113, transfer observer 0.001 / 0.23, release 0.0003 / 0.0008, overlay layers 0.005 / 0.009. These are near zero under the sim's scripted XR backend; the headset's 1.1 ms transfer (p95 10 ms) will show which of them is expensive under wineopenxr once a session records `xr_cpu_spans_ms`.

Fix round 1 re-measure (same rules; per-frame cache instead of the 8-frame cadence): after = 20261022T054343, hook p50 0.482 ms, interval p50 11.54 / p95 13.01, eye1 7.36, `discovery` 9626 reads against 1 069 062 cached calls (one read per frame); before (9d00077) = 20261022T054627, hook p50 0.787, interval p50 14.08 / p95 15.30, eye1 9.82. The per-frame cache keeps the whole saving (hook -0.30 ms, interval -2.5 ms). Overlays unchanged: HUD window 7539 releases / 0 skips, left wrist 177 / 692.
