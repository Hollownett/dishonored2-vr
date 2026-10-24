# v609 Headset-Feedback Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix what the user found in v608. Hands and weapons stay on the controllers while running. A sword regrab gives one clean grip. A fist never draws the sword. The crosshair sits on the real aim and can be turned off. AA defaults to FSR 3 native, or to DLAA on RTX, and the launcher mirrors the game. Corpses drag 2.5× lighter. Thin items stop the fingers. An empty fist rests on the palm.

**Architecture:** Eleven independent slices of an existing C++17 DLL plus the C# launcher.
- Small pure helpers go in headers, each with a Tier A check.
- `dxgi_probe.cpp` only wires them in.
- Running (G1) is measured first in the sim (Task 8), and Task 9 fixes what those numbers implicate.
- Settings move to `vr-settings.cfg` format v9. The DLL applies a one-time AA rule at Present. The launcher merges instead of overwriting.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook, Scaleform/Iggy hooks), the C# WinForms launcher (.NET 8), and the Linux sim rig (Python).

**Spec:** `docs/superpowers/specs/2026-10-24-v608-feedback-design.md` (goals G1–G8, recon R1–R8, user decisions, builder's calls, Verification). It is binding. Read it alongside this plan. Line numbers below were spot-checked at `c4a0e73` and may drift by a few lines.

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`. Ledger and run notes go in `.superpowers/sdd/2026-10-24-v609-feedback-fixes/`, which is git-excluded.
- Pinned exe `Dishonored2.exe`, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
- Engine write safety and C9 routing are as in `docs/superpowers/specs/2026-10-19-bonelab-feel-design.md`. The raw controller pose drives intent; the drawn pose drives rendering.
- No swapchain or layer changes. **No new smoothing on any hand pose** (an anti-goal).
- Hand bodies must stay useful. A fix may change how the drawn hand picks a source, never switch the bodies off.
- Saved settings always win. The only exception is the one-time v609 AA pass of Task 3, which the user decided.
- Build: `python3 tools/linux/run_build_cmd.py --only '<regex>' candidate d2vr_render_probe_arm_v609.dll`. For the full harness, drop `--only` and add `--keep-going`.
- Linux tests: `python3 -m unittest discover -s linux/tests`.
- Launcher tests: `DOTNET_ROOT=~/.dotnet-ms ~/.dotnet-ms/dotnet run --project tests/launcher_features`.
- Launcher build: `DOTNET_ROOT=~/.dotnet-ms ~/.dotnet-ms/dotnet publish launcher/DishonoredVRLauncher.csproj -c Release -p:EnableWindowsTargeting=true`.
- Test registration in `tools/build-msvc.cmd`:
  - a new C++ check joins the `for %%T in (finger_ik_check …)` list (~245);
  - a new Python check is a `python tests\<name>.py` line plus the errorlevel line, next to `python tests\physical_grip_routing_check.py` (~697).
- Harness gate: no new failures against the baseline of 628 passed / 1 failed (`fsr_taa_gpu_check --fsr4`, environment-only) / 28 skipped. Launcher tests ALL PASS. `linux/tests` OK.
- **Sim safety** (every Tier B run):
  - `PAUSE-SIM` absent;
  - no `Dishonored2.exe` or `vrserver` running, checked with `pgrep -x` guards that can't match their own shell line;
  - never kill by pattern;
  - record the installed DLL's sha256 before the run and require it unchanged afterwards, with no knob left in `build/`;
  - run a candidate with `linux/d2vr-linux sim --candidate-dll <built DLL under the installed name> --scenario <name> --lead 45 --seconds 120`.
- New behaviour knobs, each an off-switch that is absent by default:
  - `build/legacy-arm-root-carry`, `build/no-hand-body-feedforward`, `build/no-moving-controller-visual` (Task 9);
  - `build/locomotion-trace`, `build/sword-grip-trace` (diagnostic traces, Tasks 8 and 10).
- Testing honesty: Tier A and B prove logic and measured residuals. Feel and alignment are Tier C only (Task 11).
- Commit trailer, on every commit:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01BJuGoAt65W8LH5YcXkajFq`

## Review Focus

1. **A pre-v609 `vr-settings.cfg` on a non-RTX GPU with a non-default AA choice** (for example FSR 4 Quality). Expected: the one-time pass keeps the saved mode and only sets the marker. DLAA replaces a saved mode only where DLSS is offered. Test (Task 3): `pre_v609_non_rtx_keeps_saved_mode`.
2. **The launcher is open while the player changes AA or HUD in the game**, then the user saves an unrelated launcher field. Expected: the game's AA, HUD and crosshair survive, and only the edited field changes. Test (Task 4): `merge_keeps_game_changes_on_stale_form`.
3. **The adapter query never succeeds (vendor 1), or the GPU is NVIDIA but `nvngx_dlss.dll` is missing.** Expected: FSR 3 native (or the kept mode), the marker set once, one file write per session, no retry loop. Test (Task 3): `vendor_failure_sets_marker_once`.
4. **A sword regrabbed exactly across the blade**, with `pickup_dot` jittering around 0 from frame to frame. Expected: the previous hold's orientation, fixed for the whole hold. Test (Task 10): `perpendicular_regrab_keeps_previous`.
5. **The crosshair toggled off and on in the game while aiming, with the HUD at 200 % and a near-square eye.** Expected: hidden on the next frame, back on the next frame, no restart. Test (Task 5): `crosshair_toggle_live`.

---

### Task 1: G6 corpses 2.5× lighter (props unchanged)

Recon correction: `native_grab.hpp:590-595` gives capped **prop** lanes the same `tuning_` (`strength_scale` 6) as corpses. Raising the shared value would also make props 2.5× lighter. The spec keeps props, so this task splits the scale.

**Files:**
- Modify: `src/grab_force_cap.hpp` (lines 22-27 constants and `GrabTuning`; the bound at :88), `src/native_grab.hpp` (:595 `in.tuning=&tuning_`; :948 `configure_corpse`), `tools/corpse_drag_lab.cpp` (:44 pins; :465-466 and :601 comparisons)
- Test: `tests/grab_force_cap_check.cpp` (:34-50, the bounds table at :162), `tests/corpse_hold_check.cpp` (:511, :523, :535)
- Create: `tests/corpse_strength_routing_check.py`, registered in `tools/build-msvc.cmd`

**Interfaces:**
- Produces:
  - `constexpr double kLegacyCorpseStrengthScale=3., kV608CorpseStrengthScale=6., kCorpseStrengthScale=15.;`
  - `GrabTuning::prop_strength_scale=6` (new field, cfg key `prop_strength_scale`, bounds .5..10);
  - `GrabTuning GrabTuning::prop_view() const noexcept`, which returns a copy with `strength_scale=prop_strength_scale`.

- [ ] **Step 1: Write the failing tests.**
  - `grab_force_cap_check`, renamed `corpse_caps_v609` (was `corpse_caps_twice_pre_h1`):
    - `t.strength_scale==15`;
    - `now.strength_scale==2.5*kV608CorpseStrengthScale`;
    - `now.axial_cap()==4500 && now.lateral_cap()==1350`, legacy `axial_cap()==900`;
    - the capped velocity step is exactly 2.5× v608's, linear and angular;
    - the hitch and mass fields are unchanged.
  - `prop_caps_unchanged`: `GrabTuning{}.prop_view().strength_scale==6 && .axial_cap()==1800`.
  - Bounds table: `{"strength_scale",.5,25}` and `{"prop_strength_scale",.5,10}`.
  - `corpse_hold_check`:
    - the defaults are 15 (:511, :523);
    - `strength_scale 99` is still out of bounds;
    - `legacy-corpse-strength` still gives 3 (:535);
    - with the legacy knob, `prop_strength_scale` stays 6.
  - `corpse_strength_routing_check.py`: `native_grab.hpp` contains `in.tuning=corpse?&tuning_:&prop_tuning_;` and `configure_corpse` assigns `prop_tuning_=tuning_.prop_view();`. Mutation: replacing it with `in.tuning=&tuning_;` must fail.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only 'grab_force_cap|corpse_hold|corpse_strength' candidate d2vr_render_probe_arm_v609.dll`
- [ ] **Step 3: Implement.**
  - Constants and field as in Interfaces. `load_grab_tuning`'s `strength_scale` range becomes `.5,25`. Add the key `prop_strength_scale` with range `.5,10`.
  - `NativeGrab` keeps a `GrabTuning prop_tuning_` refreshed wherever `tuning_` is set, including after the legacy knob at :1126. That knob changes only `strength_scale`.
  - The hand servo stays at 1 (`hand_servo.hpp:70`). Do not touch it.
  - Lab: `kPinnedAxial=4500, kPinnedLateral=1350`. Add a scenario `v608_brisk_lift` (the brisk one-hand lift at `kV608CorpseStrengthScale`). Add a check `v609_lighter`: the shipped brisk lift's `max_lag <= .45*` v608's, no lag release, and lift time not slower.
  - If any other lab scenario fails at 15, stop and report its name and numbers. Do not retune other defaults.
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'corpse_drag_lab'`, which runs `build\corpse_drag_lab.exe` and must print `0 failed`.
- [ ] **Step 5: Commit** `feat(grab): corpses 2.5x lighter to drag; props keep the v608 scale`

### Task 2: G3 an empty fist never presses native RB

**Files:**
- Modify: `src/right_grip_owner.hpp` (add the helper), `src/dxgi_probe.cpp:8206-8209` (the right physical/native grip split), `docs/PLAYING.md` (controls table :52-53, plus a line in the sword section ~161)
- Test: `tests/right_grip_owner_check.cpp`, `tests/physical_grip_routing_check.py` (:25-26 pins, mutation list)

**Interfaces:**
- Produces: `inline bool fist_may_press_native_rb(bool physical_hold,bool native_carry,bool sword_held,bool sword_drawn) noexcept` in `src/right_grip_owner.hpp`. It is true iff any argument is true.

- [ ] **Step 1: Write the failing tests.**
  - `right_grip_owner_check`, `empty_fist_never_presses_native_rb`: all four inputs false → false. Each input alone → true (the full 16-row truth table).
  - `physical_grip_routing_check.py`:
    - replace the pin `if(right_physical) input.right_grip=right_native;` with `if(right_physical) input.right_grip=right_fist_rb?right_native:0.f;`;
    - assert `const bool right_fist_rb=d2vr::fist_may_press_native_rb(native_grab.holding(1),right_carrying,sword_grip.held(),right_sword_drawn);` appears before it;
    - assert `right_sword_drawn` is read as `(hand_weapon_bits&2u)!=0` under `hand_state_mutex`;
    - keep the block pins at :29-31 and the choke paths (`if(deliberate_choke) input.right_grip=1;` and `input.right_grip=final_choke_current?1.f:0.f;`) unchanged and asserted;
    - new mutation `right_fist_rb?right_native:0.f` → `right_native` must be rejected.
- [ ] **Step 2: Run, expect failure.** `... --only 'right_grip_owner|physical_grip_routing'`
- [ ] **Step 3: Implement.** In the `physical_grab_requested` block, read `right_carrying` (`hand_carrying_bits!=0 || hand_holding_object`) and `right_sword_drawn` under one `hand_state_mutex` lock, then gate as pinned.
  - `docs/PLAYING.md`: a fist never draws the sword. Reach over the right shoulder and squeeze to draw. A fist still blocks with a held sword and still chokes when the game offers it.
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit** `fix(controls): an empty fist never draws the sword (native RB only with something in hand)`

### Task 3: G5 in the DLL: settings v9, one-time AA default, legacy-knob migration

**Files:**
- Create: `src/aa_first_start.hpp`, `tests/aa_first_start_check.cpp` (register), `tests/aa_first_start_routing_check.py` (register), `tests/fixtures/vr-settings-v9.cfg`
- Modify:
  - `src/vr_preferences.hpp` (whole file: struct, load, save);
  - `src/vr_settings.hpp` (state :58-62, `initialize` :97-100, `cycle_temporal_aa` :249-251);
  - `src/game_xr.hpp` (constructor :1410-1418, the save at :1914-1915, near `take_temporal_request` :1309);
  - `src/dxgi_probe.cpp` (prefs load :8943-8949; Present vendor block :7449-7457; status writer).
- Test: `tests/vr_settings_check.cpp`

**Interfaces:**
- Produces, in `src/vr_preferences.hpp`:
  - `VrPreferences` gains `bool aa_chosen=false; bool crosshair=true;`.
  - Format v9 appends two tokens, `aa_chosen crosshair` (each 0/1), after the five measurement tokens. Versions 1–8 load with `aa_chosen=false, crosshair=true`. Version 10 and up is rejected ("Invalid VR preferences; the saved file was preserved"). `save_preferences` writes `9`.
- Produces, in `src/aa_first_start.hpp`:
  ```cpp
  struct LegacyAaKnob {std::string name,value;};             // value: first line, trimmed, at most 64 chars ("" for an empty file)
  std::vector<LegacyAaKnob> remove_legacy_aa_knobs(const std::filesystem::path& build_dir) noexcept; // aa-mode, fsr, fsr4, force-taa; never fsr4.off
  enum class FirstStartAa {wait,keep,dlaa,fsr3_native};
  FirstStartAa first_start_aa(bool aa_chosen,bool file_existed,std::uint32_t vendor,bool dlss_offered) noexcept;
  ```
  Rules, in order:
  1. `aa_chosen` → `keep`;
  2. `vendor==0` → `wait`;
  3. `dlss_offered` → `dlaa` (mode 7);
  4. `!file_existed` → `fsr3_native` (mode 2);
  5. otherwise `keep`.
- Produces, in `VrSettings`: `bool aa_chosen() const`, `bool crosshair() const`, `void set_crosshair(bool)`. `initialize(..., bool aa_chosen=false, bool crosshair=true)`. Any AA change through the panel sets `aa_chosen_`.
- Produces, in `GameXr`: constructor parameters `bool aa_chosen=false, bool crosshair=true` appended. `void adopt_first_start_aa(std::optional<int> mode)` sets the mode (if any) as a temporal request, marks chosen, and saves once. `bool crosshair_visible() const`. Every save passes both new fields.
- Consumed by Task 4 (format), Task 5 (`crosshair`, `set_crosshair`, `crosshair_visible`).

- [ ] **Step 1: Write the failing tests.**
  - `vr_settings_check`:
    - `prefs_v9_roundtrip`: save and load keep `aa_chosen`/`crosshair` in all four combinations.
    - `prefs_v8_defaults`: a v8 line gives `aa_chosen==false && crosshair==true`.
    - `prefs_v10_rejected`.
    - `prefs_v9_fixture`: `tests/fixtures/vr-settings-v9.cfg` parses to `temporal_aa==7, hud_percent==150, aa_chosen, !crosshair`.
    - `panel_aa_change_marks_chosen`: A on row 1 sets `aa_chosen()`.
    - The existing default pins (FSR 3 native = 2, ~:398, :486) stay.
  - `aa_first_start_check`:
    - `decision_table`: all 16 combinations of the four inputs (vendor ∈ {0, 0x10de, 0x1002, 1}) match the rules.
    - `pre_v609_non_rtx_keeps_saved_mode`: `first_start_aa(false,true,0x1002,false)==keep`.
    - `fresh_rtx_gets_dlaa`.
    - `vendor_failure_sets_marker_once`: `first_start_aa(false,true,1,false)==keep`.
    - `removes_only_legacy_knobs`: in a temp `build/` holding `aa-mode` ("3\n"), `fsr` ("0.4 1 1\n"), `fsr4` (empty), `force-taa`, `fsr4.off` and `hud-scale`, the call removes exactly the first four. It returns them sorted by name with values `"3"`, `"0.4 1 1"`, `""`, `""`. `fsr4.off` and `hud-scale` remain.
    - A missing directory returns empty and does not throw.
  - `aa_first_start_routing_check.py`:
    1. The migration (the `remove_legacy_aa_knobs` call and the `no-hand-crosshair` migration) sits inside `if(saved_settings_requested)` and is gated on `!preferences->aa_chosen` or no file.
    2. `adopt_first_start_aa` is called only after `present_adapter_vendor` is non-zero, inside the Present DLSS block, guarded by a once-per-session flag.
    3. The `game_xr.hpp` save call passes `settings_.aa_chosen()` and `settings_.crosshair()`.
    4. The diagnostic knob reads at :9486-9488 still exist (the sim's `--vr-setting temporal_aa` writes `build/aa-mode`).
- [ ] **Step 2: Run, expect failure.** `... --only 'vr_settings|aa_first_start'`
- [ ] **Step 3: Implement.**
  - **Load (:8943).** Keep `preferences_file_existed`, `aa_chosen_preference` and `crosshair_preference` as statics. When the marker is absent (no file, or a pre-v9 file):
    - call `remove_legacy_aa_knobs(root/"build")` and log one line per file: `v609 AA migration: removed build/<name> (<value>)`;
    - if `build/no-hand-crosshair` exists, set `crosshair_preference=false`, delete the file and log it.

    All of this runs before the knob reads at :9401/:9470/:9486, so a stale knob cannot block the switch.
  - **Present (:7449-7457).** Once `present_adapter_vendor` is non-zero and a first-start decision is pending, map `first_start_aa(aa_chosen_preference, preferences_file_existed, vendor, dlss_offered())` to `adopt_first_start_aa(7)`, `adopt_first_start_aa(2)`, or `adopt_first_start_aa(std::nullopt)` for `keep`. Clear the pending flag. `wait` retries next Present.
  - The temporal request flows through the existing `take_temporal_request` path (:7639), so the change applies live and the file is written back for the launcher.
  - Status: `"aa_first_start":{"state":"none|wait|keep|dlaa|fsr3_native","removed":["aa-mode=3",...],"crosshair_migrated":bool}`.
  - Sim runs (no saved settings) never migrate.
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'dxgi_probe|game_xr|settings_menu'`.
- [ ] **Step 5: Commit** `feat(settings): vr-settings v9, one-time DLAA-on-RTX default, legacy AA knob migration`

### Task 4: G5/G4 in the launcher: merge on save, re-read on activation, HUD 50–200, crosshair row

**Files:**
- Create: `launcher/Prefs.cs` (move `Prefs` out of `launcher/Core.cs:10-62` unchanged, then extend it), `tests/launcher_features/PrefsTests.cs`
- Modify:
  - `launcher/Core.cs` (remove the moved class);
  - `launcher/Program.cs`: options :96-102, `LoadSettings`/`SaveSettings` :690-720, plus an `OnActivated` override;
  - `tools/default_features.json` (remove the `hand-crosshair` row, :60-67);
  - `tests/launcher_features/LauncherFeaturesTests.csproj` (compile `..\..\launcher\Prefs.cs` and `PrefsTests.cs`; copy `..\fixtures\vr-settings-v9.cfg`);
  - `tests/launcher_features/FeaturesTests.cs` (call `PrefsTests.Run(Check)`);
  - `docs/PLAYING.md:242` (the knob list: `no-hand-crosshair` is now the Crosshair setting).

**Interfaces:**
- Consumes: format v9 and `tests/fixtures/vr-settings-v9.cfg` (Task 3).
- Produces:
  - `Prefs` gains `public int AaChosen = 0, Crosshair = 1;`. `Load` accepts versions 1–9 (v9 needs ≥ 18 tokens: `t[16]`, `t[17]`, each 0/1). `Save` writes `9`.
  - `Validate` takes HUD 50–200 ("HUD size must be 50 to 200 percent.") and requires `AaChosen`/`Crosshair` ∈ {0,1}.
  - `public static Prefs Merge(Prefs baseline, Prefs edited, Prefs disk)`: a copy of `disk` with every field where `edited` differs from `baseline` taken from `edited`. If `TemporalAa` differs, `AaChosen=1`. `Measurements` always come from `disk`.
  - `public static class PrefsTests { public static void Run(Action<string,bool> check); }`

- [ ] **Step 1: Write the failing tests** in `PrefsTests.cs`:
  - `prefs_v9_roundtrip`.
  - `prefs_v8_loads_marker_off_crosshair_on`.
  - `prefs_rejects_v10`.
  - `prefs_reads_shared_v9_fixture`: the same values as Task 3's `prefs_v9_fixture`.
  - `hud_accepts_50_to_200`: 200 is valid, 201 and 49 are rejected.
  - `merge_keeps_game_changes_on_stale_form` (Review Focus 2): the baseline and the form have AA 2 / HUD 65. Disk now has AA 7 / HUD 150 / Crosshair 0. The form edits only `Mirror`. The merge has AA 7, HUD 150, Crosshair 0 and the new Mirror.
  - `merge_takes_user_aa_edit_and_marks_chosen`.
  - `merge_without_edits_equals_disk`: the saved bytes equal the disk file's.
  - In `FeaturesTests.cs`, `crosshair_row_moved_to_settings`: no feature has the id `hand-crosshair`, and `removed_row_untouched` still holds for a leftover `no-hand-crosshair` file.
- [ ] **Step 2: Run, expect failure.** Launcher tests command (a compile error: `Prefs.Merge` missing).
- [ ] **Step 3: Implement.**
  - `Program.cs` keeps `Prefs loaded` (the baseline).
  - `SaveSettings` reads the form into `edited`, re-reads `disk=Prefs.Load(env.SettingsFile)`, saves `Prefs.Merge(loaded, edited, disk)` and sets `loaded` to the result.
  - `OnActivated`: re-read disk. Form fields equal to `loaded` take the disk value; edited fields stay. Then `loaded=disk`. A read error shows the existing red message and changes nothing.
  - `hud` options: 55, 65 (default), 75, 85, 100 (game layout), 125, 150, 200, the game's list (`vr_settings.hpp:212`).
  - New option `crosshair = new Option("Crosshair", "Shows the game's crosshair where your gun or power aims. Off hides it.", ("On (default)", 1), ("Off", 0));`, placed after `hint`.
  - The load message says the in-game VR menu wins and the launcher shows what the game saved.
- [ ] **Step 4: Run, expect pass.** Launcher tests, the launcher build (0 warnings as errors), and `python3 tests/launcher_features_source_check.py`.
- [ ] **Step 5: Commit** `feat(launcher): settings mirror the game (merge on save, re-read on focus), HUD to 200%, crosshair setting`

### Task 5: G4 crosshair on the real aim, per eye, with an in-game on/off

**Files:**
- Modify:
  - `src/native_crosshair.hpp` (:28-30 mapping; the unused `ndc_limit` :92; `place()` :154-220);
  - `src/native_hud_child_draw.hpp` (eye-node branch :363-372, `begin_frame`, the root size);
  - `src/native_interaction_aim.hpp` (`weapon_slots` :672, producers :694);
  - `src/dxgi_probe.cpp` (projection :5649-5665; per-eye store in the view prep near :4414; install :9252-9253; status);
  - `src/vr_settings.hpp` (rows, pointer hit test :340-385, `Changes` :93-94);
  - `src/settings_panel_image.hpp` (rows :132-143, row loop :257-312, plate :308-311);
  - `src/game_xr.hpp` (apply `changes.crosshair`; save).
- Test: `tests/native_crosshair_check.cpp`, `tests/native_hud_child_draw_check.cpp`, `tests/vr_settings_check.cpp` (pointer rows at :181/184 and the row loop :300-307 move to the new pitch)

**Interfaces:**
- Consumes: `VrSettings::crosshair/set_crosshair` and `GameXr::crosshair_visible` (Task 3).
- Produces:
  ```cpp
  // native_crosshair.hpp. The HUD movie is width-fitted and centred in the eye target (native_hud_stage_fit), so the
  // visible stage is stage_x wide and stage_x*eye_h/eye_w tall.
  std::array<float,2> crosshair_fitted_stage_position(float ndc_x,float ndc_y,float stage_x,float eye_w,float eye_h) noexcept;
      // {0.5*ndc_x*stage_x, -0.5*ndc_y*stage_x*eye_h/eye_w}; eye_w/eye_h <= 0 or non-finite -> crosshair_stage_position(ndc_x,ndc_y,stage_x,stage_x*9/16)
  std::array<float,2> crosshair_eye_shift_px(std::array<float,2> stage_eye,std::array<float,2> stage_placed,float root_s) noexcept; // (eye-placed)*root_s
  float crosshair_distance(std::optional<double> hit_fraction,double length=30.) noexcept; // clamp(fraction*length,.5,length), else length
  // native_hud_child_draw.hpp
  bool native_crosshair_hide(std::array<float,6>& m) noexcept;   // zero linear part, translation kept: the original still draws, invisibly
  inline static std::atomic<bool> crosshair_hidden{};
  using EyeShift=std::optional<std::array<float,2>> (*)(unsigned eye_draw_index) noexcept; inline static std::atomic<EyeShift> eye_shift{};
  static std::array<float,2> target_size() noexcept;            // {1920*s+2*tx, 1080*s+2*ty} from the movie root, {0,0} unknown
  // native_interaction_aim.hpp
  static constexpr std::uint8_t producer_crosshair=3; static constexpr unsigned crosshair_slot=15; // weapon_slots becomes 16
  // vr_settings.hpp
  constexpr int settings_row_top=64,settings_row_pitch=33,settings_row_height=30,settings_row_count=11;
  Changes::crosshair (std::optional<bool>)
  ```
- Row order: 0 Resolution, 1 AA, 2 Edge AA, 3 Monitor view, 4 Sharpening, 5 HUD size, 6 Controls hint, **7 Crosshair**, 8 Turning, 9 Calibrate | Recenter, 10 Test unlock | Back. Every hard-coded `8`/`9`/`10` row index moves: `selected_=8` in the calibration prompt, `case 8/9`, `i==8`, `i==9`, `std::min(selected,9)`, the `%10` wrap, and the test pointer rows.

- [ ] **Step 1: Write the failing tests.**
  - `native_crosshair_check`:
    - `fitted_matches_engine_on_16_9`: with eye 1920×1080 and stage_x 1920, `crosshair_fitted_stage_position` equals `crosshair_stage_position(...,1920,1080)` within 1e-4 for ndc ∈ {−1,−.5,0,.5,1}².
    - `fitted_square_eye`: eye 2000×2000, ndc (.5,.5) → (480,−480) ± 1e-3.
    - `fitted_tall_eye`: eye 1832×1920, ndc (0,1) → y = −1006.11 ± .01.
    - `fitted_bad_size_falls_back`.
    - `eye_shift_px`: placed (100,−50), eye (120,−40), s 1.5 → (30,15).
    - `distance_from_cast`: fraction .1 → 3.0; .001 → .5; nullopt → 30.
  - `native_hud_child_draw_check`:
    - `crosshair_hidden_matrix_zero_linear`: m[0..3]==0 and m[4], m[5] are kept.
    - `layout_undo_is_identity`: `native_hud_panel_relayout` then `native_crosshair_unlayout` at k ∈ {.55,.65,.85} returns the input within 1e-4. This proves the draw path already compensates the HUD scale, so placement must not pre-scale.
    - `crosshair_toggle_live` (Review Focus 5): with `crosshair_hidden` set, the eye-node matrix passed on is hidden. Cleared the next call, it is the normal scaled glyph. Use the existing eye-draw harness (`drawn()`, `glyph_factor`, ~:280-307).
    - `second_eye_draw_shifted`: with an `eye_shift` callback returning (30,15) for index 1 and nullopt for index 0, two draws after `begin_frame` pass translations equal to, then +(30,15) from, the unshifted glyph. A third frame's first draw is unshifted again.
  - `vr_settings_check`:
    - `crosshair_row_toggles`: row 7 with A flips `crosshair()` and reports `Changes::crosshair`; the stick works the same.
    - `eleven_rows_fit`: every row bar `64+33*i+15` for i < 11 lies inside the bitmap and above the description plate (y < 428).
    - The back row pointer is at `(64+33*10+15)/560.`.
- [ ] **Step 2: Run, expect failure.** `... --only 'native_crosshair|native_hud_child_draw|vr_settings'`
- [ ] **Step 3: Implement.**
  - **Stage mapping.** `place()` uses `crosshair_fitted_stage_position(x, y, stage_x, size[0], size[1])` with `size=NativeHudChildDraw::target_size()`. Remove `ndc_limit`. The clamp stays ±1. HUD scale is compensated once, by the existing `native_crosshair_unlayout` in the draw path. Add a counter `crosshair_unlayouts` there for the status.
  - **Per eye.**
    - Store each prepared view's NDC as `{nx,ny,frame}` in `crosshair_eye_ndc[eye_slot]`, using the same formula as :5659-5661 with that view's `world`/`render_fov`. Do this for every prepared view, not only `per_frame`.
    - `place()` keeps using the `per_frame` eye (in alternate-eye mode that is the rendered eye).
    - The eye-node draw counts its draws per frame (reset in `begin_frame`). Draw 0 is the placed eye. Draw 1 (paired stereo renders the UI per eye, `dxgi_probe.cpp:1645`) gets `crosshair_eye_shift_px(stage(eye1), stage(eye0), root_s)` added to the glyph translation after the unlayout.
    - No publication for that frame → no shift.
  - **Distance.** While the crosshair is visible and owned, and `hand_clamp_budget.tip_enabled() && !hand_clamp_budget.alternate()`, post `post_weapon_segment(0,crosshair_slot,origin,origin+dir*30,.02,0,-1,0,producer_crosshair)` from the aim ray receipt. Read `weapon_hit(0,crosshair_slot,now,producer_crosshair)`. `target(crosshair_distance(fraction))` replaces `target(30.f)`. Outside the budget, use 30 m.
  - **On/off.**
    - Install the crosshair hook regardless of `no-hand-crosshair`. Task 3 migrates that file.
    - Each frame, `NativeHudChildDraw::crosshair_hidden = !(game_xr ? game_xr->crosshair_visible() : crosshair_preference)`. Hidden → `native_crosshair_hide` on the eye-node matrix and no cast post.
    - `place()` keeps running, so turning it on again is immediate.
    - The panel row text is "Crosshair: On" / "Crosshair: Off". Help: "Shows the game's crosshair where your gun or power aims. Off hides it."
  - Status: `"crosshair":{"visible":bool,"hidden_draws":n,"eye_shifts":n,"unlayouts":n,"cast_posts":n,"cast_hits":n,"distance_m":x,"target":[w,h],"stage_x":x}`.
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'dxgi_probe|settings_menu|game_xr'`.
- [ ] **Step 5: Commit** `fix(crosshair): fitted-stage mapping, per-eye placement, cast distance, in-game on/off`

### Task 6: G7 thin held items stop the fingers

**Files:**
- Modify: `src/finger_ik.hpp` (`classify_held` :668-697, `held_local_extents` :705-718, `held_cylinder` :737-765), `src/dxgi_probe.cpp` (the native cylinder set :3318-3333; the finger shape block :5340-5388)
- Test: `tests/finger_ik_check.cpp`

**Interfaces:**
- Produces, in `finger_ik.hpp`:
  ```cpp
  inline constexpr double held_min_half_thickness=.0025, held_thin_half=.004;   // 5 mm minimum sheet; under 8 mm thick reads as thin
  Vec3 held_clamped_extents(Vec3 e) noexcept;          // each finite component >= 0 raised to held_min_half_thickness (negative or non-finite unchanged: still rejected)
  bool held_is_thin(Vec3 local_half) noexcept;          // smallest <= held_thin_half && smallest <= .25 * middle
  HeldShape held_slab(const std::array<Vec3,3>& axes,Vec3 centre,Vec3 local_half) noexcept; // oriented box in the model axes, half = clamped local_half, radius 0
  ```
  `classify_held` and `held_cylinder` use `held_clamped_extents(e)` in place of the raw `e`, so a zero-thickness AABB no longer throws. `length(e)>3` and non-finite bounds still throw.

- [ ] **Step 1: Write the failing tests** in `finger_ik_check`:
  - `zero_thickness_native_hold_has_shape`: axes identity, bounds `{{-.1,-.15,0},{.1,.15,0}}`. `held_cylinder` does not throw. `held_local_extents(axes, held_clamped_extents(e))` is solvable and `held_is_thin` is true. `held_slab` gives `half=={.1,.15,.0025}` and radius 0. Solving `finger_contact` with that slab seated under the palm, at `held_curl` and full wants, leaves every fingertip with `shape_distance(slab,tip) >= -contact_tolerance`.
  - `zero_thickness_physical_grab_no_throw`: `classify_held` on the same bounds returns a box (radius 0) with `half.z==.0025`.
  - `tilted_sheet_still_thin`: the sheet turned 30° about x.
  - `oversized_thin_bounds_still_reject`: `length(e)>3` still throws `invalid_argument`.
  - The existing plate/book/tumbler tests (:321-343, :760-763) pass unchanged.
- [ ] **Step 2: Run, expect failure.** `... --only 'finger_ik'`
- [ ] **Step 3: Implement.**
  - Helpers as in Interfaces.
  - In `dxgi_probe.cpp`, next to `held_cylinder_now`, keep `held_box_now={entity, clamped AABB}` and `held_local_now` (the latched `held_extents`).
  - In the shape block, for a native hold (`holder==side`):
    - thin local extents → push `held_slab(model axes, box centre, local)` with kind `"thin"`;
    - otherwise the existing `seat_held` pieces;
    - no cylinder for this entity but a box → push the clamped world AABB as a box (identity basis, radius 0) with kind `"box"`.
  - `const bool native_hold=holder==side;`. `finger_wants`, the touch held flag and `reach` use `holding || native_hold`, so the curl caps at `held_curl` (0.8) and is never a full fist.
  - Status: append `thin_holds` and `box_holds` counters to `finger_ik.hold`.
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'finger_snapshot|arm_tracking|dxgi_probe'`.
- [ ] **Step 5: Commit** `fix(fingers): thin held items (paper, notes, stamps) stop the fingers`

### Task 7: G8 empty-hand fist rests on the palm

**Files:**
- Modify: `src/finger_ik.hpp` (near `palm_normal` :342-346), `src/dxgi_probe.cpp` (the finger shape block :5340-5388)
- Test: `tests/finger_ik_check.cpp`

**Interfaces:**
- Produces: `HeldShape palm_stop_slab(const HandGeometry& hand,const WorldTrackedPose& palm) noexcept` in `finger_ik.hpp`.
  - The box's outer face lies at `held_palm_depth + contact_tolerance` along `palm_normal`.
  - Half thickness .01, behind that face.
  - Laterally it spans the index-to-pinky pivots ±.01.
  - Along the fingers it runs from .01 short of the index-to-pinky pivot line back .08 toward the wrist.
  - Basis: across, along, normal. Radius 0.
  - The knuckles lie outside it.

- [ ] **Step 1: Write the failing tests** in `finger_ik_check`:
  - `empty_fist_tips_stay_above_palm`: for both fixture hands (left and right) and both mappings, solve `finger_contact` with only `palm_stop_slab` at reach 1:
    - legacy wants = `finger_wants(false,1,1)`;
    - touch wants = `{1,1,1,1,1}`.

    Every fingertip of fingers 1–4 has signed distance ≥ 0 from the skin plane (`palm.position+n*held_palm_depth`, along `n`). The thumb tip is ≥ 0 from the same plane.
  - `slab_does_not_trap_open_hand`: with wants all 0, the result equals the call without the slab (the pose ±1e-6).
  - `knuckles_outside_slab`: every finger pivot has `shape_distance > 0`.
- [ ] **Step 2: Run, expect failure.** `... --only 'finger_ik'`
- [ ] **Step 3: Implement.** In `dxgi_probe.cpp`, for a tracked hand that is empty (`!holding && holder!=side`), not a weapon hand (`!((fk_weapon_bits>>side)&1u)`) and not natively animated (`!(hands.native_hands>>side&1u)`), append `palm_stop_slab(*geometry,palm)` (kind `"palm"`) while `count<max_contact_shapes`.
  - Weapon hands and native hands are untouched.
  - If the slab can't make `empty_fist_tips_stay_above_palm` pass, use the spec's fallback instead: a per-finger maximum curl solved once per geometry in Tier A. Report which one shipped.
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'finger_touch|finger_snapshot|dxgi_probe'`.
- [ ] **Step 5: Commit** `fix(fingers): an empty fist rests on the palm`

### Task 8: G1a measure running (scenario, trace, baseline)

**Files:**
- Create: `tools/linux/scenarios/run_hands.py`, `tools/linux/scenarios/run_hands_sword.py`
- Modify:
  - `src/full_body_driver.hpp` (`Tracking` :112-131; `publish` :299-310; diagnostics);
  - `src/dxgi_probe.cpp` (read the render-thread root beside the publish :5483-5572; trace writer; knob);
  - `tools/linux/bonelab_run_check.py` (RUN scenarios);
  - `tests/bonelab_scenarios_check.py`.
- Test: `tests/full_body_driver_check.cpp`

**Interfaces:**
- Produces:
  - `Tracking` gains `std::array<float,15> anchor_root{}; bool anchor_root_valid{};`. This is the body model frame (`player+0xf0` → `+0x1860`, read as at `dxgi_probe.cpp:2172-2177`) read on the render thread in the same block that builds the hand poses from the camera anchor.
  - The `FullBodyDriver` diagnostic `double root_gap_m{}` is the horizontal distance at publish between `frame_of(anchor_root).rigid.p` and `root_last_.p`.
  - Trace `artifacts/locomotion-hands.csv`, written only with `build/locomotion-trace`, one row per displayed frame:
    `elapsed_ms,speed_mps,root_gap_m,palm_residual_l,palm_residual_r,drawn_residual_l,drawn_residual_r,visual_gap_l,visual_gap_r,source_l,source_r,blends,contact_l,contact_r`.
    - `speed_mps`: horizontal anchor-root speed over the last 0.2 s.
    - `drawn_residual_*`: the status `drawn_palm_residual` value.
    - `visual_gap_*`: |the drawn hand position (`VisualOutput::drawn`) − the controller palm (`VisualInput::controller`)|.
    - `source_*`: 0 controller, 1 clamp, 2 body, 3 anchor.
    - `blends`: the cumulative `hand_visual` blends.
    - `contact_*`: `clamp_contact || body_contact`.
- The scenarios hold the head still and the hands at their simulated defaults. Timeline after the lead:
  - stand 8 s;
  - walk (`move_y` .5) 12 s;
  - run (`move_y` 1) 12 s;
  - sprint (`move_y` 1 plus `left_click` held) 12 s;
  - stand 8 s.

  `run_hands_sword` first draws the sword: `right_hand_offset [-0.03,0.25,0.50]` (the scabbard zone `TempleReach(-.17,-.10,-.10,.16)` relative to the default hand), `hand_pitch_degrees` +60, `right_grip` 1 for 1.5 s. Then the hand returns to the default with the grip held through the whole timeline.
- `bonelab_run_check.py --scenario run_hands|run_hands_sword` reads the trace:
  - bins: standing = `speed<0.3`, running = `speed>=2.5`;
  - FAIL if the trace is missing, or there are fewer than 200 standing or 200 running rows;
  - INFO: per side, the median `drawn_residual`, `palm_residual` and `visual_gap` standing vs running, the median `root_gap_m` running, the share of running frames on source 2, and the free-air blend bursts.
  - A **burst** is ≥ 2 increments of `blends` inside any 1 s window where `speed>=1.0` and both `contact_*` are 0.

  Task 9 turns the acceptance lines into FAILs.

- [ ] **Step 1: Write the failing tests.**
  - `full_body_driver_check`, `root_gap_measures_stale_upload_root`: `note_root` at x=0, then `publish` with `anchor_root` at x=.055 (5 m/s × 11 ms). `root_gap_m` = .055 ± 1e-6. Without `anchor_root_valid`, it is 0.
  - `bonelab_scenarios_check.py`:
    - both scenarios compile through `sim.scenario_segments`;
    - fixture traces: a stand/run trace with an equal residual → PASS rows and INFO numbers; a missing trace → FAIL; a trace with < 200 running rows → FAIL;
    - the burst counter counts 2 in a hand-made 1 s window and 0 when contact is set.
- [ ] **Step 2: Run, expect failure.** `... --only 'full_body_driver'` and `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 3: Implement** the read, the diagnostic, the trace (opened at install when the knob exists, flushed every 120 rows), the scenarios and the checks. `root_gap_m` also goes to the status `full_body` block. This task changes no behaviour: `root_at_publish_` is still `root_last_`.
- [ ] **Step 4: Run, expect pass.** Same commands.
- [ ] **Step 5: Tier B baseline runs.** Each run uses `--knob locomotion-trace`:
  - `run_hands` default;
  - `run_hands --knob no-hand-body`;
  - `run_hands_sword` default.

  Run `bonelab_run_check.py RUN --scenario <name>` for each; all PASS rows must pass. Write `.superpowers/sdd/2026-10-24-v609-feedback-fixes/g1-running-baseline.md` with every INFO number per run, the top speed reached, and the run folders. If the sim never reaches 2.5 m/s, record that and continue: Task 9 then follows its "not reproduced" rule.
- [ ] **Step 6: Commit** `test(arms): running-hands scenario, locomotion trace and baseline`

### Task 9: G1b running fixes, driven by Task 8's numbers

**Files:**
- Modify:
  - `src/full_body_driver.hpp` (`publish` :308);
  - `src/hand_servo.hpp` (`servo_step` :59, desired-velocity law);
  - `src/hand_body.hpp` (`DriveInput` :113; the call at :499);
  - `src/hand_visual_source.hpp` (`VisualInput` :26-41; constants :59-61; election :96-101);
  - `src/dxgi_probe.cpp` (feed the player velocity and speed; read the knobs);
  - `tools/linux/bonelab_run_check.py` (acceptance FAILs);
  - `docs/PLAYING.md` (knob list).
- Test: `tests/full_body_driver_check.cpp`, `tests/hand_servo_check.cpp`, `tests/hand_body_check.cpp`, `tests/hand_visual_source_check.cpp`, `tests/bonelab_scenarios_check.py`

**Interfaces:**
- Consumes: `Tracking::anchor_root`, `root_gap_m` and the trace (Task 8).
- Produces:
  - **Fix A (arm carry by the anchor's root).** `publish` sets `root_at_publish_=frame_of(t.anchor_root).rigid` when `t.anchor_root_valid` and `build/legacy-arm-root-carry` is absent. `FullBodyDriver::legacy_root_carry(bool)` is the switch.
  - **Fix B (hand-body feedforward).** The signature becomes `servo_step(..., double held_lever=kDefaultHeldLever, Vec3 feedforward={})`. Desired velocity = `feedforward` + the existing error law. The speed limit becomes `max_speed+length(feedforward)`. `DriveInput::player_velocity` (Vec3) is passed unless `build/no-hand-body-feedforward` exists.
  - **Fix C (controller in free air while moving).** `VisualInput::player_speed` (m/s). Constants `moving_speed=1.0, moving_leave=.6` (hysteresis). While moving and without contact, `agrees` cannot elect the body, so the clamp output is drawn. Contact rules are unchanged. `build/no-moving-controller-visual` disables it.
- **Which fixes ship (record the decision and numbers in the ledger report):**
  - **Reproduced** = the baseline's running-vs-standing median `drawn_residual` delta is > 1.5 cm on a tracked side, or there are free-air bursts > 0. Then:
    - apply A if the running `root_gap_m` median ≥ 0.01 m;
    - apply B if the default run's delta exceeds the `no-hand-body` run's by > 0.5 cm, or source 2 holds > 10 % of free-air running frames;
    - apply C if free-air bursts > 0;
    - if none is implicated, apply A.
  - **Not reproduced:** ship A, B and C, each behind its knob (the spec's "can't reproduce" rule).
  - Unapplied fixes are not merged. Their tests are not written.

- [ ] **Step 1: Write the failing tests** (only for the fixes that ship):
  - A, in `full_body_driver_check`, `anchor_root_carry_cancels_speed_offset`: the root moves at 5 m/s; `note_root` is 11 ms stale at publish; the upload is 5 ms after publish. The carried palm's world error vs the target moved by the root since publish is ≤ .001 m. With `legacy_root_carry(true)` it is ≥ .04 m.
  - B, in `hand_servo_check`:
    - `feedforward_holds_running_hand`: the target moves at 6 m/s along x for 2 s at 90 Hz from a matched start. The steady-state gap is ≤ .02 m with `feedforward` = 6 m/s, and ≥ .2 m with `{}`.
    - `zero_feedforward_identical`: outputs are bit-identical to the old call over a 500-step random sequence.
  - B, in `hand_body_check`, `drive_passes_player_velocity`.
  - C, in `hand_visual_source_check`:
    - `moving_free_air_draws_clamp`: speed 3, body agrees (gap .01) → source clamp.
    - `moving_contact_still_elects_body`.
    - `standing_agreement_unchanged`: speed 0 → body, as before.
    - `moving_hysteresis_no_chatter`: speed oscillating .8↔1.1 every 0.1 s for 3 s → at most 1 source change.
  - Acceptance, in `bonelab_scenarios_check.py`: the run checks now FAIL when a tracked side's running median `drawn_residual` exceeds standing + 0.015 m, or when free-air bursts > 0. Fixture traces show PASS at +0.01 m and FAIL at +0.02 m.
- [ ] **Step 2: Run, expect failure.** `... --only 'full_body_driver|hand_servo|hand_body|hand_visual_source'` and `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 3: Implement** the shipped fixes as in Interfaces. The player velocity and speed come from the Task 8 anchor-root samples (horizontal, last 0.2 s). Log each knob's state once at install.
- [ ] **Step 4: Run, expect pass.** Same commands.
- [ ] **Step 5: Tier B after-runs.** Repeat Task 8's three runs. `run_hands` default and `run_hands_sword` must PASS the acceptance: running ≤ standing + 1.5 cm, no free-air bursts. Add every after-number beside the baseline in the ledger report. Re-run `head_turn_walk` (`--lead 45 --seconds 120`) and `sword_sweep`; their `arm_flicker` and `hand_visual` numbers must be no worse than at `c4a0e73`.
  - If the acceptance still fails, stop and report the numbers. Do not add smoothing.
- [ ] **Step 6: Commit** `fix(arms): hands and weapons stay on the controllers while running`

### Task 10: G2 sword regrab gives one clean grip

**Files:**
- Create: `src/sword_blade_axis.hpp`, `tools/linux/scenarios/sword_regrab.py`
- Modify:
  - `src/vr_controls.hpp` (`SwordGrip` :389-428);
  - `src/body_ik.hpp` (`ArmState` :178-184; sneak flip :881; reverse :896-903);
  - `src/weapon_collision.hpp:33` (`align_weapon_blade` uses the helper);
  - `src/dxgi_probe.cpp`: blade axis :4930; aligns :4936/:5008; :5083; the combat aim :8290; blade-hit handling; the sword-grip trace;
  - `tools/linux/bonelab_run_check.py`, `tests/bonelab_scenarios_check.py`.
- Test: `tests/vr_controls_check.cpp` (:482-496), `tests/reverse_grip_check.cpp`, `tests/weapon_collision_check.cpp`, `tests/body_ik_check.cpp`

**Interfaces:**
- Produces, in `src/sword_blade_axis.hpp`:
  ```cpp
  Vec3 sword_blade_axis(const WorldTrackedPose& grip,bool reversed) noexcept;     // reversed ? -grip.basis[0] : grip.basis[0]; the ONE convention
  double blade_axis_gap_deg(Vec3 a,Vec3 b) noexcept;
  bool blade_hit_is_self(std::uintptr_t hit_entity,Vec3 hit_point,std::uintptr_t player,Vec3 player_root) noexcept;
      // hit_entity==player, or horizontal distance to the root < .30 m with root.z <= hit.z <= root.z+1.9
  ```
- Produces, in `SwordGrip`: `static constexpr double pickup_dead_band=.35;`.
  - A pickup (from the zone with a copy, using `draw_dot`; or from the floor, using `pickup_dot`) sets `reversed_` only when `|dot|>pickup_dead_band`. Otherwise it keeps the previous value (false on the first pickup).
  - A plain draw keeps `draw_dot<-.2`.
  - `reversed_` changes only on a draw or pickup edge.
- Produces, in `body_ik.hpp` `ArmState`: `bool sneak_flip{}, hold_calibrated{}; double reverse_weight{};`.
  - Sneak flip: enters at `rotated(n.q,{0,1,0}).z<-.5`, leaves at `>-.2`.
  - `hold_calibrated` turns true the first time `hold_steady>=.25` after a token change.
  - `reverse_weight` moves toward `reverse_grip?1:0` at 10 per second, but rises only while `hold_calibrated`. It resets to 0 on a token change.
  - The reverse is the same pivot rotation by angle `kPi*reverse_weight`.
  - Diagnostics: `reverse_weight[2]`, and `blade_axis_gap_deg` (the rig's drawn blade axis vs `sword_blade_axis`).
- Trace `artifacts/sword-grip-trace.csv` (knob `build/sword-grip-trace`), one row per displayed frame:
  `elapsed_ms,held,reversed,reverse_weight,hilt_miss,weapon_hidden_frames,blade_axis_gap_deg,blade_self_hits`.
- `sword_regrab` timeline (zone offset and pitch as in Task 8; each step ≥ 2 s):
  1. draw forward (pitch +60);
  2. a 3 s slow sweep (as `sword_sweep` phase 1);
  3. release away from the zone (drop);
  4. regrab reversed at the zone (pitch −60), with the copy present;
  5. sweep;
  6. drop;
  7. regrab forward;
  8. sweep;
  9. drop;
  10. regrab reversed;
  11. sweep;
  12. release in the zone (sheathe).
- Check `--scenario sword_regrab`: at least 4 holds, and their `reversed` are F, R, F, R in order. From each hold's start + 0.35 s to its end:
  - `hilt_miss` ≤ .01 m;
  - `reversed` constant;
  - `weapon_hidden_frames` increases by 0;
  - `blade_self_hits` increases by 0;
  - `reverse_weight` is 0 or 1 (the blend is done).

  INFO: the max `blade_axis_gap_deg` per hold.

- [ ] **Step 1: Write the failing tests.**
  - `vr_controls_check`:
    - `pickup_dead_band_keeps_previous`: a reversed draw, then a drop, then a floor pickup at `pickup_dot` .2 → still reversed.
    - `first_pickup_in_dead_band_is_forward`.
    - `perpendicular_regrab_keeps_previous` (Review Focus 4): `pickup_dot` alternating ±.2 over 50 frames of one hold never changes `reversed`.
    - `reverse_changes_only_on_edges`: while held, `draw_dot`/`pickup_dot` sweeping −1..1 leaves `reversed` unchanged.
    - The existing :485-496 cases still hold.
  - `reverse_grip_check`:
    - `sneak_flip_hysteresis`: z sequence −.6, −.4, −.3, −.1, −.3 gives flip on, on, on, off, off.
    - `reverse_waits_for_calibrated_hold`: weight 0 until `hold_steady>=.25`.
    - `reverse_blends_in_0_1s`: after calibration at 90 Hz the weight reaches 1 in 9 or 10 frames, and the hilt stays in the fist during the blend (`hilt_miss<=.01`).
  - `weapon_collision_check`:
    - `blade_axis_one_convention`: `align_weapon_blade(...,true)` aligns `-basis[0]` with the target, the same as `sword_blade_axis(g,true)`.
    - `blade_self_hit_rejected`: entity==player → true; a point .2 m from the root at z+1.0 → true; a point 1 m away → false.
  - `bonelab_scenarios_check.py`: `sword_regrab` compiles. A fixture trace passes; one with a flipped `reversed` mid-hold fails; one with `weapon_hidden_frames` rising after +0.35 s fails.
- [ ] **Step 2: Run, expect failure.** `... --only 'vr_controls|reverse_grip|weapon_collision|body_ik'` and `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 3: Implement.**
  - Every consumer of the reverse flag takes it from the one latched `sword_reversed` and builds the axis with `sword_blade_axis`: collision :4930, aligns :4936/:5008, the poke :5083, and the combat aim :8290 (keep its `GV3` type through a converted `sword_blade_axis`).
  - Blade hits for which `blade_hit_is_self` is true are dropped before `blade_dir` is used, and counted in `blade_self_hits`. The player is `body->player` and its root is the Task 8 anchor root.
  - Status: `sword_grip.blade_self_hits`, `full_body.grip[].reverse_weight`, `full_body.blade_axis_gap_deg`.
- [ ] **Step 4: Run, expect pass.** Same commands, plus `--only 'dxgi_probe|motion_sword|sword_'`.
- [ ] **Step 5: Tier B run.** `sword_regrab` with `--knob sword-grip-trace --lead 45 --seconds 120`, then `bonelab_run_check.py RUN --scenario sword_regrab` must PASS. Re-run `sword_sweep`: `arm_flicker.weapon_hidden_frames` no worse than at `c4a0e73`. Record the numbers (including the `blade_axis_gap_deg` INFO) in the ledger.
- [ ] **Step 6: Commit** `fix(sword): one blade-axis convention, regrab dead band, calibrated reverse blend, no self-hits`

### Task 11: Tier C headset checklist and final verification

**Files:**
- Create: `docs/VR-v609-feedback-TEST-CHECKLIST.md`

**Interfaces:**
- Consumes: every status field, knob and trace named above.

- [ ] **Step 1: Write the checklist.** Use the house style of `docs/VR-bonelab-feel-finger-curl-TEST-CHECKLIST.md`. Give each spec Tier C line one item, with what to watch and the status field or trace to save:
  1. Run and sprint with empty hands, the sword and the gun: lag, shake, drift. Use `build/locomotion-trace` and record which of `legacy-arm-root-carry`, `no-hand-body-feedforward`, `no-moving-controller-visual` shipped. An A/B with each knob is optional.
  2. Regrab the sword the other way round five times, including grabs across the blade. Use `build/sword-grip-trace`.
  3. Fist with an empty hand: no draw. The over-shoulder draw still works. Block and choke still work.
  4. Crosshair on the gun and two powers at near and far targets, in both eyes. Turn it off and on in the game. Also check HUD 65 % and 200 %. Save the `crosshair` status block.
  5. Fresh-install AA on the RTX laptop: expect DLAA. Check the `aa_first_start` status and the log's removed-knob lines. Change AA in the game, then open or focus the launcher: it shows the same.
  6. Drag a corpse: 2.5× lighter and still controlled. Check that a prop feels as in v608.
  7. Pick up a paper, a note and a stamp: the fingers stop on the item. Save `finger_ik.hold` `thin_holds`/`box_holds`.
  8. Make a full fist with an empty hand: the fingertips rest on the palm.
- [ ] **Step 2: Final verification.**
  - The full harness: `python3 tools/linux/run_build_cmd.py --keep-going candidate d2vr_render_probe_arm_v609.dll`. Expect no new failures against 628 / 1 (`fsr_taa_gpu_check --fsr4`) / 28, with the new checks among the passes.
  - The launcher tests: ALL PASS.
  - The launcher build.
  - `python3 -m unittest discover -s linux/tests`: OK.
  - Record the counts in the ledger.
- [ ] **Step 3: Commit** `docs: v609 headset feedback checklist`

The release (v609) is not a task. The controller does it after review, following the v608 process.

---

## Self-review

- **Spec coverage.**
  - G1 → Tasks 8 and 9 (measure first, fix by the numbers, knobs, no smoothing, acceptance 1.5 cm and no bursts).
  - G2 → Task 10 (one axis, edge-only reverse, dead band, calibrated 0.1 s blend, sneak hysteresis, self-hit, scenario).
  - G3 → Task 2 (gate, routing and unit tests, PLAYING.md).
  - G4 → Task 5 (stage mapping, HUD scale, per eye, cast or 30 m, in-game row, hide, live), plus Task 3 (field, file migration) and Task 4 (launcher row).
  - G5 → Task 3 (default, one-time DLAA, marker, knob removal with logs, `fsr4.off` kept, write-back) and Task 4 (re-read on save and activation, changed fields only, HUD 50–200).
  - G6 → Task 1.
  - G7 → Task 6.
  - G8 → Task 7.
  - Tier A, B and C → each task's tests, Tasks 8 and 10 runs plus the re-runs of `head_turn_walk`/`sword_sweep`, and Task 11.
- **Rulings where the code or spec left a choice:**
  1. Props get their own `prop_strength_scale` 6, because the corpse value also drove capped props (`native_grab.hpp:590-595`).
  2. A pre-v609 file on a GPU without DLSS keeps its saved AA (the user's one-time switch names RTX only).
  3. The legacy AA knob reads stay as diagnostics, because the sim's `--vr-setting temporal_aa` writes `build/aa-mode`. The migration deletes the files once, so only a deliberate dev knob is ever read. `build/fsr` also carries FSR tuning; its contents are logged before removal.
  4. The `hand-crosshair` Features row becomes a launcher Settings row ("Crosshair"), because a pref-backed Features row would break the Features model's file invariants.
  5. The in-game panel gets an 11th row by tightening the pitch from 36 to 33 px. There was no free row above the description plate.
  6. HUD-scale compensation stays the draw path's existing unlayout. Pre-scaling in `place()` as well would double it. A fixture proves it, and a counter shows it live.
  7. Per-eye placement is a draw-time translation shift for the second UI pass. The AS3 position is set once per frame.
  8. Regrabs in the sim go through the shoulder zone (deterministic). Floor pickup is Tier A plus Tier C.
- **Type consistency.** `aa_chosen`/`crosshair` (Tasks 3, 4, 5); `Tracking::anchor_root`/`root_gap_m` (Tasks 8, 9, 10); `sword_blade_axis` (Task 10 only); `crosshair_slot`/`producer_crosshair` (Task 5 only). The fixture `tests/fixtures/vr-settings-v9.cfg` is created in Task 3 and read in Task 4.
- **Review Focus.** Each has a named test: Task 3 `pre_v609_non_rtx_keeps_saved_mode` and `vendor_failure_sets_marker_once`; Task 4 `merge_keeps_game_changes_on_stale_form`; Task 10 `perpendicular_regrab_keeps_previous`; Task 5 `crosshair_toggle_live`.
- **Proportion.** About 1.4× the spec's length for eleven tasks. Code appears only as signatures and pinned strings.
