# Physical Hands, Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The visible hand stops at world surfaces, the arm follows it, it snaps back cleanly, pushes have weight, the palm sits in the controller, and carrying a tank no longer loses its DEBRIS layer.

**Architecture:** A new pure `HandClamp` replaces the chest-anchored palm sweep in `WeaponCollision`. It sweeps from the previous drawn hand (stored relative to the level player frame), keeps last frame's hits as half-space planes, extends the sweep by one frame of velocity, and snaps back with hysteresis. `dxgi_probe.cpp` feeds it a reach-clamped, palm-calibrated controller grip and routes drawn and raw poses per C9. Pushes get a mass-aware Δv cap. The carry gets diagnostics, then a DEBRIS re-apply that survives a transient `clear()`.

**Tech Stack:** C++17 headers (MSVC `cl /W4 /WX`, clang-cl + Wine on Linux), MinHook, OpenXR, Havok hknp through fixed RVAs; checks registered in `tools\build-msvc.cmd`; Python source guards for `dxgi_probe.cpp` routing.

**Spec:** `docs/superpowers/specs/2026-10-18-physical-hands-design.md` (binding: G1–G6, C1–C14), research `docs/superpowers/specs/2026-10-18-physical-hands-research.md`.

## Global Constraints

- Worktree `~/github/dishonored2-vr-hands`, branch `feat/physical-hands`. Never `cd` into another checkout, never `git stash`.
- Phase 1 is query-based (C1). No Havok body is created.
- The exe is pinned (`b671f824…`). Every engine write keeps its SEH guard (`__try/__except`), finite checks and existing fault counters; `catch(...)` does not catch access violations under `/EHsc`.
- Hands never push the player's view or body.
- Frame budget: ≤ 0.5 ms game-thread cast cost for both hands combined, reported as `cast_us` in the status JSON (C14).
- Casts stay on the game-thread Stage0 scope (`src/native_interaction_aim.hpp:457-475`); results are one frame late.
- C9 routing: clamped hand → rendered hand, arm IK target, drawn weapon. Raw controller → aim rays/crosshair, attack/block detection and swing velocity, throw velocity, grab desire/selection, interaction routing, native attack/block ownership. Native authored animations and a carrying hand: the clamp yields.
- Awake guards stop the hand and nothing else happens; dead or unconscious bodies are pushable (C2).
- Snap-back: gap > 0.35 m for > 0.25 s, or non-finite; re-acquire by a sweep from the chest; ease-out 1.5 m/s (C3).
- Sweep from the previous drawn hand to the reach-clamped controller palm; palm sphere 4–5 cm plus a fingertip sphere (C4).
- Push Δv ≤ F·dt/m; the time factor is not read from the engine in phase 1 (C5, C13).
- Palm calibration per side and per OpenXR interaction profile; translation ≤ 8 cm per axis, tilt ≤ 30°; default 3 cm along the palm normal plus 15° pitch (C6, C12).
- Carry: diagnostics first, then the DEBRIS re-apply fix; other candidates wait (C7).
- Every new check is registered in `tools\build-msvc.cmd` with `/std:c++17 /utf-8 /EHsc /W4 /WX /O2` and fails the build on a non-zero exit. The Linux harness `python3 tools/linux/run_build_cmd.py` must stay green (baseline 468 passed / 0 failed / 41 skipped; the pass count grows with the new checks).
- Offline checks pin logic only. Feel, comfort and visuals are the user's headset checklist (end of this plan).

## Spec deviations

Each item corrects the spec against the code. Tasks implement the corrected version.

1. **The push lab is not a build check, and `wall_cast` cannot fail today.** `tools/push_lab.cpp` is built on demand by `tools/push_lab.cmd` and is not in `build-msvc.cmd`. `wall_cast` is an `info` scenario (`push_lab.cpp:484`, `s.info=true`), and `main` returns 0 whatever fails (`:689-690`). Its "world cast" is a copy of the old clamp (`:174-205`) that models only the bottle, never the wall. Task 1 registers the lab; Task 3 drives it with the production clamp and judges the wall check.
2. **The 22.4 cm figure is stale, and C8's candidate list misses two causes.** The planner's scratch run of today's lab (g++ -O2, not the harness) gives 19.2 cm. The release happens when two things fail together. First, the 25 cm "hit near the hand" gate at `src/weapon_collision.hpp:76` stops holding once the tracked hand is 25 cm past the contact point; it exists only because of the chest anchor. Second, the meet latch (`native_hand_contact.hpp:110`) unlatches when the tracked hand passes through the tipped bottle. Gate off: 19.2 cm. Meet off: 17.1 cm. Both off: −4.77 cm. With a 0.146 m/s hand, the one-frame lag and the 45 cm clamp do not contribute. Task 1 records this under the harness; Tasks 2–3 fix both causes.
3. **`grip_ik_pipeline_check` is not registered.** It is only mentioned in `docs/GENERAL-PHYSICAL-GRIP.md:46`. Task 4 registers it. `body_ik_check` is skipped on Linux because it needs the gitignored `tests/player_clip_fixture.hpp`, so body-IK changes are pinned by the new `hand_reach_check` and by `grip_ik_pipeline_check`.
4. **C9's "existing `!meeting.prop` exemption" covers pushed props, not carried ones.** `dxgi_probe.cpp:4100` skips the cast only when the cast entity is a prop the solid hands push. Today the clamp does run on a carrying hand, and its palm sweep can hit the held object: `kWorldOccluderLayers` includes DEBRIS (research §2.4). Task 5 adds an explicit carry yield and keeps the push exemption.
5. **Throw velocity and selection read the clamped pose today, against C9.** `publish_hand(side,hands.grips[side])` (`dxgi_probe.cpp:4185`) derives `hand_velocity`, which drives the sword drop at `:6355` and `:6468`, from the clamped pose. `note_grip` (`:4360`) and the `prop_watch` reach (`:4378`) also take it. Task 5 moves velocity and selection to `raw_grips` and keeps the drop position on the drawn hand.
6. **Ragdoll pushing does not exist (C2).** Solid hands consider only `native_rigid_body` props (`native_rigid_body.hpp:26-27`). `native_ragdoll_body` also accepts a live guard who is ragdolling after a yank (`entity+0xd1` bit 0x80, `tests/native_rigid_body_check.cpp:102`). Task 7 adds ragdoll candidates that must be dead (0x08) or unconscious (0x20).
7. **The interaction profile is not queried (C12).** The mod suggests only `/interaction_profiles/oculus/touch_controller` (`game_xr.hpp:666`) and never loads `xrGetCurrentInteractionProfile`. Task 8 loads it with `xrPathToString` and falls back to the suggested path.
8. **"Hold and confirm" cannot measure an offset from the controller pose alone** (`docs/native-palm-controller-alignment.md`, "What calibration establishes"). Within C12's wiring freedom, the step is a visual fit: hold naturally, nudge with the thumbsticks, press the trigger of that hand to confirm.
9. **The C11 "player root/body frame" is the level yaw frame of the camera anchor**: `anchor.position` with the flattened `anchor.basis[0]`. Hands are mapped through that frame (`dxgi_probe.cpp:4040`). The full-body solver root is one frame stale.
10. **C5 "stays capped as today".** `hand_contact` already scales by `min(1, 2 kg/m)` and refuses props of 12 kg and more (`hand_contact.hpp:272,293`). The new F·dt/m cap is applied on top, so pushes can only get weaker than today.
11. **`cast_us` already exists**, as the mean over all eleven weapon slots (`dxgi_probe.cpp:4945`). C14 needs the clamp's own per-frame cost, so a new `hand_clamp.cast_us` is added and the old field keeps its meaning.

