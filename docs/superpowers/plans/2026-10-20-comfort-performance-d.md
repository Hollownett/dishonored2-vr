# VR Comfort and Performance D (Bonelab-style) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the camera wobble that spins players' heads by making the headset the only thing that rotates the view, smooth the engine's camera position animation, and make VR run at the headset's rate on the user's laptop (Ryzen 5900HX, RTX 3070 laptop 8 GB, Quest 3 over Steam Link) by measuring first and then applying the cheap, low-risk wins.

**Architecture:** Comfort: three pure headers decide the view, and `apply_head_view` in `src/dxgi_probe.cpp` uses them:
- `src/authored_view.hpp`: in authored scenes the game camera gives position and a snapped heading only; the HMD gives rotation.
- `src/anchor_smoother.hpp`: a vertical, speed-limited filter on the engine camera position, with exceptions for falls and teleports.
- `src/comfort_meter.hpp`: telemetry.

Turning gets a ramp, and a head-locked vignette quad shows during artificial motion. Performance: `src/perf_meter.hpp` adds a status block and measured runs on the Linux sim rig. Then the existing governor is on by default with a wider range, the first-run render profile follows the GPU's VRAM, the measured cvar preset is applied at runtime with restore, and the desktop mirror defaults to one eye.

**Tech Stack:**
- C++17 headers (MSVC `cl /W4 /WX`; clang-cl + Wine on Linux).
- Assert-based checks in `tests/*_check.cpp` registered in `tools/build-msvc.cmd`.
- The Linux sim rig `linux/d2vr-linux sim`.
- Python 3 for report scripts.

**Spec:** user words 2026-10-20 23:08 (below) and the investigation `.superpowers/sdd/plan-d-intake/findings.md` (git-ignored workspace; its content is summarised in Requirements so this plan stands alone).

## Requirements

User, verbatim: "player camera is wobbling too much, players experience head spinning. and VR performance is a huge issue, game should be better optimized for VR rendering, look for bonelab experience."

- **R1 (head owns rotation).** No game animation rotates the VR view. Takedowns, chokes, assassinations, mantles, climbs, keyholes, scripted scenes and Shadow Walk use the HMD orientation. Today `authored_eye_camera` (`src/body_anchor.hpp:9`) copies the native camera's yaw, pitch and roll, and the head is ignored. The scene's heading follows the game only in snaps (≥ 30°), never as a continuous rotation.
- **R2 (no position wobble).** Engine camera position animation must not jerk the view: landing dip, crouch/stand transition, stairs, mantle rise, swim bob. Today `source_anchor.position` (~`dxgi_probe.cpp:3828`) is used raw. Vertical motion is smoothed and speed-limited. Real falls and teleports (Blink) pass straight through.
- **R3 (comfort options).**
  - Smooth turn ramps in over 150 ms instead of starting at full speed.
  - A vignette narrows the view during artificial motion (stick locomotion and smooth turn). It is on by default at a moderate strength and can be switched off.
  - `pm_noBob` joins the existing bob and shake overrides.
- **R4 (measure).**
  - A `perf` status block gives frame pace p50/p95, missed-slot fraction, the mod's CPU time per frame, and the current render fraction.
  - A measured baseline is recorded on the user's laptop via the sim rig, before and after each performance change.
- **R5 (run at rate).**
  - The governor is on by default and can reduce resolution from Native.
  - A first-run render profile follows VRAM: ≤ 8 GB means FSR 3 Quality.
  - A measured VR cvar preset keeps only the cvars that saved ≥ 0.3 ms each.
  - The desktop mirror defaults to one eye.
  - Every change comes with its measured before/after on the laptop.
- **Out of scope** (recorded so nobody starts them; they need new render-pass ownership):
  - Foveated or radial-density rendering.
  - NVAPI variable rate shading.
  - Single render + reprojection, or alternate-eye rendering.
  - Skipping the native upscale draw.

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Each task stages only its listed files (`git add <paths>`), never `git add -A`, never `git stash`.
- Pinned exe SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. No new engine RVA for anything except `pm_noBob` (Task 5) and the cvars of Task 9. Each new cvar is validated at runtime like `CameraComfort::apply` (name-pointer, type flags, cached value range) and restored at session end.
- No Bonelab code or assets; techniques in our own words only.
- Every new behaviour has a knob that restores today's behaviour:
  - `legacy-authored-camera` (R1)
  - `no-camera-smoothing` (R2)
  - `no-comfort-vignette` (R3)
  - `no-turn-ramp` (R3)
  - `no-governor` (R5; replaces the positive `governor` knob)
  - `no-vr-cvar-preset` (R5)
  Knobs are read once in `StartProbe` next to the existing ones (~`dxgi_probe.cpp:8020-8191`). After plan C Task 2 lands, each negative knob also gets a row in `tools/default_features.json` (`kind: disable`).
