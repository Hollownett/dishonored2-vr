# Arm and weapon collisions, body holsters, gadgets and the Heart back to the inventory: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Forearms and upper arms stop at the world (the elbow bends around it), held weapons never pass through the owner's other arm,
body holsters follow the body, and grenades, mines and the Heart can be put back into the inventory at a belt pouch / chest pocket and
taken out again; each proven by simulated A/B sessions in both games.

**Architecture:** Pure policy headers with host checks (`src/arm_collision.hpp`, `src/holster_rig.hpp`, an extended
`src/hand_weapon_contact.hpp`), the solver applies the elbow swivel (`src/body_ik.hpp`, `src/full_body_driver.hpp`), and
`src/dxgi_probe.cpp` posts the natural-arm casts through `NativeInteractionAim`'s existing segment slots and wires the holster rig into the
existing grip state machines. DOTO gets everything through `tools/doto/port.py`.

**Tech Stack:** C++17 DLL (clang-cl + xwin + Wine harness on Linux), Python 3 scenario scripts, Proton simulated sessions.

**Spec:** `docs/superpowers/specs/2026-11-26-arm-weapon-collisions-holsters-design.md`
(research: `docs/research/2026-11-26-holsters-collisions/*.md`).

## Global Constraints

- Build a check: `python3 tools/linux/run_build_cmd.py --only '<check_name>'`; DOTO DLL `python3 tools/linux/build_doto.py <ver>`; D2 DLL
  `python3 tools/linux/build_doto.py <ver> --game d2`. Every changed check passes under `/W4 /WX`; clang-cl rules in `tools/linux/README.md`
  ("Known differences from cl": no nested-struct static initialisers in-class, `alignas(N) static`, no `__try` inside `__finally`).
- DOTO DLL versions continue from 145786, D2 from 1484. Candidates are staged under `~/d2vr-test-builds/candidates/v<ver>/` carrying the
  INSTALLED DLL's file name (DOTO `d2vr_render_probe_arm_v145752.dll`, D2 `d2vr_render_probe_arm_v1467.dll`).
- Sims: `tools/linux/sim_serial.sh <log> [--game doto] sim --scenario X [--settle 45] --lead 54 --knob diagnostics --candidate-dll <dll>`
  (DOTO needs `--settle 45`; D2 the default settle); one at a time; never compile while a sim runs; never name a game exe on a command line.
- Every new knob gets a default-on row in `tools/default_features.json` (user requirement since v612). Hypothesis fixes ship default-off.
- Never edit `build/doto-src/`. DOTO-only RVAs need a keep-as-is rule in `tools/doto/offsets.json`.
- Helpers: Sonnet 5.5 (`model: sonnet`) implements pure headers and checks and reviews; Haiku 5.5 (`model: haiku`) writes scenarios, knob
  rows and docs; the coordinator (Opus) owns `src/dxgi_probe.cpp`, builds and sims. Never Fable.