## Review Focus

1. Level load, death reload or world change mid-contact: stale planes and a stored hand from the old level must not pull the new hand. Pinned by Task 2 `epoch_change_resets`.
2. One controller loses tracking for a second, or the game was paused, then the hand reappears elsewhere: no sweep from a stale previous hand. Pinned by Task 2 `tracking_gap_reacquires`.
3. Crouching under a table, so the chest re-acquire origin is inside geometry: the hand stays released on the controller without flicker. Pinned by Task 2 `reacquire_from_inside_geometry_no_flicker`.
4. A grazing contact whose hit appears every other frame: the drawn hand must not flicker between surface and controller. Pinned by Task 2 `intermittent_hits_no_flicker`.
5. A corrupt, oversized or locked palm-offset file: defaults are used, the game keeps running, and the status says `default`. Pinned by Task 8 `corrupt_file_keeps_defaults`.

---

### Task 1: Reproduce and attribute the push-lab `wall_cast` failure (C8)

No production fix in this task. It produces evidence and makes the lab a build check.

**Files:**
- Modify: `tools/push_lab.cpp` (attribution knobs at `:174-184` and `:483-486`, exit code at `:689-690`)
- Modify: `tools/build-msvc.cmd` (register after the `weapon_collision_check` lines `:777-780`)
- Create: `.superpowers/sdd/2026-10-18-physical-hands-phase1/wall-cast-evidence.md` (plan workspace, git-excluded)

**Interfaces:**
- Produces: `build\push_lab.exe <out_dir>` exits 1 when a non-info scenario fails, otherwise 0. Knobs (environment, default = today's behaviour): `PUSH_LAB_GATE` (0 drops the 25 cm hit-near-hand gate at `:176`), `PUSH_LAB_MEET` (exists), `PUSH_LAB_CAST_WALL` (1: the lab cast also sweeps against the wall plane, and a wall hit is never exempted by the meet), `PUSH_LAB_CAST_LAG` (0: the cast result is used in the frame it is computed), `PUSH_LAB_MAX_SHIFT` (applies `WeaponCollision`'s 0.45 m clamp to the lab shift), `PUSH_LAB_WALL_SPEED` (wall_cast hand speed, default 0.146).

- [ ] **Step 1: Register the lab.** After `build-msvc.cmd:780`, add:
  `cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tools\push_lab.cpp /Fo:build\push_lab.obj /Fe:build\push_lab.exe`, the two `if not "%errorlevel%"=="0" exit /b 1` lines, then `build\push_lab.exe build\push-lab` with the same guard. Change `run_all` to `return failed_count?1:0;`.
