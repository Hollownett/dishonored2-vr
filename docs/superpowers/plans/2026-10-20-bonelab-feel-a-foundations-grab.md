# Bonelab Feel A: Foundations F1-F4, Corpse Grab, Living-Guard Pull and Grasp-Point Seating Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A dead or unconscious guard can be dragged, lifted and swung with one hand by a limb or the collar (forgiving weight, crash-safe motor); a living guard that is grabbed is pulled off balance and alerted without ragdolling (1L); the visible hand sits on the grasp point of every motor-held prop and corpse (user decisions 2, 3, 6 of 2026-10-20). Part 5 (hand body, held-object collision, tank fix) is plan B.

**Architecture:** Pure, separately tested headers carry the policy: a force-capped velocity step (F2), a mass table with a safety gate (F1), a ragdoll write-safety envelope (F3), corpse diagnostics (F4), a pelvis-tow planner, a grasp-anchor blend, a living-guard pull planner. `PhysicalGrab` gains a corpse mode and `NativeGrab` drives it through the envelope inside the existing 0x89fb0 dispatcher callback (velocity setter 0xc85dc0 stays the only ragdoll writer). A pure `GuardPull` planner drives a living guard through levers settled by a probe task first (stagger and alert), never through `START_RAGDOLL`. `DrawnAnchor` moves the drawn hand to the grasp point, with a knob restoring today's prop drawing.

**Tech Stack:** C++17 headers (MSVC `cl /W4 /WX`, clang-cl + Wine on Linux with `-Wshadow` family as errors), MinHook, OpenXR, Havok hknp through fixed RVAs; checks registered in `tools\build-msvc.cmd`; Python source guards for `dxgi_probe.cpp` routing; `linux/d2vr-linux sim` scripted sessions for Tier B.