- New C++ checks are registered in `tools\build-msvc.cmd` with `/std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc`.
- Linux harness: `python3 tools/linux/run_build_cmd.py --keep-going --only '<regex>' candidate d2vr_render_probe_arm_v601.dll`. Baseline 518 passed / 1 failed (`fsr_taa_gpu_check --fsr4`, environment-only) / 27 skipped; must not regress.
- Sim rig rules:
  - Never run while the user plays (the rig refuses if the game or vrserver is running).
  - Saves are backed up by the rig.
  - Each measured run is bounded (`--seconds 90`).
  - The same scenario and save are used before and after a change.
  - The NVIDIA GPU must be free (the Bonsai LLM service stays disabled).
- Testing honesty: unit checks pin logic. Only the headset declares the wobble gone (R1-R3). Sim-rig numbers are GPU and CPU cost without a real HMD; they are valid for before/after comparison, not for absolute headset fps.
- Commits end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. **A snap in the middle of a takedown that points the player away from the victim.** At takedown start, the scene heading takes the native camera yaw at once (one snap). After that, only changes ≥ 30° snap. Pinned by Task 2 `enter_takes_native_yaw_once` and `small_native_yaw_changes_do_not_rotate`.
2. **Smoothing that makes a real fall or a Blink feel laggy or sink through the floor.** A drop faster than 3 m/s held for 100 ms follows directly, and any jump > 1.0 m in one frame resets the filter. Pinned by Task 3 `fall_passes_through`, `teleport_resets`, `blink_up_resets`.
3. **The authored path failing for a scene whose native camera is not level (looking down into a keyhole).** The HMD gives pitch. The game's pitch is dropped, and the keyhole view must still show the keyhole: the scene frame's yaw comes from the native forward, projected to level. A native forward pointing straight up or down falls back to the previous yaw. Pinned by Task 2 `vertical_native_forward_keeps_previous_yaw`.
4. **A governor that drops a strong GPU to low resolution, or flaps.** The floor is only reached on sustained misses; the upper bound returns to the preference; the existing hysteresis and lockout stay. Pinned by Task 7 `native_preference_steps_down_then_back`, `never_below_floor`, `no_change_without_misses`.
5. **The cvar preset leaking into the user's flat game.** Every runtime cvar change is restored at session end, and a later user or game change is preserved. Pinned by Task 9 `preset_restores_on_end` and `preserves_later_change` (the `CameraComfort::restore` pattern).

---

## File Structure

Created:
- `src/comfort_meter.hpp`, `tests/comfort_meter_check.cpp` (Task 1)
- `src/authored_view.hpp`, `tests/authored_view_check.cpp` (Task 2)
- `src/anchor_smoother.hpp`, `tests/anchor_smoother_check.cpp` (Task 3)
- `src/comfort_vignette.hpp`, `tests/comfort_vignette_check.cpp` (Task 4)
- `src/perf_meter.hpp`, `tests/perf_meter_check.cpp`, `tools/perf_run_report.py`, `docs/VR-PERF-LAPTOP.md` (Task 6)
- `src/render_profile.hpp`, `tests/render_profile_check.cpp` (Task 8)
- `src/vr_cvar_preset.hpp`, `tests/vr_cvar_preset_check.cpp` (Task 9)
- `docs/VR-comfort-perf-d-TEST-CHECKLIST.md` (Task 11)

Modified:
- `src/dxgi_probe.cpp` (`apply_head_view` ~3765-4830, status JSON ~5252, `StartProbe` knobs ~8020-8191, governor ~1209/8129)
- `src/turn_control.hpp` (ramp)
- `src/game_xr.hpp` (vignette quad via `prepare_overlays` ~593)
- `src/camera_comfort.hpp` (`pm_noBob`)
- `src/vr_perf_governor.hpp`, `src/vr_preferences.hpp` (mirror default, first-run profile)
- `docs/PLAYING.md`, `tools/build-msvc.cmd`

---

## Task 1: Comfort telemetry (evidence before and after)

**Files:** Create `src/comfort_meter.hpp`, `tests/comfort_meter_check.cpp`. Modify `src/dxgi_probe.cpp` (feed in `apply_head_view` for eye 0; status `"comfort"` next to `"carry"` ~5252), `tools/build-msvc.cmd`.

**Interfaces:**
- `struct ComfortSample {double t_s; Vec3 anchor_pos; double anchor_yaw; double hmd_yaw; bool authored;};`
- `class ComfortMeter { void feed(const ComfortSample&) noexcept; std::string json() const; };`. The meter reports:
  - `authored_ms` (total time in authored view), `authored_entries`;
  - `anchor_v_peak` (m/s, vertical anchor speed), `anchor_jerk_events` (frames whose vertical anchor acceleration exceeds 30 m/s²);
  - `view_yaw_rate_peak` (rad/s of view yaw not caused by the HMD: the change in anchor_yaw per second), `view_yaw_events` (frames > 1.5 rad/s).
  Non-finite samples and dt ≤ 0 or > 0.25 s are skipped and counted (`skipped`).
