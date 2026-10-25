# Scripted-Moment Recenter and VR Cutscenes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Climbs, takedowns, chokes, finishers and authored dialogs play in front of where the player looks, through a
gentle 0.4 s world turn that is kept afterwards. In-engine cutscenes render in stereo VR, with a three-mode comfort setting
(VR comfort as the default, VR full, Theater).

**Architecture:**
- A pure `ScriptedRecenter` helper owns an extra yaw offset composed into the head origin for authored views. It eases
  in at entry, re-aims on a 30° heading snap, and folds into `HeadCalibration` at exit with continuity.
- A read-only spike decides go or no-go for cutscenes. On go, a pure `CutscenePolicy` classifies mask-32 frames, and
  the existing authored head-view path renders them in stereo, with cut detection.
- Two VR menu rows and the launcher mirror expose the settings.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook), C# launcher (.NET 8 WinForms), the Python sim rig.

**Spec:** `docs/superpowers/specs/2026-10-25-camera-recenter-vr-cutscenes-design.md`. Read all of it, especially
"Settled after the builder check".

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`. Plan workspace (git-excluded):
  `.superpowers/sdd/2026-10-25-camera-recenter-vr-cutscenes/`. The ledger is `progress.md` there.
- Pinned exe `Dishonored2.exe`, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
- No new *kinds* of engine writes. G1 writes nothing new. G2 may reuse the gameplay head-view writes and restores.
- Values, verbatim from the spec:
  - dead zone **10°**;
  - entry and re-aim ease **0.4 s** smoothstep;
  - heading re-aim at **30°** (`AuthoredHeading::snap_rad`);
  - a cut is a native camera jump of **> 0.5 m or ≥ 30° yaw in one frame**, and hard cuts re-aim **instantly**;
  - an exit residual is eased out over **0.4 s**;
  - sim target: yaw residual **< 3°** at ease end.
- Cutscene modes `vr_comfort` (default), `vr_full`, `theater`. Setting "Recenter on scripted moments", default on.
- Off-switch knobs (absent by default): `build/no-scripted-recenter`, `build/no-vr-cutscenes`. A diagnostic trace knob:
  `build/scripted-view-trace` → `artifacts/scripted-view.csv`, at most 4096 rows.
- Build: `python3 tools/linux/run_build_cmd.py --only '<regex>' candidate d2vr_render_probe_arm_v610.dll`. Full harness:
  drop `--only` and add `--keep-going`.
- Test registration in `tools/build-msvc.cmd`: a C++ check gets a `cl … tests\X.cpp` line and a run line next to
  `authored_view_check` (~404–406). A Python check is a `python tests\<name>.py` line plus its errorlevel line.
- Linux tests: `python3 -m unittest discover -s linux/tests`.
- Launcher tests: `DOTNET_ROOT=~/.dotnet-ms ~/.dotnet-ms/dotnet run --project tests/launcher_features`.
- Harness gate: no new failures against the v609 baseline (632 passed / 1 failed `fsr_taa_gpu_check --fsr4`,
  environment-only). Launcher 127 pass. `linux/tests` 230 OK.
- **Sim safety**, on every Tier B run:
  - `PAUSE-SIM` absent;
  - no `Dishonored2.exe` or `vrserver` running (`pgrep -x` guards that can't match their own shell line);
  - never kill by pattern;
  - record the installed DLL's sha256 before the run and require it unchanged afterwards, with no knob left in `build/`;
  - the GNOME screen unlocked;
  - run with `linux/d2vr-linux sim --candidate-dll <built DLL> --scenario <name> --lead 45 --seconds <n>`.
- Testing honesty: Tier A/B prove logic and numbers; comfort is Tier C only.
- Commit trailer, on every commit:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01BJuGoAt65W8LH5YcXkajFq`

## Review Focus

1. **The player presses "Recenter view" or pauses during the ease.** Expected: a recenter epoch drops the offset
   (no fold, no jump beyond the recenter itself); pause keeps the offset and resumes. Test (Task 1):
   `epoch_change_drops_offset`; (Task 2) the pause path in `scripted_recenter_routing_check.py`.
2. **Back-to-back scripted moments** (a mantle chained into a second mantle, or a takedown straight into a climb) before
   the first ease ends. Expected: the second entry computes its delta from the current composed origin, with no double
   turn and no jump. Test (Task 1): `reentry_during_ease_is_continuous`.