**Spec:** `docs/superpowers/specs/2026-10-19-bonelab-feel-design.md` (binding: G1-G3, F1-F4, Part 1, "Resolved questions (2026-10-20, answered by the user)", which overrides the spec's earlier defaults C2, C3, C7 feel and C11). Companion: `docs/superpowers/plans/2026-10-20-bonelab-feel-b-hand-body.md` (Part 5, may start after Task 5 here). Research: `~/.local/share/d2vr-re/research/corpse-grab-prep-2026-10-18.md` ("corpse prep"), `~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` ("Bonelab §").

**Later plans (not here):** Part 2 throw history and spin, Part 3 touch-sensor finger curl, Part 4 palm-facing scoring. Plan B holds Part 5.

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel` (base 6913ce0). Never `cd` into another checkout, never `git stash`. Plan workspace (git-excluded): `.superpowers/sdd/2026-10-20-bonelab-feel-foundations-corpse/`.
- Pinned exe `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. All RVAs are fixed to it; every hook byte check stays.
- Every engine write keeps the SEH guard (`guarded_call`), finite checks and fault counters; handle generation (`(gen+1)<<24 | index`) and world identity are re-validated on every write. Writes run only in the 0x89fb0 dispatcher callback (velocity setter 0xc85dc0). Event posts go through 0x8112c0. Nothing calls 0xc90de0, 0xc8e5b0 or 0xc85860.
- Force caps (F2), Bonelab base: about 300 N along the arm axis, about 90 N laterally, torque cap 25-30 N·m over an inertia estimate; `Δv_max = F_max·dt/(m_object + m_hand)`, `v_new = v_object + clamp(v_motor − v_object, Δv_max)`. **Forgiving (user, 2026-10-20):** all three caps are multiplied by `strength_scale` (default 3, tuned in the headset) so one hand lifts and swings a whole body; the technique (no teleport, smooth catch-up) is unchanged. Caps live in `build/grab-tuning.cfg`. Mass floor and ceiling are lab-tuned. Two hands on one body: each hand's mass share is half.
- Caps apply only to bodies the mod drives with velocity writes (corpses, limbs, motor-driven props). Native HoldObject items (bottle, tank, severed arm) keep the engine's keyframed bind; their collision is plan B.
- Living guards (1L, user: "pull + stagger"): the grab pulls him toward the hand, staggers him and alerts him; no `START_RAGDOLL`, no `VELOCITY_IMPULSE`, no ragdoll write to a living guard. The old yank (ragdoll) path stays behind `build/legacy-yank`.
- Release: grasp anchor lags the raw controller by more than 0.5 m for 0.25 s (replaces the old 1.5 m error release, corpses only; props keep 1.5 m); 0.3 s regrab cooldown after a forced release; 0.25 s visual blend onto the grasp point; drawn hand bounded to 0.35 m from the raw controller; a corpse release writes no velocity (throws are Part 2).
- Event posts: `START_RAGDOLL 0xb9` at most once per wake of an entity, never while the primary body reads dynamic (`flags & 7 == 2`), never on the 400 ms timer for corpses; one retry after 1 s, then refuse the grab with a short haptic pulse; global ceiling 2 posts per second; `VELOCITY_IMPULSE 0x149` is not used by anything new. Kill-switch files `build/no-ragdoll-posts` and `build/no-corpse-grab`, read at start and logged.
- Runaway: any of the 17 bodies above 12 m/s or non-finite while held releases the hold, blocks writes to that entity for 1 s, counts `runaway`; the mod never corrects a runaway with another write. Any SEH fault on a ragdoll write latches ragdoll writes off for the session and sets a status flag.
- Frame budget: new work ≤ 0.2 ms game-thread time for both hands combined (`cost_us` / `cost_max_us`), on top of phase 1's ≤ 0.5 ms cast budget.
- C9 routing stays the law: raw controller pose drives intent (grab desire, selection, aim); the drawn pose (here: anchored to the held body) drives the hand and arm IK target; native authored animations keep ownership and the clamp yields. **Props (user decision 6):** the drawn hand sits on the grasp point of every motor-held prop and corpse (changes accepted behaviour); `build/no-prop-grasp-point` restores today's prop drawing. A native HoldObject item is already seated in the palm by the grip fit, so its drawing is unchanged.
- `mass_verified` and `time_factor_verified` stay false until a runtime probe proves them; unknown mass falls back to the class table, never a guess shown as measured.
- Every task brief cites the Bonelab technique or numbers it follows, in our own words (Bonelab §1.2 dynamic grips, §1.3 snatch grab and 0.25 s pose blend, §1.4/§2.1 force-limited hand and effective mass, §1.7 regrab cooldown, §2.2 "the hand lags when blocked"). No Bonelab code or assets.
- Every new check is registered in `tools\build-msvc.cmd` with `/std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc` (add `/Ithird_party\minhook\include` when the check includes MinHook users) and fails the build on non-zero exit. Linux harness (`python3 tools/linux/run_build_cmd.py`, baseline 483 passed / 0 failed / 41 skipped) must stay green with the `-Wshadow`, `-Wshadow-field`, `-Wshadow-field-in-constructor` family as errors; run one task's checks with `python3 tools/linux/run_build_cmd.py --keep-going --only '<regex>' candidate d2vr_render_probe_arm_v601.dll`.
- Mutation checks run after the task commit: apply the listed constant edit with `sed` on the committed header, rerun the check (it must fail), then `git checkout -- <header>`. Record kills in the ledger.
- Testing honesty: Tier A (offline labs, unit tests) and Tier B (`linux/d2vr-linux sim --scenario <name>`) pin logic and crash-safety; only Tier C (headset checklist) declares feel met. The first Part 1 headset run is a tuning session, not a validation run.

## Spec deviations

Each item corrects or narrows the spec against the code. Tasks implement the corrected version.

1. **`ragdoll_grab_bone` is unchanged** (it classifies limb versus torso in `NativeWeaponHands::wake` and in the yank path). Task 6 adds `ragdoll_hold_bone` and a `nearest_ragdoll_body(..., allow_collar)` parameter used only for dead or unconscious guards.
2. **Prop motor holds get the force cap but keep the 1.5 m error release** (`physical_grab.hpp:126`) and the throw-on-open-fist; the 0.5 m / 0.25 s lag rule and the no-velocity release are corpse behaviour only (throws are Part 2).
3. **Sim sessions last 10-120 s** (`linux/d2vr_linux/sim.py:496`). S5 (200 cycles) runs as 4 sessions of 50; Task 13 sums the counters. Tier B needs saves that cannot be committed, so Task 1 documents them and the ledger records "Tier B outstanding" for any task whose saves are missing.
4. **The 2/s post ceiling applies to the corpse wake path** (`NativeWeaponHands::wake`). The legacy living-guard yank (`ragdoll`, `hold_down`, behind `build/legacy-yank`) honours only the `no-ragdoll-posts` kill-switch.
5. **Release with an open fist writes no velocity for corpses** (spec Part 1 "release at rest: no velocity written"). Today `NativeGrab::update_lane` throws the limb with hand velocity (`native_grab.hpp:363-369`); Task 10 skips that write for corpse lanes.
6. **The wake path survives for settled corpses only.** `NativeWeaponHands::yank` (`:350-368`) ragdolls a living guard with `shove` + `START_RAGDOLL`; the spec's 1L replaces it with a pull and stagger (Tasks 14-16). The shove (`NativeWeaponHands::shove`, DAMAGETYPE_SHOVED, no damage, alerts him) is the one existing lever reused; the pull and stagger levers are probed first (Task 14) and never assumed.

## Review Focus

1. Garbage engine reads (zero or `0xffff` mass half, NaN or huge motion velocity, a recycled body handle) must never reach a write, a divide or a clamp. Pinned by Task 3 `garbage_mass_rejected` and Task 10 `readback_garbage_drops_hold_without_write`.
2. A state transition on the exact tick of another event: unconscious guard wakes while the fist opens, world change or death reload during a lag release, tracking loss on the cooldown boundary. Pinned by Task 10 `state_change_and_open_fist_same_tick` and `world_change_during_lag_release`.
3. Grip flapping around the 0.35/0.45 hysteresis (200 grab/release cycles) must not storm event posts, rebinds or regrab through the cooldown. Pinned by Task 4 `flapping_200_cycles_within_ceiling` and Task 7 `cooldown_blocks_regrab_flapping_grip`.
4. A pause, hitch or first-sample `dt` (0, negative, 0.5 s) must not turn `F·dt/m` into a velocity spike. Pinned by Task 2 `hitch_dt_clamped` and Task 9 lab scenario `hitch`.
5. A guard that dies, is knocked out, starts a takedown or navigation animation, or is pulled into a wall or off a ledge during the pull; and a pull of a guard that was already fleeing. Pinned by Task 15 `state_change_ends_pull`, `pull_blocked_by_world_stops` and `fleeing_guard_not_pulled`.

---

## File Structure

Created (one responsibility each):
- `src/grab_force_cap.hpp`: `GrabTuning`, tuning-file loader, `cap_velocity_step`, `effective_mass`, `bone_inertia` (F2).
- `src/grab_mass.hpp`: bone class table, names, `ragdoll_body_mass`, `prop_body_mass`, `hanging_mass` (F1).
- `src/mass_probe.hpp`: read-only probe rows and CSV (F1).
- `src/ragdoll_safety.hpp`: `ReleaseReason`, `EventPostBudget`, `RunawayDetector`, `EntityLockout`, `FaultLatch`, `KillSwitches` (F3).
- `src/corpse_diagnostics.hpp`: `CorpseDiagnostics` JSON, bounded CSV, `CorpseTraceWriter` (F4).
- `src/corpse_tow.hpp`: `plan_tow` (pelvis tow, capped, flagged).
- `src/drawn_anchor.hpp`: `DrawnAnchor` (grasp-point blend, 0.35 m bound).
- `src/guard_pull.hpp`: `GuardPull` planner for 1L.
- `tools/corpse_drag_lab.cpp`: Tier A lab, build check.
- `tools/linux/check_test_saves.py`, `tools/linux/scenario_lib.py`, `tools/linux/bonelab_run_check.py`, `tools/linux/scenarios/corpse_*.py`, `guard_pull*.py`: Tier B.
- `docs/BONELAB-FEEL-SIM-SAVES.md`, `docs/VR-bonelab-feel-corpse-TEST-CHECKLIST.md`, `docs/BONELAB-FEEL-GUARD-PULL-RE.md` (probe evidence).
- Tests: `tests/grab_force_cap_check.cpp`, `grab_mass_check.cpp`, `ragdoll_safety_check.cpp`, `corpse_diagnostics_check.cpp`, `corpse_tow_check.cpp`, `drawn_anchor_check.cpp`, `guard_pull_check.cpp`, `corpse_hold_check.cpp`, `corpse_routing_check.py`, `guard_pull_routing_check.py`, `sim_saves_check.py`, `bonelab_scenarios_check.py`.

Modified:
- `src/physical_grab.hpp`: corpse mode (`corpse_motor`, lag release, cooldown, `anchor`).
- `src/native_rigid_body.hpp:96-100,149-159`: collar gate and state-gated candidates.
- `src/native_grab.hpp:158-170,222-262,316-328,331-410,508-520`: lane fields, discovery gate, tow, corpse motor tick, accessors.
- `src/native_weapon_hands.hpp:350-432`: budgeted `wake`, retry and refusal; living-guard pull replaces `yank` (legacy behind a knob).
- `src/dxgi_probe.cpp`: knobs (~8135), status JSON (`"corpse"`, `"guard_pull"`, next to `"carry"` ~5250), drawn-hand override after the clamp block (~4272-4453).
- `tools/build-msvc.cmd`: register each new check next to its neighbours (`physical_grab_check` ~366, `push_lab` ~797).

---

### Task 1: Tier B test saves and their validator

Documentation and a validator only. No engine code.

**Files:**
- Create: `docs/BONELAB-FEEL-SIM-SAVES.md`, `tools/linux/check_test_saves.py`
- Test: `tests/sim_saves_check.py`; register after `tests\live_session_tools_check.py` in `tools/build-msvc.cmd` as `python tests\sim_saves_check.py`

**Interfaces:**
- Produces: `check_save_dir(path: pathlib.Path) -> list[str]` (problem strings, empty when usable: a directory holding at least one `SLOT*` subfolder and `profile.bin`); CLI `python3 tools/linux/check_test_saves.py DIR...` exit 1 on any problem. Named saves under `~/d2vr-test-saves/`: `tank-floor` (used by plan B), `corpse-dead`, `guard-sleeping` (optional), `guard-awake`.

- [ ] **Step 1: Failing test** `tests/sim_saves_check.py` (unittest): `empty_dir_reports_missing_slot_and_profile` (two problems), `not_a_directory_reports_one_problem`, `slot_and_profile_ok` (no problems), `slot_without_profile` (one problem).
- [ ] **Step 2: Run, expect failure.** `python3 tests/sim_saves_check.py` fails with `ModuleNotFoundError: check_test_saves`.
- [ ] **Step 3: Implement** the validator (`importlib` load of the tools file by path in the test, as `linux/tests` do).
- [ ] **Step 4: Write `docs/BONELAB-FEEL-SIM-SAVES.md`.** Per save: how to make it (play to the spot, manual save, quit, copy `savegame/` `SLOT*` + `profile.bin` to `~/d2vr-test-saves/<name>`), and the placement the scenarios assume. `tank-floor`: standing 0.5 m from a whale-oil tank on the floor in front of the knees, a wall within 2 m ahead, open floor to the right. `corpse-dead`: a dead guard lying on open floor with the right wrist 0.5 m in front of the knees, head toward the player's left, 3 m of free floor ahead, no other guard awake within 15 m. `guard-sleeping` (optional): an unconscious guard in the same placement. `guard-awake`: an unaware guard standing still (or seated), upper body 0.4 m from the right hand's rest position, 3 m of free floor behind him, no wall within 2 m behind him, no other guard within 15 m. Include the run line `linux/d2vr-linux sim --test-save ~/d2vr-test-saves/tank-floor --scenario <name> --status-field corpse --status-field held_clamp` and the rule: scenarios need a settled corpse (woken by the squeeze, S1) so no pre-wake is required.
- [ ] **Step 5: Run, expect pass; ask the user.** `python3 tests/sim_saves_check.py` passes. The controller then asks the user for the three saves (ledger note: "Tier B saves: present / absent"). Absent saves never block Tier A work: affected Tier B items are listed outstanding in the ledger.
- [ ] **Step 6: Commit.**
  ```bash
  git add docs/BONELAB-FEEL-SIM-SAVES.md tools/linux/check_test_saves.py tests/sim_saves_check.py tools/build-msvc.cmd
  git commit -m "test: document and validate the Tier B test saves for the bonelab-feel scenarios"
  ```

---

### Task 2: F2 force-capped step and the grab tuning file (Bonelab §1.4, §2.1)

Bonelab limits how hard the hand pulls (about 300 N along the forearm, about 90 N sideways, 30 N·m torque) and counts the held object in the hand's effective mass. We have only velocity writes, so the same law caps the change of speed per motor tick.

**Files:**
- Create: `src/grab_force_cap.hpp`
- Test: `tests/grab_force_cap_check.cpp`; register after `physical_grab_check` (`build-msvc.cmd:366-369`)

**Interfaces:**
- Consumes: `Vec3`, `finite`, `dot`, `length` (`tracked_pose.hpp`).
- Produces (namespace `d2vr`):
  ```cpp
  struct GrabTuning {double axial_n=300,lateral_n=90,torque_nm=30,strength_scale=3,hand_kg=2,mass_floor_kg=.5,mass_ceiling_kg=60,
      lift_fraction=.5,dt_max=1./30;bool tow=false,use_measured_mass=false;
      double axial_cap() const {return axial_n*strength_scale;} double lateral_cap() const {return lateral_n*strength_scale;} double torque_cap() const {return torque_nm*strength_scale;}};
  enum class TuningSource {defaults,file};
  struct TuningLoad {GrabTuning value;TuningSource source{TuningSource::defaults};std::string error;};
  TuningLoad load_grab_tuning(const std::filesystem::path& file) noexcept; // build/grab-tuning.cfg: "key value" lines
  double effective_mass(double object_kg,const GrabTuning&,double share=1.);   // hand_kg + share*clamp(object_kg,floor,ceiling)
  double bone_inertia(double mass_kg,double radius_m);                          // 0.4*m*r^2, at least 1e-4
  struct CappedStep {Vec3 linear{},angular{};bool linear_capped{},angular_capped{};double implied_force_n{},implied_torque_nm{};};
  CappedStep cap_velocity_step(Vec3 v_object,Vec3 w_object,Vec3 v_motor,Vec3 w_motor,Vec3 arm_axis,
      double object_kg,double inertia_kgm2,double dt,const GrabTuning&,double share=1.);
  ```
  `linear` / `angular` are the velocities to write (`v_object + clamped delta`). `hand_kg` is phase 1's hand mass (`HandContactConfig::strength_kg`, 2 kg).

- [ ] **Step 1: Failing tests** (CHECK macro as in `hand_clamp_check.cpp`):
  - `axial_cap_300n`: with `strength_scale=1`, object 10 kg, dt 0.01, motor +10 m/s along `arm_axis` from rest: `linear.x == 300*0.01/12` (±1e-9), `linear_capped`; with the default scale 3 it is `900*0.01/12`.
  - `lateral_cap_90n`: scale 1, motor across the axis: `|linear| == 90*0.01/12`; default `270*0.01/12`.
  - `light_limb_follows`: object 0.6 kg, command 0.3 m/s: not capped, `linear == v_motor`.
  - `mass_floor_and_ceiling`: object 0.01 kg uses 0.5 (`300*0.01/2.5`); 500 kg uses 60 (`300*0.01/62`).
  - `torque_cap`: scale 1, inertia 0.02, ω error 100 rad/s, dt 0.01: `|angular| == 30*0.01/0.02` (15), `angular_capped`.
  - `two_hands_share_half`: share 0.5, object 20 kg: effective mass 12, not 22.
  - `hitch_dt_clamped`: dt 0.5 gives exactly the dt = 1/30 result; dt 0, negative, NaN return `v_object`/`w_object` unchanged. (Review Focus 4)
  - `non_finite_motor_holds`: any NaN in `v_motor` or `w_motor` returns the object velocities with both flags false.
  - `implied_force_within_cap_1pct`: 1000 pseudo-random cases (fixed seed): `implied_force_n` along axis ≤ `axial_cap()*1.01`, lateral ≤ `lateral_cap()*1.01`.
  - tuning: `defaults_when_missing` (source defaults, empty error), `file_overrides` (`axial_n 250`, `strength_scale 2`, `tow 1`), `corrupt_keeps_defaults` (unknown key, NaN, out of bounds: axial 50-600, lateral 20-300, torque 5-60, strength_scale 0.5-10, lift_fraction 0-1, floor 0.1-5, ceiling 10-200; 5000 bytes; a key without value: each gives source defaults, non-empty error, every field default).
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --keep-going --only 'grab_force_cap' candidate d2vr_render_probe_arm_v601.dll`. Expected: compile error, header missing.
- [ ] **Step 3: Implement** `src/grab_force_cap.hpp`. Algorithm not fixed by the tests: with `Δ = v_motor − v_object`, split `Δ_axial = dot(Δ, arm_axis)·arm_axis` (axis normalised; a non-unit or non-finite axis falls back to the whole Δ as lateral) and `Δ_lat = Δ − Δ_axial`; limit `|Δ_axial| ≤ axial_cap()·dt/m` and `|Δ_lat| ≤ lateral_cap()·dt/m` with `m = effective_mass(...)`, `dt = min(dt, dt_max)`; angular `|Δω| ≤ torque_cap()·dt/inertia`. `implied_force_n = m·|Δ_written|/dt`. The loader never throws (read at most 4 KB).
- [ ] **Step 4: Run, expect pass.** Same command. Register the check in `build-msvc.cmd`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/grab_force_cap.hpp tests/grab_force_cap_check.cpp tools/build-msvc.cmd
  git commit -m "feat: force-capped velocity step and grab tuning file (F2)"
  ```
- [ ] **Step 6: Mutation pass.** `strength_scale=3=>1` (killed by Task 9's forgiving scenarios), `300=>400` (axial), `1./30=>1./10` (dt_max), `.5=>.1` (floor), drop the dt clamp: each fails the check. Record in the ledger.

---

### Task 3: F1 mass table, safety gate and the read-only mass probe (+ probe run)

**Files:**
- Create: `src/grab_mass.hpp`, `src/mass_probe.hpp`
- Modify: `src/native_hand_contact.hpp:53-65` (only if the include chain is too heavy: move `NativeMotion` and `native_motion` unchanged to `src/native_motion.hpp`, include it from both), `src/native_grab.hpp` bind sites (`:372-381`, `:400-406`): call the probe when `build/mass-probe` exists
- Test: `tests/grab_mass_check.cpp`; the probe-row test goes into `tests/native_rigid_body_check.cpp` (its 17-handle fixture, `:72-108`)

**Interfaces:**
- Consumes: `GrabTuning` (Task 2), `NativeRigidBody`, `NativeGrabBounds`, `prop_mass` (`hand_contact.hpp:241`), `native_motion` (`native_hand_contact.hpp:54`).
- Produces:
  ```cpp
  inline constexpr std::array<double,17> ragdoll_class_kg{12,10,18,1.5,4.5, 2.0,1.6,.6, 2.0,1.6,.6, 7,3.4,1.2, 7,3.4,1.2};
  inline constexpr double prop_class_kg=12.,body_class_total_kg=75.;
  const char* ragdoll_bone_name(int bone);   // "pelvis","spine","chest","neck","head","l_upper_arm","l_forearm","l_hand","r_upper_arm","r_forearm","r_hand","l_thigh","l_calf","l_foot","r_thigh","r_calf","r_foot"; "?" out of range
  enum class MassSource {cls,measured};
  struct BodyMass {double kg{};MassSource source{MassSource::cls};bool rejected{};};
  BodyMass ragdoll_body_mass(int bone,std::optional<std::uint16_t> inverse_half,bool use_measured);
  BodyMass prop_body_mass(std::optional<std::uint16_t> inverse_half,const NativeGrabBounds&,bool use_measured);
  double hanging_mass(int bone,double lift_m,const GrabTuning&);  // lift_fraction*(75-class[bone])*clamp(lift_m/.25,0,1)
  struct MassProbeRow {std::uint64_t tick{};std::uint32_t entity_id{},handle{},record_flags{};int bone{-1};
      std::uint16_t raw_inverse_half{};double decoded_kg{},class_kg{};bool rejected{};};
  std::string mass_probe_csv_header();std::string mass_probe_csv_row(const MassProbeRow&);
  template<class Read> std::vector<MassProbeRow> probe_ragdoll_masses(Read& read,std::uintptr_t base,std::uintptr_t entity,std::uint64_t tick);
  template<class Read> std::optional<MassProbeRow> probe_prop_mass(Read& read,const NativeRigidBody&,std::uint64_t tick);
  ```
  A read counts as plausible when the half decodes (`prop_mass` rule) to a finite inverse > 1e-4 whose reciprocal lies within a factor 12 of the class value; `rejected` is true when a read was present and not plausible. `source` is `measured` only when `use_measured` and plausible; `mass_verified` stays false regardless.

- [ ] **Step 1: Failing tests** in `grab_mass_check.cpp`: `class_table_pinned` (17 values and total 75 as above), `names_pinned` (index 2 "chest", 3 "neck", 16 "r_foot", 17 "?"), `garbage_mass_rejected` (halves 0, 0xffff, 0x7fc0 NaN, 0x7f80 inf: kg == class, `rejected`, source cls, whether or not `use_measured`) (Review Focus 1), `plausible_read_ignored_until_enabled` (a half decoding to 2 kg for a 1.6 kg forearm: kg 1.6 while `use_measured=false`, 2.0 and `measured` when true), `no_read_is_not_rejected`, `hanging_mass_ramp` (0 at 0 m, half at 0.125 m, full `0.5*(75-class)` at 0.25 m and beyond, 0 for a negative or NaN lift). In `native_rigid_body_check.cpp`: `mass_probe_lists_17_rows` (17 rows, bones 0-16 in order, names, raw half from the fixture's motion array, `record_flags` from the pool record) and `mass_probe_csv_row_fields` (field count equals the header's).
- [ ] **Step 2: Run, expect failure.** `... --only 'grab_mass|native_rigid_body'`. Expected: compile error.
- [ ] **Step 3: Implement.** `probe_ragdoll_masses` walks bones 0-16 with `native_ragdoll_body` + `native_motion`; a bone that does not resolve is skipped. The bind-site hook (guarded by `build/mass-probe`, read once at start; at most 200 rows per session) appends rows to `artifacts/mass-probe.csv` from the dispatcher thread with a plain `std::ofstream` flushed per set. Status gains `"mass":{"source":"class"|"measured","verified":false,"read_rejected":N}` via Task 5's block.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'native_grab_compile'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/grab_mass.hpp src/mass_probe.hpp src/native_grab.hpp src/native_hand_contact.hpp tests/grab_mass_check.cpp tests/native_rigid_body_check.cpp tools/build-msvc.cmd
  git commit -m "feat: ragdoll mass class table, plausibility gate and a read-only mass probe (F1)"
  ```
- [ ] **Step 6: Probe run (evidence, Tier B; skip if saves are missing and mark outstanding).** Create `build/mass-probe`, run `linux/d2vr-linux sim --test-save ~/d2vr-test-saves/corpse-dead --scenario oil_tank_drop --lead 60` (the scenario's hand reaches the nearest physical object; use `corpse_probe` if Task 13 has landed). Copy `artifacts/mass-probe.csv` to `.superpowers/sdd/2026-10-20-bonelab-feel-foundations-corpse/mass-probe-table.md` with: raw half per bone, decoded kg versus class, a bottle/crate/torso comparison, and the **collar mapping verdict** (bones 2 and 3 are the chest and neck bodies: position relative to bones 1/4 on a lying body). Gate for Task 6: if the verdict is not "2 = chest, 3 = neck", stop and report.

---

### Task 4: F3 ragdoll write-safety envelope (pure)

**Files:**
- Create: `src/ragdoll_safety.hpp`
- Test: `tests/ragdoll_safety_check.cpp`; register after `native_rigid_body_check` (`build-msvc.cmd:386-389`)

**Interfaces:**
- Consumes: `Vec3`.
- Produces:
  ```cpp
  enum class ReleaseReason : unsigned {none,open_fist,lag,lifecycle,runaway,fault,state_changed,refused};
  enum class PostDecision {allow,already_dynamic,already_posted,retry_wait,ceiling,disabled,refused};
  class EventPostBudget {public:  // times in seconds, monotonic
      static constexpr double ceiling_per_second=2,retry_after=1;
      PostDecision request(std::uint32_t entity_id,bool primary_dynamic,double now);  // allow = the caller posts 0xb9 now
      void forget(std::uint32_t entity_id);          // entity gone or became dynamic: next wake starts fresh
      void disable(bool);                            // build/no-ragdoll-posts
      std::uint64_t posted() const;std::uint64_t suppressed() const;std::uint64_t refused() const;};
  struct RunawayVerdict {bool runaway{};double max_speed{};bool non_finite{};};
  class RunawayDetector {public:static constexpr double limit=12;
      static RunawayVerdict check(const std::array<std::optional<Vec3>,17>& velocities);};   // missing bodies are ignored
  class EntityLockout {public:static constexpr double seconds=1;
      void lock(std::uint32_t entity_id,double now);bool locked(std::uint32_t entity_id,double now) const;};
  class FaultLatch {public:static void trip() noexcept;static bool tripped() noexcept;static void reset_for_tests() noexcept;};
  struct KillSwitches {bool no_posts{},no_corpse_grab{};static KillSwitches read(const std::filesystem::path& build_dir);};
  ```
  `request` rules in order: disabled; `primary_dynamic` → `already_dynamic` (no post); first request for the entity → `allow`; a second within `retry_after` → `retry_wait`; the second at or after `retry_after` with the body still not dynamic → `allow` once; any later → `refused` (counted, the caller pulses the haptic and refuses the grab); `ceiling` when 2 posts were already allowed in the last second.

- [ ] **Step 1: Failing tests:** `once_per_wake`, `already_dynamic_never_posts`, `retry_after_1s_once_then_refused` (allow at 0, `retry_wait` at 0.99, allow at 1.0, `refused` at 5), `forget_restarts`, `ceiling_2_per_second` (3 entities at the same instant: allow, allow, `ceiling`; the third allowed at +1.0), `flapping_200_cycles_within_ceiling` (one entity, `forget` and re-request 200 times at 0.3 s spacing: `posted()` never exceeds 2 in any 1 s window) (Review Focus 3), `disabled_blocks_all`, `runaway_speed_12` (12.01 trips, 11.99 does not), `runaway_non_finite`, `runaway_ignores_missing_bodies`, `lockout_1s` (locked at 0.99, free at 1.0, other entity unaffected), `fault_latch_sticks`, `kill_switches_read` (temp dir with `no-ragdoll-posts`, with `no-corpse-grab`, with neither).
- [ ] **Step 2: Run, expect failure.** `... --only 'ragdoll_safety'`. Expected: compile error.
- [ ] **Step 3: Implement** with the rules above; fixed-size state (16 entities, oldest evicted), no allocation per call.
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/ragdoll_safety.hpp tests/ragdoll_safety_check.cpp tools/build-msvc.cmd
  git commit -m "feat: ragdoll write-safety envelope: post budget, runaway detector, lockout, fault latch (F3)"
  ```
- [ ] **Step 6: Mutation pass.** `12=>13`, `ceiling_per_second=2=>3`, `seconds=1=>.5`, `retry_after=1=>.5`: each fails.

---

### Task 5: F4 corpse diagnostics, status block and trace

**Files:**
- Create: `src/corpse_diagnostics.hpp`
- Modify: `src/dxgi_probe.cpp` (status `,"corpse":` next to `,"carry":` ~5250; `CorpseTraceWriter` start/stop beside `carry_trace` ~8061 and ~8260/8274/8667/8740; knob `build/bonelab-trace`)
- Test: `tests/corpse_diagnostics_check.cpp`; `tests/corpse_routing_check.py` (new, grows in Tasks 10-12); register both

**Interfaces:**
- Consumes: `ReleaseReason` (Task 4), `MassSource` (Task 3).
- Produces:
  ```cpp
  struct CorpseMotorSample {std::uint64_t tick{};unsigned side{};int bone{-1};double lag_m{},body_speed{},peak_all17{},mass_kg{},cap_fraction{},implied_force_n{};
      bool capped{},tow_written{},tow_saturated{};MassSource mass_source{};};
  class CorpseDiagnostics {public:  // counters are atomics; note_* from the dispatcher thread, json() from the status thread
      void note_bind(unsigned side,int bone,MassSource);void note_release(unsigned side,ReleaseReason);
      void note_motor(const CorpseMotorSample&);void note_mass_rejected();void note_post(bool allowed);
      void note_wake_attempt();void note_wake_refused();void note_near_player(unsigned side);void note_anchor_gap(unsigned side,double metres);
      void set_fault_latch(bool);void set_tuning(bool from_file,bool tow);
      std::string json() const;  // {"holds":[l,r],"binds":[..],"releases":{"open_fist":[..],"lag":[..],"lifecycle":[..],"runaway":[..],"fault":[..],"state_changed":[..]},"motor_samples":[..],"capped_writes":[..],"cap_fraction_mean":[..],"mass_source":"class","mass_verified":false,"mass_read_rejected":N,"lag_m_max":[..],"peak_body_speed":[..],"peak_bone_speed_all17":[..],"runaway":N,"tow_writes":N,"tow_disabled_saturated":N,"event_posts_b9":N,"event_posts_suppressed":N,"fault_latch":false,"wake_attempts":N,"wake_refused":N,"body_near_player":[..],"bones":{"limb":N,"collar":N},"drawn_anchor_gap_m":[..],"tuning":"default"|"file"}
      static std::string csv_header();static std::string csv_row(const CorpseMotorSample&);
      static constexpr std::size_t csv_max_rows=20000;};
  class CorpseTraceWriter {public:bool start(const std::filesystem::path&) noexcept;void push(const CorpseMotorSample&) noexcept;void stop() noexcept;};  // bounded ring of 1024, own thread, like CarryTraceWriter
  ```
  Bone histogram: bones 2 and 3 count as `collar`, all others `limb`. `holds` is binds minus releases (never negative).

- [ ] **Step 1: Failing tests:** `json_has_every_listed_key` (parse by substring for each key in the spec's "Diagnostics" list), `releases_split_by_reason_and_side`, `collar_versus_limb_histogram` (bones 2, 3 → collar; 7, 13 → limb), `cap_fraction_mean` (3 capped of 4 samples → 0.75), `peaks_keep_maximum`, `holds_never_negative`, `csv_row_field_count_matches_header`, `csv_capped_at_20000_rows` (writer to a temp file). Python guard: `"corpse":` precedes `"carry":` in the status line source; `corpse_trace.stop()` appears at every place `carry_trace.stop()` does.
- [ ] **Step 2: Run, expect failure.** `... --only 'corpse_diagnostics|corpse_routing'`.
- [ ] **Step 3: Implement** both headers' classes and the `dxgi_probe.cpp` wiring (zero-valued until later tasks note). The trace file is `native-corpse-trace.csv`, started only when `build/bonelab-trace` exists.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/corpse_diagnostics.hpp src/dxgi_probe.cpp tests/corpse_diagnostics_check.cpp tests/corpse_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: corpse diagnostics status block and bounded trace (F4)"
  ```

---

### Task 6: Collar target and the dead-or-unconscious gate (Part 1 behaviour decisions)

Needs the Task 3 probe verdict (bones 2 and 3 are chest and neck). Living guards are Tasks 14-16.

**Files:**
- Modify: `src/native_rigid_body.hpp:96-100` (add `ragdoll_hold_bone`), `:149-159` (`nearest_ragdoll_body` parameter), `src/native_grab.hpp:222-243` (discovery lambda and revalidate lambda), `:372-381` (yank path passes `allow_collar=false`)
- Test: `tests/native_rigid_body_check.cpp` (fixture `:72-108`)

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr bool ragdoll_hold_bone(int bone) {return ragdoll_grab_bone(bone) || bone==2 || bone==3;}   // limbs, chest, neck
  template<class Read> std::optional<NativeRigidBody> nearest_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,Vec3 hand,bool allow_collar=false);
  ```
  `ragdoll_grab_bone` is unchanged. In `focus_body`'s discovery lambda a guard becomes a candidate only when `ragdoll_pushable_state(read_, *entity)` (dead 0x08 or unconscious 0x20) and calls `nearest_ragdoll_body(..., allow_collar=true)`; an awake guard yields no candidate there.

- [ ] **Step 1: Failing tests:** `collar_bones_are_hold_targets` (`ragdoll_hold_bone` true for 2, 3, 6, 7, 9, 10, 12, 13, 15, 16; false for 0, 1, 4, 5, 8, 11, 14), `nearest_default_excludes_collar` (hand at the chest: result bone is a limb, as before), `nearest_with_collar_picks_chest_or_neck` (hand 2 cm from the chest body: bone 2; hand at the neck: bone 3; head 4 and pelvis 0 never), `awake_ragdolling_guard_not_a_candidate` (state byte 0x80 only: `ragdoll_pushable_state` false; discovery lambda returns none: test through the exposed predicate), `unconscious_guard_collar_ok` (state 0x20).
- [ ] **Step 2: Run, expect failure.** `... --only 'native_rigid_body|native_grab'`.
- [ ] **Step 3: Implement** (`ragdoll_grab_bone` untouched so `NativeWeaponHands::wake` classification does not change).
- [ ] **Step 4: Run, expect pass.** Same command, plus `--only 'native_grab_lifetime|native_grab_sequence'` (must stay green).
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_rigid_body.hpp src/native_grab.hpp tests/native_rigid_body_check.cpp
  git commit -m "feat: chest and neck are physical hold targets for dead or unconscious guards"
  ```

---

### Task 7: `PhysicalGrab` capped motor: corpse lag release, cooldown, grasp anchor for corpses and props (Bonelab §1.2-1.4, §1.7)

A snatch grab never knocks the object away: the bind stores the hand-to-limb offset at the contact point and starts with zero commanded jump. The hold stays dynamic and is driven by the capped step; a body the hand cannot bring along is let go of after a lag, with a short regrab cooldown.

**Files:**
- Modify: `src/physical_grab.hpp` (add after `motor`, `:122-132`; contact storage in `bind`, `:101-108`)
- Test: `tests/physical_grab_check.cpp` (extend)

**Interfaces:**
- Consumes: `cap_velocity_step`, `GrabTuning`, `bone_inertia` (Task 2), `ReleaseReason` (Task 4).
- Produces:
  ```cpp
  struct CorpseMotorInput {Vec3 body_velocity{},body_spin{};bool velocity_known{},corpse{true};   // corpse=false: a prop (cap only; 1.5 m release and throw on open fist as `motor()`)
      double object_kg{},inertia_kgm2{},share{1.},dt{};const GrabTuning* tuning{};};
  struct CorpseMotor {GrabMotor motor;double lag_m{};bool capped{};double cap_fraction{};double implied_force_n{};ReleaseReason reason{ReleaseReason::none};};
  // in PhysicalGrab:
  static constexpr double lag_release_m=.5,lag_release_s=.25,regrab_cooldown_s=.3;
  std::optional<CorpseMotor> corpse_motor(const WorldTrackedPose& object,bool usable,std::uint64_t now,std::uint64_t source_tick,const CorpseMotorInput&);
  Vec3 anchor(const WorldTrackedPose& object) const;     // the grasped point: object.position + object basis * contact_local_ (valid while held())
  ReleaseReason release_reason() const;                  // why the last release happened
  ```
  `bind(object, contact, max)` stores `contact_local_ = grab_local(object, contact − object.position)`; `bind(object)` stores zero. Corpse release returns `GrabMotor{{}, {}, true}` (no velocity); a prop release returns what `release(throw_velocity)` returns today. When `velocity_known` is false the capped step uses zero body velocity and `reason` stays none (the caller counts the fallback). A forced release (`lag`) blocks `bind` until `tick_ ≥ release tick + 300 ms`; an open-fist release does not. `motor()` (props, living guards) is unchanged.

- [ ] **Step 1: Failing tests** (extend `physical_grab_check.cpp`, `assert` style as the file):
  - `corpse_bind_no_jump`: bind at a contact 0.1 m from the object centre; first `corpse_motor` call with the hand unmoved gives zero linear and angular.
  - `corpse_motor_is_capped`: tuning `strength_scale=1`, hand moves 0.4 m in 0.1 s with a 10 kg limb: `capped`, `|linear − body_velocity|` ≤ `300*dt/12 + 1e-9` along the hand's forward axis; with the default scale 3 the bound is `900*dt/12`.
  - `release_writes_no_velocity`: open fist: `motor.release`, `linear == angular == 0`, reason `open_fist`.
  - `lag_release_after_quarter_second`: object held 0.6 m from the target for 240 ms: not released; at 260 ms: released, reason `lag`, zero velocity. An error of 0.49 m for 5 s never releases. A 0.6 m error that clears at 200 ms resets the timer (a second 200 ms excursion does not release).
  - `cooldown_blocks_regrab_flapping_grip` (Review Focus 3): after a `lag` release, press/release at 100 ms intervals: `bind` is false until 300 ms, true at 300 ms; after an `open_fist` release a bind is immediately possible.
  - `stuck_body_lag_release_no_write` (Review Focus 4 companion): object pinned 0.8 m away, `velocity_known=false`: released at 0.25 s with zero velocity.
  - `two_hands_split_mass`: `share=.5`, 20 kg: cap uses effective mass 12.
  - `anchor_tracks_object_rotation`: contact (0.1, 0, 0) in object frame; rotating the object 90 degrees about z moves `anchor` to (0, 0.1, 0) relative to the object origin.
  - `prop_mode_keeps_old_release`: `corpse=false`: a 1.6 m error releases (as `motor()`), a 0.8 m error for 5 s does not, open fist returns the throw velocity, and the step is capped. `motor()` itself is unchanged (existing asserts stay).
- [ ] **Step 2: Run, expect failure.** `... --only 'physical_grab_check'`.
- [ ] **Step 3: Implement.** Corpse target and base command are the existing `motor()` ones (`error*18 + feed`, clamp 10 m/s and 5 rad/s) before the cap; the cap's arm axis is the hand's forward basis row at `previous_` (`basis[0]`). The cap fraction is the share of the delta removed.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'native_grab'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/physical_grab.hpp tests/physical_grab_check.cpp
  git commit -m "feat: PhysicalGrab capped motor with corpse lag release, regrab cooldown and grasp anchor"
  ```
- [ ] **Step 6: Mutation pass.** `lag_release_m=.5=>.6`, `lag_release_s=.25=>.5`, `regrab_cooldown_s=.3=>.1`: each fails.

---

### Task 8: Pelvis tow planner behind a flag (corpse prep §5.4)

**Files:**
- Create: `src/corpse_tow.hpp`
- Modify: `src/native_grab.hpp:316-328` (replace the inline math by `plan_tow`; keep the write/fault handling)
- Test: `tests/corpse_tow_check.cpp`; register after `physical_grab_check`

**Interfaces:**
- Consumes: `cap_velocity_step`, `GrabTuning` (Task 2).
- Produces:
  ```cpp
  struct TowInput {Vec3 target{},pelvis{},pelvis_velocity{};double bind_reach{},pelvis_kg{},dt{};bool limb_saturated{};const GrabTuning* tuning{};};
  struct TowOutput {bool write{};Vec3 linear{};double bind_reach{};bool disabled_saturated{};};
  TowOutput plan_tow(const TowInput&);
  ```
  Rules: `tuning->tow` false → no write; `bind_reach ≤ 0` → set it to `clamp(reach, .4, 1.4)` and no write; `reach ≤ bind_reach + .05` → no write; else speed `min((reach − bind_reach)*6, 4)` horizontally (z = 0), then limited by `cap_velocity_step` against `pelvis_velocity` with `pelvis_kg`; `limb_saturated` → no write and `disabled_saturated`. (Today's constants are `native_grab.hpp:319-321`; the cap and flag are new.)

- [ ] **Step 1: Failing tests:** `off_by_default_no_write`, `first_call_sets_reach_clamped` (0.1 → 0.4, 3 → 1.4), `inside_deadband_no_write` (reach = bind + 0.05), `speed_6_per_metre_max_4` (reach − bind = 0.5 → 3; 2.0 → capped at 4 before the force cap), `horizontal_only` (target 1 m above: `linear.z == 0`), `mass_cap_binds` (pelvis_kg 12, dt 0.01: speed ≤ `300*0.01/14`), `saturated_limb_disables` (`disabled_saturated`, no write), `non_finite_input_no_write`.
- [ ] **Step 2: Run, expect failure.** `... --only 'corpse_tow'`.
- [ ] **Step 3: Implement.** Default `GrabTuning::tow=false` stays until Task 9 records the limb-only versus limb-plus-tow comparison; if the lab prefers tow, Task 9 flips the default and this test's first case.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'native_grab_compile'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/corpse_tow.hpp src/native_grab.hpp tests/corpse_tow_check.cpp tools/build-msvc.cmd
  git commit -m "feat: capped, horizontal, flagged pelvis tow planner"
  ```

---

### Task 9: Corpse-drag lab (Tier A) and tuning record

Judged simulation of a 17-body chain driven by the real `PhysicalGrab::corpse_motor`, `cap_velocity_step` and `plan_tow`, with smooth per-frame input at 90 Hz (30 ms stepped input is an info variant only; phase 1 learned it misleads).

**Files:**
- Create: `tools/corpse_drag_lab.cpp`
- Modify: `tools/build-msvc.cmd` (after the `push_lab` lines ~797-800: compile with `/Isrc`, run `build\corpse_drag_lab.exe build\corpse-lab`, exit 1 on any judged failure)
- Create (git-excluded): `.superpowers/sdd/2026-10-20-bonelab-feel-foundations-corpse/corpse-lab-tuning.md`

**Interfaces:**
- Consumes: Tasks 2, 7, 8. Produces: `build\corpse_drag_lab.exe <out_dir>` writing `scenarios.json` and one CSV per scenario; exit 1 when a judged scenario fails.
- Lab model: bodies are point masses with `ragdoll_class_kg`, joints are max-separation constraints (limb lengths from the 17-bone radius table) with damping, a floor at z = 0 with friction, optional wall. The model's honesty limit (no Havok solver) is stated in the JSON header.

- [ ] **Step 1: Write the lab with these judged scenarios (starting limits; tune once, record every change):**
  - `drag_3m`: wrist grab, hand moves 3 m along the floor at 1.2 m/s; the pelvis is within 0.5 s of the hand's stop within 0.15 m of rest; no body above 6 m/s.
  - `lift_half_metre`: ankle grab, lift 0.5 m; the force implied by the commanded Δv (`implied_force_n`) never exceeds the (scaled) cap by more than 1%.
  - `forgiving_one_hand_lift` (user decision 2): a 75 kg body hangs from one ankle (lift fraction applied) and the hand lifts it 0.5 m within 1.0 s with no lag release and no body above 6 m/s; with `strength_scale=1` the same scenario takes longer than 1.0 s (the judged contrast that makes the scale matter).
  - `forgiving_swing`: the hand swings the hanging body at 3 m/s for 2 s: no lag release, peak over all 17 bodies under 12 m/s.
  - `flick_4ms`: 4 m/s flick; peak speed over all 17 bodies stays under 12 m/s (the runaway limit).
  - `release_at_rest`: opening the fist at rest writes no velocity (zero writes counted).
  - `runaway_seeded`: a seeded 13 m/s spike on one body: `RunawayDetector` trips, the hold ends, no counter-write is issued, the entity is locked for 1 s.
  - `two_hands_two_limbs`: both hands lift different limbs: the combined implied force ≤ sum of the two caps (+1%).
  - `stuck_on_wall`: body blocked by a wall while the hand continues: released by lag at 0.25 s after the error passes 0.5 m, zero velocity written.
  - `hitch`: one 0.5 s frame during a drag: no body above 6 m/s after it (Review Focus 4).
  - `collar_lift`: chest grab lift; same cap check.
  - info: `tow_on_off` (limb-only versus limb + tow: drag distance, peak speed, joint stretch), `stepped_30ms` variants of `drag_3m`, `lift_half_metre`.
- [ ] **Step 2: Run, expect failure** (before tuning). `python3 tools/linux/run_build_cmd.py --keep-going --only 'corpse_drag_lab' candidate d2vr_render_probe_arm_v601.dll`. Expected: compile ok, at least one judged scenario fails on the first run; list which.
- [ ] **Step 3: Tune within the spec's ranges** (lift fraction, mass floor/ceiling, torque 25-30), one change at a time, each recorded with its before/after scenario numbers in `corpse-lab-tuning.md`. Decide the tow default from `tow_on_off` (record why); if on, flip `GrabTuning::tow` and Task 8's `off_by_default_no_write` in the same commit. Update the defaults in `grab_force_cap.hpp` and the pinned numbers in `tests/grab_force_cap_check.cpp` / `tests/grab_mass_check.cpp` that change.
- [ ] **Step 4: Run, expect pass.** Same command: `corpse_drag_lab: N scenarios, 0 failed`; plus `--only 'grab_force_cap|grab_mass|corpse_tow'`.
- [ ] **Step 5: Mutation pass.** `300=>450` axial, `12=>16` runaway limit, drop the lag rule, drop the dt clamp: each makes at least one judged scenario fail. Record kills.
- [ ] **Step 6: Commit.**
  ```bash
  git add tools/corpse_drag_lab.cpp tools/build-msvc.cmd src/grab_force_cap.hpp src/grab_mass.hpp tests/grab_force_cap_check.cpp tests/grab_mass_check.cpp
  git commit -m "test: corpse-drag lab with judged scenarios and mutation checks; record tuned defaults"
  ```

---

### Task 10: `NativeGrab` corpse hold through the envelope (Part 1 engine integration)

The risky task: every corpse write is validated, capped, read back and bounded. Use a `DeepSeek` second-opinion review (concurrency, SEH, lifecycle).

**Files:**
- Modify: `src/native_grab.hpp`: `Lane` (`:158-170`) gains `corpse{}`, `mass{}`, `bind_pelvis_z{}`, `last_capped{}`; discovery gate (Task 6); `update_lane` held branch (`:341-371`); `tow` (`:316-328`); bind sites (`:372-381`, `:400-406`); accessors near `holding`/`held` (`:508-515`); `release_all_locked` (`:261-264`); public `configure_corpse`
- Modify: `src/dxgi_probe.cpp`: knobs near `:8135` (read `build/no-corpse-grab`, `build/no-ragdoll-posts`, `build/grab-tuning.cfg`; log each as the neighbouring knobs do; call `native_grab.configure_corpse`), status `,"corpse":` (Task 5) now fed
- Test: `tests/corpse_hold_check.cpp` (new; ragdoll + motion-array fixture in the style of `tests/native_grab_lifetime_check.cpp:1-120` and `native_rigid_body_check.cpp:72-108`; `NativeGrabCheck` friend gets the new setters); extend `tests/corpse_routing_check.py`

**Interfaces:**
- Consumes: Tasks 2-8.
- Produces:
  ```cpp
  struct CorpseOptions {bool enabled=true;GrabTuning tuning{};};
  void NativeGrab::configure_corpse(const CorpseOptions&);          // before the first update
  CorpseDiagnostics& NativeGrab::corpse_diagnostics();
  struct HoldAnchorView {bool active{};Vec3 anchor{};std::uint64_t tick{};};
  HoldAnchorView NativeGrab::hold_anchor(unsigned side);         // render thread, mutex-protected copy of the last dispatcher tick
  ```
  Behaviour: a lane holding a ragdoll body (`bone ≥ 0`) whose entity passes `ragdoll_pushable_state` is `corpse`; a legacy-yanked living guard is not (old `motor()` unchanged). A lane holding a prop (`bone < 0`) runs the same pipeline with `corpse=false`: mass from `prop_body_mass` (bounds from the body), the force cap, the old 1.5 m release and the throw on open fist, no 17-body runaway check (one body), the `FaultLatch` still guarding the write. `hold_anchor` is active for both kinds. Per motor tick, in order: `FaultLatch::tripped()` or `EntityLockout::locked` → release (reason `fault` / `runaway`), no write; state byte `+0xd1` lost `0x28` → release `state_changed`; read all 17 bodies' velocity via `native_motion` (missing ones skipped) → `RunawayDetector::check` → on trip release `runaway`, `EntityLockout::lock`, **no write**; held-bone `native_motion` read failure → `velocity_known=false` (counted) and, if the read returned NaN/huge, drop the hold without a write; mass from `ragdoll_body_mass(bone, raw half, tuning.use_measured_mass)` with `hanging_mass(bone, pelvis_z − bind_pelvis_z)` added; `corpse_motor`; skip the write on `motor.release`; write under `CrashOperation` + `write_velocity`; an SEH fault trips `FaultLatch` (then the existing `release_all_locked`); tow via `plan_tow` with `limb_saturated = last_capped`. Existing lifecycle branches (world mismatch, `!same_body`, `!physical_usable`) release with reason `lifecycle`. `body_near_player`: horizontal distance from the player entity (`registry[0]`, +0x300) under 0.4 m and |dz| under 1.0 m. `no-corpse-grab` makes corpse bodies non-candidates.

- [ ] **Step 1: Failing tests** in `corpse_hold_check.cpp`:
  - `dead_guard_wrist_binds_and_write_is_capped`: dead guard (state 0x08), 10 kg limb at rest, hand moves 0.4 m in 100 ms: one velocity write; |Δv| ≤ cap + 1%.
  - `collar_binds_dead_not_awake`: bone 2 binds a state-0x20 guard; an awake ragdolling guard (0x80) is not a candidate.
  - `open_fist_writes_nothing`: release writes zero velocity writes for the corpse lane (deviation 8), `releases.open_fist` 1; a prop lane still throws (regression).
  - `state_changed_releases_no_write`: `+0xd1` 0x20 → 0x00 mid-hold: release, reason `state_changed`, `fixture_writes` unchanged.
  - `state_change_and_open_fist_same_tick` (Review Focus 2): both on one tick: exactly one release counted, no write.
  - `world_change_during_lag_release` (Review Focus 2): the lag timer expires on the tick the world pointer changes: one release, reason `lifecycle`, no write, no fault.
  - `runaway_seeded_releases_no_counter_write`: another bone's velocity 13 m/s: release, `runaway` 1, no write on that tick or for the next 1 s (the held entity), writes resume for a different entity.
  - `fault_latch_blocks_further_ragdoll_writes`: first write faults (fixture): `FaultLatch::tripped()`, status flag set; a later grab on another guard issues no ragdoll write; prop writes still work.
  - `readback_garbage_drops_hold_without_write` (Review Focus 1): NaN, 1e9 and a recycled handle in the motion array each end the hold with no write and no crash.
  - `kill_switch_disables_corpse_candidates`.
  - `two_hands_two_limbs_independent`: both lanes hold different bones of one entity; each write ≤ its own cap; combined ≤ sum.
  - `prop_hold_capped_old_release`: a 2 kg prop lane (default tuning) writes the pre-change values when the cap does not bind (golden numbers from the current code for a slow hand), writes the capped value when it does (12 kg prop, fast hand), still releases beyond 1.5 m and still throws on open fist.
- [ ] **Step 2: Run, expect failure.** `... --only 'corpse_hold|corpse_routing|native_grab'`.
- [ ] **Step 3: Implement** per Behaviour above. No new engine call; the event posts are Task 11. Fill `CorpseDiagnostics` (`note_bind/release/motor`, `set_fault_latch`, `note_mass_rejected`, `note_near_player`). Python guard: the new `Lane` writes go through `write(` only, `FaultLatch::trip` appears in the fault branch, `EntityLockout` consulted before `corpse_motor`.
- [ ] **Step 4: Run, expect pass.** Same command; then the full harness `python3 tools/linux/run_build_cmd.py`: 0 failed, 41 skipped; record the passed count.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_grab.hpp src/dxgi_probe.cpp tests/corpse_hold_check.cpp tests/corpse_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: corpse hold through the write-safety envelope: capped motor, readback, runaway, lifecycle, fault latch"
  ```

---

### Task 11: Budgeted wake, one retry, refusal haptic (F3 event posts)

**Files:**
- Modify: `src/native_weapon_hands.hpp:378-432` (`wake`, a retry service beside `hold_down` called at `:168`, `post` kill-switch), `src/dxgi_probe.cpp` (`d2vr::NativeWeaponHands` refusal read in the input path near `:6782`, `game_xr->pulse(side,.6f,.08f)`)
- Test: `tests/corpse_hold_check.cpp` (post cases use a stubbed `post` seam) or a new `tests/wake_budget_check.cpp` if the stub does not fit; extend `tests/corpse_routing_check.py`

**Interfaces:**
- Consumes: `EventPostBudget`, `KillSwitches` (Task 4), `CorpseDiagnostics` (Task 5).
- Produces:
  ```cpp
  // NativeWeaponHands
  inline static EventPostBudget post_budget;                          // process-wide, fed by dxgi_probe knobs
  static bool take_wake_refusal(unsigned side) noexcept;              // input thread: true once per refusal
  template<class Read> static void service_wake_retries(std::uintptr_t base,Read& read,std::uint64_t now_ms);
  ```
  `wake` calls `post_budget.request(entity_id, primary_dynamic=false, now)`: `allow` → post 0xb9 once (as today) and remember the entity for the retry; `retry_wait` / `already_posted` → no post; `refused` → `wake_refused` counter, no `yanked`, side flagged for the haptic; `disabled` → no post. `service_wake_retries` runs from the same spot as `hold_down`: for a remembered entity whose primary body still does not read dynamic after 1 s, request again (the second allow is the one retry). `hold_down` and `ragdoll` (the legacy living-guard yank, Task 16 gates it behind `build/legacy-yank`) post only when `post_budget` is not disabled (no ceiling, deviation 4). No `0x149` is added.

- [ ] **Step 1: Failing tests:** `wake_posts_once_per_squeeze`, `already_dynamic_corpse_posts_nothing`, `retry_after_one_second_then_refused_with_flag` (fixture body never turns dynamic: posts at 0 and 1.0 s, nothing after, `take_wake_refusal` true once then false), `kill_switch_blocks_wake_and_legacy_yank_posts`, `corpse_never_reposted_on_400ms_timer` (`hold_down` leaves a state-0x08 entity alone, as today), `ceiling_two_posts_per_second_across_entities`.
- [ ] **Step 2: Run, expect failure.** `... --only 'corpse_hold|wake_budget|corpse_routing'`.
- [ ] **Step 3: Implement.** Count `event_posts_b9` / `event_posts_suppressed` / `wake_attempts` / `wake_refused` in `CorpseDiagnostics` from `wake`.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_weapon_hands.hpp src/dxgi_probe.cpp tests/corpse_hold_check.cpp tests/corpse_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: budget START_RAGDOLL posts: once per wake, one retry, refusal haptic, kill-switch"
  ```

---

### Task 12: Drawn hand on the grasp point of corpses and props (G3, user decision 6; Bonelab §1.3 pose blend)

The raw controller keeps driving intent. For every motor-held body (corpse limb, collar, physical-grab prop) the drawn hand and arm IK target move to the grasped point over 0.25 s, never farther than 0.35 m from the raw controller; beyond that the hand shows the controller (the lag release then ends a corpse hold). `build/no-prop-grasp-point` restores today's drawing for props (corpses always anchor). Native HoldObject items are palm-seated by the grip fit already and are untouched.

**Files:**
- Create: `src/drawn_anchor.hpp`
- Modify: `src/dxgi_probe.cpp` (after the clamp block, before `publish_hand(side,...)` at `:4453`: replace `hands.grips[side].position` from `DrawnAnchor`; `clamp_yields` already yields for `native_grab.holding(side)`, `:4272`), `tests/corpse_routing_check.py`
- Test: `tests/drawn_anchor_check.cpp`; register after `physical_grab_check`

**Interfaces:**
- Consumes: `NativeGrab::hold_anchor` (Task 10, active for corpse and prop lanes).
- Produces:
  ```cpp
  class DrawnAnchor {public:
      static constexpr double blend_seconds=.25,bound_m=.35;
      struct Result {Vec3 position{};double weight{};bool bounded{};double gap_m{};};
      Result update(unsigned side,bool active,Vec3 raw,Vec3 anchor,double now);   // seconds, monotonic; position == raw when weight 0
      void reset(unsigned side);
  };
  ```
  Weight rises linearly 0 → 1 over 0.25 s while `active` and falls over 0.25 s when not (smoothstep applied to it); the result is `raw + (anchor − raw)·weight` with the offset's length limited to 0.35 m (`bounded`); non-finite `raw` / `anchor` or a time step above 0.25 s resets to weight 0; `gap_m` is `|result − anchor|` (fed to `note_anchor_gap`).

- [ ] **Step 1: Failing tests:** `blend_takes_quarter_second` (weight 0 at t0, 0.5 at +0.125 s within smoothstep 0.5, 1 at +0.25 s), `bound_035` (anchor 1 m away: result is 0.35 m from raw, `bounded`), `release_blends_out`, `weight_zero_is_exactly_raw`, `non_finite_resets`, `hitch_resets`, `sides_independent`, `no_teleport_on_bind` (the first active frame's result equals raw ±1e-12). Python guard in `corpse_routing_check.py`: the override sits after `hand_clamp.update(`/`note_drawn(` and before `publish_hand(side,`, reads only `hold_anchor`, never touches `raw_grips`, and `no-prop-grasp-point` is read in the knob block.
- [ ] **Step 2: Run, expect failure.** `... --only 'drawn_anchor|corpse_routing'`.
- [ ] **Step 3: Implement** both. `active` is false for native holds, for living guards, and for prop lanes when `build/no-prop-grasp-point` exists (read at start, logged, status `"prop_grasp_point":true|false`). Status gains `prop_seat_gap_m` (max `gap_m` over prop holds) for the headset.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'hand_clamp_routing|dxgi_probe'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/drawn_anchor.hpp src/dxgi_probe.cpp tests/drawn_anchor_check.cpp tests/corpse_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: draw the hand on the grasp point of corpses and props with a 0.25 s blend, bounded to 0.35 m"
  ```
- [ ] **Step 6: Mutation pass.** `blend_seconds=.25=>.1`, `bound_m=.35=>.5`: each fails.

---

### Task 13: Part 1 Tier B scenarios, run checker and Tier C checklist

**Files:**
- Create: `tools/linux/scenario_lib.py` (segment helpers moved from `oil_tank_drop.py`: `glide`, `hold`, `lead_in`, two-hand variants; `oil_tank_drop.py` keeps its behaviour and imports it), `tools/linux/scenarios/corpse_drag.py` (S1), `corpse_lift.py` (S2), `corpse_collar.py` (S3), `corpse_two_hands.py` (S4), `corpse_stress.py` (S5: 50 cycles over 5 bodies if present, else on one), `corpse_reload.py` (S6), `corpse_prompt.py` (S7), `corpse_probe.py` (reach + hold for the F1 table), `tools/linux/bonelab_run_check.py`, `docs/VR-bonelab-feel-corpse-TEST-CHECKLIST.md`
- Test: `tests/bonelab_scenarios_check.py` (register in `build-msvc.cmd`)

**Interfaces:**
- Produces: each scenario's `build(lead_ms) -> list[dict]` (script v6; menu A-pulses via `scenario_lib.lead_in`); `python3 tools/linux/bonelab_run_check.py RUN_DIR --scenario NAME` reads `RUN_DIR/artifacts/xr-live-status.json` and prints PASS/FAIL per assertion. Assertions: all scenarios `corpse.fault_latch == false`, `corpse.runaway == 0`, `peak_bone_speed_all17 < 12`, `event_posts_b9` within `2 × wake_attempts`, `physical_grab_lost == 0`; S1: `binds ≥ 1`, `releases.open_fist ≥ 1`, `lag_m_max` recorded; S3: `bones.collar ≥ 1`; S4: both `holds`/`binds` sides ≥ 1; S5: summed over sessions `binds ≥ 40` per session; S6: `releases.lifecycle ≥ 1`, no fault; S7: records `releases`/`binds` and `carries` to say which action won (informational line).

- [ ] **Step 1: Failing test** `bonelab_scenarios_check.py`: every `corpse_*` scenario loads through `linux/d2vr_linux/sim.py` `scenario_segments(name, 60000)` and `compile_script(segments, 120)` succeeds; script length for each ≤ 120 − lead; `corpse_stress` has ≥ 50 grab/open pairs spaced ≥ 0.4 s apart (past the 0.3 s cooldown); `bonelab_run_check` returns FAIL on a fixture status with `fault_latch:true` and PASS on a clean fixture (fixtures written to a temp dir).
- [ ] **Step 2: Run, expect failure.** `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 3: Implement** the scenarios (hand poses from `oil_tank_drop.py` constants: `REACH`, `CHEST`, `FLOOR`; drag = glide 3 m using `move_y` plus hand follow; lift = ankle height glide to 0.5 m; S6 stops the script at a point where the user's save-load hotkey cannot be pressed, so S6 reloads through the pause menu with `when: "always"` A-presses; if that cannot be scripted, S6 is Tier C item 7 and the ledger says so), the run checker, and the Tier C doc: the spec's six items in order (item 2 reads "lift a whole body with one hand and swing it: forgiving, nothing flies off") plus 7 (unconscious guard wakes mid-hold, `state_changed` counter) and 8 (every prop: bottle, crate, book: the visible hand sits on the object's grasp point; then `build/no-prop-grasp-point` restores the old drawing; save `prop_seat_gap_m`), each with the `corpse` fields to save, "walk the body around for two minutes" under item 2, and a first section "Tuning session": run drag/lift, then edit `build/grab-tuning.cfg` (`axial_n`, `lateral_n`, `lift_fraction`, `tow`) and note the values the user prefers.
- [ ] **Step 4: Run, expect pass.** `python3 tests/bonelab_scenarios_check.py` and `python3 tools/linux/run_build_cmd.py --keep-going --only 'bonelab_scenarios' candidate d2vr_render_probe_arm_v601.dll`.
- [ ] **Step 5: Tier B runs (skip with a ledger "outstanding" line for any missing save).** `linux/d2vr-linux sim --test-save ~/d2vr-test-saves/corpse-dead --scenario corpse_drag --lead 45 --status-field corpse`, then `python3 tools/linux/bonelab_run_check.py ~/d2vr-test-builds/sim-runs/<newest> --scenario corpse_drag`. Repeat S2-S7; S5 four times (50 cycles each) and sum. Copy each check's output to the ledger. The Task 3 probe run uses `corpse_probe` if it was not done earlier.
- [ ] **Step 6: Commit.**
  ```bash
  git add tools/linux/scenario_lib.py tools/linux/scenarios tools/linux/bonelab_run_check.py tests/bonelab_scenarios_check.py docs/VR-bonelab-feel-corpse-TEST-CHECKLIST.md tools/build-msvc.cmd
  git commit -m "test: corpse Tier B scenarios, run checker and the Part 1 headset checklist"
  ```

---

### Task 14: Living-guard pull and stagger: RE and probe (1L; decides the levers)

No production behaviour change. The existing living-guard path ragdolls him with `NativeWeaponHands::shove` + `ragdoll` (`START_RAGDOLL`) (`native_weapon_hands.hpp:350-368`); the user wants a pull and stagger with the guard alerted and not ragdolled. Which engine levers give a stagger and a pull is unknown, so they are measured first. Use a DeepSeek second-opinion review on the findings.

**Files:**
- Create: `src/guard_probe.hpp`, `tools/linux/scenarios/guard_probe.py`, `docs/BONELAB-FEEL-GUARD-PULL-RE.md`
- Modify: `src/native_weapon_hands.hpp` (probe hook beside `yank`, dev knob only), `tools/build-msvc.cmd`
- Test: `tests/guard_probe_check.cpp`

**Interfaces:**
- Consumes: `NativeWeaponHands::shove` / `guard_at` / `living` (`native_weapon_hands.hpp:319-368,440`), `NativeMelee::unaware` (`native_melee.hpp:139-148`, RVA 0x5bf560, read-only engine call under SEH), teleport `0x87d1e0` (`native_weapon_hands.hpp:533`).
- Produces (read-only, dev knob `build/guard-probe`, off by default):
  ```cpp
  struct GuardSample {std::uint64_t tick{};std::uint32_t entity_id{};Vec3 position{};unsigned char state_d1{};bool unaware{};bool bodies_dynamic{};bool read_ok{};};
  std::string guard_sample_csv_header();std::string guard_sample_csv_row(const GuardSample&);
  enum class GuardExperiment {none,shove,shove_decl,teleport_step};
  struct GuardProbePlan {GuardExperiment experiment{};std::string decl;double step_m{};unsigned steps{};};
  std::optional<GuardProbePlan> parse_guard_probe(const std::string& file_text);   // "shove" | "shove_decl <name>" | "teleport_step <metres> <count>"
  template<class Read> GuardSample sample_guard(Read& read,std::uintptr_t base,std::uintptr_t npc,std::uint64_t tick);
  ```
  `bodies_dynamic` is true when bone 0's body `flags & 7 == 2` (ragdolled). The experiment fires once on the next full-fist squeeze at a living guard (the existing `guard_at` selection), samples every 50 ms for 6 s into `artifacts/guard-probe.csv`, and the file is deleted after use so a run does one experiment. `teleport_step` moves him in `count` steps of `step_m` toward the hand with `teleport` (finite checks as the sword copy's), horizontal only.

- [ ] **Step 1: Failing tests** (`guard_probe_check.cpp`): `parse_accepts_three_forms`, `parse_rejects_garbage_and_bounds` (step above 0.5 m or count above 20 rejected; empty or unknown word rejected), `csv_row_field_count_matches_header`, `sample_garbage_read_is_marked_not_ok` (fixture with an unreadable model pointer: `read_ok` false, no crash), `sample_flags_ragdoll_bodies` (bone 0 flags `&7==2` gives `bodies_dynamic`).
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --keep-going --only 'guard_probe' candidate d2vr_render_probe_arm_v601.dll`.
- [ ] **Step 3: Implement** the header, the knob read, and the one-shot hook.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe|native_grab_compile'`.
- [ ] **Step 5: Commit** the probe.
  ```bash
  git add src/guard_probe.hpp src/native_weapon_hands.hpp tools/linux/scenarios/guard_probe.py tests/guard_probe_check.cpp tools/build-msvc.cmd
  git commit -m "feat: one-shot living-guard lever probe (dev knob, read-only samples)"
  ```
- [ ] **Step 6: Static scan (evidence).** List the damage decl names in the pinned exe (`strings -a Dishonored2.exe | grep 'components/base/damage/'`) and note every name containing shove, push, stun, knock, stagger or hit-react; for each candidate record where the decl is consumed if the existing Ghidra scripts (`tools/D2StaticResearch.java`, `D2TakedownRefs.java`) find it quickly. This is a time-boxed hour; unresolved is a valid answer.
- [ ] **Step 7: Runs (Tier B; needs `guard-awake`; otherwise mark outstanding and stop at Task 15 with the planner only).** One session per experiment: `shove` (today's shove alone, no ragdoll post), `shove_decl <each candidate from Step 6>`, `teleport_step 0.1 5`. Record in `docs/BONELAB-FEEL-GUARD-PULL-RE.md` per experiment: displacement of the guard (toward or away from the hand), whether `unaware` flipped to false (alerted), whether `bodies_dynamic` stayed false (no ragdoll), his state byte, whether he snapped back or kept the teleported position after 2 s, and any crash. End with a **lever decision table**: `stagger` = which decl (or "shove default"), `alert` = confirmed by `unaware==false` within how long, `pull` = `teleport_step` / `none`. If `pull` is "none", say so: Task 15 then ships stagger and alert only.
- [ ] **Step 8: Commit the evidence.**
  ```bash
  git add docs/BONELAB-FEEL-GUARD-PULL-RE.md
  git commit -m "docs: living-guard stagger, alert and pull lever measurements"
  ```

---

### Task 15: `GuardPull` planner (1L policy, pure)

**Files:**
- Create: `src/guard_pull.hpp`
- Test: `tests/guard_pull_check.cpp` (extend the Task 14 file or a new one; register after `drawn_anchor_check`)

**Interfaces:**
- Consumes: Task 14's decision table (constants below are its starting values; replace with the measured ones in this task's commit).
- Produces:
  ```cpp
  struct GuardPullConfig {double reach=.5,hold_distance=.45,max_speed=2.5,max_accel=12,release_gap=1.5,gap_seconds=.25,
      max_hold_seconds=3,regrab_cooldown=1.;};
  enum class PullLever {none,teleport_step};
  struct GuardPullInput {double now{};bool fist{};bool guard_valid{};bool guard_living{};bool fleeing{};bool authored_animation{};
      Vec3 hand{},guard{};std::optional<ClampHit> blocked_by;};   // blocked_by: last frame's world cast along the planned step
  enum class PullAction {none,stagger,pull,release};
  struct GuardPullOutput {PullAction action{};Vec3 step{};ReleaseReason reason{ReleaseReason::none};bool blocked{};};
  class GuardPull {public:explicit GuardPull(GuardPullConfig={},PullLever=PullLever::none);
      GuardPullOutput update(unsigned side,const GuardPullInput&);
      void reset(unsigned side);
      std::uint64_t staggers(unsigned side) const;std::uint64_t pulls(unsigned side) const;std::uint64_t blocked(unsigned side) const;};
  ```
  Rules: a fresh `fist` with a valid living, non-fleeing guard within `reach` (horizontal) and no cooldown → one `stagger` (this is the shove that alerts him) and the hold starts; while held, each update returns `pull` with a horizontal `step` toward the hand (speed ramp limited by `max_accel`, at most `max_speed`, zero inside `hold_distance`) only when the lever is not `none`; a `blocked_by` hit zeroes the step and counts `blocked`; `release` with reason `open_fist` / `state_changed` (guard no longer living) / `lifecycle` (`guard_valid` false) / `lag` (guard farther than `release_gap` from the hand for `gap_seconds`) / `lifecycle` for `authored_animation`; `max_hold_seconds` ends the hold (`lag`); after any release, `regrab_cooldown` before a new stagger. The step is never non-finite and never has a z part.

- [ ] **Step 1: Failing tests:** `fresh_fist_staggers_once` (one `stagger`, then `pull`s; holding the fist 10 s adds no second stagger), `no_pull_when_lever_none` (stagger and alert still happen, `step` zero), `pull_speed_and_accel_limits` (speed ≤ 2.5, Δspeed per second ≤ 12, over a 5 m approach), `stops_inside_hold_distance`, `pull_blocked_by_world_stops` (Review Focus 5: a `blocked_by` hit gives zero step and `blocked` counted; clears when the hit is gone), `fleeing_guard_not_pulled` (no stagger, no pull), `state_change_ends_pull` (guard dies or is knocked out mid-pull: `release` `state_changed`, no step), `authored_animation_releases`, `open_fist_releases_then_cooldown_1s` (fist again at 0.9 s: nothing; at 1.0 s: stagger), `gap_release_after_quarter_second`, `max_hold_3s`, `non_finite_input_releases`, `sides_independent`, `flapping_fist_200_cycles_staggers_at_most_once_per_second` (Review Focus 3 companion).
- [ ] **Step 2: Run, expect failure.** `... --only 'guard_pull_check'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/guard_pull.hpp tests/guard_pull_check.cpp tools/build-msvc.cmd
  git commit -m "feat: GuardPull planner: stagger once, capped horizontal pull, release rules"
  ```
- [ ] **Step 6: Mutation pass.** `max_speed=2.5=>5`, `regrab_cooldown=1.=>.2`, `gap_seconds=.25=>.5`, drop the `fleeing` check: each fails.

---

### Task 16: Wire the living-guard pull into the hands, retire the ragdoll yank (1L engine integration)

**Files:**
- Modify: `src/native_weapon_hands.hpp:350-368` (`yank` becomes `legacy_yank`; new `pull_guard`), `:162` (op dispatch), `src/dxgi_probe.cpp` (squeeze routing near `:6782`, knobs `build/no-guard-pull` and `build/legacy-yank`, status `,"guard_pull":`)
- Create: `tools/linux/scenarios/guard_pull.py` (S8), `docs/VR-bonelab-feel-guard-pull-TEST-CHECKLIST.md`
- Test: `tests/guard_pull_routing_check.py` (new); extend `tests/bonelab_scenarios_check.py` and `tools/linux/bonelab_run_check.py` (S8 assertions)

**Interfaces:**
- Consumes: `GuardPull` (Task 15), the stagger (`shove`) and pull (`teleport`) levers per the Task 14 table, `CorpseDiagnostics` (Task 5), `NativeMelee::unaware`.
- Produces:
  ```cpp
  // NativeWeaponHands
  template<class Read> static bool pull_guard(std::uintptr_t base,Read& read,const Request& r);   // Op::yank now routes here unless legacy_yank
  inline static std::atomic<bool> legacy_yank{false},guard_pull_enabled{true};
  inline static std::atomic<std::uint64_t> guard_staggers{},guard_pulls{},guard_pull_blocked{},guard_alerted{},guard_released{};
  ```
  Status `guard_pull`: `staggers`, `pulls`, `blocked`, `alerted` (counted when `unaware` reads false within 1 s of the stagger), `releases` by reason, `lever`, `legacy_yank`, `ragdoll_posts_for_living` (must stay 0). Behaviour: the squeeze selects the guard as today (`guard_at` / `grabbed_`), `GuardPull::update` runs every game-thread tick the hand holds; `stagger` calls the existing `shove` once (no `ragdoll`); `pull` moves him with `teleport` by `step` after the finite and live-entity checks used for the sword copy, and a forward cast along the step (clamp slot) feeds `blocked_by`; `NativeGrab::yanked` is not called for living guards, so no limb is bound. Lifecycle (world change, load, death reload, focus loss) resets the planner. With `build/legacy-yank` the old yank runs exactly as before. `build/no-guard-pull` makes an awake guard stop the hand and do nothing else (the phase-1 behaviour).

- [ ] **Step 1: Failing tests** (`guard_pull_routing_check.py`, source-only like `hand_clamp_routing_check.py`): `pull_guard` never calls `ragdoll(` or `post(base,read,0xb9` or `0x149`; `yank` is reachable only when `legacy_yank` is set; `shove(` appears in `pull_guard` exactly once, guarded by the stagger action; `teleport` calls sit behind the live-entity check; the status line contains `"guard_pull":` and `ragdoll_posts_for_living`; both knob files are read in the knob block. Scenario test: `guard_pull` loads and compiles; run-check fixture for S8 FAILs on `ragdoll_posts_for_living:1`, PASSes on a clean fixture with `staggers ≥ 1`, `alerted ≥ 1`, `releases` recorded.
- [ ] **Step 2: Run, expect failure.** `... --only 'guard_pull_routing|bonelab_scenarios'`.
- [ ] **Step 3: Implement.** The `pull` lever constant comes from the Task 14 table (`PullLever::none` ships when the evidence says no pull lever works; the status says `lever:"none"`).
- [ ] **Step 4: Run, expect pass.** Same command plus the full harness: 0 failed, 41 skipped.
- [ ] **Step 5: Tier B (`guard-awake`; else outstanding).** S8: `guard_pull` scenario (reach, full fist on his upper body, hold 2 s with a slow pull away, open), `bonelab_run_check.py RUN --scenario guard_pull`; record whether the game logged any crash and `ragdoll_posts_for_living`.
- [ ] **Step 6: Tier C doc** `docs/VR-bonelab-feel-guard-pull-TEST-CHECKLIST.md`: grab an unaware guard (he is pulled and staggers, turns alert, does not fall into ragdoll); grab a fleeing guard (nothing); pull one into a wall (stops); let go and re-grab within a second (no second stagger); `build/legacy-yank` for comparison. Save `guard_pull`.
- [ ] **Step 7: Commit.**
  ```bash
  git add src/native_weapon_hands.hpp src/dxgi_probe.cpp tools/linux/scenarios/guard_pull.py tools/linux/bonelab_run_check.py tests/guard_pull_routing_check.py tests/bonelab_scenarios_check.py docs/VR-bonelab-feel-guard-pull-TEST-CHECKLIST.md tools/build-msvc.cmd
  git commit -m "feat: grabbing a living guard staggers, alerts and pulls him instead of ragdolling him"
  ```

---

## Headset checklists (user; not automated)

Part 1 and props: `docs/VR-bonelab-feel-corpse-TEST-CHECKLIST.md` (Task 13). Living guards: `docs/VR-bonelab-feel-guard-pull-TEST-CHECKLIST.md` (Task 16). Keep the `corpse` and `guard_pull` status blocks from each run. The first corpse run is a tuning session (`build/grab-tuning.cfg`: `strength_scale`, `axial_n`, `lateral_n`, `lift_fraction`, `tow`).

## Self-review

- **Spec coverage:**
  - F1: Task 3 (table, gate, probe, collar verdict). F2: Tasks 2, 7, 9 (forgiving `strength_scale`, judged contrast). F3: Tasks 4, 10, 11. F4: Task 5.
  - Part 1: collar and state gate (6), capped motor, lag, cooldown, anchor (7), tow flag (8), lab (9), engine integration and lifecycle reasons incl. `state_changed` (10), wake retry and refusal haptic (11), S1-S7 and Tier C (13).
  - Resolved question 3 (1L): lever probe (14), policy (15), integration, retire ragdoll yank, S8 (16).
  - Resolved question 6 (props): Task 12 (motor props and corpses, knob), Task 10 (props lane through the capped pipeline); native HoldObject items already palm-seated, called out in Global Constraints.
  - Resolved question 5 (hand body) and the tank fix: plan B.
  - Other resolved questions: tuning file (2, 10), collar routing (6), caps scope (10), unconscious wake (10), 0xb9 retry-then-refuse (4, 11), saves (1, 13), mass probe anchoring and collar verdict (3).
- **Step scan:** no "TBD" or "handle edge cases"; Task 14 is an explicit probe task whose output (lever table) is a named input to Tasks 15-16, with a stated outcome for "no pull lever".
- **Type consistency across tasks:** `GrabTuning` (2→3,7,8,10), `ReleaseReason` (4→5,7,10,15), `MassSource` (3→5,10), `cap_velocity_step` (2→7,8), `CorpseMotor` / `anchor` (7→10,12), `hold_anchor` (10→12), `GuardPull` (15→16), `ClampHit` (existing→15).
- **Review Focus** lines each have a named test in the owning task (2, 3, 4, 7, 9, 10, 15).
- **Proportion:** 16 tasks; Tasks 10 and 16 are the large engine ones, Task 14 is the only open-ended one.