- "Fixed / works" claims need an A/B sim against v145785 (DOTO) / v1483 (D2) or the shadow knob.
- Units: metres, seconds, world Z up; grips 0..1 with press >= 0.6 / release < 0.4 (the mod's grip hysteresis everywhere).

## Review Focus

- Elbow flip-flop: two planes on opposite sides of the elbow circle (a narrow gap) must not alternate theta between two far solutions frame
  to frame. Test in Task 2 (`corridor_no_flip`).
- Hand far beyond reach (arm fully stretched, rho ~ 0): the swivel cannot help, must report residual and keep theta at the last value, no NaN.
  Test in Task 2 (`straight_arm_residual`).
- A grenade let go at the belt DURING a throw swing (hand faster than 1 m/s) must be thrown, never put back. Test in Task 4 (`fast_release_never_stows`).
- An empty hand that closes at the belt to grab a body / bottle lying there must not equip a gadget (grab candidate wins). Test in Task 4
  (`grab_candidate_suspends_draw`) and in the wiring (Task 7: `suspended` includes grab candidates).
- Planes from a door that opens: the plane must expire within 0.15 s once casts stop hitting it (no elbow frozen bent). Test in Task 2
  (`plane_expires`).

---

## Phase 0: Lab facts

### Task 1: Lab spots and the living-guard contact experiment (E1)

**Files:**
- Create: `tools/linux/scenarios/npc_contact_probe.py`, `tools/linux/scenarios/lab_spot_scan.py`
- Create: `docs/research/2026-11-26-holsters-collisions/lab-facts.md`

**Interfaces:**
- Produces: in `lab-facts.md`, for each game's lab save: a `setviewpos x y z yaw` spot with a wall within 0.30-0.45 m of the right
  shoulder (wall on the right), a spot facing a wall at 0.6 m, a table/desk spot (D2: reuse `loot_reach.py`'s desk), and E1's answer:
  which hit kinds (`weapon_collision.contact.hits` [world,npc,dynamic,self,held]) and hand clamp contact classes a LIVING guard produced.

- [ ] **Step 1:** `lab_spot_scan.py`: after the lead, `setviewpos` is NOT used; at the save's start position turn in 30-degree steps (snap turn
  input or console `setviewang`), and after each step run `d2vr trace 3` (result lines in `artifacts/vr-command-result.txt`), plus one
  `d2vr trace 3 down`. Also print the player position: console `getviewpos` if the engine has it, else the status field the mod writes
  (`gameplay`/player block). Run on DOTO (v145785 candidate) and D2 (v1483).
- [ ] **Step 2:** Pick the spots from the trace distances; confirm each with a 20 s sim that teleports there and takes `d2vr spacewarp capture 2`
  (PNG via `tools/linux/rgba_to_png.py`). Write them into `lab-facts.md`.
- [ ] **Step 3:** `npc_contact_probe.py` (DOTO, v145785): `god`, `d2vr guard behind 0.7`, open right hand pushed slowly 0.5 m forward into
  the guard's back (0.1 m/s) and held 2 s; then a shoulder draw (zone as `holster_draw.py`), the blade pushed slowly into him and held 2 s.
  Read `xr-live-status.json` `weapon_collision.contact.hits`, `hand_clamp.contact_frames`, `hand_clamp` state; record in `lab-facts.md`.
- [ ] **Step 4:** Commit the two scenarios and `lab-facts.md` ("Lab: wall/table spots and the living-guard contact experiment (E1)").

## Phase A: Arm collision

### Task 2: `ArmCollision` (pure planes, swivel solve, follower)

**Files:**
- Create: `src/arm_collision.hpp`, `tests/arm_collision_check.cpp`
- (The coordinator registers `arm_collision_check` in `tools/build-msvc.cmd` before dispatch.)

**Interfaces:**
- Produces (namespace `d2vr`, header-only, `#include "tracked_pose.hpp"` for `Vec3`/math):
  - `struct ArmCollisionConfig {double forearm_radius=.045, upper_radius=.055, forearm_skip=.05, upper_skip=.04, plane_seconds=.15,
    same_normal_deg=15, same_offset=.03, edge_fraction=.5, skin=.005, max_swivel_deg=110, max_inward=.10, engage_rate=12,
    release_tau=.08, release_rate=4, violation_engage=.01, jump=.35;};`
  - `struct ArmPlane {Vec3 point{},normal{};double radius{},last_hit{};};` (normal unit, toward free space)
  - `struct ArmGeometry {Vec3 shoulder{},elbow{},wrist{};bool valid{};};` (the NATURAL elbow, world)
  - `struct ArmCast {Vec3 from{},to{};double radius{};};`
  - `struct ArmHit {Vec3 point{},normal{};bool initial_overlap{},normal_valid{true},solid{true};};` (`solid`: world or a static/keyframed entity)
  - `struct ArmCollisionStats {std::uint64_t casts{},hits{},planes_made{},refreshed{},expired{},edge_discards{},overlaps{},resets{};};`
  - `class ArmCollision { public: static constexpr std::size_t max_planes=4; explicit ArmCollision(ArmCollisionConfig={});`
    `std::array<std::optional<ArmCast>,2> casts(const ArmGeometry&) const;` ([0] forearm from wrist+skip toward elbow, [1] upper arm from
    shoulder+skip toward elbow; none for an invalid geometry or a segment shorter than skip+2 cm)
    `void observe(unsigned side,const ArmGeometry& cast_geometry,const std::array<std::optional<ArmHit>,2>& hits,double now);`
    `std::size_t planes(unsigned side,std::array<ArmPlane,max_planes>& out,double now) const;` (unexpired only)
    `void reset(unsigned side); void reset(); const ArmCollisionStats& stats(unsigned side) const; const ArmCollisionConfig& config() const; };`
  - `Vec3 swivel_elbow(Vec3 shoulder,Vec3 wrist,Vec3 natural_elbow,double theta);` (rotate about unit(wrist-shoulder), right-hand rule)
  - `double swivel_violation(Vec3 shoulder,Vec3 wrist,Vec3 elbow,const ArmPlane* planes,std::size_t n,double skin);` (max over planes of
    `radius+skin-dot(elbow-point,normal)`, clamped >= 0)
  - `struct SwivelResult {double theta{},residual{};bool feasible{true},constrained{};};`
  - `SwivelResult solve_swivel(Vec3 shoulder,Vec3 wrist,Vec3 natural_elbow,Vec3 outward,const ArmPlane* planes,std::size_t n,const ArmCollisionConfig&);`
  - `double follow_swivel(double current,double target,double current_violation,double dt,const ArmCollisionConfig&);`

Rules (spec 5.2/5.3): `observe` turns a hit into a plane only when `solid && !initial_overlap && normal_valid` and the cast's free end
(wrist for [0], shoulder for [1]) lies on the free side by >= `edge_fraction*radius` (else `edge_discards`); a plane within
`same_normal_deg` and `same_offset` of an existing one refreshes it (point, normal, last_hit); more than `max_planes`: the oldest goes.
A wrist or shoulder step > `jump` between consecutive observe calls resets the side. `solve_swivel`: 5-degree grid over
[-max_swivel_deg, max_swivel_deg] refined to 1 degree around the best; feasible = violation 0 and `dot(E(theta)-natural,outward) >=
-max_inward`; pick the feasible theta with the smallest |theta|; none: the smallest worst violation (ties: smaller |theta|), `feasible=false`,
`residual` = that violation; `constrained` = n>0. `follow_swivel`: if |target| > |current| or current_violation > violation_engage: move
toward target at <= engage_rate*dt; else critically damped toward target with release_tau, step <= release_rate*dt.

- [ ] **Step 1: Write `tests/arm_collision_check.cpp`** with these cases (assert-based like `tests/hand_weapon_contact_check.cpp`; print
  `arm_collision_check ok`). Geometry: right arm, shoulder (0,-0.2,1.45), wrist (0.45,-0.25,1.25), natural elbow from the Boneworks-like
  down/out pole, bones 0.30/0.28.
  - `side_wall_swivels`: plane x-axis wall at y=-0.30 (normal +y) under the natural elbow (elbow y=-0.33): solve gives |theta|>0, violation 0,
    elbow y >= -0.30+0.055+0.005-1e-6, feasible.
  - `table_under_elbow`: plane z=1.10 normal +z with the natural elbow at z=1.05: elbow rises above 1.10+r+skin.
  - `corner_two_planes`: side wall + table together: feasible, both satisfied.
  - `straight_arm_residual`: wrist at full reach (distance = 0.58 m): rho ~0, any plane through the elbow gives feasible=false, residual>0,
    finite theta.
  - `inward_limit`: a plane that only the inward side clears beyond 0.10 m: infeasible, theta within limits.
  - `edge_discard`: a forearm hit whose plane puts the wrist inside: no plane, `edge_discards`==1.
  - `plane_expires`: a plane hit at t=0 is gone at t=0.16, present at 0.14.
  - `refresh_not_duplicate`: the same wall hit twice 10 ms apart (normal 3 deg off, 1 cm offset): one plane, `refreshed`==1.
  - `corridor_no_flip`: two walls 0.16 m apart around the elbow so feasible thetas exist on both sides: 200 frames with alternating
    hit/miss answers: |theta change| per frame <= engage_rate*dt and the side of the solution never changes.
  - `release_eases`: after the plane expires, follow_swivel takes >= 3 frames (dt 1/90) to go from 1.0 rad to < 0.05 rad, never overshoots.
  - `engage_fast`: a new plane needing 0.6 rad: reached within 5 frames.
  - `degenerate_inputs`: NaN shoulder, zero-length arm: casts empty, solve returns theta 0 and feasible, no NaN.
- [ ] **Step 2:** Run `python3 tools/linux/run_build_cmd.py --only 'arm_collision_check'`. Expected: FAIL (header missing).
- [ ] **Step 3:** Implement `src/arm_collision.hpp` per the interfaces and rules above.
- [ ] **Step 4:** Run the check again. Expected: PASS (`arm_collision_check ok`).
- [ ] **Step 5:** Commit `src/arm_collision.hpp tests/arm_collision_check.cpp` ("Arm collision policy: natural-arm casts, half-space planes, elbow swivel").

### Task 3: The solver draws the swivelled elbow; the driver publishes the arm joints

**Files:**
- Modify: `src/body_ik.hpp` (Inputs, Output, ArmState, the arm solve around `boneworks_elbow_pole` / `solve_arm`, ~lines 1050-1070)
- Modify: `src/full_body_driver.hpp` (Tracking, Diagnostic, the solve loop ~960, `arm_frozen` ~625)
- Test: `tests/body_ik_check.cpp`, `tests/full_body_driver_check.cpp` (add cases)

**Interfaces:**
- Consumes: Task 2 (`ArmPlane`, `ArmCollisionConfig`, `solve_swivel`, `follow_swivel`, `swivel_elbow`, `swivel_violation`).
- Produces:
  - `body::ArmCollisionInput {ArmPlane planes[ArmCollision::max_planes]{};std::size_t count{};bool enabled{},shadow{};}` in `body_ik.hpp`;
    `Inputs::arm_collision[2]`; `Output::swivel[2], swivel_residual[2]` (rad, m), `Output::swivel_constrained[2]`.
  - `Tracking::arm_collision[2]` (world planes as published; the solve space IS the publication's world, as the grips) copied into
    `Inputs` for every substep; planes are dropped (count 0) on a rig turn (`turn_rig`).
  - `Diagnostic::shoulder[2], elbow[2], natural_elbow[2]` (world `Vec3`, from the arm upload: `<side>_arm_jnt`, `<side>_elbow_jnt`, and
    `swivel_elbow(S,W,E,-swivel)`), `Diagnostic::swivel[2]`, `swivel_residual[2]`, `arm_joints_valid[2]`.
- [ ] **Step 1: Write the failing cases**: `body_ik_check` `swivel_moves_only_the_elbow` (a plane under the natural elbow: wrist and
  shoulder unchanged within 1e-6, elbow clears the plane by >= r+skin after 10 solves at dt 1/90), `shadow_reports_only` (shadow: elbow
  identical to no planes, Output::swivel non-zero), `no_planes_identical` (enabled, count 0: joints bit-identical to disabled);
  `full_body_driver_check` `publishes_arm_joints` (after a solve and an arm upload: shoulder/elbow/natural_elbow finite, natural_elbow ==
  elbow when no planes).
- [ ] **Step 2:** Run `python3 tools/linux/run_build_cmd.py --only 'body_ik_check|full_body_driver_check'`. Expected: FAIL.
- [ ] **Step 3:** Implement: in the arm solve compute the natural elbow with the unmodified pole, then (enabled) `solve_swivel` with the solver's
  `outward`, `follow_swivel` on `ArmState::swivel` (new, reset with the arm state), and unless shadow re-solve with the pole turned by
  `swivel` about `unit(toward-shoulder)` (never store the turned pole into `state.pole`). Raw-hand (`raw_arm`) solves ignore collision.
- [ ] **Step 4:** Run the checks. Expected: PASS, and every other case in both checks still passes.
- [ ] **Step 5:** Commit ("Solver: elbow swivel from arm planes; driver publishes shoulder/elbow/natural elbow").

## Phase B: Weapons

### Task 4 is in Phase C (numbered for the review focus); Task 5 follows.

### Task 5: A held weapon never passes through the other forearm

**Files:**
- Modify: `src/hand_weapon_contact.hpp`
- Test: `tests/hand_weapon_contact_check.cpp` (registered in `tools/build-msvc.cmd` by the coordinator)

**Interfaces:**
- Produces: `HwcSide::has_forearm{}`, `HwcSide::forearm{}` (`HwcCapsule`); `HwcResult::forearm{}` (bool: the resolved pair involved a forearm).
- [ ] **Step 1: Failing cases**: `blade_through_forearm` (side 1 blade crosses side 0's forearm, side 0 has no weapon: only correction[1]
  non-zero, it separates by the overlap, capped 0.05), `hand_through_forearm` (side 0 hand inside side 1's forearm: only correction[0]),
  `forearms_cross_free` (the two forearms overlap, hands far: no contact), `deepest_pair_wins` (a hand-hand overlap 1 cm and a blade-forearm
  overlap 3 cm: the forearm pair is resolved), and all existing cases unchanged.
- [ ] **Step 2:** Run `python3 tools/linux/run_build_cmd.py --only 'hand_weapon_contact_check'`. Expected: FAIL.
- [ ] **Step 3:** Implement: pairs (side i hand/weapon) x (side j forearm), j != i, the same penetration test; the forearm side's weight is 0.
- [ ] **Step 4:** Run the check. Expected: PASS.
- [ ] **Step 5:** Commit ("Hand/weapon contact: the other arm's forearm is solid").

## Phase C: Holsters and putting things back

### Task 4: `HolsterRig` (pure places, put-back rule, squeeze-and-pull draw)

**Files:**
- Create: `src/holster_rig.hpp`, `tests/holster_rig_check.cpp`
- Modify: `src/vr_controls.hpp` (`TempleReach::update` gains `std::array<bool,2> also_in={}`: in = sphere || also_in[i]), `tests/vr_controls_check.cpp`

**Interfaces:**
- Produces (namespace `d2vr`):
  - `enum class HolsterPlace : unsigned char {none,shoulder_left,shoulder_right,hip_left,hip_right,pouch_left,pouch_right,heart_pocket,count};`
  - `enum class HeldKind : unsigned char {empty,sword,gun,gadget,heart,other};`
  - `struct BodyFrames {Vec3 pelvis{},pelvis_forward{},pelvis_left{};bool pelvis_valid{};Vec3 chest{},chest_forward{},chest_left{};bool chest_valid{};Vec3 head{},head_forward{};bool head_valid{};};` (forward/left level, unit)
  - `struct PlaceSpec {unsigned frame{};Vec3 offset{};double radius{},exit{};bool mirrored{};};` (frame 0 pelvis, 1 chest; offset = (forward,
    side, up); mirrored: side points to the place's own side, else to the body's left)
  - `struct HolsterConfig {std::array<PlaceSpec,static_cast<std::size_t>(HolsterPlace::count)> places; double pull_commit=.07,
    pull_seconds=1.2,cooldown=.6,stow_max_speed=1.0;};` defaults (spec table 7.1): shoulder {1,(-.02,.17,.17),.16,.03,mirrored},
    hip {0,(0,.21,-.05),.17,0,mirrored}, pouch {0,(.14,.09,0),.13,.03,mirrored}, heart_pocket {1,(.12,.07,-.02),.13,.03,not mirrored}.
    Head fallback (no fresh frame): hip head+(0,.20,-.62), pouch head+(.14,.09,-.62), heart pocket head+(.12,.07,-.32), shoulders none.
  - `std::optional<HolsterConfig> parse_holster_places(std::string_view text,const HolsterConfig& base);` (lines `name fwd side up radius`,
    `#` comments; unknown name or non-finite -> nullopt; names `shoulder hip pouch heart_pocket`)
  - `class HolsterRig { public: explicit HolsterRig(HolsterConfig={});`
    `std::optional<Vec3> centre(HolsterPlace,const BodyFrames&) const;`
    `struct InPlace {HolsterPlace place{};double distance{};};`
    `InPlace classify(unsigned hand,Vec3 hand_pos,HeldKind held,const BodyFrames&);` (nearest place that is relevant to `held`: sword ->
    shoulders, gun -> hips, gadget/heart -> every place, empty -> pouches + heart pocket (draw places); hysteresis +exit while the same place
    held last call)
    `bool put_back(HeldKind held,HolsterPlace place,bool armed,double speed_mps) const;` (gadget: any place, !armed, speed < stow_max_speed;
    heart: any place; else false)
    `struct DrawIn {float grip{};bool empty_hand{},suspended{},have_item{};Vec3 hand{};double now{};HolsterPlace place{};};`
    `struct DrawOut {bool armed{},committed{},cancelled{},empty{};HolsterPlace place{};};`
    `DrawOut draw(unsigned hand,const DrawIn&);` (draw places: pouches -> gadget, heart_pocket -> heart; arming only on a press EDGE inside the
    place with an empty hand, not suspended, not cooling; `have_item` false -> `empty` once, no arming; commit when the hand is >= pull_commit
    from where it armed, grip still >= 0.4, within pull_seconds; release or timeout -> cancelled; suspension while armed -> cancelled)
    `void note_action(unsigned hand,double now);` `bool cooling(unsigned hand,double now) const;` `void reset(); };`
- [ ] **Step 1: Failing cases** in `tests/holster_rig_check.cpp`: `pelvis_places` (centres from a pelvis frame match the table, mirrored per
  side), `head_fallback` (no frame: hip/pouch/pocket from the head, shoulders none), `nearest_wins` (hand between hip_left and pouch_left:
  the nearer), `kind_filter` (a sword hand at the pouch: none; a gadget hand at the hip: hip_left), `exit_hysteresis` (inside at 0.12 from the
  pouch centre, then 0.15: still pouch, then 0.17: none), `fast_release_never_stows` (gadget, pouch, speed 1.2: false; 0.5: true; armed: false),
  `heart_any_place`, `pull_commits` (press inside, move 0.08 within 0.5 s: committed once), `pull_cancel_on_release`, `pull_timeout`
  (1.3 s still: cancelled), `edge_only` (grip already 1.0 when entering: never armed), `grab_candidate_suspends_draw` (suspended at the edge:
  not armed; suspended while armed: cancelled), `empty_pouch` (have_item false: `empty` once per press), `cooldown` (a draw then a press
  0.3 s later: nothing; 0.7 s: armed), `parse_places` (valid text changes the pouch offset; a bad line -> nullopt).
  `tests/vr_controls_check.cpp`: `temple_reach_also_in` (also_in true outside the sphere: in, latch and consume behave as inside).
- [ ] **Step 2:** Run `python3 tools/linux/run_build_cmd.py --only 'holster_rig_check|vr_controls_check'`. Expected: FAIL.
- [ ] **Step 3:** Implement `src/holster_rig.hpp` and the `TempleReach` parameter (default `{}` keeps every caller's behaviour).
- [ ] **Step 4:** Run the checks. Expected: PASS.
- [ ] **Step 5:** Commit ("Holster rig: body places, put-back rule, squeeze-and-pull draw; TempleReach union input").

### Task 6: The chest frame in the driver's diagnostic

**Files:**
- Modify: `src/full_body_driver.hpp` (`Diagnostic`: `Xf chest{}; Vec3 chest_forward{},chest_left{};` set beside `pelvis` from
  `center_spine03_jnt` in the body upload, ~line 568), `tests/full_body_driver_check.cpp` (`publishes_chest_frame`)
- [ ] Steps: failing case -> run `--only 'full_body_driver_check'` FAIL -> implement -> PASS -> commit ("Driver: chest frame for body holsters").
  (May be done together with Task 3 by the same implementer.)

## Phase D: Wiring, sims (coordinator)

### Task 7: `dxgi_probe.cpp` wiring, knobs, status, launcher rows

**Files:**
- Modify: `src/native_interaction_aim.hpp` (`weapon_slots` 17 -> 19; `arm_forearm_slot=17, arm_upper_slot=18`; comment line of the slot map)
- Modify: `src/dxgi_probe.cpp` (the put-back reuses `Op::slot1_unequip`, dual wield only, exactly as the Heart's hip put-away)
- Modify: `tools/default_features.json` (rows: arm-collision `no-arm-collision`, forearm-contact `no-forearm-contact`, gadget-stow
  `no-gadget-stow`, holster-draw `no-holster-draw`, heart-pocket `no-heart-pocket`, body-shoulder-zone `legacy-shoulder-zone`; all disable-kind,
  default true, with user-facing help text), launcher tests if they count rows.

Wiring (spec 5.1, 6.1, 7.2-7.5): per frame per side, the natural arm from `full_body.diagnostic()` (fresh <= 100 ms, `arm_joints_valid`) ->
`ArmCollision::casts` posted with request ids on slots 17/18 (`producer_clamp`), last frame's strict answers -> `observe` (hit class from the
same classification the clamp uses: world / entity_static solid, dynamic and NPC not solid); planes -> `Tracking::arm_collision[side]`
(enabled unless knob off or suspended: arm natively owned, a physical hold / door / carry in that hand, shoulder carry on, logic != 2, an
authored view; a suspension resets the side). Forearm capsules (diag elbow -> wrist, r 0.045) into `HandWeaponContact`. Holster rig: body
frames from the diagnostic; the shoulder union through `TempleReach` `also_in` (unless `legacy-shoulder-zone`); the `GadgetGrip` drop at a
place that `put_back` accepts -> `Op::slot1_unequip` and the pouch's remembered equip key (instead of `NativeGadgetDrop::drop`); `HeartGrip`
put-away at any place (dual wield, as today); in dual wield only, the draw state machine per empty hand for pouches and the Heart pocket -> commit: the slot-1
claim (`ArmSet::catch_gadget`) then `Op::test_equip` with the key (gadget) or "heart"; haptics per spec 7.5; status blocks `arm_collision`,
`holsters`, `hand_weapon_contact.forearm`.

- [ ] **Step 1:** Implement the slots, the Op, the wiring and the status; build both DLLs: `python3 tools/linux/build_doto.py 145786` and
  `python3 tools/linux/build_doto.py 1484 --game d2`. Expected: both link, no `/force:unresolved`.
- [ ] **Step 2:** Run the affected host checks: `python3 tools/linux/run_build_cmd.py --only 'arm_collision_check|holster_rig_check|hand_weapon_contact_check|vr_controls_check|body_ik_check|full_body_driver_check|throw_gesture_check|native_ranged_fire_check'`. Expected: PASS.
- [ ] **Step 3:** Commit ("Wiring: arm collision casts and swivel, forearm contact, body holsters, gadget and Heart put-back and draw").

### Task 8: Scenarios

**Files:** Create in `tools/linux/scenarios/`: `arm_wall.py` (lab-facts wall-on-the-right spot: right hand forward-low, sweeps 0.3 m
sideways toward the wall and back at 0.2 m/s, then down to the hip and up overhead; console `d2vr spacewarp capture 2` at 3 marked times),
`arm_table.py` (desk/table spot: elbow-low reach down onto the top, 2 s hold, slide), `weapon_wall.py` (facing the wall at 0.6 m: draw the
sword, push the blade slowly into the wall, sweep along it; then `d2vr equip pistol`/Voltaic and press the barrel into the wall),
`gadget_stow.py` (DOTO: `d2vr equip hook`; D2: a grenade per lab-facts: take it, bring the hand to the left pouch, open: put back; squeeze
there and pull 0.10 m up: taken out; control: open away from the body: drop; a 1.5 m/s swing through the pouch with the grip opening there:
thrown/placed, not stowed), `heart_pocket.py` (D2: `d2vr equip heart`, put back at the chest pocket, take out, put back at the hip).
- [ ] Steps: write each scenario after `holster_draw.py`'s pattern (docstring with the timeline and the pass criteria) -> dry-run
  `python3 -c` import + `build(54000)` returns sorted, non-overlapping segments -> commit ("Scenarios: arms, weapons at walls, gadget and Heart put-back").

### Task 9: Sim A/B and results

- [ ] **Step 1:** DOTO v145786: `arm_wall`, `arm_table`, `weapon_wall`, `gadget_stow`, each once with `--knob arm-collision-shadow` (baseline
  geometry, nothing applied) and once without. Pass: `arm_collision.engaged_frames` > 0, `max_residual_mm` reported, `max_step_deg` <=
  engage_rate*dt*57.3 + 0.5, captures show the elbow out of the wall; `gadget_stow`: `holsters.stows.gadget` >= 1, the mine count unchanged
  after the put-back, `holsters.draws` >= 1 and the mine back in the hand, the control drop/throw still happen.
- [ ] **Step 2:** D2 v1484: `arm_wall`, `heart_pocket`, `gadget_stow` (if the save has a grenade), `holster_draw` regression.
- [ ] **Step 3:** Regression set on DOTO v145786 vs v145785: `holster_draw`, `sword_sweep`, `mine_throw_stick`, `hook_mine_mode_vr`,
  `power_grip_cancel`: the same pass results; weapon contact counters (`max_jump_mm`, `plane_changes_per_s`) no worse.
- [ ] **Step 4:** Write the results table into `docs/DOTO-VR.md` (row linux4) and `lab-facts.md`; commit.

### Task 10: Docs, release notes, review, package

- [ ] Update `docs/VR-CONTROLS.md` (the belt pouches and the Heart pocket; the guide parity check `controls_guide_xr_check` / `vr_controls_guide_check.py`
  must pass), `README.md` feature lines, `docs/VR-v1484-RELEASE.md` (D2 v1484 + DOTO v145786, every item marked sim-proven / built / not
  done) and a headset checklist `docs/VR-v1484-TEST-CHECKLIST.md`.
- [ ] Full host-check suite `python3 tools/linux/run_build_cmd.py --keep-going` vs the v1483 baseline (766/62/27/7): no new failures.
- [ ] Whole-branch review (Sonnet reviewer + `dsh` read-only second opinion), fix what is real.
- [ ] Package `~/d2vr-test-builds/v1484-release/` (as v1483); publishing stays the user's.
