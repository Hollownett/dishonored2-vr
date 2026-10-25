# Small Objects, Palm Aim, Fingers and Living-NPC Grab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The hand you see is the hand that chooses, holds and touches. The palm picks loot (coins, food, notes) on a table, a
shelf, the floor and in an open drawer. The pick stays steady under tremor. Props and corpses catch on the first squeeze,
and also with grip alone. The fingers ease into a grip and stop on tables, shelves and thin props. Awake guards and
civilians can be held, and a hard pull ragdolls them.

**Architecture:**
- One pure Bonelab §1.1 palm cost lives in `selection_policy.hpp`. It ranks the native Use hits (loot, usables), the
  physical candidates (props, corpse bones) and the living-NPC bones, and the grip-alone decision compares them in one list.
- G1 is measured first in the sim. The measurement decides the floor and drawer fix.
- `NativeGrab` gains a 150 ms grab-intent buffer, all-bone corpse candidates, a hover-time LOS request and a living-NPC
  hold lane. The living hold never writes velocity to a keyframed body.
- The finger pipeline in `dxgi_probe.cpp` gets a composed wants stack, a 0.25 s grip pose blend and a solver that can stop
  an extended finger. Free-hand probes follow each finger's current pose, and a sticky probe cache prevents flicker.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook), the Python sim rig (`linux/`, `tools/linux/`), Ghidra for the
`3b80a0` read (only on that branch).

**Spec:** `docs/superpowers/specs/2026-10-25-small-objects-fingers-design.md`. Read all of it, especially "Settled after the
builder check", G6 and Verification. Line numbers below were spot-checked at `6f00968`. The v610 camera plan edits
`dxgi_probe.cpp` first, so `dxgi_probe.cpp` lines will drift. Locate each site by the quoted anchor, not by the number.

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`. Plan workspace (git-excluded):
  `.superpowers/sdd/2026-10-25-small-objects-fingers/`. The ledger is `progress.md` there.
- This work ships as **v611**, after the v610 camera work. The build name is `d2vr_render_probe_arm_v611.dll`. The checklist
  is `docs/VR-v611-TEST-CHECKLIST.md` (the spec says v610; the user has since moved this work to v611).
- Pinned exe `Dishonored2.exe`, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
- Engine write safety and C9 routing are as in the bonelab-feel spec. Selection reads the **raw** controller pose plus the
  phase-1 palm fit (C10). Rendering reads the drawn pose. No swapchain, layer or render-path changes.
- No new *kinds* of engine writes, except two the spec allows for G6: the existing `shove` on hold start, and (only if Task 13
  clears it) the existing `teleport` (`0x87d1e0`) as the pull. Velocity is **never** written to a keyframed (living) body.
  Forging native hit records is not allowed.
- Values, verbatim from the spec:
  - palm cost `((0.6 − dot(palmDir, toTarget))² + 1) · (d_n² + 1) · approach · priority`;
  - `d_n` = distance ÷ (arm length ÷ 4), where the arm length is the calibration's, or **0.65 m** if uncalibrated;
  - hover centre: **0.015 m** along the palm axis plus **0.8 ×** hover radius along the palm direction;
  - approach multiplier **0.8–1.2**; hysteresis **× 0.86**; loot priority **0.8**; loot bounds padded to **≥ 3 cm** radius;
  - gates: **0.45 m** acquire and **0.55 m** retain (unchanged); grip-alone gate **0.12 m**;
  - grab buffer **150 ms** (knob 0–300 ms); the decision waits for a fresh native target up to **100 ms**, never longer;
  - corpse priorities: limbs **1.0**, torso and pelvis **1.1**; living bones **1.0**;
  - grip pose blend **0.25 s** (Bonelab `SetHandPose`); loot take close **0.1 s**, reopen **0.25 s**; held floor **0.6**;
  - hover pre-curl `0.5 × squeeze` × closeness (0 at 0.45 m, 1 at contact); pinch below a **3 cm** bounds radius;
  - hyperextension limit **15°** at the middle and tip joints; probe-hit life **150 ms**; stickiness **2 probe cycles**;
    opening tau **60 ms**;
  - palm-normal cast **~8 cm**; fingertip cast to **2 cm** past the tip; thin prop = smallest axis **< 2 cm**, under 1 m;
  - surface tolerance **5 mm** (still or slow, under **0.3 m/s**), **15 mm** sliding fast;
  - cast budget **≤ 2** per free hand per frame, **3** only if the sim shows no frame-time loss against v609;
  - solve budget all 5 fingers, falling back to 2 plus the fingers in reach if `finger_ik` `avg_us` rises **> 50 µs** per hand;
  - G6: pull only with a gap **> 6 cm**, at most **5 cm per 100 ms**, horizontal only; tether break at **> 0.35 m for
    0.2 s**; hard pull **> 2.2 m/s** away from him; the hand returns to the controller with the usual **0.12 s** ease.
- New `build/grab-tuning.cfg` keys, loaded in `grab_force_cap.hpp` and bounded. A bad key keeps all defaults, as today:
  `grab_buffer_ms` 150 (0–300), `grip_alone_gate_m` .12 (.05–.25), `hover_radius_m` .25 (.1–.45), `priority_loot` .8,
  `priority_usable` 1, `priority_prop` 1, `priority_limb` 1, `priority_torso` 1.1, `priority_living` 1 (each .5–2).
- Off-switch knobs, all absent by default:
  - `build/legacy-palm-select` restores the beam order and the nearest-AABB pick;
  - `build/no-grip-alone-grab`;
  - `build/legacy-finger-ease` (G4);
  - `build/no-finger-surface` (G5);
  - `build/no-living-hold` (G6; today's fist yank stays).

  The trace knob `build/loot-reach-trace` writes `artifacts/native-loot-reach.csv` (at most 8192 rows, flushed every 64).
- Build: `python3 tools/linux/run_build_cmd.py --only '<regex>' candidate d2vr_render_probe_arm_v611.dll`. Full harness:
  drop `--only` and add `--keep-going`.
- Test registration in `tools/build-msvc.cmd`:
  - a new C++ check joins the `for %%T in (aa_first_start_check finger_ik_check …)` list (~245);
  - a new Python check is a `python tests\<name>.py` line plus its errorlevel line, next to
    `python tests\physical_grip_routing_check.py` (~701).
- Linux tests: `python3 -m unittest discover -s linux/tests`.
- Harness gate: no new failures against the **v610 final harness result** recorded in the camera plan's ledger. If v610 has
  not landed, use the v609 baseline: 632 passed / 1 failed `fsr_taa_gpu_check --fsr4` (environment-only), launcher 127,
  `linux/tests` 230.
- Updating a check that encodes old behaviour (the beam order, the held-floor order, immediate opening, the nearest-AABB
  pick, the 40 ms quick Use) is expected. The commit names the goal it serves. A changed check with no goal is reverted.
- **Sim safety**, on every Tier B run:
  - `PAUSE-SIM` absent;
  - no `Dishonored2.exe` or `vrserver` running (`pgrep -x` guards that can't match their own shell line);
  - never kill by pattern;
  - record the installed DLL's sha256 before the run and require it unchanged afterwards, with no knob left in `build/`;
  - the GNOME screen unlocked;
  - run with `linux/d2vr-linux sim --candidate-dll <built DLL> --scenario <name> --lead 45 --seconds <n>`.
- **Saves:** never overwrite the user's saves. The sim backs them up and restores them. Use `--test-save <folder>` for
  another save. Find test spots read-only with `tools/entities.py` and `tools/vr_selftest.py --near item|bottle|corpse|npc`,
  then teleport with the scripted console line from Task 1. If no usable save exists, record the gap and ask the user.
- **Bonelab rule:** each task names the Bonelab technique or number it follows. "None" is stated where none exists.
- Testing honesty: Tier A and B prove logic and numbers. Feel and visuals are Tier C only. Tell the user when a headset
  session is needed.
- Commit trailer, on every commit:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01BJuGoAt65W8LH5YcXkajFq`

## Review Focus

1. **The left hand.** The palm direction is mirrored between the hands (grip frame x forward, y left, z up). A sign slip makes
   the left hand rank by the back of the hand. Expected: both hands prefer what their own palm faces. Tests (Task 2):
   `left_and_right_palm_directions_mirror`. Tier B (Task 5): status `grab_focus.palm_dot` > 0.7 per side, where `palm_dot`
   is the dot between the probe's palm direction and the drawn `palm_normal`.
2. **Both hands reach for the same coin, prop or corpse bone.** Expected: the hand that chose first keeps it. The other hand
   gets its runner-up, and the choice never ping-pongs between the hands. Tests: (Task 3) `other_hand_pick_ranked_last`;
   (Task 5) `two_hands_same_body_no_ping_pong`.
3. **The chosen thing vanishes or its slot is reused during the 150 ms buffer** (the other hand took it, or a coin was looted
   and its registry slot respawned). Expected: the buffer expires cleanly, with no bind to the recycled entity. Test
   (Task 5): `buffer_target_vanishes_expires`.
4. **A pause, a load or an epoch change mid-hold, or a held living NPC who dies or is knocked out.** Expected: the living
   hold ends with no velocity write, and he is free. A body that became a corpse is offered again through the corpse path
   only. Tests (Task 14): `epoch_change_releases_living_hold`, `living_target_dies_releases_without_write`.