- [ ] **Step 2: Run it.** `python3 tools/linux/run_build_cmd.py --only push_lab`. Expected: compile succeeds (fix any `/W4` warning in `tools/push_lab.cpp` itself, never in `src/`), the run prints `push_lab: 26 scenarios, 0 failed`, the step passes. `wall_cast` prints `info … FAILED:` with three checks.
- [ ] **Step 3: Reproduce.** Read `build/push-lab/scenarios.json` for `wall_cast`. Expected: "farthest proxy past the wall plane" ≈ 19.2 cm (the planner's g++ run), deepest overlap ≈ 4.46 cm, written speed ≈ 0.649 m/s. Record the exact values. Read `build/push-lab/wall_cast.csv` around t = 4.2–4.4 s: the `shift_x` collapse (−0.20 → 0) coincides with the cast point moving past 25 cm from `raw` and with the meet unlatching.
- [ ] **Step 4: Add the knobs listed in Interfaces**, each a one-line `env_or` read in `simulate()`, with defaults that leave every scenario's numbers unchanged. Re-run Step 2: still 0 failed, `wall_cast` values identical to Step 3.
- [ ] **Step 5: Attribution matrix.** The harness passes its environment through to Wine (`wine_env()`, `tools/linux/run_build_cmd.py:314-318`), so run one row at a time as `<knobs> python3 tools/linux/run_build_cmd.py --only push_lab`, e.g. `PUSH_LAB_MEET=0 python3 tools/linux/run_build_cmd.py --only push_lab`. For each row, record `hand_over_wall` and the deepest overlap from `build/push-lab/scenarios.json`:

  | Row | Knobs | Planner's g++ value |
  |---|---|---|
  | baseline | none | 19.2 cm |
  | gate off | `PUSH_LAB_GATE=0` | 19.2 cm |
  | meet off | `PUSH_LAB_MEET=0` | 17.1 cm |
  | gate + meet off | `PUSH_LAB_GATE=0 PUSH_LAB_MEET=0` | −4.77 cm |
  | wall in cast | `PUSH_LAB_CAST_WALL=1` | 19.2 cm |
  | 45 cm clamp | `PUSH_LAB_MAX_SHIFT=1` | (record) |
  | no lag, slow | `PUSH_LAB_CAST_LAG=0` | (record) |
  | fast, lag | `PUSH_LAB_WALL_SPEED=2 PUSH_LAB_GATE=0 PUSH_LAB_MEET=0` | (record) |
  | fast, no lag | `PUSH_LAB_WALL_SPEED=2 PUSH_LAB_GATE=0 PUSH_LAB_MEET=0 PUSH_LAB_CAST_LAG=0` | (record) |

- [ ] **Step 6: Write the evidence file.** Include the matrix, the CSV excerpt from Step 3, and one line per C8 candidate: chest-anchored origin (through its 25 cm gate), one-frame-late results (difference between the two fast rows), the `!meeting.prop` exemption (the meet unlatch), the 45 cm clamp, plus the lab model gap from deviation 1. **Gate:** if the harness rows do not show "gate and meet together" as the cause at 0.146 m/s, stop and report to the controller before Task 2. The fix follows the cause.
- [ ] **Step 7: Commit.**
  ```bash
  git add tools/push_lab.cpp tools/build-msvc.cmd
  git commit -m "test: run the push lab as a build check and add wall_cast attribution knobs"
  ```

---

### Task 2: `HandClamp`, the hand stops at surfaces (G1, G3, C3, C4, C8, C11, C14)

**Files:**
- Create: `src/hand_clamp.hpp`
- Modify: `src/native_world_trace.hpp:185` (add `to_clamp_hit` beside `to_collision_hit`)
- Test: `tests/hand_clamp_check.cpp`; register after `weapon_collision_check` in `tools/build-msvc.cmd` with `/Isrc`

**Interfaces:**
- Consumes: `Vec3`, `WorldTrackedPose` (`tracked_pose.hpp`); `CollisionEvent`, `CollisionHaptic` (`weapon_collision.hpp`).
- Produces (namespace `d2vr`):
  ```cpp
  struct ClampProbe {Vec3 local{};double radius{};};      // sphere in the grip frame (x forward, y left, z up)
  struct ClampHit {Vec3 point{},normal{};std::uintptr_t entity{};std::uint16_t material{0xffff};bool initial_overlap{};};
  struct ClampSweep {Vec3 from{},to{};double radius{};bool valid{};};
  struct HandClampConfig {
      double palm_radius=.05,skin=.005,plane_seconds=.1,release_speed=1.5,snap_gap=.35,snap_seconds=.25,
             rearm_seconds=.1,gap_seconds=.25,haptic_cooldown=.08,speed_for_full=3.;
      unsigned overlap_frames=3,max_planes=4;};
  struct ClampInput {
      double now{};                       // monotonic seconds; per-side dt is derived from it
      std::uint64_t epoch{};              // pose_epoch ^ (reference_epoch<<32) ^ player; a change resets the side
      WorldTrackedPose root{};            // level player frame (C11, deviation 9)
      WorldTrackedPose target{};          // calibrated, reach-clamped controller grip
      Vec3 chest{};                       // free-space re-acquire origin
      std::optional<ClampHit> palm_hit,tip_hit,chest_hit; // results of the sweeps this side posted last frame
      ClampProbe tip{};bool tip_enabled{true},post{true},yield{};};
  enum class ClampState {free,contact,released};
  struct ClampOutput {
      bool valid{};WorldTrackedPose drawn{};Vec3 shift{};ClampState state{};bool snapped{};
      std::optional<ClampHit> contact;double approach_speed{};
      std::optional<CollisionEvent> event;std::optional<CollisionHaptic> haptic;
      ClampSweep palm_sweep,tip_sweep;};  // palm_sweep is chest->target while released
  class HandClamp {
  public:
      explicit HandClamp(HandClampConfig={});
      ClampOutput update(unsigned side,const ClampInput&);
      void note_drawn(unsigned side,const WorldTrackedPose& root,Vec3 drawn); // the position finally drawn (meet shift, blade push)
      void reset(unsigned side);
      std::uint64_t snapbacks(unsigned side) const;std::uint64_t contact_frames(unsigned side) const;};
  WorldTrackedPose level_frame(Vec3 position,Vec3 forward);   // forward flattened; {1,0,0} when degenerate
  template<std::size_t N> ClampProbe leading_probe(const std::array<ClampProbe,N>& open_hand,const WorldTrackedPose& grip,Vec3 motion,ClampProbe fallback);
  class CastBudget {public:static constexpr double budget_us=500,window_seconds=2;
      void note(double frame_us,double now);bool tip_enabled() const;bool alternate() const;double mean_us() const;double max_us() const;void reset();};
  inline ClampHit to_clamp_hit(const WorldTraceHit&);  // in native_world_trace.hpp
  ```

- [ ] **Step 1: Write the failing tests** in `tests/hand_clamp_check.cpp` (CHECK macro as in `weapon_collision_check.cpp`). The fixture runs 90 Hz frames with a fake world that answers posted sweeps one frame late: a wall at x = 1 (normal −x), a floor at z = 0, and an optional second wall at y = 1 (normal −y).
  - `sweep_starts_at_previous_drawn`: hand at x 0.5 for 2 frames, then target x 1.3. The posted `palm_sweep.from` equals the previous drawn position (±1e-9). From the frame after the hit arrives, `drawn.position.x ≤ 1-.05+1e-6` and `≥ 1-.05-.005-1e-6`.
  - `corner_two_planes`: target (1.2,1.2,0.5) with both walls. Settles with x ≤ 0.95 and y ≤ 0.95 within 3 frames, and stays finite.
  - `grazing_slides`: hand held 4 cm into the wall while the target moves +z at 0.5 m/s. `drawn.x` stays within [0.945,0.95] and `drawn.z` follows the target to ±1 mm.
  - `fast_approach_late_results`: target approaches the wall at 3 m/s from x 0.5. No frame has `drawn.x > 0.95+0.005`, which needs the predictive extension: `palm_sweep.to == target + v·dt` (±1e-6).
  - `deep_start_overlap_snaps`: three consecutive `palm_hit` with `initial_overlap=true`. The third frame returns `snapped=true`, `state==released`, `drawn.position==target.position`.
  - `snap_after_gap_time`: target 0.5 m behind the wall. `snapped` is false at 0.24 s of gap > 0.35 and true by 0.26 s. `snapbacks(side)==1`.
  - `no_snap_short_gap`: a 0.30 m gap for 2 s never snaps.
  - `rearm_after_clear`: after a snap, `chest_hit` present for 0.5 s keeps `released` with `drawn==target`. Then 0.1 s with no `chest_hit` returns to `free`, and the next contact clamps again.
  - `non_finite_snaps`: a NaN in the stored shift (inject through `note_drawn` with NaN) gives a finite target, `drawn==target` and a snap. A NaN target gives `valid=false`.
  - `walk_and_turn_carry` (C11): the hand rests 0.3 m in front of the root in free space; the root translates 1 m and yaws 45° in one frame with the target carried the same way. `palm_sweep.from` equals the carried previous hand (±1e-9), no snap, `drawn==target`.
  - `epoch_change_resets`: contact against the wall, then `epoch` changes with the target behind the wall. Output is `released`-free re-acquire: no planes from before (drawn == target that frame), `palm_sweep` is chest→target.
  - `tracking_gap_reacquires`: no update for 0.4 s, then a target 0.6 m away. No sweep from the stale hand (`palm_sweep.from == chest`), no snap counted.
  - `reacquire_from_inside_geometry_no_flicker`: released, and `chest_hit` always has `initial_overlap`. 60 frames stay `released` with `drawn==target` and no state change.
  - `intermittent_hits_no_flicker`: pressed into the wall, `palm_hit` present only on even frames. `drawn.x` stays within [0.945,0.95] on every frame.
  - `release_eases_out`: pressed 0.1 m into the wall, then the target jumps 0.2 m back into free space. `|shift|` falls by ≤ 1.5·dt per frame to 0, with no frame inside the wall.
  - `tip_probe_stops_fingers`: `tip={ {0.12,0,0},0.03 }` while moving +x. The tip centre stays at x ≤ 1-.03 (±0.005); with `tip_enabled=false`, only the palm is held.
  - `leading_probe_picks_front`: three probes at local x = 0, 0.08, 0.15 with motion +x picks 0.15; motion −x picks 0; zero motion returns `fallback`.
  - `budget_drops_tip_then_alternates`: 2 s at 600 µs gives `!tip_enabled() && !alternate()`; 2 s more at 550 µs gives `alternate()`; 400 µs throughout keeps `tip_enabled() && !alternate()`.
  - `yield_passes_through`: `yield=true` gives `drawn==target`, no sweeps valid, and resets the side.
  - `event_on_new_contact`: the first contact frame yields `event` (not blade, entity and material from the hit, speed = approach) and `haptic` amplitude in [0.2,1]; the next frame yields neither.
- [ ] **Step 2: Register and run, expect failure.** `python3 tools/linux/run_build_cmd.py --only hand_clamp_check`. Expected: compile error, `hand_clamp.hpp` not found.
- [ ] **Step 3: Implement `src/hand_clamp.hpp`.** Pure, with no `<windows.h>`. Per-side state: `prev_local` (drawn position in root coordinates), `prev_target_local`, `shift_local`, up to `max_planes` planes `{point, normal, probe local, radius, age, entity, material}`, `last_now`, `epoch`, `state`, `over_time`, `clear_time`, `overlap_count`, counters. `update` order:
  1. `yield` → reset the side and return `drawn=target`. A non-finite target → reset and return `valid=false`. An epoch change, or `now-last_now > gap_seconds`, or no state yet → re-acquire: state `free` with no planes, `prev=target`, and post chest→target as `palm_sweep` (the result is consumed as `chest_hit` next frame; a hit becomes a plane). `dt=min(now-last_now,.1)`.
  2. Fold in hits. `palm_hit`/`tip_hit` become planes (`radius` = palm_radius or tip.radius, probe local = {0,0,0} or `tip.local`). Count consecutive palm `initial_overlap`; at `overlap_frames` → snap. Age the planes by `dt` and drop those over `plane_seconds`.
  3. `released` → `drawn=target`, post chest→target. `chest_hit` absent adds `dt` to `clear_time`, otherwise resets it; at `rearm_seconds` → `free` with `prev=target`.
  4. Otherwise run the constraint. Clamp a non-finite stored shift or previous position by snapping.
     ```
     p = target.p; for 3 passes, for each plane: o = R(target)·plane.local;
         d = dot(p + o - plane.point, plane.n) - (plane.radius + skin); if d < 0: p -= plane.n * d
     wanted = p - target.p
     shift = |wanted| >= |shift| ? wanted : shift + clamp_len(wanted - shift, release_speed*dt)
     p = target.p + shift; re-project p through the planes once more (the eased hand never enters one)
     over_time = |shift| > snap_gap ? over_time + dt : 0; over_time > snap_seconds → snap
     v = (target.p - root∘prev_target_local)/dt
     palm_sweep = {root∘prev_local, target.p + v*dt, palm_radius}
     tip_sweep  = tip_enabled ? {root∘prev_local + R(prev)·tip.local, target.p + R(target)·tip.local + v*dt, tip.radius} : invalid
     ```
     A snap sets `released`, clears the planes and shift, sets `drawn=target`, increments `snapbacks`. `contact` is the plane with the largest correction this frame. `state=contact` while any plane corrected p or `|shift|>1e-4`. Event and haptic on the free→contact edge, with the amplitude and duration formulas of `weapon_collision.hpp:99-100` and `haptic_cooldown`. `post=false` keeps the sweeps invalid. Store `prev_local` from `drawn`; `note_drawn` overwrites it with what was really drawn.
  `CastBudget`: rolling mean over `window_seconds`. Mean > `budget_us` while the tip is on turns the tip off; mean > `budget_us` with the tip already off turns `alternate` on. Both stay off/on until `reset()`. `level_frame` and `leading_probe` follow their comments. `leading_probe` returns the probe maximising `dot(R·local, unit(motion)) + radius`; when |motion| < 0.05 m/s it returns `fallback`.
- [ ] **Step 4: Run the tests, expect a pass.** `python3 tools/linux/run_build_cmd.py --only hand_clamp_check`. Expected: `PASS`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_clamp.hpp src/native_world_trace.hpp tests/hand_clamp_check.cpp tools/build-msvc.cmd
  git commit -m "feat: hand clamp that sweeps from the previous drawn hand with snap-back"
  ```

---

### Task 3: Push lab on the production clamp, and the meet latch fix (C8: `wall_cast` passes)

**Files:**
- Modify: `src/native_hand_contact.hpp:97-115` (`meet` and the latch rule)
- Modify: `tools/push_lab.cpp:152-237` (replace the copied cast and latch), `:100` (per-check `info`), `:483-493` (judge `wall_cast`)
- Test: `tests/hand_contact_check.cpp` (latch), `build\push_lab.exe` (Task 1 registration)

**Interfaces:**
- Consumes: `HandClamp`, `ClampInput`, `ClampOutput`, `leading_probe` (Task 2).
- Produces: `static bool NativeHandContact::next_meet_latch(bool latched,double depth,bool clamp_holding) noexcept;` → `depth>meet_latch || (latched && (depth>=meet_unlatch || clamp_holding))`. `Meeting meet(unsigned side,std::uintptr_t cast_entity,bool clamp_holding=false);` uses it. `struct Check` gains `bool info{}`: an info check is reported and never fails its scenario.

- [ ] **Step 1: Write the failing tests.**
  - In `tests/hand_contact_check.cpp`, `meet_latch_holds_while_clamped`: `next_meet_latch(true,.03,true)` is true; `next_meet_latch(true,.03,false)` is false; `next_meet_latch(false,.09,false)` is true; `next_meet_latch(true,.06,false)` is true.
  - In `tools/push_lab.cpp`, `wall_cast` becomes non-info. Check "the drawn hand stops short of the wall … ≤ 1 cm" and "the bottle stays within the wall" are judged; "deepest drawn overlap" and "never launched" become `info` checks, because they measure the tipped-bottle push, not G1.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "hand_contact_check|push_lab"`. Expected: `hand_contact_check` fails to compile (no `next_meet_latch`), `push_lab` reports `wall_cast fail` at ≈19.2 cm and exits 1.
- [ ] **Step 3: Implement.** `meet()` calls `next_meet_latch(meet_latched_[side],depth,clamp_holding)`. In `simulate()`, when `s.cast` is set, the lab owns one `HandClamp`. Its world answers each posted sweep one frame late against the prop (`rig::shape_distance`, entity 1) and against the wall plane when `s.wall` is set (entity 0). The root is identity, the chest is `s.chest`, the tip comes from `leading_probe` over the live open hand's proxies in grip coordinates (`hand_proxies(live_hand,live_pose({}),{0,0,0,0,0})`, radius = proxy radius). The meet latch uses `NativeHandContact::next_meet_latch(meet_latched,out,clamp_out.state==ClampState::contact && clamp_out.contact && clamp_out.contact->entity==1)`. The drawn hand is `meet && !latched ? raw + meet_shift : clamp drawn` (the existing table lift stays), followed by `clamp.note_drawn`. Delete the copied shift block (`:174-180`) and the old cast loop (`:197-205`). Keep the Task 1 knobs that still apply, and drop `PUSH_LAB_GATE` and `PUSH_LAB_MAX_SHIFT`, whose code is gone.
- [ ] **Step 4: Run, expect a pass.** Same command. Expected: `hand_contact_check` PASS; `push_lab: 26 scenarios, 0 failed`. `wall_cast pass`, hand past the wall ≤ 1 cm. Every scenario that passed in Task 1 still passes. If one does not, report it with its numbers; do not widen its limit.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_hand_contact.hpp tools/push_lab.cpp tests/hand_contact_check.cpp
  git commit -m "fix: hold the drawn hand at a pinned prop; the push lab runs the production clamp"
  ```

---

### Task 4: Reach clamp and IK feed (G2, C4)

**Files:**
- Create: `src/hand_reach.hpp`
- Modify: `src/body_ik.hpp:909-924` (use `soft_reach`), `:884-885` (record the reach state per side)
- Modify: `src/full_body_driver.hpp:142-155` (`Diagnostic`), `:426-431` (`diagnostic()` fills the world reach)
- Test: `tests/hand_reach_check.cpp` (new); `tests/grip_ik_pipeline_check.cpp` (extend); register both in `tools/build-msvc.cmd` with `/Isrc` (`grip_ik_pipeline_check` also with `/Ithird_party\minhook\include`)

**Interfaces:**
- Produces:
  ```cpp
  struct SoftReach {double target_distance{},extension{1};};
  SoftReach soft_reach(double d,double full);        // body_ik's rule, moved unchanged: soft=.08*full, extension<=1.06
  double max_exact_reach(double full);               // largest d with soft_reach(d,full).target_distance==d, by bisection
  struct ArmReach {Vec3 shoulder{};double max_wrist{};Vec3 wrist_local{};bool valid{};}; // world; wrist = grip.p + R(grip)·wrist_local
  WorldTrackedPose reach_clamp(const WorldTrackedPose& grip,const ArmReach& reach,bool* clamped=nullptr); // translation only
  ```
  `body::BodySolver` records per side, at each arm solve, the solve-space shoulder (`w[upper_[i]].p` after the clavicle shrug), `full=la+lb`, and the grip-to-wrist offset (`inverse(grip)·wrist_target`). `fullbody::Diagnostic` gains `ArmReach reach[2]{}`, converted to world with `world_from_solve = root_last_·inverse(root_solved_)` (the mapping `:406` already uses), and `max_wrist = max_exact_reach(full)`.

- [ ] **Step 1: Write the failing tests.**
  - `hand_reach_check`: `soft_reach(.5,.6)` gives `{.5,1}`. For `full=.6`, `soft_reach(max_exact_reach(.6)-1e-4,.6).target_distance` equals d to 1e-9, and at `+1e-3` it is < d. `reach_clamp`: a grip whose wrist is 0.2 m beyond `max_wrist` moves along shoulder→wrist so that |wrist−shoulder| == max_wrist (±1e-9), orientation unchanged, `clamped=true`. A grip inside reach is unchanged, `clamped=false`. `valid=false` or a non-finite reach leaves it unchanged.
  - `grip_ik_pipeline_check`, `ik_feed_inside_reach`: the `Rig` fixture with a left grip moved 0.1 m forward (inside reach). After `driver.arm_frozen`, the palm (`rig.palm`) is within 1 cm of the commanded grip.
  - `grip_ik_pipeline_check`, `ik_feed_clamped_reach`: a grip 0.4 m beyond reach. Seed once, take `driver.diagnostic().reach[0]`, apply `reach_clamp`, publish, solve. The palm is within 1 cm of the clamped grip, and the unclamped grip's palm is > 5 cm short (the gap the clamp removes).
- [ ] **Step 2: Register and run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "hand_reach_check|grip_ik_pipeline_check"`. First confirm that `grip_ik_pipeline_check` passes unmodified once registered; if it does not, stop and report. Expected after adding the tests: compile errors for the missing names.
- [ ] **Step 3: Implement.** Move the soft-IK formula from `body_ik.hpp:912-921` into `soft_reach` and call it there; the arm output must stay bit-identical, which `full_body_driver_check` and `grip_ik_pipeline_check` cover. Record the reach state in `BodySolver`, expose it to the `Driver`, and fill `Diagnostic::reach` in `diagnostic()`.
- [ ] **Step 4: Run, expect a pass.** `python3 tools/linux/run_build_cmd.py --only "hand_reach_check|grip_ik_pipeline_check|full_body_driver_check|drawn_palm_residual_check"`. Expected: all PASS.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_reach.hpp src/body_ik.hpp src/full_body_driver.hpp tests/hand_reach_check.cpp tests/grip_ik_pipeline_check.cpp tools/build-msvc.cmd
  git commit -m "feat: reach-clamp the hand target so the arm reaches the drawn hand"
  ```

---

### Task 5: Wire the clamp into the hand publication (G1–G3, C9, C14)

**Files:**
- Modify: `src/weapon_collision.hpp:47-116` (blade only: drop `anchor` and the palm sweep)
- Modify: `tests/weapon_collision_check.cpp` (palm assertions removed, now covered by Task 2; blade assertions use the new signature)
- Modify: `src/native_interaction_aim.hpp:588-606` (`weapon_slots=12`, slot 11 = tip probe), `:466-470` (time slots 0 and 11 separately)
- Modify: `src/dxgi_probe.cpp`: `:361` (statics), `:2364-2379` (`publish_hand`), `:4042-4106` (clamp block), `:4185`, `:4200-4201` (hoist ownership above the clamp), `:4360`, `:4378`, status JSON at `:4945`
- Test: `tests/hand_clamp_routing_check.py` (new source guard; register as `python tests\hand_clamp_routing_check.py` beside `held_object_routing_check.py`, `build-msvc.cmd:541`)

**Interfaces:**
- Consumes: Task 2 (`HandClamp`, `CastBudget`, `leading_probe`, `level_frame`, `to_clamp_hit`), Task 3 (`meet(side,entity,clamp_holding)`), Task 4 (`reach_clamp`, `Diagnostic::reach`).
- Produces: `WeaponCollision::update(unsigned side,double dt,Vec3 grip,Vec3 dir,double blade_length,Query&& query,Vec3 blade_offset={},double blade_radius=0)` (blade sweep from the drawn grip; `CollisionConfig` loses `palm_radius`). `static void publish_hand(unsigned side,const d2vr::WorldTrackedPose& drawn,const d2vr::WorldTrackedPose& raw)`: `hand_world` = drawn, `hand_velocity` from raw. `NativeInteractionAim::clamp_cast_us_` (atomic double, the last scope's slots 0+11 cost) and `tip_slot=11`. Status JSON:
  `"hand_clamp":{"active":[l,r],"contact_frames":[l,r],"snapbacks":[l,r],"reach_clamped":[l,r],"yields":[l,r],"cast_us":mean,"cast_us_max":max,"tip_probe":bool,"alternate":bool}`.

- [ ] **Step 1: Write the failing routing check** `tests/hand_clamp_routing_check.py` (style of `held_object_routing_check.py`, compact-whitespace matching). It asserts that:
  - `raw_grips=hands.grips` comes before `reach_clamp(` and `hand_clamp.update(`;
  - the clamp input has `yield` built from `authored_hands` and from the carrying side (`hand_holding_object`, `hand_hold_side`, `native_grab.holding(side)`);
  - `publish_hand(side,hands.grips[side],raw_grips[side])`, `note_grip(side,raw_grips[side]`, and the `reach_hands` reading `raw_grips`;
  - `t.grip[side]` is still built from `hands.grips[side]`, and `solid_hands.publish(side,can_push,hands.grips[side],raw_grips[side]`;
  - `.meet(side,cast_held[side],` passes a clamp-holding argument;
  - `hand_clamp.note_drawn(` comes after the blade update;
  - `post_weapon_segment(side,d2vr::NativeInteractionAim::tip_slot,`;
  - the status contains `\"hand_clamp\":{\"active\":`.

  Then update `tests/weapon_collision_check.cpp` to the new signature: drop the palm and anchor cases, keep every blade, pivot, cap and `align_weapon_blade` case.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "hand_clamp_routing|weapon_collision_check"`. Expected: assertion errors and a compile error.
- [ ] **Step 3: Implement the blade-only `WeaponCollision`** as in Interfaces. The tip push for a blade straight into a surface stays in `shift`.
- [ ] **Step 4: Implement the dxgi wiring**, in this order inside the `if(weapon_collision_enabled && !authored_camera)` block (`:4051`):
  1. Hoist `avatar_states` and `native_ownership` (`:4200-4201`) above `:4050`. Compute `authored_hands` there too.
  2. Build `root=d2vr::level_frame(anchor position, anchor.basis[0])`, `now` from QPC in seconds, `epoch=hands.epoch^(hands.reference_epoch<<32)^body->player`.
  3. Per tracked side: `target=d2vr::reach_clamp(g,diag.reach[side],&clamped)` when `full_body.diagnostic()` has `body_valid` and `output_age_seconds<.25`. Set `yield` when the side is in `authored_hands`, or the side carries (`hand_holding_object && hand_hold_side==side` with fresh `hand_state_tick`), or `native_grab.holding(side)`. Hits come from `weapon_hit(side,0,now)` (palm, or chest while released) and `weapon_hit(side,tip_slot,now)`, through `to_clamp_hit`. The tip is `leading_probe(probes[side],target,v,last_tip[side])`, where `probes[side]` is rebuilt from `hand_geometry` whenever `arm_binding_cache` changes (open hand, grip frame, palm offset `drawn_palm_frame(side,g).position-g.position`). Without geometry, use `{{.09,0,0},.035}`. `post` is `!alternate() || ((frame+side)&1)==0`. `tip_enabled=budget.tip_enabled()`.
  4. Post the valid sweeps to slot 0 and `tip_slot`. Build `meeting` as today with `clamp_holding = out.state==contact && out.contact && out.contact->entity==cast_held[side]`. `g.position = meeting.prop ? g.position : out.drawn.position`, then `+= meeting.shift`.
  5. Run the blade `weapon_collision.update` from `g.position`, apply `r.grip`/`r.blade_dir` as today, then `hand_clamp.note_drawn(side,root,g.position)`.
  6. Downstream event use: `event = r.event ? r.event : out.event`, likewise for the haptic. `weapon_contact_frames` counts `out.state==contact || r.contact`. Replace `r.event`/`r.haptic` in `:4108-4182` with these two.
  7. After the loop, `budget.note(NativeInteractionAim::clamp_cast_us_.load(),now)`. Reset `hand_clamp` and `budget` where `weapon_collision` is reset (`:7830`).

  Then `publish_hand(side,hands.grips[side],raw_grips[side])` at `:4185`, `note_grip(side,raw_grips[side],…)` at `:4360`, `reach_hands` from `raw_grips` at `:4378`, and the status block next to `"weapon_collision"` in `:4945`.
- [ ] **Step 5: Run, expect a pass.** `python3 tools/linux/run_build_cmd.py --only "hand_clamp|weapon_collision_check|push_lab|routing"`, then the DLL step `--only dxgi_probe`. Expected: all PASS, the DLL compiles with no new tracked warnings.
- [ ] **Step 6: Commit.**
  ```bash
  git add src/weapon_collision.hpp src/native_interaction_aim.hpp src/dxgi_probe.cpp tests/weapon_collision_check.cpp tests/hand_clamp_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: draw hands at the clamp; raw controller keeps aim, throw and selection"
  ```

---

### Task 6: Mass-aware pushes and a time-factor input (G4, C5, C13)

**Files:**
- Modify: `src/hand_contact.hpp:27-61` (config), `:62-89` (result and input), `:309-318` (cap and scale)
- Modify: `src/native_hand_contact.hpp:236-253` (pass `dt`, `time_factor=1`), counters at `:74`
- Modify: `src/dxgi_probe.cpp:4945` (status `"hand_push"`)
- Modify: `tools/push_lab.cpp:213-221` (`input.dt=kDt`)
- Test: `tests/hand_contact_check.cpp`

**Interfaces:**
- Produces: `HandContactConfig::push_force_n=120.` (a starting value; Bonelab's lateral range is 90–112 N, research Q4). `ContactInput::dt` (seconds since this prop's last write, 0 = no cap) and `ContactInput::time_factor` (default 1; not read from the engine in phase 1). `HandContact::capped` (the Δv cap bound this write). `NativeHandContact::capped_writes` counter. Status: `"hand_push":{"force_n":120,"capped":N,"time_factor":1,"time_factor_verified":false}`.

- [ ] **Step 1: Write the failing tests** in `hand_contact_check.cpp`:
  - `push_force_cap`: an 8 kg box, a 3 m/s swat into it, `input.dt=1/90.`. The written speed is ≤ `120/90/8+1e-9` and `capped` is true. With `dt=0`, the write equals today's.
  - `bottle_unchanged`: the 0.5 kg slow ride from the existing carry loop with `dt=1/90.` writes exactly the same velocities as `dt=0`.
  - `time_factor_scales`: the same push with `time_factor=.5` writes half the goal speed (±1e-9) of `time_factor=1`.
  - `bad_time_factor_writes_nothing`: `time_factor` NaN, 0 or −1, or `dt` NaN or −1 gives `push=false`.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only hand_contact_check`. Expected: compile error.
- [ ] **Step 3: Implement.** Reject bad `time_factor` or `dt` before any write. Use `goal=goal*gain*in.time_factor`. After the ramp line (`:316`), when `in.dt>0`: `cap_target=max(current,0.)+push_force_n*in.dt/mass`; if `target>cap_target`, set `target=cap_target` and `capped=true`. In the service, `dt` = now − this prop's last write (`PropDrag` keeps `wrote_at_`; expose `double since_write(double now) const`), clamped to [1/240, 1/30], 0 for a first write.
- [ ] **Step 4: Run, expect a pass.** `python3 tools/linux/run_build_cmd.py --only "hand_contact_check|push_lab|native_full_hand_contact_check"`. Expected: PASS, push lab 0 failed.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_contact.hpp src/native_hand_contact.hpp src/dxgi_probe.cpp tools/push_lab.cpp tests/hand_contact_check.cpp
  git commit -m "feat: cap each push by force x dt / mass; time-factor input defaults to 1"
  ```

---

### Task 7: Knocked-out and dead guards are pushable; awake guards are not (C2)

**Files:**
- Modify: `src/native_rigid_body.hpp` (after `nearest_ragdoll_body`, about `:149`)
- Modify: `src/native_hand_contact.hpp:441-459` (`prop_class` returns a kind), `:202-227` (full read and refresh of a ragdoll candidate)
- Test: `tests/hand_contact_check.cpp` (reuse the ragdoll fixture layout of `tests/native_rigid_body_check.cpp:72-105`)

**Interfaces:**
- Produces: `template<class Read> std::optional<NativeRigidBody> pushable_ragdoll_body(Read&,std::uintptr_t base,std::uintptr_t npc,Vec3 hand);` returns the nearest of all 17 bones whose body is dynamic, and only when `npc+0xd1` has 0x08 (dead) or 0x20 (unconscious). `NativeHandContact::prop_class` returns `std::uint8_t`: 0 none, 1 prop, 2 arkNPC (`0x1c5e108`). A kind-2 `Near` has no `refresh_body`. It is re-read with `native_ragdoll_body(…,bone)` each service, counted against `max_full_reads`, and its bone is re-chosen at `next_full`.

- [ ] **Step 1: Write the failing tests:**
  - `push_unconscious_guard`: NPC with `0xd1=0x20` and dynamic bodies, palm moving into bone 10. Exactly one write lands on bone 10's handle (0x100000a).
  - `awake_guard_not_pushed`: bone flags keyframed (`0xce`), `0xd1=0`. No write; the entity is skipped until `ignore_until`.
  - `yanked_guard_not_pushed`: `0xd1=0x80` with dynamic bodies. No write.
  - `ragdoll_meetable`: the unconscious guard's entity appears in `meet()`'s pushables, so the clamp exempts it as it does a prop.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "hand_contact_check|native_rigid_body_check"`.
- [ ] **Step 3: Implement** as in Interfaces. The keep-out (`near_player`) and held checks apply to ragdolls unchanged.
- [ ] **Step 4: Run, expect a pass.** Same command plus `--only push_lab`. Expected: PASS.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_rigid_body.hpp src/native_hand_contact.hpp tests/hand_contact_check.cpp
  git commit -m "feat: solid hands push dead and unconscious guards, never awake ones"
  ```

---

### Task 8: Grip-to-palm calibration (G5, C6, C12)

**Files:**
- Create: `src/palm_offset.hpp`
- Modify: `src/vr_settings.hpp:28` (state), `:75` (`Changes`), `:239-268` (calibration page: a hand-fit sub-page)
- Modify: `src/game_xr.hpp:120-130` and `:983-990` (load `xrGetCurrentInteractionProfile`, `xrPathToString`), `:1265-1294` (preview and confirm), `:1310` (save next to the preferences)
- Modify: `src/simulated_xr.hpp:241,301` (stubs returning the touch profile)
- Modify: `src/settings_panel_image.hpp` (hand-fit page text)
- Modify: `src/dxgi_probe.cpp:4035-4041` (apply the offset before `raw_grips`), status JSON
- Test: `tests/palm_offset_check.cpp` (new, register with `/Isrc`); `tests/vr_settings_check.cpp` (extend)

**Interfaces:**
- Produces:
  ```cpp
  struct PalmOffset {Vec3 translation{};double pitch_deg{},yaw_deg{};};   // grip frame (x forward, y left, z up)
  inline constexpr double palm_offset_max_translation=.08,palm_offset_max_tilt_deg=30;
  PalmOffset default_palm_offset(unsigned side);  // translation {0, side?-.03:+.03, 0} (toward the back of the hand), pitch 15, yaw 0
  bool palm_offset_valid(const PalmOffset&);      // finite and within the bounds
  PalmOffset clamp_palm_offset(PalmOffset);
  WorldTrackedPose apply_palm_offset(const WorldTrackedPose& grip,const PalmOffset&,bool tilt);
      // position += R·translation; tilt: rotate the basis about basis[1] by +pitch (forward tips toward -up), then about basis[2] by yaw
  class PalmOffsetStore {public:
      void load(const std::filesystem::path&);          // throws on a bad file; the caller keeps defaults and the file
      void save(const std::filesystem::path&) const;    // write .tmp, then replace
      std::pair<PalmOffset,bool> get(unsigned side,const std::string& profile) const; // bool: saved
      void set(unsigned side,const std::string& profile,const PalmOffset&);};
      // file: "d2vr-palm-offsets 1\n" then "<side> <profile> tx ty tz pitch yaw\n"; ≤ 16 entries, ≤ 4096 bytes,
      // profile starts with "/interaction_profiles/" and has no whitespace
  class PalmFitStep {public:
      void begin(unsigned side,PalmOffset current);
      void nudge(float move_x,float move_y,float turn_x,float turn_y,double dt); // 2 cm/s along forward/left, 20°/s pitch/yaw, clamped
      PalmOffset value() const;std::optional<PalmOffset> confirm();void cancel();bool active() const;};
  ```
  `VrSettings` adds a hand-fit sub-page entered with X on the calibration page. On it, the trigger of a hand confirms that hand (`Changes::palm_offset` = `{side,PalmOffset}`), and B returns without saving. `GameXr` gains `std::string interaction_profile(unsigned side)` (fallback `/interaction_profiles/oculus/touch_controller`) and `PalmOffset palm_offset(unsigned side,bool* saved)` (the preview while the page is open). It saves to `preferences_file_.parent_path()/L"vr-palm-offsets.txt"`. Status: `"palm_offset":{"source":["default"|"saved","…"],"profile":"…","error":bool}`. Weapon hands get the translation only (`tilt=false`), so the user-accepted weapon direction is unchanged; see the open question.

- [ ] **Step 1: Write the failing tests:**
  - In `palm_offset_check`:
    - `default_values`: left translation y +0.03, right −0.03, pitch 15.
    - `bounds`: 0.081 m or 30.1° is not valid, and `clamp_palm_offset` brings it to the bound.
    - `apply_is_rigid`: the basis stays orthonormal and right-handed (±1e-9), and translation follows the grip axes.
    - `tilt_sign`: pitch +15 lowers `basis[0].z` for a level grip.
    - `store_round_trip`: `set`/`save`/`load`/`get` per side and profile.
    - `corrupt_file_keeps_defaults`: a bad header, an out-of-bounds entry, 5000 bytes, or a profile with a space each throws on load; `get` still returns the default with `saved=false`.
    - `fit_step`: nudging at full stick for 10 s stays within the bounds, and `confirm` returns the nudged value.
  - In `vr_settings_check`: on the calibration page, X opens hand fit; the right trigger yields `Changes::palm_offset` for side 1; B leaves without a change; Y and A keep their old meanings off the sub-page.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "palm_offset_check|vr_settings_check"`.
- [ ] **Step 3: Implement** the header, the settings sub-page and its panel text ("Hand fit: hold the controllers naturally. Left stick moves the hand, right stick tilts it. Trigger confirms that hand. B returns."), the `GameXr` loading, profile, preview and save, and the `simulated_xr` stubs. In `dxgi_probe.cpp:4040`, apply `apply_palm_offset(…,game_xr->palm_offset(side,&saved),!armed)` to `hands.grips[side]` before `raw_grips=hands.grips`. `armed` is read from `hand_weapon_bits` under `hand_state_mutex`. On an armed/empty change, blend the tilt over 0.15 s so equipping does not pop.
- [ ] **Step 4: Run, expect a pass.** Same command plus `--only "game_xr|simulated|dxgi_probe"`. Expected: PASS, the DLL builds.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/palm_offset.hpp src/vr_settings.hpp src/game_xr.hpp src/simulated_xr.hpp src/settings_panel_image.hpp src/dxgi_probe.cpp tests/palm_offset_check.cpp tests/vr_settings_check.cpp tools/build-msvc.cmd
  git commit -m "feat: per-side, per-profile grip-to-palm calibration with a hand-fit step"
  ```

---

### Task 9: Carry diagnostics (C7, G6; before any carry fix)

**Files:**
- Create: `src/carry_diagnostics.hpp`
- Modify: `src/native_held_object.hpp:89-96` (`clear(unsigned reason=0)`, per-reason counters), `:348-363` (`anchor` records the quaternion norm deviation before normalising)
- Modify: `src/drawn_palm_residual.hpp:39-46` (`std::int64_t age_ms(std::uint64_t now) const`; −1 when there is none)
- Modify: `src/dxgi_probe.cpp`: the `clear` call sites `:2778` (reason 1), `:2900` (reason 2), `:4399` (reason 3); `publish_held_object` notes one `CarryFrame` per frame; status JSON; CSV `native-carry-trace.csv`
- Test: `tests/carry_diagnostics_check.cpp` (new, `/Isrc`); `tests/held_object_routing_check.py` (extend)

**Interfaces:**
- Produces:
  ```cpp
  struct CarryFrame {std::uint64_t tick{};std::uintptr_t entity{};std::uint32_t layer{0xff};float render_scale{};
      std::int64_t residual_age_ms{-1};float rf_quat_norm{};float anchor_quat_dev{};unsigned clear_reason{};bool clamp_hit_held{};};
  class CarryDiagnostics {public:
      void note(const CarryFrame&);void clear_seen(unsigned reason);
      std::string json() const;        // {"frames":,"rigid_frames":,"scale_frames":,"clears":[r0..r3],"swaps":,"restores":,"max_residual_age_ms":,"max_quat_dev":,"clamp_hits_held":}
      static std::string csv_header();static std::string csv_row(const CarryFrame&);};
  ```
  `NativeHeldObject` exposes `static std::optional<std::uint32_t> held_layer(base,read,entity)` (filter word & 0x1f, read only) and `inline static std::atomic<float> anchor_quat_dev_max`. `rf_quat_norm` is the norm of the 4 floats at `rf+0x3e0`, read on the render thread. `clamp_hit_held` is true when the palm or tip hit entity equals the held entity. The CSV is capped at 20 000 rows per session; I/O failure never throws into present.

- [ ] **Step 1: Write the failing tests.**
  - `carry_diagnostics_check`: 10 frames of a hold, with a `clear_seen(2)` at frame 5 and the layer going from 4 to 3 from frame 6. JSON has `rigid_frames==5` and `clears[2]==1`. `render_scale` non-zero on frames 0–1 only gives `scale_frames==0`; on frame 7 it gives 1. `residual_age_ms` −1 prints as `null`. A CSV row has 10 comma-separated fields.
  - `held_object_routing_check.py`: `NativeHeldObject::clear(1)`, `clear(2)` and `clear(3)` at the three sites; `carry_diagnostics.note(` inside `publish_held_object`; `\"carry\":` in the status.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "carry_diagnostics|held_object_routing|native_held_object_check"`.
- [ ] **Step 3: Implement** as in Interfaces. `anchor()` stores `max(|norm-1|)` before it rejects or normalises.
- [ ] **Step 4: Run, expect a pass.** Same command plus `--only dxgi_probe`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/carry_diagnostics.hpp src/native_held_object.hpp src/drawn_palm_residual.hpp src/dxgi_probe.cpp tests/carry_diagnostics_check.cpp tests/held_object_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: per-frame carry diagnostics in the status and a carry trace CSV"
  ```

---

### Task 10: Re-apply the DEBRIS layer after a transient clear (G6, C7), then the full run

**Files:**
- Modify: `src/native_held_object.hpp:153-188` (split the layer service from `world_render`; add a settled mirror)
- Modify: `src/native_interaction_aim.hpp:549-550` (restore before re-apply)
- Modify: `src/dxgi_probe.cpp:2906` (request while the layer is not settled)
- Test: `tests/native_held_object_check.cpp` (after the layer block, `:287-303`); `tests/held_object_routing_check.py`

**Interfaces:**
- Consumes: Task 9 counters (`layers_swapped`, `layers_restored`, `CarryDiagnostics`).
- Produces: `template<class Read> static void service_hold_layers(std::uintptr_t base,Read& read,std::uintptr_t entity);` On the game thread: if `layer_restore_.exchange(false)`, call `restore_layer`; then, if `entity`, call `debris_layer(entity)`. `static bool layer_settled(std::uintptr_t entity) noexcept;` (atomic mirror: the entity was swapped, or checked and not RIGID_BODY; cleared by `restore_layer` and by `clear`). `world_render` keeps only the render-scale write.

- [ ] **Step 1: Write the failing tests** in `native_held_object_check.cpp` (fixture and `filter_seam_` as at `:287-303`):
  - `transient_clear_reapplies_debris`: `service_hold_layers(entity)` swaps (writes 1, `layer_settled` true). `Held::clear()` makes `layer_settled` false. `service_hold_layers(entity)` restores and then swaps again: writes 3, the last is DEBRIS, `layers_swapped==2`.
  - `hold_end_restores`: `clear()` followed by `service_hold_layers(0)` leaves RIGID_BODY as the last write.
  - `bottle_settled_without_write`: a DEBRIS-layer entity gives `layer_settled` true with no write.
  - `held_object_routing_check.py`: Stage0 calls `service_hold_layers(` before the render-scale `world_render(`; `publish_held_object` requests when `fp!=0.f || !d2vr::NativeHeldObject::layer_settled(*e)`.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only "native_held_object_check|held_object_routing"`.
- [ ] **Step 3: Implement** as in Interfaces. `held_world_` carries the entity for both requests; Stage0 runs `service_hold_layers(held)` and then `world_render(held)`.
- [ ] **Step 4: Run, expect a pass.** Same command. Then the full harness: `python3 tools/linux/run_build_cmd.py`. Expected: 0 failed, 41 skipped. Passed = 468 plus the new steps (`push_lab`, `hand_clamp_check`, `hand_reach_check`, `grip_ik_pipeline_check`, `hand_clamp_routing_check`, `palm_offset_check`, `carry_diagnostics_check`), each counted as the harness counts compile and run steps. Record the exact count in the commit message.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/native_held_object.hpp src/native_interaction_aim.hpp src/dxgi_probe.cpp tests/native_held_object_check.cpp tests/held_object_routing_check.py
  git commit -m "fix: re-apply the held object's DEBRIS layer after a transient clear"
  ```

---

## Headset checklist (user; not automated)

Run on the Windows build after `tools\build-msvc.cmd` passes. Keep the status JSON (`hand_clamp`, `hand_push`, `palm_offset`, `carry`) from each run.

1. Press a palm onto a table and onto a wall: the hand rests on the surface while the real hand keeps going, and the arm reaches it with no wrist gap.
2. Slide along a wall: the hand slides with no jitter.
3. Swat a bottle and a wall fast: the bottle flies, and the hand never shows inside the wall.
4. Push a crate (it resists, the hand is held back) and a bottle (it moves with the hand).
5. Touch an awake guard (the hand stops, nothing else happens) and push a knocked-out guard (the body moves).
6. Reach through a wall and pull back: one clean snap-back and no flicker. Walk and snap-turn with a hand on a wall: no snap-back.
7. Calibrate (Pause → VR Settings → Calibrate → X: hand fit), then confirm the hand sits in the controller for both hands. Draw the sword and pistol: weapon direction unchanged.
8. Carry an oil tank for 60 s standing and 60 s walking: no image breaks. Keep `native-carry-trace.csv` and the `carry` status.
9. Frame time: `hand_clamp.cast_us` ≤ 500 with both hands near geometry; note whether `tip_probe` turned off.
10. Combat with a stopped hand: aim, crosshair, attack/block, powers and takedowns behave as before.

## Self-review

- **Spec coverage:**
  - G1, C4, C8: Tasks 1–3, 5.
  - G2, C10: Tasks 4, 5.
  - G3, C3, C11: Tasks 2, 5.
  - G4, C5, C13: Task 6.
  - C2: Tasks 5 (awake guard stops the hand), 7.
  - G5, C6, C12: Task 8.
  - G6, C7: Tasks 9, 10.
  - C9: Task 5 routing check.
  - C14: Tasks 2 (`CastBudget`), 5 (status).
  - Done-means checks:
    - clamp policy: `hand_clamp_check`;
    - snap-back: `hand_clamp_check`;
    - reach: `hand_reach_check`;
    - IK feed: `grip_ik_pipeline_check`;
    - pushes: `hand_contact_check`;
    - calibration: `palm_offset_check`;
    - carry re-apply: `native_held_object_check`;
    - `wall_cast`: `push_lab`.
- **Types:** These names are used the same way across tasks: `ClampHit`, `ClampInput`, `ClampOutput`, `HandClamp::note_drawn`, `leading_probe`, `CastBudget`, `next_meet_latch`, `ArmReach`, `reach_clamp`, `PalmOffset`, `apply_palm_offset`, `CarryFrame`, `service_hold_layers`, `layer_settled`.
- **Open for the user:** whether the palm tilt should also tilt held weapons. Task 8 applies translation only to armed hands, to keep the accepted weapon direction.