3. **Headset tracking lost (non-finite or stale HMD pose) at entry.** Expected: no delta is computed and no turn
   happens; the next valid frame inside the moment may start it. Test (Task 1): `non_finite_hmd_no_turn`.
4. **A delta near ±180°** (the player looking backwards). Expected: it wraps to the shortest turn and never spins the
   long way, and a ±180° tie picks a stable side. Test (Task 1): `wrap_near_180_shortest`.
5. **A cutscene that alternates between in-engine and video frames, or loses one eye for a single frame.** Expected: the
   sticky theater fallback for the rest of that cutscene, with no per-frame flicker. Test (Task 5):
   `fallback_sticks_until_exit`.

---

### Task 1: Pure `ScriptedRecenter` helper and `HeadCalibration::rotate_origin` (Tier A)

**Files:**
- Create: `src/scripted_recenter.hpp`
- Modify: `src/head_calibration.hpp` (add `rotate_origin`)
- Test: `tests/scripted_recenter_check.cpp`, plus additions to `tests/head_calibration_check.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct ScriptedRecenterParams {double dead_zone_rad=0.17453292519943295,ease_s=0.4;};
  struct ScriptedRecenterInput {
      double t;                    // seconds, monotonic
      bool authored;               // authored_camera this frame
      std::optional<double> hmd_rel_yaw; // tracked_yaw(centre_pose, head_origin) wrapped to (-pi,pi]; nullopt if not finite
      std::optional<double> heading; // AuthoredHeading result this frame
      std::uint64_t heading_snaps; // AuthoredHeading::snaps()
      std::uint64_t epoch;         // reference epoch ^ player/camera/generation identity hash
      std::optional<double> body_yaw; // gameplay body heading (anchor yaw) when !authored, for the exit fold
      bool instant;                // a hard cut this frame (Task 6); false in G1
  };
  struct ScriptedRecenterOutput {double offset_rad;            // compose: origin' = yaw(offset) * head_origin
      std::optional<double> fold_rad;                        // set on the exit frame: rotate HeadCalibration by this
      bool easing;std::uint64_t entries,reaims,folds,skipped_small;};
  class ScriptedRecenter {public:
      ScriptedRecenterOutput update(const ScriptedRecenterInput&,const ScriptedRecenterParams& = {});
      void reset();};
  Quaternion yaw_quaternion(double yaw);  // about +Y (tracking up), matching HeadCalibration's yaw convention
  // head_calibration.hpp:
  void HeadCalibration::rotate_origin(double yaw_rad) noexcept; // yaw-only, keeps epoch/player/camera/generation and position
  ```
- **The entry delta**: `delta = hmd_rel_yaw`. The view yaw in authored mode is `heading + (hmd − origin)`, so offsetting
  the origin by `delta` puts the action in front of the current gaze.
- **Re-aim**: when `heading_snaps` increases, ease to the current `hmd_rel_yaw` measured against the composed origin.
- **The exit fold**: `fold_rad = offset + wrap(body_yaw − heading_at_exit)`. The authored view is `heading + (hmd − origin')`
  and gameplay is `body + (hmd − origin'')`, so this keeps the world-space view yaw continuous when the game's body yaw
  differs from the held heading. If `body_yaw` is not available on the exit frame, fold the
  offset alone and mark the residual. The next update then eases any remaining continuity error over 0.4 s.

- [ ] **Step 1: Write the failing tests** (`scripted_recenter_check.cpp`):
  - `entry_eases_to_delta`: entry with `hmd_rel_yaw` = 40° gives an offset of 0 at t0, monotonic, 40° ± 1e-6 at t0+0.4, and
    smoothstep at the midpoint (20° ± 0.5°).
  - `small_delta_skipped`: 9.9° → offset stays 0, `skipped_small` 1; 10.1° → eases.
  - `head_motion_during_ease_respected`: the offset target is fixed at entry, and changing `hmd_rel_yaw` mid-ease does
    not change the target.
  - `snap_reaims`: `heading_snaps` +1 with a new relative yaw of 35° (against the composed origin) → eases again,
    `reaims` 1.
  - `exit_folds_with_continuity`: authored true → false with heading 1.0 rad and `body_yaw` 1.2 rad → `fold_rad` =
    offset + wrap(1.2 − 1.0), offset 0 afterwards, `folds` 1.
  - `epoch_change_drops_offset`: an epoch change mid-ease → offset 0, no `fold_rad`.
  - `reentry_during_ease_is_continuous`: exit and re-entry inside 0.4 s → no frame-to-frame offset+fold step larger than
    the ease rate allows.
  - `non_finite_hmd_no_turn`.
  - `wrap_near_180_shortest`: 179° and −179° each turn less than 180°, and exactly 180° picks +π consistently.
  - `instant_applies_full`: `instant=true` with delta 50° → offset 50° on that frame.
  - mutation guards: the dead zone 10° (constant check), the ease 0.4 s.