- Produces `"comfort":{...}` in xr-live-status.json; Tasks 2-4 and 11 read it.

- [ ] **Step 1: Failing tests:**
  - `steady_anchor_reports_zero`: 90 Hz, static: peaks 0, events 0.
  - `landing_dip_counts_jerk`: a 10 cm dip over 50 ms gives `anchor_jerk_events >= 1`.
  - `authored_time_accumulates`: 1 s authored gives `authored_ms` 1000 ± 12 and `authored_entries == 1`.
  - `yaw_rate_from_anchor_not_hmd`: HMD yaw rotating 3 rad/s with the anchor fixed gives 0 events; anchor yaw at 2 rad/s gives events.
  - `bad_samples_skipped`: NaN and dt 0.5 are counted in `skipped`, not in the peaks.
- [ ] **Step 2: Run, see fail:** `--only 'comfort_meter'`.
- [ ] **Step 3: Implement**, green. Wire `feed` once per frame (eye 0) after `owned_anchor`, using `source_anchor.position` before any smoothing (Task 3 adds a second field `view_pos` later), the anchor heading and the HMD yaw (`tracked_yaw`), plus `authored_camera`.
- [ ] **Step 4: Baseline run on the sim rig:** `linux/d2vr-linux sim --scenario idle --seconds 90 --status-field comfort` (and the walking scenario, if one exists: `ls tools/linux/scenarios`). Paste the block into the commit message as the "before" numbers.
- [ ] **Step 5: Mutation:** drop the authored accumulation; `authored_time_accumulates` must fail. Restore. Full harness, then commit `feat: comfort telemetry (authored time, anchor jerk, view yaw rate)`.

## Task 1b: Sim rig runs a candidate DLL without touching the user's install, plus an idle scenario (R4 tooling)

Added 2026-10-21 (ledger ruling): the sim rig runs whatever DLL is installed in the user's mod home, so a new build could not be measured without changing the user's install.

**Files:** Modify `linux/d2vr_linux/sim.py`, `linux/d2vr_linux/cli.py`, `linux/tests/test_sim.py`, `linux/README.md` (section on the sim rig). Create `tools/linux/scenarios/idle.py`.

**Interfaces:**
- `d2vr-linux sim --candidate-dll PATH`:
  - Before the launch it checks that PATH is a PE DLL whose name matches the DLL the mod home uses (read the same marker or manifest the installer uses).
  - It copies the installed DLL to `<backup-root>/<run-id>/installed-<name>` and verifies the copy by SHA-256.
  - It then places the candidate in the mod home atomically.
  - After the run it always restores the original: in a `finally`, through the same guard the saves and knobs use, verified by SHA-256.
  - On restore failure it prints the exact manual command and exits non-zero.
  - `--dry-run` prints the plan, including both hashes.
  - `--restore` also restores a DLL left by an interrupted run (the backup records it).
- `tools/linux/scenarios/idle.py`: `build(lead_ms) -> list[dict]` with the same segment format as `oil_tank_drop.py`. It runs the neutral lead-in with A pulses on menus only, then 90 s of standing still. The head yaws ±20° slowly over 10 s and pitches ±10°; there is no stick input.

- [ ] **Step 1: Failing tests** in `linux/tests/test_sim.py`:
  - `test_candidate_dll_swapped_and_restored` (fake mod home: after run, installed bytes equal the original);
  - `test_candidate_restored_after_launch_failure`;
  - `test_candidate_wrong_name_refused`;
  - `test_restore_recovers_interrupted_candidate`;
  - `test_dry_run_prints_hashes`;
  - `test_idle_compiles` (monotonic segments, last end ≤ 120000, no stick input in gameplay segments).
- [ ] **Step 2: Implement**, run `python3 -m unittest linux.tests.test_sim`, green.
- [ ] **Step 3: Real run, only if the game and SteamVR are not running** (the rig refuses otherwise; never kill them). Build the current candidate with the harness, then run `linux/d2vr-linux sim --scenario idle --candidate-dll <harness output dll> --status-field comfort`. Write the `comfort` block into `docs/VR-PERF-LAPTOP.md` (create it with a "Comfort baseline" section) as the plan-D "before" numbers. Check afterwards that the installed DLL hash equals the pre-run hash. If the rig cannot run, record "baseline outstanding: <reason>".
- [ ] **Step 4: Commit** `feat(linux): sim rig runs a candidate DLL and restores the install; idle scenario`.

## Task 2: The head owns rotation in authored scenes (R1)