5. **A frame hitch** (one 100 ms frame) during the 0.25 s ease or while a finger rests on a table. Expected: no curl step
   above the blend rate at the next frame. Stickiness is counted in probe cycles, not frames, so a hitch does not drop the
   surface. Tests: (Task 9) `ease_with_hitch_no_step_above_rate`; (Task 12) `stickiness_survives_hitch`.

---

### Task 1: G1 measurement — scripted teleport, loot trace, `loot_reach` scenario (Tier B)

Bonelab: none. This measures the engine's own Use gates (`3b80a0` eligibility, `3b7b90` LOS), which Bonelab has no
counterpart for.

**Files:**
- Modify: `src/simulated_gameplay_script.hpp` (version 8 `console` token per segment); `tools/simulated_gameplay_script.py`
  (emit it); `tools/linux/scenario_lib.py` (`console(t, line)` helper). If the v610 camera plan's Task 3 already added a
  scripted teleport, reuse it and skip this bullet.
- Modify: `src/dxgi_probe.cpp`: the script player fires a segment's console line once at segment entry, through the existing
  `console` test-command path (`if(verb=="console")` ~3570). The trace knob is read next to the other `build/` knobs.
- Modify: `src/native_interaction_aim.hpp`: the loot trace. `sort_body` (~1093–1125) logs the set and the ranks. The existing
  `3B7B90` continuation hook logs the LOS calls and results per entity. `target()` (~879) logs the native-selected owner.
  `prompt_target()` (~1667) logs the prompt owner.
- Create: `tools/linux/scenarios/loot_reach.py`; add a `loot_reach` report mode to `tools/linux/bonelab_run_check.py`
- Test: `tests/simulated_gameplay_script_console_check.py`, `tests/loot_reach_trace_check.py` (a source-routing check), and
  the registration in `tests/bonelab_scenarios_check.py`
- Ledger: `g1-measure.md`

**Interfaces:**
- Produces: script v8. Each segment ends with one token, `-` or a percent-encoded console line (`%20` for a space, `%25`
  for `%`). The decoded line is at most 200 chars, from `[A-Za-z0-9 ._-]`. The DLL fires it once when the segment begins.
  The compiler emits v8 only when a segment has `"console"`.
- Produces: `scenario_lib.console(t_ms, line)`, which returns a 1-frame segment carrying `console`.
- Produces: `artifacts/native-loot-reach.csv`, written only with the knob. It has one row per (frame, side, candidate in
  the set), at most 8192 rows, flushed every 64. Columns:
  `t,side,palm_x,palm_y,palm_z,dir_x,dir_y,dir_z,owner,spawn,takeable,set_count,rank,los_called,los_blocked,selected_owner,prompt_owner`.
  `rank` is the index after `order_native_use_hits`. `los_called` and `los_blocked` cover this owner in this stage-0
  cycle.
- Produces: `bonelab_run_check.py RUN --scenario loot_reach --coin X Y Z [--case table|floor|drawer]`. The coin is the
  takeable owner whose bounds are nearest the given point. Over the frames where the coin is within 0.45 m of the palm, the
  check prints `in_set_rate`, `rank0_rate`, `los_called_rate` (frames at rank 0), `los_blocked_rate` and `selected_rate`.
  It then prints one classification:
  - `PASS`: `selected_rate` ≥ 0.8.
  - `ORDER`: in the set ≥ 0.8, but outranked (`rank0_rate` < 0.5).
  - `ELIGIBILITY`: in the set ≥ 0.8, at rank 0 ≥ 0.5, and `los_called_rate` < 0.2. LOS is never asked because the native
    permission or `3b80a0` checks rejected it first (inferred from the `3b8430` loop order in
    `docs/native-use-ranking-20261010.md`).
  - `NOT_IN_SET`: `in_set_rate` < 0.2.
  - `LOS`: `los_blocked_rate` ≥ 0.8.
  - `MIXED`: anything else.

- [ ] **Step 1: Write the failing checks.**
  - `simulated_gameplay_script_console_check.py`:
    - a v8 script round-trips `console setviewpos 1.5 -2.25 0.5 90`;
    - `-` means none;
    - a 201-char line, a `;` and a v7 script with the token are each rejected;
    - the compiler still writes v7 when no segment has `console`.
  - `loot_reach_trace_check.py`:
    - the trace opens only when `build/loot-reach-trace` exists;
    - the row cap is 8192;
    - the logging never changes the hit records, the LOS return value or the output byte (the trace reads after the
      original calls);
    - a mutation that moves the row write before `self->sort_(...)` must fail.
- [ ] **Step 2: Run** `--only 'simulated_gameplay|loot_reach'`. Expect a FAIL.
- [ ] **Step 3: Implement** the v8 token, the once-per-entry firing and the trace, as in Interfaces.
- [ ] **Step 4: Run** the same `--only`. Expect a PASS.
- [ ] **Step 5: Find the spots** (read-only).
  - Run a long `idle` session and use `tools/entities.py` and `tools/vr_selftest.py --near item` against it.
  - Find a coin on a table, a coin on the floor and an open drawer with loot. Try the user's save set, then other saves
    through `--test-save`.
  - Record each `setviewpos` line and the coin's position in `g1-measure.md`. A missing drawer is recorded and does not
    block the rest.
- [ ] **Step 6: Write `loot_reach.py`.** Per case:
  - teleport, look down 35°;
  - sweep the right palm over the coin: 0.35 m to 0.05 m above it, palm down, then palm toward it from the side, ±6 cm
    laterally;
  - hold still for 2 s, squeeze grip for 0.6 s, release, and repeat with the left hand.
- [ ] **Step 7: Run** `linux/d2vr-linux sim --candidate-dll build/candidate/d2vr_render_probe_arm_v611.dll --scenario loot_reach --knob loot-reach-trace --lead 45 --seconds 120`,
  then the check for each case. Record the per-case classification and rates in `g1-measure.md`.
- [ ] **Step 8: Commit** `test(loot): G1 measurement - scripted teleport, native loot trace, loot_reach scenario`.

### Task 2: The shared palm cost (pure, Tier A)

Bonelab §1.1 (`Hand.UpdateHovering`). The hover sphere sits in front of the palm. The costs are the facing term, the
distance term and the approach term, times the priority, with 0.86 hysteresis on the current pick.
`HoverSphereRadius` was not extracted from the prefabs (study line 568). Builder's call: radius 0.25 m. Its centre then
sits 0.2 m ahead of the palm, and its far edge is the 0.45 m acquire gate ("scaled to the mod's gate").

**Files:**
- Modify: `src/selection_policy.hpp` (new types and functions after the gate constants ~7–9)
- Modify: `src/grab_force_cap.hpp` (`GrabTuning` fields and loader keys, as in Global Constraints)
- Test: `tests/palm_scoring_check.cpp` (create, register); `tests/grab_force_cap_check.cpp` (the bounds table)

**Interfaces:**
- Produces (`selection_policy.hpp`):
  ```cpp
  inline constexpr double palm_axis_push=.015,hover_lead=.8,palm_facing_bias=.6,palm_hysteresis=.86,
      approach_min=.8,approach_max=1.2,approach_per_mps=.2,loot_min_half=.03,default_arm_length=.65;
  enum class PalmKind : unsigned char {loot,usable,prop,bone,living};
  struct PalmProbe {Vec3 palm{},forward{},direction{},velocity{};double arm_length=default_arm_length;bool valid{};};
  PalmProbe palm_probe(unsigned side,const WorldTrackedPose& raw_grip,const PalmOffset& fit,
                       std::optional<double> arm_length,Vec3 velocity);   // C10: apply_palm_offset(raw,fit,1.0); direction = side?+basis[1]:-basis[1]
  Vec3 palm_point(const PalmProbe&);                     // palm + forward*palm_axis_push
  Vec3 hover_centre(const PalmProbe&,double hover_radius);  // palm_point + direction*hover_lead*hover_radius
  struct PalmTarget {std::uint64_t id{};PalmKind kind{};Vec3 minimum{},maximum{};double priority=1;Vec3 velocity{};};
  struct PalmScore {double cost{},distance{};Vec3 point{};};
  std::optional<PalmScore> palm_cost(const PalmProbe&,const PalmTarget&,double hover_radius,double gate);
  struct PalmPick {std::optional<std::size_t> index;double cost{},runner_up{};bool hysteresis_held{},fallback{};};
  template<class Targets> PalmPick pick_by_palm(const PalmProbe&,const Targets&,std::uint64_t current,
      double hover_radius,double acquire=grab_acquire_distance,double retain=grab_retain_distance);
  class PalmSelection {public:   // per hand: hysteresis owner plus counters
      PalmPick update(const PalmProbe&,const std::vector<PalmTarget>&,double hover_radius,std::uint64_t now_ms);
      std::uint64_t current() const;unsigned switches_10s(std::uint64_t now_ms) const;
      std::uint64_t hysteresis_held() const,scoring_fallback() const;void reset();};
  ```