- [ ] **Step 2: Add the `head_calibration_check.cpp` case** `rotate_origin_keeps_identity`: after `rotate_origin(0.3)`,
  `get()` with the same epoch, player, camera and generation returns the origin with its yaw +0.3 rad (from
  `tracked_yaw`) and the same position, and does not recapture (`captures()` unchanged).
- [ ] **Step 3: Register both** in `tools/build-msvc.cmd` and run
  `python3 tools/linux/run_build_cmd.py --only 'scripted_recenter|head_calibration' candidate d2vr_render_probe_arm_v610.dll`.
  Expect a FAIL (missing header).
- [ ] **Step 4: Implement** `scripted_recenter.hpp` (header-only, `noexcept` where possible, no allocation) and
  `rotate_origin`.
- [ ] **Step 5: Run the same command.** Expect a PASS.
- [ ] **Step 6: Commit** `feat(camera): scripted-moment recenter helper (ease, re-aim, exit fold)`.

### Task 2: Wire the recenter into the authored view (G1), with diagnostics

**Files:**
- Modify: `src/dxgi_probe.cpp`:
  - the authored decision and comfort block ~4436–4566;
  - the authored eye view ~5728;
  - the listener ~5822;
  - the vignette drive ~7152–7161;
  - the status JSON next to the comfort meter output;
  - knob reads next to `legacy_authored_camera`.
- Modify: `src/comfort_vignette.hpp`: `vignette_target_allowed(..., bool recenter_easing)` lets the vignette show while
  authored *and* easing.
- Test: `tests/scripted_recenter_routing_check.py` (a source-routing check in the style of
  `tests/authored_view_routing_check.py`); `tests/comfort_vignette_check.cpp` (the new case).

**Interfaces:**
- Consumes: Task 1 `ScriptedRecenter`, `yaw_quaternion`, `HeadCalibration::rotate_origin`.
- Produces: `scripted_recenter_enabled` (a bool global: `!build/no-scripted-recenter` and the setting from Task 7,
  default on); the status block `scripted_view` with
  `{entries, reaims, folds, skipped_small, easing, last_delta_deg, last_state_index, enabled}`; the trace
  `build/scripted-view-trace` → `artifacts/scripted-view.csv` with columns
  `t,authored,state_index,hmd_rel_deg,rel_composed_deg,heading_deg,offset_deg,fold_deg,body_deg,view_yaw_deg`
  (`rel_composed_deg` = HMD yaw against the composed origin, so 0 means the gaze is on the action) (≤ 4096 rows, flushed every
  64).

- [ ] **Step 1: Write the failing routing check.** It asserts:
  - `scripted_recenter.update` is called once per displayed frame, in the `per_frame` block after `authored_heading.update`;
  - both `authored_head_view` call sites pass `composed_origin` (= `yaw_quaternion(offset) * head_origin.orientation`),
    not `head_origin.orientation`;
  - the fold calls `head_calibration.rotate_origin(*fold_rad)`;
  - `legacy_authored_camera` and `build/no-scripted-recenter` bypass the helper;
  - paused or menu frames (logic ≠ 2) don't call `update` (the offset is kept);
  - `vignette_target_allowed` gets the easing flag.
- [ ] **Step 2: Add the `comfort_vignette_check.cpp` case** `authored_easing_allows_vignette`: authored and easing →
  true; authored and not easing → false; menu open → false.
- [ ] **Step 3: Run** `--only 'scripted_recenter|comfort_vignette|authored_view'`. Expect a FAIL.
- [ ] **Step 4: Implement the wiring.**
  - `state_index` comes from `native_action_camera` (extend its poll result with the first active scripted/combat
    index, `int state_index=-1`).
  - `body_yaw` is the levelled anchor yaw already fed to `comfort_meter`.
  - `epoch` = `game_xr->reference_epoch()` mixed with the player, camera and generation.
  - The status block and the trace go next to the comfort meter.