**Files:** Create `src/authored_view.hpp`, `tests/authored_view_check.cpp`. Modify `src/dxgi_probe.cpp` (~4735-4741 and the authored FOV ~3814: the +5% widening is kept only with `legacy-authored-camera`), `docs/PLAYING.md`, `tools/build-msvc.cmd`.

**Interfaces:**
- `class AuthoredHeading { std::optional<double> update(const GameCameraPose& native, bool authored, std::uint64_t epoch) noexcept; void reset() noexcept; std::uint64_t snaps() const; };`
  - On entering authored (the first `authored==true` after false, or an epoch change), it returns the native forward's level yaw.
  - After that it returns the held yaw, changed only when |native_yaw - held| ≥ `snap_rad` (30° = 0.5236). It then snaps to the native yaw and counts `snaps`.
  - A native forward with |forward.z| > 0.97 (vertical) keeps the held yaw.
  - Returns nullopt when not authored.
- `GameCameraPose authored_head_view(const GameCameraPose& native_centre, double heading_yaw, const TrackedPose& centre, const TrackedPose& eye, double units_per_metre=1)`.
  - Position: the native centre position plus the eye offset, rotated by the HMD into the heading frame (the same 0.15 m rejection as `authored_eye_camera`).
  - Basis: HMD orientation in the level frame whose yaw is `heading_yaw` (the same convention as `to_game_pose` with a levelled anchor).
  - It throws `std::invalid_argument` on non-finite input; the caller treats that like a failed `authored_eye_camera` today.
- Knob `legacy-authored-camera`: keep today's `authored_eye_camera` path.

- [ ] **Step 1: Failing tests:**
  - `enter_takes_native_yaw_once`
  - `small_native_yaw_changes_do_not_rotate`: native yaw drifts 0 → 25° over 1 s, and the held yaw stays.
  - `large_change_snaps_once`: a 40° step gives one snap.
  - `vertical_native_forward_keeps_previous_yaw`
  - `epoch_change_reenters`
  - `head_rotation_moves_view`: HMD yaw +20° turns the output basis +20° about up, while native roll 15° and pitch -30° do not appear in the output.
  - `ipd_offset_preserved`: eyes 64 mm apart produce outputs 64 mm apart.
  - `non_finite_throws`
- [ ] **Step 2: Run, see fail:** `--only 'authored_view'`.
- [ ] **Step 3: Implement**, green.
- [ ] **Step 4: Wire.** In the authored branch replace the basis and position copy with `authored_head_view(source_anchor, *heading, centre_pose, eye_pose)`. `heading` comes from `authored_heading.update(*frozen_native, true, head_pose_epoch)` once per frame (eye 0), and both eyes use it. In the non-authored branch call `authored_heading.update(..., false, ...)`. Status: `"comfort".authored_snaps`.
- [ ] **Step 5: Routing source check** `tests/authored_view_routing_check.py` (registered):
  - `authored_head_view(` is used in `apply_head_view`;
  - `authored_eye_camera(` remains only under the `legacy_authored_camera` flag;
  - the knob string is read.
- [ ] **Step 6: Sim rig:** a takedown cannot be scripted without a save. Run `--scenario idle` to prove no regression in ordinary play: `comfort.authored_ms` 0, no crash. Add a headset item for Task 11: "choke a guard from behind; turn your head during the animation; the view follows your head; no spin".
- [ ] **Step 7: Mutation:** set `snap_rad` to 0; `small_native_yaw_changes_do_not_rotate` must fail. Restore. Full harness, docs (`PLAYING.md` comfort section), then commit `feat: the headset owns view rotation during takedowns and scripted scenes`.

## Task 3: Smooth the engine camera's vertical motion (R2)

**Files:** Create `src/anchor_smoother.hpp`, `tests/anchor_smoother_check.cpp`. Modify `src/dxgi_probe.cpp` (apply to `source_anchor.position` in the non-authored path before `head_body_anchor.get`, and in the authored path to the native centre; feed `ComfortMeter` with the smoothed `view_pos` as well), `docs/PLAYING.md`, `tools/build-msvc.cmd`.

**Interfaces:**
- `struct AnchorSmootherConfig {double tau_s=.12, max_rate_mps=1.5, fall_rate_mps=3, fall_hold_s=.1, teleport_m=1.0;};`
- `class AnchorSmoother { Vec3 update(const Vec3& raw, double dt, std::uint64_t epoch) noexcept; void reset() noexcept; std::uint64_t resets() const; };`
  - The vertical axis (game z) follows `raw.z` with a first-order filter (`tau_s`) and a speed limit (`max_rate_mps`).
  - When `raw.z` has been falling faster than `fall_rate_mps` for `fall_hold_s`, output z = raw z (a real fall).
  - A jump of more than `teleport_m` in one update (any axis), an epoch change, dt ≤ 0, dt > 0.25 or non-finite input: reset to raw.
  - Horizontal axes pass through unchanged, because locomotion latency causes sickness.
  - Units: the caller converts game units with `units_per_metre` (1 today).