- The rules the tests pin:
  - **Distance and direction** are measured from `palm_point` to the target's closest point on its bounds.
  - **Loot bounds** are padded to a half-extent ≥ `loot_min_half` per axis, about their centre, for scoring only.
  - **Eligibility:**
    - a new target needs its closest point within `hover_radius` of `hover_centre` and within `acquire` of `palm_point`;
    - the current pick is kept while it stays within `retain` of `palm_point`;
    - a target with `distance` ≤ `palm_axis_push` is always eligible, so a palm on the item never loses it to the sphere.
  - **Approach:** `approach = clamp(1 − approach_per_mps·v_close, 0.8, 1.2)`. `v_close` is the closing speed
    `dot(hand.velocity − target.velocity, toTarget)` in m/s.
  - **Hysteresis:** the current pick's cost × 0.86.
  - **Ties** go to the lower `id`, so the order is stable.
  - **A non-finite probe** gives `fallback = true` and keeps `current`. It is never a new pick.
- Produces (`grab_force_cap.hpp`): the `GrabTuning` fields `grab_buffer_ms`, `grip_alone_gate_m`, `hover_radius_m`,
  `priority_loot`, `priority_usable`, `priority_prop`, `priority_limb`, `priority_torso`, `priority_living`, plus
  `double priority(PalmKind kind,int bone=-1) const noexcept` (bones 0/1 → torso; living → living).

- [ ] **Step 1: Write the failing tests** (`palm_scoring_check.cpp`):
  - `facing_beats_nearer_behind_knuckles`: a book 0.30 m along the palm direction beats a cup 0.12 m behind the knuckles.
  - `back_of_hand_loses`: the same object scored with the direction negated costs ≥ 2.5× as much.
  - `approach_breaks_tie`: two equal targets; the one closing at 1 m/s wins, and its approach factor is 0.8 exactly. The
    clamp holds at 0.8 and 1.2.
  - `coin_beats_table_shell`: a 5 cm coin (padded to 3 cm) lies on a table AABB 2 cm below the palm point. The coin is
    loot at priority 0.8, the table is usable at 1.0. The coin wins.
  - `cost_monotonic_in_distance`: along the palm direction, the cost rises strictly from 0.02 m to 0.45 m.
  - `jitter_never_flips`: two coins 8 cm apart, the hand 3 cm nearer coin A. 500 seeded frames of ±1 cm and ±3° jitter
    keep `current` = A, with `switches_10s` 0.
  - `clear_move_flips`: moving 6 cm toward B flips to B within 3 frames, `switches_10s` 1.
  - `hysteresis_ratio_pinned`: with a challenger whose raw cost is 0.87× the current pick's, the pick holds. At 0.85×
    it switches. (This mutation-guards 0.86.)
  - `facing_bias_pinned`: the facing term is exactly 1.16 at dot 1 and 3.56 at dot −1 (mutation guard for 0.6).
  - `gates_pinned`: a target is accepted at 0.449 m and refused at 0.451 m. A held current is kept at 0.549 m and dropped
    at 0.551 m.
  - `loot_priority_pinned`: `GrabTuning{}.priority(PalmKind::loot)` is 0.8, and the torso bones 0 and 1 give 1.1.
  - `left_and_right_palm_directions_mirror`: the same raw grip basis gives opposite `direction` for side 0 and side 1.
    Each hand prefers the target on its own palm side.
  - `non_finite_probe_keeps_pick`: NaN palm → `fallback`, `current` unchanged, `scoring_fallback` 1.
  - `arm_length_default`: with no calibration, `d_n` uses 0.65 m.
  - `grab_force_cap_check`: each new key with its bounds; out of bounds → defaults plus an error.
- [ ] **Step 2: Run** `--only 'palm_scoring|grab_force_cap'`. Expect a FAIL.
- [ ] **Step 3: Implement** as in Interfaces (header-only, no allocation in `palm_cost`).
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `feat(select): Bonelab palm cost, hysteresis and grab-tuning keys (G2)`.

### Task 3: Loot ordering by the palm (G1, G2)

Bonelab §1.1: the priority multiplier and the 0.86 hysteresis. Loot gets priority 0.8 and replaces the 6 cm bonus.

**Files:**
- Modify: `src/selection_policy.hpp` `order_native_use_hits` (~59–84). The hand-mode beam (`grab_beam_radius` and
  `use_loot_bonus` in the cost) is replaced. The constants stay only for the legacy knob.
- Modify: `src/native_interaction_aim.hpp`:
  - `Publication` (~146) gains `PalmProbe palm`;
  - `publish_hands` (~1495) gains the per-side probes;
  - `sort_body` (~1107) passes them;
  - a new private `owner_bounds(std::uintptr_t)` uses the `model+0x1840` read of `static_target_point_locked` (~1373),
    refactored to share it;
  - a new `std::optional<PalmTarget> target_bounds(int side,std::uint64_t now)`.
- Modify: `src/dxgi_probe.cpp`. At `native_interaction_aim.publish_hands(` (~4723), build the probes from `grip_poses`,
  `game_xr->palm_offset(side)`, `avatar_fit_reuse.active_fit()->arm_reach_metres[side]` and `hand_velocity[side]`.
- Test: `tests/native_use_order_check.cpp` (update); `tests/palm_use_routing_check.py` (create, register)

**Interfaces:**
- Consumes: Task 2 `palm_cost`, `PalmProbe`, `GrabTuning::priority`.
- Produces:
  ```cpp
  template<class Owner,class Loot,class Bounds>
  bool order_native_use_hits(NativeUseHit* hits,unsigned count,const PalmProbe& probe,std::uintptr_t preferred,
      std::uintptr_t other_hand,Owner owner,Loot loot,Bounds bounds,const GrabTuning& tuning,bool legacy_beam=false);
  // bounds(owner) -> std::optional<std::pair<Vec3,Vec3>>; missing bounds: the hit point as a zero-size box.
  // Hits beyond the hover gate keep today's fallback cost (distance + 1). other_hand's owner sorts last. Records are never altered.
  ```
  `NativeInteractionAim::target_bounds(side,now)` returns the live native target as a `PalmTarget`. Its kind is `loot` if
  `takeable(owner)`, else `usable`. Task 6 and Task 10 use it.
- An occupied hand (holding a prop, a body, a weapon or the sword) offers nothing. The existing occupancy gate
  (`hand_target`, ~8318) is asserted, not changed.

- [ ] **Step 1: Update and add tests** (`native_use_order_check.cpp`). The beam block (~36–49) is replaced, with the reason
  "G1: palm cost replaces the forward beam":
  - `palm_faced_coin_beats_nearer_shell`;
  - `coin_behind_knuckles_loses` (the anti-goal);
  - `loot_priority_replaces_bonus`: a coin 0.15 m away beats a safe face at 0.10 m. A coin 0.6 m away does not beat a
    safe at 0.05 m.
  - `other_hand_pick_ranked_last`;
  - `preferred_keeps_with_086`;
  - `legacy_beam_knob_keeps_old_order`: the old beam assertions run with `legacy_beam=true`.
  - The record-integrity, NaN and 65-count cases stay as they are.
- [ ] **Step 2: Write `palm_use_routing_check.py`.** It asserts:
  - `sort_body` passes `scope.scope->hand.palm`;
  - `publish_hands` receives probes built from `grip_poses` (the raw controller, C9), not `hands.grips`;
  - `build/legacy-palm-select` sets `legacy_beam`;
  - the occupancy gate expression in `hand_target` is unchanged.
- [ ] **Step 3: Run** `--only 'native_use_order|palm_use_routing|native_interaction'`. Expect a FAIL.
- [ ] **Step 4: Implement.**
- [ ] **Step 5: Run.** Expect a PASS.
- [ ] **Step 6: Re-measure.** Re-run Task 1 Step 7 with this build. Record the new classification per case in
  `g1-measure.md`. Task 4 branches on these numbers.
- [ ] **Step 7: Commit** `feat(loot): the palm picks loot - Bonelab cost replaces the forward beam (G1, G2)`.

### Task 4: Floor and drawer loot — measure, then fix the cause (G1)

Bonelab: none (native eligibility). The branch rules are fixed here, so the implementer decides nothing beyond them.

**Branch rules**, per case, from the Task 3 Step 6 classification:

| Classification | Branch |
| --- | --- |
| `PASS` or `ORDER` that now passes | none. Record and close the case. |
| `ELIGIBILITY` | **A**: RE `3b80a0`, then feed the facing and height inputs the hand viewpoint inside the gate |
| `NOT_IN_SET` | **B**: aim the stage-0 direct ray at the palm-chosen loot's bounds centre |
| `LOS` with a real blocker (closed drawer, wall) | none. This is the anti-goal working. Record it. |
| `LOS` on an open drawer, `MIXED`, or a branch that fails its re-measure | **stop**: record the trace and list the case as a known gap. Do not try a third fix. |

**Files (branch A):**
- Read: Ghidra project `~/.local/share/d2vr-re/snapshots/dishonored2-ghidra-2026-10-19`, function `3b80a0`. It calls the
  range-origin getter `0x417820` from `0x3b81d8` (already hooked).
- Create: `docs/native-use-eligibility-3b80a0.md`. It covers the inputs of the facing, height and range checks, each
  getter and its call RVA, and the exe hash.