- [ ] **Step 5: Run** the same `--only` set plus `native_action_camera|action_camera_alignment`. Expect a PASS.
- [ ] **Step 6: Commit** `feat(camera): scripted moments turn gently to the action and keep the new direction`.

### Task 3: Sim scenario `authored_turn` and state map (Tier B, G1)

**Files:**
- Create: `tools/linux/scenarios/authored_turn.py`
- Modify: `tools/linux/bonelab_run_check.py` (a check for `authored_turn` that reads `scripted-view.csv`),
  `tests/bonelab_scenarios_check.py` (registration)
- Ledger: `.superpowers/sdd/2026-10-25-camera-recenter-vr-cutscenes/g1-sim.md`

**Interfaces:**
- Consumes: Task 2's trace and status.
- Produces: `bonelab_run_check.py RUN --scenario authored_turn`, which passes when:
  - every entry with `|hmd_rel| ≥ 10°` ends its ease (0.4 s after entry) with `|rel_composed_deg|` < 3°, minus the head's
    own yaw change since entry;
  - no frame-to-frame `view_yaw` step exceeds the ease's peak rate plus the head's own motion, by 1° of slack, at entry and
    exit;
  - at least one entry was observed.

- [ ] **Step 1: Find a spot.** Use `tools/entities.py` and `vr_selftest.py --near` (the read-only tools) on the current
  save to find a mantle ledge (a crate or a low wall) and an unaware guard. Record the coordinates and the `setviewpos`
  commands in `g1-sim.md`. If none is found in the current save, try the other saves in the mod's save folder, restored
  by the sim. Never overwrite the user's saves.
- [ ] **Step 2: Write the scenario.** The lead-in is as in `head_turn_walk.py`. Teleport. Hold the head at a 40° yaw. Walk
  into the ledge and press jump to mantle (A). Then teleport behind the guard, head at −35°, and do a grip-alone
  takedown (the existing takedown input). Then idle with the head straight.
- [ ] **Step 3: Write the check** in `bonelab_run_check.py`, with the PASS rules above, plus an INFO line per entry
  (`state_index`, delta, residual).
- [ ] **Step 4: Run** `linux/d2vr-linux sim --candidate-dll build/candidate/d2vr_render_probe_arm_v610.dll --scenario authored_turn --knob scripted-view-trace --lead 45 --seconds 120`.
  Then `python3 tools/linux/bonelab_run_check.py RUN --scenario authored_turn`. Expect a PASS. Record which
  `state_index` the mantle and the takedown use in `g1-sim.md`, and in the comment table in `native_action_camera.hpp`.
- [ ] **Step 5: Regression.** `head_turn_walk` still passes its existing check.
- [ ] **Step 6: Commit** `test(camera): authored_turn scenario and recenter residual check`.

If the takedown cannot be triggered in the sim, the mantle alone is the gate. Record that and continue.

### Task 4: Cutscene capture spike — go or no-go (read-only)

**Files:**
- Modify (dev knob only, default off): `src/dxgi_probe.cpp`, with a `build/cinematic-probe` knob that, while
  `game_logic_mask()==32`, logs per frame:
  - how many times the eye setup and begin hooks fire;
  - each call's view pose and FOV;
  - the native player camera identity versus the active render camera pointer;
  - `native_views().size()`;
  - whether the paired-binds baseline holds.

  It writes to `artifacts/cinematic-probe.csv` (≤ 2048 rows). It never changes presentation.
- Create: `tools/linux/scenarios/cutscene.py`
- Update: `docs/native-authored-scene-presentation.md` (new section "Cinematic32 capture, 2026-10-25")
- Ledger: `g2-spike.md`

**Interfaces:**
- Produces: a go/no-go verdict in the ledger and the doc. **Go** means that under mask 32 the hooks see the camera the
  game renders from (the player camera, or a stable other camera whose pose the mod can read and write through the same
  paired-view path), and that the head-view writes take effect (a test frame with a 5 cm offset visibly moves the image,
  checked by `capture_frame` diff). **No-go** means anything else, and the doc states the missing lever.