- Knob `no-camera-smoothing`.

- [ ] **Step 1: Failing tests:**
  - `landing_dip_is_damped`: a 10 cm dip and return over 120 ms gives an output dip ≤ 4 cm.
  - `crouch_transition_rate_limited`: a 0.5 m step down gives an output speed ≤ 1.5 m/s, converging within 0.6 s.
  - `fall_passes_through`: a 5 m/s fall gives output equal to raw after 100 ms.
  - `teleport_resets`: a 3 m horizontal jump resets.
  - `blink_up_resets`: a +1.5 m vertical jump resets.
  - `horizontal_untouched`
  - `epoch_resets`
  - `bad_dt_resets`
  - `stairs_are_smooth`: 0.2 m steps every 0.35 s give an output with no step > 0.02 m per 11 ms frame.
- [ ] **Step 2: Run, see fail:** `--only 'anchor_smoother'`.
- [ ] **Step 3: Implement**, green.
- [ ] **Step 4: Wire** both paths and the knob. Status `"comfort".smoother_resets`.
- [ ] **Step 5: Sim rig before/after:** with a walking or crouching scenario, compare `comfort.anchor_jerk_events` on `view_pos` against `anchor_pos`, and paste both. If no such scenario exists, add `tools/linux/scenarios/crouch_walk.py`: 10 s walk, 3 crouch toggles, then stand. Copy the lead-in helper from `oil_tank_drop.py`, plus a compile test in `linux/tests/test_sim.py`.
- [ ] **Step 6: Mutation:** set `max_rate_mps` to 100; `crouch_transition_rate_limited` must fail. Restore. Full harness, then commit `feat: smooth and speed-limit the engine camera's vertical motion`.

## Task 4: Turn ramp and locomotion vignette (R3)

**Files:** Create `src/comfort_vignette.hpp`, `tests/comfort_vignette_check.cpp`. Modify `src/turn_control.hpp` (ramp), `tests/turn_control_check.cpp` (or the existing turning check; find with `ls tests | grep -i turn`), `src/game_xr.hpp` (vignette quad), `src/dxgi_probe.cpp` (knobs, feed), `docs/PLAYING.md`, `tools/build-msvc.cmd`.

**Interfaces:**
- `TurnControl`: smooth-mode speed is multiplied by a ramp from 0 to 1 over `ramp_s = .15`, starting when the stick leaves the 0.15 deadzone and resetting when it returns. `Config` gains `double ramp_s=.15` (0 = off, set by the `no-turn-ramp` knob).
- `struct VignetteInput {double move_speed_mps; double turn_rate_rad_s; double dt;};`
- `class ComfortVignette { float update(const VignetteInput&) noexcept; };` returns an opacity from 0 to `max_opacity`.
  - Target = `max_opacity * clamp(max(move/3.0, turn/1.5), 0, 1)`.
  - Rise time 0.1 s, fall time 0.3 s.
  - `max_opacity` .6 by default; the knob file `comfort-vignette` may contain 0..1.
  - `no-comfort-vignette` forces 0.
  - Snap turns produce no vignette (they are instant).
  - `move_speed_mps` is the artificial (stick) locomotion speed only, not the head's.
- `GameXr`: a head-locked quad (space = view space if the runtime supports `XR_REFERENCE_SPACE_TYPE_VIEW`, which is already used for the hint layer if present; otherwise rebuilt each frame from the head pose). It shows a radial-alpha texture (transparent centre radius 0.55, black edge), created in `prepare_overlays`, and is submitted last with the opacity as a colour-scale alpha via `XR_KHR_composition_layer_color_scale_bias` when available. Without the extension, the opacity is baked into the texture alpha by a cheap clear plus a precomputed alpha ramp on update, at most 10 Hz.

- [ ] **Step 1: Failing tests:**
  - `ramp_starts_at_zero_reaches_full_at_150ms`
  - `ramp_resets_on_centre`
  - `ramp_zero_is_old_behaviour`
  - `vignette_zero_at_rest`
  - `vignette_rises_with_stick_speed`
  - `vignette_falls_slowly`
  - `snap_turn_no_vignette`
  - `knob_strength_parsing` (`".3"` → .3, `"abc"` → default, `"2"` → clamp 1)