- Modify: `src/native_interaction_aim.hpp`. Give the facing and height inputs the same swap that `range_origin` (~233–255)
  gives the eye. The swap is allowed only for the current stage-0 hand scope, and only when the palm-chosen loot's bounds
  are within 0.45 m of `palm_point`. `3b7b90` LOS stays original.
- Test: `tests/native_use_eligibility_origin_check.cpp`. It covers the pure gate (`hand_viewpoint_allowed(probe,bounds)`)
  and a fixture that checks the original getters are called exactly once and their output is used outside the scope.

**Files (branch B):**
- Create: `src/loot_discovery.hpp`, a bounded cache of `takeable` entities within 0.55 m of either palm. It scans the
  registry in slices like `NativeGrabDiscovery`, with capacity 16.
- Modify: `src/native_interaction_aim.hpp` `ray()` (~950–957). When the palm pick from `native_use_volume_focus`
  (selection_policy ~28) exists for this hand, the stage-0 direct ray points from the hand at that item's bounds centre.
  The game still selects it with its own eligibility, LOS and prompt. `native_eligible` means "aimable" here. The policy
  comment is updated to say so.
- Test: `tests/loot_direct_aim_check.cpp`:
  - `direct_ray_targets_chosen_bounds_centre`;
  - `no_choice_keeps_volume_ray`;
  - `other_hand_choice_ignored`;
  - `beyond_gate_ignored`.

**Interfaces:**
- Produces: on either branch, `NativeInteractionAim::loot_fix_branch()` (`'A'`, `'B'` or `0`), reported in the status
  block `grab_focus` (Task 15).

- [ ] **Step 1: Decide** the branch per case from the table. Write it in `g1-measure.md`.
- [ ] **Step 2 (A or B): Write the failing tests** for that branch. Run `--only 'native_use_eligibility|loot_direct'` and
  expect a FAIL.
- [ ] **Step 3 (A only): RE `3b80a0`** and write the doc. If the facing or height input is not a hookable getter call (for
  example an inlined read of the camera axis), stop the branch and record why.
- [ ] **Step 4: Implement the branch.** Run the tests and expect a PASS.
- [ ] **Step 5: Re-measure** with Task 1's scenario and check. The case passes at `selected_rate` ≥ 0.8. The prompt owner
  must equal the taken owner on every take: the check reads the owner that vanished within 500 ms of a press against the
  `prompt_owner` at the press. No take may come from an owner whose LOS was blocked on every frame. If the case still
  fails, it goes to the known-gap row.
- [ ] **Step 6: Commit** `fix(loot): floor and drawer loot via <branch> (G1)`, or
  `docs(loot): floor/drawer loot measured, known gap recorded`.

### Task 5: Physical pick by the palm and the 150 ms grab buffer (G2, G3)

Bonelab §1.1: the same cost ranks the physical grips. The buffer has no Bonelab number. 150 ms is the spec's call: two
reach-arrival frames at 72 Hz, with margin.

**Files:**
- Modify: `src/native_grab.hpp`:
  - `grab_focus_candidate` (~89–102) and the acquisition loop in `update_lane` (~723–735) both rank by `pick_by_palm`;
  - `focus_body` (~296) builds `PalmTarget`s from the candidates, and excludes the other lane's focus and its held body;
  - `Publication` gains `PalmProbe palm`, and `publish` (~883) takes it;
  - `filter_native_grip` (~993–1060): the deciding frame with no candidate starts the buffer instead of latching native;
  - the lane gains `PalmSelection selection` and `GrabIntentBuffer buffer`.
- Modify: `src/right_grip_owner.hpp`: a `Role::pending` role with `begin_pending()` / `resolve_pending(bool physical)`.
  `filter()` returns 0 while pending.
- Create: `src/grab_intent_buffer.hpp` (pure)
- Modify: `src/dxgi_probe.cpp` `native_grab.publish(` (~4665). It passes the same probe that Task 3 builds.
- Test: `tests/grab_intent_buffer_check.cpp` (create, register); `tests/native_grab_sequence_check.cpp`;
  `tests/native_grab_compile.cpp` (~69–75, update: nearest → palm, goal G3); `tests/right_grip_owner_check.cpp`

**Interfaces:**
- Consumes: Task 2 `PalmSelection`, `pick_by_palm`, `GrabTuning::grab_buffer_ms` and `priority`.
- Produces:
  ```cpp
  class GrabIntentBuffer {public:
      enum class Step {idle,waiting,caught,expired};
      void start(std::uint64_t now,std::uint64_t window_ms,double gate_m);
      Step step(std::uint64_t now,std::optional<double> candidate_distance);   // caught only if distance<=gate and now<start+window
      bool waiting() const;std::uint64_t buffered() const,expired() const;void cancel();};
  inline std::optional<NativeRigidBody> grab_focus_candidate(const std::vector<NativeRigidBody>&,const PalmProbe&,
      PalmSelection&,const GrabTuning&,std::uintptr_t other_entity,std::uint32_t other_handle,double gate,bool legacy_nearest=false);
  struct FocusScore {std::uint64_t id{};PalmKind kind{};double cost{},runner_up{},distance{};};
  std::optional<FocusScore> NativeGrab::focus_score(unsigned side,UINT64 now);   // read by Task 6 and the status block
  ```