- [ ] **Step 1: Find an in-engine cutscene save.** Candidates are a mission start or end (for example the Dreadful Wale
  intro scenes, or Meagan Foster in the Wale). Use the user's save set restored by the sim. If none triggers mask 32
  within 60 s, record the gap in the ledger, mark G2 as **no-go (no test save)**, skip Tasks 5–6, and go to Task 7.
  Then tell the user at the end that a save just before a cutscene is needed.
- [ ] **Step 2: Implement the probe.** Add `cinematic_probe_check` to the routing checks: the probe reads only, and it is
  off without the knob.
- [ ] **Step 3: Run** `linux/d2vr-linux sim --candidate-dll … --scenario cutscene --knob cinematic-probe --lead 45 --seconds 150`.
- [ ] **Step 4: Decide** go or no-go from the CSV. Write `g2-spike.md` and the doc section.
- [ ] **Step 5: Commit** `diag(cutscene): read-only Cinematic32 capture probe and verdict`.

### Task 5 (only on go): Immersive cutscene classification and present path

**Files:**
- Create: `src/cutscene_policy.hpp`
- Modify:
  - `src/dxgi_probe.cpp`: `render_world_scene()` ~738, `sample_stereo` ~6797, the `logic==2` gates of the head-view
    path that the spike shows must also admit 32, and the present call;
  - `src/game_xr.hpp`: `present(..., bool authored_scene_render=false)` per `docs/native-authored-scene-presentation.md`
    §5.
- Test: `tests/cutscene_policy_check.cpp`, `tests/cutscene_routing_check.py`

**Interfaces:**
- Produces:
  ```cpp
  enum class CutsceneMode {vr_comfort,vr_full,theater};
  enum class CutsceneFallback {none,mode_theater,knob_off,incomplete_pair,duplicate_eyes,camera_unreadable,fault};
  struct CutsceneFrame {unsigned logic;bool paired_complete,eyes_distinct,camera_readable,fault;};
  class CutscenePolicy {public:
      bool immersive(const CutsceneFrame&,CutsceneMode,bool knob_off); // sticky: false for the rest of the cutscene once any frame fails
      CutsceneFallback reason() const;void end();};                   // end() when logic leaves 32
  ```
- Only exact logic 32 is eligible. Mixed masks (2|32), loading, pause (4) and menus are rejected.

- [ ] **Step 1: Failing tests:**
  - `exact_32_complete_pair_immersive`;
  - `mixed_mask_rejected` (34, 36, 288);
  - `theater_mode_never_immersive`;
  - `fallback_sticks_until_exit`;
  - `end_resets`;
  - `duplicate_eyes_rejected`.
  - The routing check: an immersive frame uses projection layers, the native menu pointer is disabled, raw buttons pass
    (skip), and returning to pause gives the quad again.
- [ ] **Step 2: Run** `--only 'cutscene'`. Expect a FAIL.
- [ ] **Step 3: Implement**, with no change for `theater` or the knob.
- [ ] **Step 4: Run.** Expect a PASS. Then run the full `authored|game_xr|alt_pose|paired` checks.
- [ ] **Step 5: Commit** `feat(cutscene): in-engine cutscenes render in stereo VR (sticky theater fallback)`.

### Task 6 (only on go): VR comfort and VR full views, with cut detection

**Files:**
- Create: `src/cutscene_view.hpp`
- Modify: `src/dxgi_probe.cpp` (the authored view composition when the cutscene is immersive)
- Test: `tests/cutscene_view_check.cpp`

**Interfaces:**
- Consumes: Task 1 (`instant` input), Task 5 (`immersive`).
- Produces:
  ```cpp
  struct CameraCut {bool cut;double jump_m,jump_yaw_rad;};
  CameraCut detect_cut(const GameCameraPose& prev,const GameCameraPose& now,double units_per_metre); // >0.5 m or >=30 deg yaw
  GameCameraPose cutscene_full_view(const GameCameraPose& scene,const TrackedPose& centre,const TrackedPose& eye,
      const Quaternion& origin,double units_per_metre);  // the scene's full rotation with the HMD on top
  ```
- **VR comfort** reuses `authored_head_view` with the scene camera's position, `AuthoredHeading` for the heading, and
  `ScriptedRecenter` with `instant = detect_cut(...).cut`.
- **VR full** uses `cutscene_full_view`.
- The vignette runs while the scene camera translates faster than the comfort-meter threshold (comfort mode only).