- [ ] **Step 2: Run, see fail:** `--only 'turn_control|comfort_vignette'`.
- [ ] **Step 3: Implement**, green.
- [ ] **Step 4: Wire.** Feed the vignette from the locomotion stick magnitude times the game's walk speed (or a 3 m/s nominal if no speed read exists, documented) and the smooth-turn rate. The quad is created between frames only (the SteamVR rule in `game_xr.hpp:237`). The status block `"comfort".vignette` reports `{"enabled", "max", "frames_shown"}`.
- [ ] **Step 5: Sim rig:** a run with stick locomotion (scenario from Task 3) shows `vignette.frames_shown > 0`, no `end_frame_rejections` increase, and no `swapchain_creates_in_frame`.
- [ ] **Step 6: Mutation:** let snap-turn input count toward the vignette; `snap_turn_no_vignette` must fail. Restore. Full harness, then commit `feat: smooth-turn ramp and locomotion comfort vignette`.

## Task 5: `pm_noBob` in the comfort set (R3)

**Files:** Modify `src/camera_comfort.hpp`, `tests/camera_comfort_check.cpp` (find the existing check: `grep -ln CameraComfort tests`), `docs/image-quality-research.md`.

**Interfaces:**
- A third `Profile` entry for `pm_noBob` at RVA `0x39a3490`: type and value `1`. The name RVA comes from the pinned exe. Find it with the Ghidra project `~/.local/share/d2vr-re/snapshots/dishonored2-ghidra-2026-10-19` (string `pm_noBob`, then the cvar header's name pointer at +0x30) and record it in a comment with how it was found.
- `entries_` grows to 3. Validation stays all-or-nothing.

- [ ] **Step 1: Verify the RVA and name.** Read the header bytes from the exe image: `python3 -I` with `pefile` or a manual section map. Check the name pointer, the type flags (`&7`) and the default value, and write them into the commit message. If any check fails, stop: the task ends with "not applied", the reason, and no code change.
- [ ] **Step 2: Failing test** `applies_three_entries_atomically` (the existing fake-memory pattern), and `third_entry_mismatch_changes_nothing`.
- [ ] **Step 3: Implement**, green. Run `--only 'camera_comfort'`, then the full harness.
- [ ] **Step 4: Mutation:** a wrong type for the new entry; `third_entry_mismatch_changes_nothing` must fail. Restore. Commit `feat: pm_noBob joins the VR camera comfort overrides`.

## Task 6: Performance telemetry and the laptop baseline (R4)

**Files:** Create `src/perf_meter.hpp`, `tests/perf_meter_check.cpp`, `tools/perf_run_report.py`, `tests/perf_run_report_check.py`, `docs/VR-PERF-LAPTOP.md`. Modify `src/dxgi_probe.cpp` (feed where the governor is fed ~1209; status `"perf"`), `tools/build-msvc.cmd`.

**Interfaces:**
- `class PerfMeter { void feed(std::uint64_t pace_ns, std::uint64_t period_ns, bool missed, std::uint64_t mod_cpu_ns, float render_fraction) noexcept; std::string json() const; };`
  - Rolling window: the last 900 frames. The `json` reports `pace_ms_p50`, `pace_ms_p95`, `missed_fraction`, `mod_cpu_ms_mean`, `mod_cpu_ms_p95`, `fraction`, `period_ms` and `frames`.
  - Percentiles come from a fixed 900-entry ring with a sorted copy on `json()` only (once per status write, not per frame).
- `mod_cpu_ns`: the time spent inside our Present hook excluding `xrWaitFrame`. Use the existing in-hook timing if `render-perf` already measures it (`grep -n 'render_perf' src/dxgi_probe.cpp`); otherwise use `QueryPerformanceCounter` around the hook body minus the wait.
- `tools/perf_run_report.py <xr-live-status.json> [xr-frame-timing.csv]` prints one line, `p50 / p95 / missed% / mod cpu / fraction`, and compares it with a previous report given via `--before <json>`.

- [ ] **Step 1: Failing tests:**
  - C++: `percentiles_known_values`, `missed_fraction`, `window_rolls_over`, `ignores_zero_period`.
  - Python: `report_formats_line`, `before_after_delta`.
- [ ] **Step 2: Run, see fail; implement; green.** `--only 'perf_meter|perf_run_report'`.
- [ ] **Step 3: Laptop baseline.** On the sim rig with today's defaults (FSR 3 Native, resolution 1.0, mirror both eyes), make three bounded runs: `linux/d2vr-linux sim --scenario idle --seconds 90 --status-field perf` (and the walking scenario). Write `docs/VR-PERF-LAPTOP.md`:
  - the hardware (`nvidia-smi --query-gpu=name,memory.total --format=csv`, `lscpu | head`);
  - the scenario;
  - each run's report line;
  - and the A/B rows: aa mode Native vs FSR 3 Quality vs Balanced (vr-settings.cfg), mirror both vs one eye, governor on vs off.
  This document is the "before" for Tasks 7-10.
- [ ] **Step 4: Commit** `feat: perf status block and laptop baseline`.

## Task 7: Governor on by default, from Native down to 0.5 (R5)

**Files:** Modify `src/vr_perf_governor.hpp`, `tests/vr_perf_governor_check.cpp` (existing; find it with `ls tests | grep -i governor`), `src/dxgi_probe.cpp` (~1209, `reset_perf_governor`, knob ~8129: `governor` becomes `no-governor`), `docs/PLAYING.md`.

**Interfaces:**
- `PerfGovernorConfig` defaults: `min_scale=.5f`. `max_scale` = the preference fraction, where Native = 1.0 means the top step is "off" (`apply_upscale` treats ≥ .999 as off).
- The first step down from Native goes to `.94`. The existing hysteresis, `up_hold`, `up_lockout_after_down` and `settle` stay.
- Before shipping, Step 3 measures the cost of a scale change. If the Native ↔ scaled transition stalls longer than 0.6 s, the governor starts at the FSR 3 Quality fraction instead (ruling recorded in the commit).

- [ ] **Step 1: Failing tests:**
  - `native_preference_steps_down_then_back`: misses step down to .94 and lower; calm windows after the lockout step back up to 1.0 (off).
  - `never_below_floor`
  - `no_change_without_misses`
  - `quality_preference_caps_at_077`
- [ ] **Step 2: Implement**, green. `--only 'vr_perf_governor'`.
- [ ] **Step 3: Measure** on the sim rig: one 90 s run with the governor on, from Native. Record the number of changes, the longest stall (`perf.pace_ms_p95` and the frame-timing CSV max) and the final fraction in `docs/VR-PERF-LAPTOP.md`.
- [ ] **Step 4: Mutation:** set `min_scale` to .3; `never_below_floor` must fail (the test pins .5). Restore. Full harness, then commit `feat: dynamic resolution on by default, Native down to 50%`.

## Task 8: First-run render profile from the GPU (R5)

**Files:** Create `src/render_profile.hpp`, `tests/render_profile_check.cpp`. Modify `src/vr_preferences.hpp` (mirror default `right_eye`), `src/dxgi_probe.cpp` (read the adapter desc when `vr-settings.cfg` is absent: `IDXGIAdapter::GetDesc` `DedicatedVideoMemory`, `VendorId`), `docs/PLAYING.md`.

**Interfaces:**
- `struct RenderProfile {int temporal_aa; double resolution;};`
- `RenderProfile first_run_profile(std::uint64_t vram_bytes, unsigned vendor_id) noexcept`:
  - ≤ 8 GiB gives FSR 3 Quality (3) at 1.0;
  - ≤ 12 GiB gives FSR 3 Native (2) at 1.0;
  - larger gives FSR 3 Native at 1.0;
  - an unknown (0) VRAM gives FSR 3 Quality.
  Applied only when no `vr-settings.cfg` exists; the user's saved settings always win.
- `VrPreferences::mirror` default becomes `MirrorMode::right_eye`. An existing cfg keeps its value.

- [ ] **Step 1: Failing tests:**
  - `laptop_3070_gets_quality`: 8 GiB, NVIDIA 0x10DE.
  - `big_gpu_gets_native`
  - `unknown_vram_is_quality`
  - `saved_settings_win` (pure helper `choose_preferences(optional<VrPreferences> saved, RenderProfile)`)
  - `mirror_default_one_eye`
- [ ] **Step 2: Implement**, green. `--only 'render_profile|vr_preferences'`.
- [ ] **Step 3: Measure** mirror both vs right eye on the sim rig (`perf.mod_cpu_ms_mean`) into `docs/VR-PERF-LAPTOP.md`.
- [ ] **Step 4: Commit** `feat: first-run render profile from VRAM; desktop mirror shows one eye`.

## Task 9: Measured VR cvar preset (R5)

**Files:** Create `src/vr_cvar_preset.hpp`, `tests/vr_cvar_preset_check.cpp`. Modify `src/dxgi_probe.cpp` (apply at session start next to `CameraComfort`, restore at session end; knob `no-vr-cvar-preset`), `docs/VR-PERF-LAPTOP.md`, `docs/PLAYING.md`.

**Interfaces:**
- Step 1 decides the candidates by measurement, through the game config file on the sim rig (no RVA needed to measure): append `<cvar> <value>` to the Proton prefix game config used by the rig (the rig already appends `r_fullscreen "0"`; back up the config first, the same way), then make a 90 s run per cvar.
- Candidates:
  - Shadows: `r_shadowMapSize`-class names. Find the real names in `docs/image-quality-research.md` and `strings` on the exe.
  - Volumetric fog quality.
  - Reflection quality (SSR).
  - View distance / LOD scale.
  - Particle quality.
  - Decal count.
  - Texture streaming budget (8 GB VRAM).
- A cvar enters the preset only if it saves ≥ 0.3 ms p50 pace in two runs and the screenshot (`--screenshot` in the rig if available) shows no broken image.
- `class VrCvarPreset` follows `CameraComfort`: a constexpr table `{rva, name_rva, type, int, float}`; `apply(base, read, exchange)` is all-or-nothing with identity validation; `restore(read, exchange)` preserves later changes.
  - The RVAs and name RVAs come from Ghidra (as in Task 5), one comment per entry with its measured gain.
  - If no candidate qualifies, the task ships only the measurement document and no code (recorded as a ruling).

- [ ] **Step 1: Measure** and write the table (cvar, value, p50 before/after, two runs, kept yes/no) into `docs/VR-PERF-LAPTOP.md`. Restore the game config from the backup afterwards and verify that `diff` against the backup is empty.
- [ ] **Step 2: Failing tests** (fake memory, the `camera_comfort_check` pattern):
  - `preset_applies_all_or_nothing`
  - `preset_restores_on_end`
  - `preserves_later_change`
  - `identity_mismatch_changes_nothing`
- [ ] **Step 3: Implement**, green; wire, with the knob.
- [ ] **Step 4: Sim rig after-run:** check `perf` against the baseline and add the row.
- [ ] **Step 5: Mutation:** skip the restore; `preset_restores_on_end` must fail. Restore. Full harness, then commit `feat: measured VR graphics preset applied at runtime and restored`.

## Task 10: Tail spikes (R5)

**Files:** `docs/VR-PERF-LAPTOP.md` (investigation), plus the code the evidence names, each change with its own named test.

- [ ] **Step 1: Evidence.** From three sim runs' `xr-frame-timing.csv`, list every frame > 50 ms with the hook phase that took the time (the CSV columns: mirror Present, discovery, pre-XR, post-XR, xrEndFrame). Report the top three causes.
- [ ] **Step 2: Fix only causes that are our own code.** Known candidates:
  - The desktop mirror Present spike: with the mirror off or at one eye (Task 8), check whether it remains.
  - Housekeeping work on the render thread, such as status JSON writes or file I/O: move it off the render thread or rate-limit it. `grep -n 'xr-live-status' src/dxgi_probe.cpp` shows the writer and how often it runs.
  Each fix gets a pure, testable piece (for example a `RateLimiter` with tests `at_most_once_per_interval`, `first_call_runs`) and a before/after row.
- [ ] **Step 3: Commit** per fix: `perf: <cause>`.

## Task 11: Headset checklist (all requirements)

**Files:** Create `docs/VR-comfort-perf-d-TEST-CHECKLIST.md`; modify `docs/PLAYING.md` (cross-link).

- [ ] **Step 1:** One line per check, each with its expected result and the status field it reads:
  1. Choke and stab takedowns from behind: turn your head during the animation, and the view follows your head with no spinning (`comfort.authored_ms > 0`, `authored_snaps` small).
  2. A keyhole and a mantle onto a ledge: no rotation imposed.
  3. Jump down a floor, crouch and stand, walk stairs: no jolts (`comfort.anchor_jerk_events` against the Task 1 before-number).
  4. Blink up to a roof: an instant move, no slow slide.
  5. Smooth turn: eases in; the vignette narrows during stick walking and turning. `no-comfort-vignette` removes it.
  6. 10 minutes of play: the perf block shows `missed_fraction < 0.02` at the headset's refresh rate. In Steam Link set 72 Hz or 80 Hz if 90 Hz misses.
  7. Each `legacy-*` and `no-*` knob restores the old behaviour.
- [ ] **Step 2: Run all suites once;** commit `docs: comfort and performance D headset checklist`.

---

## Self-Review

- **Coverage:**
  - R1: Task 2. R2: Task 3. R3: Tasks 4 and 5. R4: Tasks 1 and 6. R5: Tasks 7-10.
  - Headset confirmation: Task 11.
  - Out-of-scope items are listed under Requirements.
- **Order:** measurement before change (Task 1 before 2-4; Task 6 before 7-10).
- **Interfaces:**
  - `ComfortMeter` (Task 1) gains `view_pos` in Task 3 (stated in both).
  - The `PerfMeter` json is read by `perf_run_report.py` (Task 6) and by Tasks 7-10.
  - Knob names are consistent with Global Constraints.
- **Known risks:**
  - (a) In authored scenes the native camera may also move the position in a large arc (a takedown swing). Task 3's smoother also applies there, and Task 2 keeps the position native, because a position mismatch would put the player inside the victim.
  - (b) The vignette needs a view-space or per-frame head-locked quad. If SteamVR rejects it, Task 4 falls back to off with a status reason, and that is a finding, not a failure.
  - (c) Sim-rig numbers have no real HMD compositor load; they are relative.
  - (d) The Task 5 and 9 RVAs are found once and validated at runtime, so a wrong one fails closed with no change.