- The buffer:
  - It starts only when the hand is not occupied and nothing is in the gate at the deciding frame.
  - It catches only a candidate that reaches `gate` (0.45 m for a fist, Task 6's 0.12 m for grip alone) inside the window.
  - A release, an epoch change or an occupied hand cancels it. On expiry the role resolves native and the squeeze passes as
    today.
  - `grab_buffer_ms` 0 disables it.
- The status block `grab_focus` starts here, per side: `{id,kind,cost,runner_up,switches_10s,hysteresis_held,
  scoring_fallback,buffered_grabs,buffer_expired,palm_dot}`.

- [ ] **Step 1: Write the failing tests.**
  - `grab_intent_buffer_check`:
    - `catches_inside_window` (distance 0.30 at +100 ms with a 0.45 gate → caught);
    - `expires_after_window` (+151 ms → expired);
    - `never_catches_beyond_gate` (0.46 at +50 ms → waiting, then expired);
    - `zero_window_disabled`;
    - `buffer_target_vanishes_expires` (the candidate goes `nullopt`, or a different `id` appears at the same spot inside
      the window: no catch of the recycled id, expiry at 150 ms);
    - `epoch_change_clears_pending_buffer`.
  - `native_grab_sequence_check`:
    - a press 100 ms before the hand reaches a prop binds on arrival;
    - a press with nothing reached in 150 ms resolves native, and `native_presses` increments once;
    - `two_hands_same_body_no_ping_pong`: the left hand holds the focus on body X; the right hand gets its runner-up, and
      after 200 frames neither lane's focus has changed.
  - `right_grip_owner_check`: `pending` returns 0, then resolves to physical or native exactly once.
  - `native_grab_compile`: in clutter, the palm-faced box wins over an overlapping box listed first (G3: "the thing the palm
    faces wins").
- [ ] **Step 2: Run** `--only 'grab_intent_buffer|native_grab|right_grip_owner'`. Expect a FAIL.
- [ ] **Step 3: Implement.** `build/legacy-palm-select` restores the nearest pick and disables the buffer.
- [ ] **Step 4: Run** the same set plus `corpse_hold|physical_grab`. Expect a PASS.
- [ ] **Step 5: Commit** `feat(grab): the palm picks props and bones; a 150 ms buffer catches early squeezes (G2, G3)`.

### Task 6: Grip alone grabs — the one-list decision and the 0.12 m gate (G3)

Bonelab and Alyx grab on grip alone (study §1.1, step 5: "when the controller reports *grabbed*"). Bonelab gives no number
for the gate. 0.12 m ("the hand is on it") is the spec's call.

**Files:**
- Create: `src/grip_decision.hpp` (pure)
- Modify: `src/vr_controls.hpp` `GripIntent` (~290–325):
  - a new `Kind::grab` and `Out::{grab,grab_started}`;
  - `update(...)` gains `GripAloneChoice choice` and `bool one_list`. With `one_list` true, the 40 ms quick Use is
    replaced by the choice. False keeps today's behaviour exactly, for the knob.
- Modify: `src/dxgi_probe.cpp`:
  - the `grip_intent_out={grip_intent[0].update(` site (~8033) computes the choice per hand;
  - `fist_hands` / `grab_hands` (~8037);
  - the squeeze published to `native_grab.publish(` (~4665) when the hand is in a fist **or a grab**;
  - `right_physical` and the left `filter_native_grip(0,` argument (~8366–8374) include `grip_intent_out[h].grab`;
  - the knob `build/no-grip-alone-grab`.
- Test: `tests/grip_decision_check.cpp` (create, register); `tests/vr_controls_check.cpp` (~196–205, update with the reason
  "G3: one-list decision replaces the 40 ms quick Use"); `tests/physical_grip_routing_check.py` (update its pins)

**Interfaces:**
- Consumes: Task 3 `target_bounds`, Task 5 `focus_score`, Task 2 `palm_cost`.
- Produces:
  ```cpp
  enum class GripAloneChoice {pending,use,grab};
  struct GripAloneInput {std::optional<PalmTarget> native;std::optional<FocusScore> physical;PalmProbe probe;
      bool native_fresh{};std::uint64_t since_ms{},now_ms{};bool choke_owns{},takedown_offered{},occupied{};};
  GripAloneChoice decide_grip_alone(const GripAloneInput&,const GrabTuning&,double hover_radius) noexcept;
  ```
- Rules, in order:
  1. An occupied hand, `choke_owns` or `takedown_offered` → `use` (today's path; chokes and takedowns keep priority).
  2. The physical candidate counts only at `distance` ≤ `grip_alone_gate_m`.
  3. If `!native_fresh` and `now − since < 100`, the result is `pending`.
  4. Otherwise both candidates are scored with the same probe, and the lower cost wins (loot or usable → `use`;
     prop or bone → `grab`). With nothing in reach, the result is `use`, which is today's native role.
- `GripIntent`:
  - while deciding with the grip first: `use` → `Kind::use`, `grab` → `Kind::grab`, `pending` → keep deciding;
  - after the 100 ms window, `pending` → `Kind::use`;
  - a trigger joining inside the window still makes a fist, and the late fist is unchanged;
  - `Kind::grab` passes no trigger and ends when the grip is released.

- [ ] **Step 1: Write the failing tests.**
  - `grip_decision_check`:
    - `loot_wins_use`;
    - `door_wins_use`;
    - `prop_on_palm_wins_grab`;
    - `prop_at_013_is_not_grip_alone` (it is fine for a fist: the 0.45 gate);
    - `block_next_to_table_stays_use` (a table shell usable at 0.2 m and no prop within 0.12 m → `use`);
    - `choke_offer_wins`;
    - `takedown_offer_wins`;
    - `waits_for_fresh_native_up_to_100` (pending at 99 ms, decided at 100 ms);
    - `occupied_hand_never_grabs`.
  - `vr_controls_check`: `grab` choice → `Kind::grab`, `grab_started` once, `pass_trigger` false; `pending` for 100 ms
    → `use`; a trigger at 60 ms → fist; the late fist case unchanged.
  - `physical_grip_routing_check.py`:
    - pins `const bool right_physical=grip_intent_out[1].fist || grip_intent_out[1].grab || native_grab.holding(1);`;
    - the squeeze publish includes `grab_hands`;
    - `fist_may_press_native_rb` is still applied (`if(right_physical) input.right_grip=right_fist_rb?right_native:0.f;`);
    - mutations removing `.grab` must fail.
- [ ] **Step 2: Run** `--only 'grip_decision|vr_controls|physical_grip_routing|right_grip_owner'`. Expect a FAIL.
- [ ] **Step 3: Implement.** `build/no-grip-alone-grab` passes `one_list=false`: today's `GripIntent`, with the 40 ms
  quick Use.
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `feat(controls): grip alone grabs what the palm is on; Use still wins for loot and doors (G3)`.

### Task 7: Corpse all-bone candidates and thin-prop bounds (G3)

Bonelab §1.1: a per-grip priority multiplier, default 1.

**Files:**
- Modify: `src/native_rigid_body.hpp`:
  - `corpse_candidate` (~170) and `nearest_ragdoll_body` (~146) take a `PalmProbe` and pick the bone by `palm_cost` over
    all 17 bones;
  - `ragdoll_hold_bone` keeps its meaning for the wake classification;
  - bounds acceptance (~72–82) adds the thin-prop rule;
  - `native_rigid_body(…, BodyReject* why=nullptr)` reports why a body was refused.
- Modify: `src/native_grab.hpp`: `hold_bone_allowed` (~345) keeps the collar squeeze rule (`bone 2/3` needs `collar_grab_`
  and a squeeze ≥ 0.35). The torso and pelvis (0, 1) bind through the same `begin_hold` as the collar.
- Test: `tests/corpse_hold_check.cpp`; `tests/native_rigid_body_bounds_check.cpp` (create, register)

**Interfaces:**
- Produces:
  ```cpp
  enum class BodyReject : unsigned char {ok,not_prop,pickable,no_component,not_dynamic,no_model,bounds_size,bounds_centre};
  template<class Read> std::optional<NativeRigidBody> corpse_candidate(Read&,std::uintptr_t base,std::uintptr_t entity,
      const PalmProbe&,const GrabTuning&,bool allow_collar);
  inline bool accept_bounds(const NativeGrabBounds&,Vec3 rendered_origin,Vec3 pose,BodyReject* why) noexcept; // pure, the rule below
  ```
- **Candidate bones:** all 17 except the head (4). Collar bones (2, 3) are included only under their squeeze rule.
  Priority: limbs 1.0, torso and pelvis 1.1.
- **Thin-prop rule:** the size limits are unchanged. If the only failing limit is the rendered-origin offset (> 0.25 m) and
  the pose lies inside the AABB (`closest(pose)==pose` within 1 mm), accept the AABB. A dynamic body that fails
  `(flags&7)==2` is reported (`not_dynamic`), never forced.

- [ ] **Step 1: Write the failing tests.**
  - `corpse_hold_check`:
    - `torso_reach_catches` (a palm 5 cm over the spine bone picks bone 1);
    - `forearm_preferred_over_torso_at_equal_distance` (the 1.1 priority);
    - `head_never_candidate`;
    - `collar_needs_squeeze_rule` (unchanged);
    - the existing limb cases stay green.
  - `native_rigid_body_bounds_check`:
    - `centre_offset_only_accepted_when_pose_inside`;
    - `centre_offset_with_pose_outside_rejected`;
    - `size_limits_unchanged`;
    - `reject_reason_reported`.
- [ ] **Step 2: Run** `--only 'corpse_hold|native_rigid_body_bounds|corpse_drag_lab'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expect a PASS, and `corpse_drag_lab` prints `0 failed`.
- [ ] **Step 5: Commit** `feat(grab): corpses catch at the torso, pelvis and thighs; thin props keep their bounds (G3)`.

### Task 8: Discovery near the hands and the hover-time LOS (G3)

Bonelab §1.1: "the scan runs every physics step". The hint pass gives the reached object the same freshness.

**Files:**
- Modify: `src/native_grab.hpp`:
  - `NativeGrabDiscovery::advance` (~117) takes `std::span<const unsigned> hints` (registry indices), looked up every tick
    before the slice;
  - `focus_body` (~296) passes the hints: the `>>16` registry indices of entities from this hand's probe hits
    (`weapon_hit` slots 3–8, Task 12) and of its native Use hit owners, at most 8, at most 250 ms old;
  - the obstruction path: `cleared` (~347), `request_obstruction` and `service_obstruction` (~1066) accept a hover request.
- Modify: `src/native_interaction_aim.hpp`: a `std::array<unsigned,8> hint_indices(int side,std::uint64_t now)` accessor
  over the stage-0 hit owners.
- Test: `tests/native_grab_lifetime_check.cpp`; `tests/native_grab_obstruction_check.cpp`

**Interfaces:**
- **Hover LOS:** it is requested when `focus_body` picks a candidate while the grip is under 0.35, at most once per hand
  per 100 ms. The verdict is still valid at the press if:
  - it is ≤ 100 ms old;
  - the hand moved ≤ 2 cm since;
  - it is for the same body;
  - the body's contact point moved ≤ 1 cm.
  Otherwise the press asks again (today's path).
- **Decision rule:** if the `paper_grab` sim (Task 15) still shows `press_to_held_ms` > 2 ticks on more than 10% of
  presses, add a second per-tick pass over cached bodies within 1 m of either hand. Record the numbers either way.
- The status reports `obstruction_required` once. Static reading says it is on whenever physical grab is attached
  (`attach_physical_grab` → `require_obstruction()`, dxgi_probe.cpp ~9519).

- [ ] **Step 1: Write the failing tests.**
  - `native_grab_lifetime_check`: `hint_found_same_tick` (a body at a hinted index is cached the same tick, with the cursor
    far away); `hints_bounded_to_8`.
  - `native_grab_obstruction_check`:
    - `hover_verdict_used_at_press` (no new request, bind on the press tick);
    - `stale_hover_verdict_requeried` (101 ms, or the hand moved 2.1 cm);
    - `hover_requests_rate_limited`;
    - the existing press-path cases stay green.
- [ ] **Step 2: Run** `--only 'native_grab_lifetime|native_grab_obstruction'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `perf(grab): discovery looks where the hand is; LOS asked at hover, not at press (G3)`.

### Task 9: Grab ease-in — floor order, the 0.25 s pose blend and no unwrapped frame (G4)

Bonelab `SetHandPose(pose, 0.25 s)` (study 75, 135, 351); the 0.6–1.0 held clamp (study 327); the 50 ms finger smoothing
`clamp01(20·dt)` (study 323).

**Files:**
- Modify: `src/finger_touch_curl.hpp` `update` (~68–75): the held floor goes into `target` **before** the smoothing.
- Create: `src/grip_pose_blend.hpp` (pure)
- Modify: `src/finger_ik.hpp`:
  - `finger_contact` (~429) gains `bool ease_opening=false` and `double max_open_lag_rad=0`;
  - `follow` (~600) eases opening with `finger_ik_tau` when `ease_opening` is set, never lagging the target by more than
    `max_open_lag_rad`;
  - `FingerMemo` keys on `reach` rounded to 0.05.
- Modify: `src/dxgi_probe.cpp` finger block (~5385–5500):
  - a hover shape per side (from `native_grab.focus_score` and the Task 3 `target_bounds`, through `classify_held`) is the
    first shape on the grab frame when no measured shape exists yet;
  - the later fitter shape is passed with `ease_opening=true`;
  - `reach` is `lerp(1, held_curl, blend weight)`, not a flip;
  - the blend wraps the solver output;
  - the knob `build/legacy-finger-ease`;
  - the status field `finger_ik.ease_active`.
- Test: `tests/finger_touch_curl_check.cpp` (update the held-floor case: "G4: floor before smoothing");
  `tests/grip_pose_blend_check.cpp` (create, register); `tests/finger_ik_check.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class GripPoseBlend {public:   // the output lerps from the pose at grab start to the solver's full wrap
      void start(const std::array<std::array<float,3>,5>& from,double t_s,double duration_s=.25);
      double weight(double t_s) const;   // linear, clamp((t-t0)/duration,0,1)
      std::array<std::array<float,3>,5> apply(const std::array<std::array<float,3>,5>& target,double t_s) const;
      bool active(double t_s) const;void stop();};
  ```
  `start` fires on the hold edge (holding false → true, for a physical grab or a native hold). The hand-to-object seat ease
  stays 0.12 s.

- [ ] **Step 1: Write the failing tests.**
  - `finger_touch_curl_check`: `held_floor_rises_through_filter`. An open hand with `held=true` and dt 1/72 gives
    curl ≤ 0.6·(20/72) + 1e-6 on frame 1, ≥ 0.59 after 0.3 s, and never a step > 0.6·20·dt.
  - `grip_pose_blend_check`:
    - `no_step_above_rate` (each frame's change ≤ |target − start|·dt/0.25 + 1e-6);
    - `late_shape_no_second_jump` (the target changes at +80 ms: no frame step above the rate);
    - `ease_with_hitch_no_step_above_rate` (one dt of 0.1 s: the step is ≤ rate·0.1);
    - `weight_one_after_025`.
  - `finger_ik_check`:
    - `eased_opening_bounded_lag` (a shape that opens the target 20°: the pose follows with tau 60 ms and never lags more
      than `max_open_lag_rad`);
    - `memo_stable_under_reach_blend` (a reach moving 0.81 → 0.80 does not invalidate the memo);
    - the existing "opening is immediate" assertions stay for `ease_opening=false`.
- [ ] **Step 2: Run** `--only 'finger_touch_curl|grip_pose_blend|finger_ik'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the same set plus `finger_touch_routing|finger_snapshot`. Expect a PASS.
- [ ] **Step 5: Commit** `feat(fingers): the hand eases into a grip over 0.25 s, no snap and no second jump (G4)`.

### Task 10: Hover pre-curl, pinch, the loot take close and the finger priority stack (G4)

Bonelab `Grip.OnHandHoverUpdate` pre-curls toward the grip ∝ squeeze (study 74); `LerpPose` takes 0.1 s by default
(study 351). The priority order is the spec's ("Settled after the builder check"): the surface contact is the final stage,
as in bonelab-feel Part 3.

**Files:**
- Create: `src/finger_wants_stack.hpp` (pure)
- Modify: `src/dxgi_probe.cpp` finger block. `wants` (~5466) comes from `compose_finger_wants`. The surface contact stays
  the last stage, because `finger_contact` stops the composed wants on every shape. The take edge is `fist_take[h]` or a
  `use_started` on a loot `target_bounds` (~8210). The item's disappearance is that owner leaving the registry, or the
  target going away for 100 ms.
- Test: `tests/finger_wants_stack_check.cpp` (create, register)

**Interfaces:**
- Produces:
  ```cpp
  struct WantsInput {std::array<float,5> touch{};bool held{};std::optional<double> hover_distance;double hover_radius_bounds{};
      float squeeze{};double take_weight{};};   // take_weight from TakeClose
  std::array<float,5> compose_finger_wants(const WantsInput&) noexcept;
  class TakeClose {public:void press(double t);void gone(double t);double weight(double t) const;};  // 0->1 over .1 s, 1->0 over .25 s after gone
  ```
- Composition, lowest priority first:
  1. the touch curl;
  2. the pre-curl `max(·, 0.5·squeeze·closeness)` on fingers 1–4, where `closeness = clamp(1 − d/0.45, 0, 1)`;
  3. the pinch, below a 3 cm bounds radius: a blend with weight 0.3·closeness toward `{0.6,0.55,0.15,0.15,0.15}`;
  4. the take close `max(·, held_floor·take_weight)`;
  5. the held wrap (held wants as today).

  The world contact then caps all of them.

- [ ] **Step 1: Write the failing tests:**
  - `precurl_scales_with_closeness` (0 at 0.45 m, 0.5·squeeze at contact);
  - `pinch_only_below_3cm`;
  - `take_close_timing` (weight 1 at +0.1 s; 0 at 0.25 s after `gone`; monotonic);
  - `held_overrides_precurl`;
  - `priority_order` (take beats pre-curl; held beats take);
  - `table_caps_precurl` (with a `slab_shape` under the fingers, `finger_contact` given the composed wants keeps every
    fingertip outside the slab: a hand reaching a coin on a table stays on the table).
- [ ] **Step 2: Run** `--only 'finger_wants_stack|finger_ik'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `feat(fingers): hover pre-curl, pinch for small items, a take that reads as a grab (G4)`.

### Task 11: The solver stops an extended finger (G5)

Bonelab has no per-finger collision (study 353–355). This follows the Alyx look, with Bonelab's 50 ms smoothing kept.

**Files:**
- Modify: `src/finger_ik.hpp`:
  - a new constant `finger_hyperextension_degrees=15` for the middle and tip joints of fingers 1–4 (the thumb excluded);
  - `finger_contact`'s `solve` (~470–540): when the wanted pose is buried (`march` reports `buried`, or `inside` at
    `u=0`), search the nearest non-penetrating pose, first more closed along the finger's path, then more open to the
    15° limit. If still buried, stop at the limit;
  - `FingerCurls` gains per-finger `blocked`, `hyperextended`, `stuck_frames`.
- Test: `tests/finger_ik_check.cpp`

**Interfaces:**
- Produces: `FingerCurls::blocked` and `hyperextended` (`std::array<bool,5>`), and `stuck_frames` (`std::array<unsigned,5>`,
  frames in a row at the hyperextension limit). The status `finger_ik` block reports them (Task 12). Bisection or `march`
  is the implementer's choice, within the budget of Task 12.

- [ ] **Step 1: Write the failing tests:**
  - `flat_hand_on_plane_stays_flat`: the palm is 1 cm above a slab and the fingers are straight with their tips 4 mm in.
    Every middle and tip joint ends in [−15°, +5°], every fingertip is ≥ −1 mm outside, and `hyperextended` is set.
  - `pressed_deeper_stops_at_limit` (tips 3 cm in: the joints at −15°, `blocked`, `stuck_frames` counting up);
  - `finger_sliding_over_edge_stays_outside` (a slab edge moving under the finger over 30 frames: no tip below −2 mm);
  - `thin_slab_stops_fingers` (`held_slab` 4 mm thick across the fingers);
  - `bend_preferred_when_clear` (a tip touching a wall ahead while the palm is clear: it closes, and `hyperextended` stays
    false);
  - the existing wrap cases stay green.
- [ ] **Step 2: Run** `--only 'finger_ik'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `feat(fingers): an extended finger stops on a surface, bends or lifts at most 15 degrees (G5)`.

### Task 12: Free-finger probes, surface shapes, stickiness and the budget (G5)

Bonelab's finger box is disabled on overlap (`CheckFingerEnable`, study 355). Here the probes find the surface instead.
The 50 ms smoothing stays (study 323).

**Files:**
- Create: `src/finger_probe_cache.hpp` (pure: per hand, per probe slot, the sticky shape)
- Modify: `src/finger_ik.hpp`: `finger_sweep` (~1080) gets an overload along the finger's current `pose`, from the knuckle
  to 2 cm past the tip. `palm_sweep` reach becomes 0.08 for a free hand. `slab_shape` keeps `half=.12`.
- Modify: `src/native_interaction_aim.hpp` `weapon_hit` (~717): an optional `ttl_ms` parameter (default 100). The finger
  slots read with 150.
- Modify: `src/dxgi_probe.cpp`:
  - the finger block (~5440–5450) posts the palm-normal cast every frame for a free hand (slot 3, replacing the
    alternate-frame sweep) and one fingertip cast along the current pose;
  - an optional third cast is a second fingertip;
  - `finger_solve_budget` (~5393) is 5 for a free hand;
  - `finger_collider` (~3158–3180) turns a prop under 1 m with its smallest local axis < 2 cm into a `held_slab` from its
    oriented bounds, instead of `classify_held`;
  - status `finger_ik` gains `probe_hits_world`, `probe_hits_thin` and the Task 11 fields.
- Test: `tests/finger_probe_cache_check.cpp` (create, register); `tests/finger_ik_check.cpp`;
  `tests/finger_collider_routing_check.py` (create, register)
- Ledger: `g5-budget.md`

**Interfaces:**
- Produces:
  ```cpp
  class FingerProbeCache {public:
      void probed(unsigned slot,std::optional<rig::HeldShape> hit,std::uint64_t now);   // a probe of this slot answered
      unsigned shapes(std::uint64_t now,std::array<rig::HeldShape,8>& out) const;        // live: hit <=150 ms, or < 2 misses since
      void clear();};
  rig::FingerSweep finger_sweep(const HandGeometry&,const WorldTrackedPose& palm,unsigned f,const std::array<float,3>& pose,double past_tip=.02);
  ```
- The tolerance passed to Task 9's `max_open_lag_rad` is 5 mm ÷ the finger lever when the hand is under 0.3 m/s, else
  15 mm ÷ the lever. Weapon hands and native hands are unchanged.
- **Budget decision rule:**
  - run `finger_touch` and `run_hands` with v609 and with this build. Record `finger_ik.avg_us`, `solve_us` and the frame
    time in `g5-budget.md`;
  - if `avg_us` per hand rises by more than 50 µs, ship 2 fingers per frame plus all fingers with a shape in reach;
  - run once more with the knob `build/finger-probe-3`; if the frame time shows no loss against v609 (median within
    0.1 ms, p99 within 0.3 ms), the third cast ships as the default, else 2 casts;
  - `FullHandQueryTransaction` as the default source is not attempted. Its cost is recorded only.

- [ ] **Step 1: Write the failing tests.**
  - `finger_probe_cache_check`:
    - `hit_lives_150ms`;
    - `stickiness_two_cycles` (with probes every 5th frame, a finger whose probe misses once keeps its shape, and drops it
      after the second miss);
    - `no_flicker_on_cadence_5` (alternating hit and miss at cadence 5: the shape is present on every frame);
    - `stickiness_survives_hitch` (a 100 ms frame between probes: still present);
    - `lift_off_drops_within_two_cycles` (no stuck open hand).
  - `finger_ik_check`: `current_pose_sweep_reaches_past_tip` (a flat hand's sweep points along the finger and ends 2 cm
    past the tip).
  - `finger_collider_routing_check.py`:
    - `held_slab` is used for thin props in `finger_collider`;
    - the palm cast is posted every frame for a free hand;
    - the weapon and native-hand skip (`touch_side`) is unchanged;
    - the cast count per free hand is ≤ the constant `finger_probe_casts`.
- [ ] **Step 2: Run** `--only 'finger_probe_cache|finger_ik|finger_collider'`. Expect a FAIL.
- [ ] **Step 3: Implement.** `build/no-finger-surface` restores the v609 probes, budget and TTL.
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Measure** per the budget rule. Set `finger_solve_budget` and `finger_probe_casts` from the numbers, and
  commit them with `g5-budget.md` summarised in the commit body.
- [ ] **Step 6: Commit** `feat(fingers): free fingers find and rest on tables, shelves, walls and thin props (G5)`.

### Task 13: G6 measure first — `guard_probe` experiments (Tier B)

Bonelab: none (no NPC grab in the study). This runs the plan A Task 14 probe, which has never been run.

**Files:**
- Modify: `src/guard_probe.hpp`: `GuardSample` gains `yaw_deg`, from model axes `+0x186c` row 0 (atan2 in the ground
  plane), plus the header and row columns.
- Modify: `tools/linux/scenarios/guard_probe.py`: a teleport segment (Task 1) to the recorded spot before the squeeze.
- Modify: `docs/BONELAB-FEEL-GUARD-PULL-RE.md` (the experiment table and the lever decision table)
- Test: `tests/guard_probe_check.cpp` (the yaw column and the row format)
- Ledger: `g6-probe.md`

**Interfaces:**
- Produces: a lever verdict in the doc, by these rules:
  - **stagger:** usable if, after `shove`, `bodies_dynamic` stays 0 for all 6 s, the position moves ≥ 2 cm, and the model
    z never drops more than 0.3 m;
  - **alert:** usable if `unaware` goes 0 with `unaware_ok` 1 within 1000 ms;
  - **pull:** with `teleport_step 0.05 5`, usable if each step moves him ≥ 4 cm toward the hand, he returns less than
    50% toward the start within 2 s (no snap-back), and no sample-to-sample yaw change exceeds 10° (no yaw pop). Else the
    pull is `none`.
  - With no usable guard spot, the verdict is `no test save`, and Task 14 uses the conservative fallback.

- [ ] **Step 1: Add the yaw column.** Its test checks that the header and row carry `yaw_deg` and that a −179° to 179°
  wrap reads as a 2° change in the checker helper. Run `--only 'guard_probe'`: FAIL, implement, PASS.
- [ ] **Step 2: Find the spot.** In an `idle` session, list living NPCs with `tools/entities.py` and
  `tools/vr_selftest.py --near npc-front`. Pick an unaware guard with no other awake guard within 10 m. Record the
  `setviewpos` line, 0.4 m in front of his chest, in `g6-probe.md`. Try the user's saves, then `--test-save` saves.
- [ ] **Step 3: Run `shove`.** Write `shove` into the mod home's `build/guard-probe`, then run
  `linux/d2vr-linux sim --candidate-dll … --scenario guard_probe --lead 45 --seconds 60`. Confirm the knob file is gone
  afterwards.
- [ ] **Step 4: Run** `teleport_step 0.05 5` the same way.
- [ ] **Step 5: Record** both CSV summaries and the verdict in the doc and the ledger.
- [ ] **Step 6: Commit** `docs(guard): living-guard lever measurements (shove, teleport_step) and verdict (G6)`.

### Task 14: G6 the living-NPC hold

Bonelab §1.2: the held body hangs on the physical hand. Here the hand seats on the grasp bone. The hard-pull threshold
2.2 m/s is today's yank.

**Files:**
- Create: `src/living_npc_hold.hpp` (pure policy)
- Modify: `src/native_rigid_body.hpp`: `living_bone(...)` reads a keyframed bone (`(flags&7)==6`) of a living NPC
  (`!(npc+0xd1 & 0x28)`): its position and radius only, never a `NativeRigidBody`.
- Modify: `src/native_grab.hpp`:
  - discovery records living NPC entities within 3 m (at most 8 per lane);
  - `focus_body` scores their bones 2, 3, 5, 6, 8 and 9 in the one list (priority 1.0, the 0.12 m grip-alone gate);
  - the lane gains `std::optional<LivingHold> living`;
  - `hold_anchor` (~953) publishes the bone as the anchor (`HoldAnchorView::living=true`);
  - `living_held(side)`;
  - `reserve_npc(side,entity_id)` for choke and takedown priority.
- Modify: `src/native_weapon_hands.hpp`: `yank` (~382) accepts an explicit target (`Request::target`), so the hard pull
  ragdolls the held NPC; and `Op::living_start` runs `shove`. The pull reuses `teleport` only when the Task 13 verdict
  allows it. That is a compile-time constant `living_pull_enabled`, set from the verdict.
- Modify: `src/dxgi_probe.cpp`:
  - the yank site (~8131) also fires when `native_grab.living_held(h)` and the hand speed away from him is > 2.2 m/s;
  - the reserve feed: the right hand's `choke_offer->spawn`, `control_active_choke`, and either hand's takedown offer for
    that NPC;
  - the finger block (~5407) adds the bone capsule (`classify_held` with the bone) for a living hold;
  - the knob `build/no-living-hold`.
- Test: `tests/living_npc_hold_check.cpp` (create, register); `tests/living_hold_routing_check.py` (create, register)

**Interfaces:**
- Produces:
  ```cpp
  struct LivingBone {std::uintptr_t entity{};std::uint32_t entity_id{};int bone{};Vec3 position{};double radius{};};
  template<class Read> std::optional<LivingBone> living_bone(Read&,std::uintptr_t base,std::uintptr_t entity,int bone);
  struct LivingTick {Vec3 controller{},bone{};Vec3 hand_velocity{};float grip{};double dt{};bool alive{};std::uint64_t epoch{};};
  struct LivingStep {Vec3 anchor{};std::optional<Vec3> pull_step;bool release{},hard_pull{};};
  class LivingHold {public:
      LivingHold(LivingBone grasp,Vec3 controller,std::uint64_t epoch,bool pull_enabled);
      LivingStep tick(const LivingTick&);};
  ```
- `tick`:
  - `release` when the grip is < 0.35, the epoch changed, `!alive`, or the controller stays > 0.35 m from the bone for
    0.2 s;
  - `hard_pull` when the hand speed along `controller − bone` exceeds 2.2 m/s;
  - `pull_step` only when `pull_enabled` and the gap is > 6 cm: horizontal, ≤ 5 cm per 100 ms, toward the controller.
- At start the NPC gets `shove` (the stagger and, per Task 13, the alert). If the alert lever is `none`, the release notes
  say "stagger only".
- After a `release`, the drawn hand returns to the controller with the 0.12 s ease.
- After `hard_pull`, the hold ends and the yank runs on that NPC: shove, `START_RAGDOLL` (its budget and
  `build/no-ragdoll-posts` stay), the pull velocity, then today's `yanked` limb hold. Today's fist yank without a hold is
  unchanged.
- **Conservative fallback** (Task 13 verdict `no test save` or pull `none`): `pull_enabled=false`. The hold, shove and
  tether ship, marked for the headset run in the checklist.

- [ ] **Step 1: Write the failing tests.**
  - `living_npc_hold_check`:
    - `tether_breaks_after_02s_beyond_035` (0.36 m for 0.19 s holds; 0.21 s releases);
    - `grip_release_frees`;
    - `hard_pull_at_22_away_only` (2.3 m/s away → `hard_pull`; 2.3 m/s toward him → none);
    - `pull_step_bounded_horizontal` (a gap of 0.2 m with a vertical part → a step ≤ 5 cm per 100 ms with no z);
    - `no_pull_when_disabled`;
    - `no_pull_inside_6cm`;
    - `epoch_change_releases_living_hold`;
    - `living_target_dies_releases_without_write` (`alive=false` → `release`, and no step).
  - `living_hold_routing_check.py`:
    - `NativeGrab::write` is never reached with a living lane (the living lane has no `held` body);
    - `reserve_npc` is fed from `choke_offer` and the takedown offer before `filter_native_grip`;
    - the yank site's living branch passes `r.target`;
    - `build/no-living-hold` disables the living candidates;
    - a mutation that drops the reserve check must fail.
- [ ] **Step 2: Run** `--only 'living_npc_hold|living_hold_routing|native_grab|guard_probe'`. Expect a FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expect a PASS.
- [ ] **Step 5: Commit** `feat(grab): hold an awake guard or civilian by the collar or arm; a hard pull ragdolls him (G6)`.

### Task 15: Diagnostics completion and the sim scenarios (Tier B)

Bonelab: none.

**Files:**
- Modify: `src/native_grab.hpp` / `src/dxgi_probe.cpp`. The status `grab_focus` block gains:
  - `press_to_held_ms` (last and max per side);
  - the last thin prop's `{entity_id,path:"pickable"|"rigid",bounds:BodyReject}`;
  - `obstruction_required`;
  - `loot_fix_branch`;
  - `living:{held,bone,tether_ms,pull_steps,hard_pulls}`.
- Create: `tools/linux/scenarios/paper_grab.py`, `corpse_middle_grab.py`, `hand_on_table.py`, `guard_hold.py`
- Modify: `tools/linux/bonelab_run_check.py` (a check per scenario); `tests/bonelab_scenarios_check.py` (registration)
- Test: `tests/grab_focus_status_check.py` (create, register: field names and that the JSON stays valid with no hand)
- Ledger: `tier-b.md`

**Interfaces:**
- Produces: `bonelab_run_check.py RUN --scenario <name>` passes when:
  - `paper_grab` (a paper stack on a desk among props; one press 120 ms before contact, then one press on contact, five
    times each, grip alone and fist): every press catches the paper (`grab_focus.id` = the paper id, with
    `buffered_grabs` ≥ 5), there are no wrong picks, `press_to_held_ms` is ≤ 2 ticks on ≥ 90% of presses, and
    `grab_focus.palm_dot` is > 0.7 on both sides (Review Focus 1);
  - `corpse_middle_grab` (a reach to the torso): the bind is on bone 0 or 1 in at least 4 of 5 reaches;
  - `hand_on_table` (a flat hand pressed on a table, then slid over it): every drawn fingertip is ≤ 5 mm below the probed
    surface while slow and ≤ 15 mm while fast, `stuck_frames` is 0 once lifted, and `finger_ik.avg_us` is within the Task
    12 budget;
  - `guard_hold` (at the Task 13 spot):
    - a grip on his collar starts the hold;
    - `bodies_dynamic` stays 0 during the hold;
    - `unaware` goes false (if the Task 13 alert verdict is usable);
    - a slow move pulls him or doesn't, per the verdict;
    - a fast pull ragdolls him, and the hand holds a limb (`yank_holds` +1);
    - a release frees him;
    - a reach behind him with the right hand chokes him (the choke counter +1, and no living hold).

- [ ] **Step 1: Write `grab_focus_status_check.py`** and run `--only 'grab_focus_status'`: FAIL, implement, PASS.
- [ ] **Step 2: Find the spots** (desk with paper, corpse) as in Task 1 Step 5. Record them in `tier-b.md`.
- [ ] **Step 3: Write the four scenarios and their checks.**
- [ ] **Step 4: Run each** with `linux/d2vr-linux sim --candidate-dll build/candidate/d2vr_render_probe_arm_v611.dll --scenario <name> --lead 45 --seconds 120`.
  Expect a PASS from each check. If the Task 8 decision rule fires, implement the second pass and re-run `paper_grab`.
- [ ] **Step 5: Regression.** `loot_reach`, the existing corpse scenarios, `finger_touch`, `run_hands` and `sword_regrab`
  still pass their checks. Compare the frame time against v609 in `run_hands` (median within 0.1 ms, p99 within 0.3 ms)
  and record it.
- [ ] **Step 6: Commit** `test(grab): grab_focus diagnostics and the paper, corpse-middle, hand-on-table and guard-hold scenarios`.

### Task 16: The v611 checklist and full verification

**Files:**
- Create: `docs/VR-v611-TEST-CHECKLIST.md`
- Modify: `docs/PLAYING.md` (grip alone grabs; one line each for living-NPC holds and fingers on surfaces)

- [ ] **Step 1: Write the checklist.** Use the spec's Tier C lines, plus these:
  - the left hand alone for every pickup line;
  - two hands reaching for the same coin and the same corpse;
  - a pause during a guard hold;
  - a block with the right grip next to a table and next to a crate (it must block, not grab).

  List every Task 4 known gap (for example drawers) and every Task 13 or 14 fallback ("stagger only", "no pull") as a known
  gap with its trace, also for the release notes. Tell the user a headset session is needed.
- [ ] **Step 2: Full harness.** `python3 tools/linux/run_build_cmd.py --keep-going candidate d2vr_render_probe_arm_v611.dll`.
  Expect no new failures against the gate in Global Constraints. Then the launcher tests (all pass) and `linux/tests` (OK).
- [ ] **Step 3: Commit** `docs: v611 hands checklist (palm aim, grabs, fingers, living NPCs)`.

## Self-Review

- **Spec coverage:**
  - G1: Tasks 1, 3, 4 and 10 (take close).
  - G2: Tasks 2, 3 and 5.
  - G3: Tasks 5–8.
  - G4: Tasks 9 and 10.
  - G5: Tasks 11 and 12.
  - G6: Tasks 13 and 14.
  - Diagnostics: Tasks 1 (loot trace), 5 and 15 (`grab_focus`), 9, 11 and 12 (`finger_ik`).
  - Tier A: each task's tests. Tier B: Tasks 1, 4, 12, 13 and 15. Tier C: Task 16.
  - Anti-goals:
    - LOS kept: Task 4;
    - behind the knuckles: Tasks 2 and 3;
    - the prompt matches the take: Task 4 Step 5;
    - chatter: Task 2;
    - jitter and stuck fingers: Tasks 11 and 12;
    - the buffer never reaches out: Task 5;
    - frame rate: Tasks 12 and 15;
    - mushy hands (the blend is on the pose only): Task 9;
    - choke first: Tasks 6 and 14;
    - snap-back: Task 13.
- **Types:**
  - `PalmProbe`, `PalmTarget`, `PalmKind` and `palm_cost` come from Task 2, and `FocusScore` from Task 5. Task 6 consumes
    them unchanged.
  - `GrabTuning::priority(kind,bone)` serves Tasks 3, 5, 7 and 14.
  - `GripAloneChoice` exists only in Task 6.
  - `HoldAnchorView::living` exists only in Task 14.
- **Proportion:** code appears only as interface blocks. The bodies are left to the implementer, within the stated rules.

## Decisions made where the spec left room

- **The hover radius is 0.25 m.** Its centre sits 0.2 m in front of the palm, and its far edge is the 0.45 m gate. Distance
  and direction are measured from the palm point (Bonelab measures palm → target). Bonelab's own value was not extracted.
- **"Palm axis"** is read as the grip forward axis (basis[0]) and **"palm direction"** as the palm normal (±basis[1] by side).
- **Approach** is `clamp(1 − 0.2·v_close, 0.8, 1.2)`.
- **Loot** includes native carryables (`takeable()`, as today's loot bonus), so bottles keep their current preference.
- **Corpse candidates** are all bones except the head. The collar keeps its knob and squeeze rule. Living bones are the
  collar, upper arms and forearms.
- **The 20 × 20 cm slab:** the code's slab is already 24 × 24 cm (`half=.12`; the spec's "12 × 12" read the half extent
  as the size), so the size stays.
- **Eased opening** is bounded by the surface tolerance, as a maximum angular lag, so eased fingers never sink more than
  5 mm (or 15 mm when fast).
- **The Linux sim has no teleport**, so Task 1 adds a scripted console line (script v8), unless the v610 camera work
  already added one.
- **v611:** the version and the checklist name follow the user's instruction over the spec's v610. The harness baseline is
  v610's result.