- [ ] **Step 1: Failing tests:**
  - `cut_thresholds` (0.49 m no, 0.51 m yes; 29.9° no, 30.1° yes);
  - `full_view_identity_head_equals_scene`;
  - `full_view_adds_head_yaw`;
  - `comfort_cut_reaims_instantly` (ScriptedRecenter with instant gives the full delta that frame);
  - `pan_without_cut_no_reaim`.
- [ ] **Step 2: Run** `--only 'cutscene_view'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Tier B.** `cutscene` scenario in each mode (the mode set by the settings file before the run, Task 7). Capture
  both eyes with the existing frame capture. Comfort and full submit distinct stereo, theater submits the quad, skip
  (the native button) ends the scene, and the return to logic 2 is clean (no `incomplete_pair` after exit). Record in
  `g2-sim.md`.
- [ ] **Step 5: Commit** `feat(cutscene): VR comfort (head-owned, cut re-aim) and VR full views`.

### Task 7: VR menu rows, persistence and the launcher mirror

**Files:**
- Modify:
  - `src/vr_settings.hpp`: two rows inserted before "Calibrate | Recenter", `settings_row_count` 11 → 13, all switch
    indices after them shifted;
  - `src/settings_panel_image.hpp`: draw them;
  - `src/vr_settings_native.hpp`;
  - the settings save/load (follow every place `crosshair` is persisted in the v609 commits `5e92297` and `511b602`);
  - `launcher/Prefs.cs`, `launcher/Program.cs`.
- Test: `tests/vr_settings_check.cpp` (or the existing settings check that covers crosshair), `tests/launcher_features/PrefsTests.cs`

**Interfaces:**
- Produces: `VrSettings::scripted_recenter()` / `set_scripted_recenter(bool)` (default true), with the text
  `"Recenter on scripted moments: On|Off"`; `VrSettings::cutscene_mode()` / `set_cutscene_mode(CutsceneMode)` (default
  `vr_comfort`), with the text `"Cutscenes: VR comfort|VR full|Theater"`, cycled left/right; `Out::scripted_recenter`,
  `Out::cutscene_mode`.
- The settings file keys are `scripted_recenter=1` and `cutscene_mode=vr_comfort|vr_full|theater`. Unknown → default. The
  settings version bumps by one, with a migration that keeps every existing key.
- If Task 4 was no-go, the cutscene row still exists but offers only `Theater`, with the text "Cutscenes: Theater (VR not
  yet supported)". The setting is still stored, so a later build can honour it.
- Launcher: the same two settings on the settings page, mirrored from the game file (merge-on-save; the in-game menu
  wins, as for crosshair in `511b602`).

- [ ] **Step 1: Failing tests:**
  - row hit-testing for the new rows and the shifted rows (pointer and stick);
  - defaults;
  - parse of each value and of an unknown value;
  - migration keeps the old keys;
  - launcher `merge_keeps_game_cutscene_mode`.
- [ ] **Step 2: Run** `--only 'vr_settings|settings_panel'` and the launcher tests. Expect a FAIL.
- [ ] **Step 3: Implement.** The `dxgi_probe` reads the two settings live (no restart) into `scripted_recenter_enabled`
  and the cutscene mode.
- [ ] **Step 4: Run.** Expect a PASS. Also capture the settings panel image in the existing panel-image check, so the
  13 rows fit in 560 px (the last row's bottom ≤ 560).
- [ ] **Step 5: Commit** `feat(settings): recenter-on-scripted-moments and cutscene mode rows, launcher mirror`.

### Task 8: Checklist, full verification

**Files:**
- Create: `docs/VR-v610-TEST-CHECKLIST.md` (this plan's Tier C lines; the hands plan appends its own)
- Modify: `docs/PLAYING.md` (the two settings, one line each)

- [ ] **Step 1: Write the checklist.** Use the spec's Tier C lines, plus: "pause and Recenter view during a climb: no
  jump beyond the recenter"; "two climbs in a row"; "look backwards (180°) and climb: turns the short way". If Task 4 was
  no-go, say so and ask for a save just before a cutscene.
- [ ] **Step 2: Full harness.** `python3 tools/linux/run_build_cmd.py --keep-going candidate d2vr_render_probe_arm_v610.dll`.
  Expect no new failures against 632 / 1 (`fsr_taa_gpu_check --fsr4`). Then the launcher tests (all pass) and
  `linux/tests` (OK).
- [ ] **Step 3: Commit** `docs: v610 camera checklist`.
