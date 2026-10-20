# Bonelab Feel B: Physics Hand Body, Held-Object Collision and the Tank Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The hands are Havok bodies (Bonelab-style physical hands) so things held collide with the world, and a dropped whale-oil tank rests on the floor instead of falling through it. The held-body clamp and safe release ship first as the fix and stay as the fallback when the hand body is unavailable (user decision 5 of 2026-10-20: "full hand body now").

**Architecture:** Fallback first (Tasks B1-B5): a pure `HeldClamp` plane clamp on the native hold's drive target, a `SafeRelease` that keeps the DEBRIS layer until the dropped object is clear of the world, wired in `publish_held_object`. Then an RE and probe block (B6-B7), the offline pure layer (B8-B10: command encoders and filter mask, servo law, lifecycle machine), a creation spike across candidate routes with a go/no-go gate (B11-B12), and only then the production `HandBody` (B13), its drive and proofs (B14), the visible hand from the body (B15) and the coupling of held things to it (B16). Every engine call after B6 is behind SEH, a knob, a session fault latch and world-identity checks.

**Tech Stack:** C++17 headers (MSVC `cl /W4 /WX`, clang-cl + Wine with the `-Wshadow` family as errors), MinHook, Havok hknp through fixed RVAs and the engine command queue drained by 0x89fb0; checks in `tools\build-msvc.cmd`; Python source guards; `linux/d2vr-linux sim` for Tier B.

**Spec:** `docs/superpowers/specs/2026-10-19-bonelab-feel-design.md` (Part 5, F2, "Resolved questions (2026-10-20, answered by the user)" item 5). Research: `docs/superpowers/specs/2026-10-18-physical-hands-research.md` ("research §2.x, Q4 (B)/(C), Recommended approach phases 2-4, Open RE items 1-12") and `~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` ("Bonelab §").

**Depends on plan A** (`docs/superpowers/plans/2026-10-20-bonelab-feel-a-foundations-grab.md`): Tasks B1-B4 are independent of it except Task 1's test saves; B5 needs plan A Task 13's `tools/linux/scenario_lib.py` and `tools/linux/bonelab_run_check.py`; B9 and B16 consume plan A Task 2 (`cap_velocity_step`, `GrabTuning`), B13 and B14 consume plan A Tasks 4-5 (`FaultLatch`, `CorpseDiagnostics` style), B16 consumes plan A Tasks 7, 10 and 12 (`PhysicalGrab::anchor`, `NativeGrab`, `DrawnAnchor`).

**Later plans (not here):** Part 2 throw history and spin, Part 3 touch-sensor finger curl, Part 4 palm-facing scoring; full physics doors (they build on this hand body).

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `cd` into another checkout, never `git stash`. Plan workspace (git-excluded): `.superpowers/sdd/2026-10-20-bonelab-feel-b-hand-body/`.
- Pinned exe `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. Every RVA used is byte-pinned before it is called; a mismatch disables the hand body (fallback runs).
- Every engine write keeps the SEH guard, finite checks, fault counters, handle generation (`(gen+1)<<24 | index`) and world identity checks. Held-clamp tasks (B1-B5) add no new engine call type. From B11 on, only these may be called, and only from the hand-body code: `createBody` 0xc8e5b0 (cinfo ctor 0xca65f0), `destroyBodies` 0xc8f6b0, `setBodyMotionType` 0xc86800, `setBodyCollisionFilterInfo` via 0x97600, `setBodyTransform` 0xc85860 (snap-back only, after an overlap check), velocity 0xc85dc0, always through the 0x89fb0 command queue when physics is busy (0x8a7820) and directly otherwise. `createConstraint` 0xc90de0 stays out.
- Hand body: a sphere per hand, dynamic, gravity compensated, filter word = a hand layer plus a collides-with mask that omits PLAYER 6, PLAYER_HITBOX 7, PLAYER_DEAD 8 and PLAYER_PHYSICS 18 (research §2.4: the mutual test 0x992c0 then never pairs it with the player); force caps along the arm axis about 300 N and laterally about 90 N, torque 25-30 N·m (plan A's `GrabTuning` base values, unscaled for the hand itself); hand mass the small fixed value phase 1 uses (`HandContactConfig::strength_kg`, 2 kg); the chest reaction dropped (hands never move the player).
- The four proofs (done criteria for the body): no crash for 10 minutes cumulative near props, guards and doors; the player is never pushed; save/load is clean; frame-time delta ≤ 0.2 ms (frame budget: new work ≤ 0.2 ms for both hands, `cost_us` / `cost_max_us`, on top of phase 1's ≤ 0.5 ms cast budget; held-clamp casts count against it, ≤ 4 casts per side per frame).
- Held-body clamp numbers: sweep from the last published pose with the engine cast 0xc89bc0, held body excluded; release when the drive target is 0.35 m from the clamped target for 0.25 s; safe release pushes out at ≤ 1.5 m/s for ≤ 0.5 s while the DEBRIS layer is kept, restores anyway and counts `release_overlap_unresolved` when it cannot; no `setBodyTransform` in the fallback.
- Release/regrab: 0.5 m lag for 0.25 s, 0.3 s regrab cooldown, 0.25 s visual blend, drawn hand within 0.35 m of the raw controller (plan A values; this plan reuses them).
- C9 routing stays the law: the raw controller pose drives intent (grab, selection, aim); the drawn pose (body or clamped) drives the hand, arm IK and drawn weapon; native authored animations keep ownership and the hand body yields.
- Time factor: `time_factor_verified` and `mass_verified` stay false; commanded velocities use factor 1 unless the factor is readable, and the status records it.
- Every task brief cites the Bonelab technique or numbers it follows, in our own words (Bonelab §1.2 the held object is a dynamic body on the hand's joint, §2.1 the hand is a physical body pulled by clamped forces with braking, §2.2 the hand lags when blocked, §1.4 effective mass). No Bonelab code or assets.
- Every new check is registered in `tools\build-msvc.cmd` with `/std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc` and fails the build on non-zero exit. The Linux harness (`python3 tools/linux/run_build_cmd.py`, baseline 483 passed / 0 failed / 41 skipped plus plan A's additions) stays green with the `-Wshadow`, `-Wshadow-field`, `-Wshadow-field-in-constructor` family as errors; run one task with `python3 tools/linux/run_build_cmd.py --keep-going --only '<regex>' candidate d2vr_render_probe_arm_v601.dll`.
- Mutation checks run after the task commit: edit the listed constant with `sed` on the committed header, rerun the check (it must fail), `git checkout -- <header>`; record kills in the ledger.
- Testing honesty: Tier A and Tier B pin logic and crash-safety only; only Tier C (headset) declares feel and comfort met. Anything the sim cannot reach is a Tier C item, never a silent skip.

## Spec deviations

1. **`HandClamp` is reused for types and ranking only.** `src/hand_clamp.hpp` (946 lines) is hand-shaped (palm/tip probes, chest re-acquire, snap-back). B1 adds a small `HeldClamp` reusing `ClampHit`, `ClampSweep` and the `BodyClass` rank.
2. **A native hold is forced to end with the game's own Use press**, not a new lever: `dxgi_probe.cpp:7270` (`held_use_until`, `input.x=true`) already drops a held object.
3. **The tank's drive target is `NativeHeldObject::Target` (`publish_held_object`, render thread).** The clamp substitutes `t.pos` before `publish`. Hits come from the game thread one frame late (`NativeInteractionAim::weapon_hit_`).
4. **A hand body alone cannot stop a keyframed native hold.** HoldObject keyframes the item toward the mod-published target, so a sphere on the hand never sees the tank's box. For native holds the mechanism is therefore the `HeldClamp` of B1-B4 in both modes; with the hand body ready, B16 feeds it the hand body's pose as the requested target and drives the body toward the clamped result, so hand, body and object agree. Motor-driven holds (props and corpses through `PhysicalGrab`) collide for real: the held body is dynamic and the hand body is its anchor. An experiment that moves carryables from native holds to physical holds is not part of this plan (earlier sim: lost holds, `native_rigid_body.hpp:38-41`); say so to the user at the checkpoint (B12).
5. **The spec's staged "5B spike with go/no-go" is now the build** (user decision). The checkpoint (B12) asks the user only if the RE shows the body infeasible.
6. **Sim sessions last 10-120 s** (`linux/d2vr_linux/sim.py:496`): the 10-minute soak is 5 sessions of 120 s, S14 (100 drops) is 4 sessions of 25; the checker sums the counters.

## Review Focus

1. Stale or recycled handles and world pointers: a hand-body write after a world change, load, death reload or streaming change must be impossible. Pinned by B10 `stale_world_never_writes` and B13 `write_after_world_change_refused`.
2. A body created inside geometry or snapped back into it: depenetration must not fire the hand (or a held object) out at speed. Pinned by B9 `snap_target_overlap_refused` and B2 `release_inside_floor_resolves_within_half_second`.
3. Garbage readback (NaN, huge velocity, unreadable bounds, zero-size model box) must never reach a command record or a clamp. Pinned by B1 `unreadable_bounds_no_clamp`, B8 `non_finite_never_encodes` and B14 `readback_garbage_latches_body_off`.
4. Pause, hitch or first-frame `dt` (0, negative, 0.5 s) in the servo law and the clamp's plane ageing. Pinned by B9 `hitch_dt_clamped` and B1 `epoch_change_resets_planes`.
5. A corner (floor plus wall) with the sweep one frame late, and a released object inside a corner with conflicting normals. Pinned by B1 `corner_two_planes_no_penetration` and B2 `corner_release_resolves_or_counts`.

---

## File Structure

Created:
- `src/held_clamp.hpp`: `HeldClamp`, `held_proxies`, `SafeRelease` (fallback and native-hold clamp).
- `src/hand_body_probe.hpp`: read-only runtime probe rows (B7).
- `src/hand_body_commands.hpp`: command-record encoders, filter words and the mutual-collision predicate (B8).
- `src/hand_servo.hpp`: the force-capped servo law with braking and snap-back request (B9).
- `src/hand_body_lifecycle.hpp`: create/destroy state machine (B10).
- `src/hand_body_spike.hpp`: the creation routes under test (B11); superseded by `src/hand_body.hpp` (B13).
- `src/hand_body.hpp`: production `HandBody` manager (B13-B14).
- `src/hand_visual_source.hpp`: body versus clamp versus controller choice and blend (B15).
- `src/hold_coupling.hpp`: what a held thing is driven toward (B16).
- `tools/held_clamp_lab.cpp`: Tier A lab.
- `tools/linux/scenarios/tank_wall.py`, `tank_drop.py`, `tank_drop_cycles.py`, `hand_body_soak.py`, `hand_body_saveload.py`, `tank_hand_body.py`.
- `docs/native-hand-body-re.md` (evidence), `docs/VR-bonelab-feel-held-clamp-TEST-CHECKLIST.md`, `docs/VR-bonelab-feel-hand-body-TEST-CHECKLIST.md`.
- Tests: `tests/held_clamp_check.cpp`, `held_clamp_routing_check.py`, `hand_body_re_doc_check.py`, `hand_body_probe_check.cpp`, `hand_body_commands_check.cpp`, `hand_servo_check.cpp`, `hand_body_lifecycle_check.cpp`, `hand_body_check.cpp`, `hand_visual_source_check.cpp`, `hold_coupling_check.cpp`, `hand_body_routing_check.py`.

Modified:
- `src/native_held_object.hpp:97-101,192-212` (layer upkeep and deferred restore), `src/native_interaction_aim.hpp:458-497,618-660` (held sweeps and release probe reuse the clamp slots), `src/native_grab.hpp:431-446` (release depen write beside `apply_poke`; B16 hand-body target for motor holds), `src/dxgi_probe.cpp` (`publish_held_object` 2840-3010, forced Use ~7270, status JSON beside `"carry"` ~5250, knobs ~8135, publication block ~4211-4453), `tools/linux/bonelab_run_check.py`, `tools/build-msvc.cmd`.

---

### Task B1: `HeldClamp`, the held object stops at surfaces (fallback and native-hold clamp; Bonelab §1.2, §2.1, §2.2)

Bonelab's held object is a dynamic body on a force-limited joint, so a wall stops it and the hand lags. We have no joint: the held body's drive target is moved to where the body is clear of the world, and the drawn hand stays with it.

**Files:**
- Create: `src/held_clamp.hpp`
- Test: `tests/held_clamp_check.cpp`; register after `hand_clamp_check` (`build-msvc.cmd:803-806`)

**Interfaces:**
- Consumes: `ClampHit`, `ClampSweep`, `BodyClass`, `Vec3`, `NativeGrabBounds`, `WorldTrackedPose`.
- Produces:
  ```cpp
  struct HeldProxy {Vec3 local{};double radius{};};                         // sphere in the held body's frame
  inline constexpr std::size_t held_proxy_capacity=4;
  struct HeldProxies {std::array<HeldProxy,held_proxy_capacity> sphere{};std::size_t count{};};
  std::optional<HeldProxies> held_proxies(const NativeGrabBounds& world_box,const WorldTrackedPose& pose);   // spheres along the long axis; none when unreadable
  struct HeldClampInput {double now{};std::uint64_t epoch{};WorldTrackedPose requested{};std::optional<HeldProxies> proxies;
      std::array<std::optional<ClampHit>,held_proxy_capacity> hits{};bool yield{};};
  struct HeldClampOutput {bool valid{};WorldTrackedPose target{};Vec3 shift{};bool clamped{};bool no_bounds{};
      std::array<ClampSweep,held_proxy_capacity> sweeps{};double gap_m{};};
  struct HeldClampConfig {double skin=.005,plane_seconds=.1,release_gap=.35,release_seconds=.25;unsigned max_planes=4;};
  class HeldClamp {public:explicit HeldClamp(HeldClampConfig={});
      HeldClampOutput update(const HeldClampInput&);       // last frame's hits in, this frame's sweeps and the clamped target out
      bool should_release() const;                          // gap > 0.35 m for > 0.25 s
      void note_published(const WorldTrackedPose& target);  // what was finally published (next sweeps start there)
      void reset();};
  ```
  Algorithm not fixed by the signature: sweeps run from the last published pose's proxies to the requested pose's proxies (radius per proxy); each answered hit becomes a plane (point, normal, rank by `BodyClass`); the clamped target is the requested pose pushed out along the planes iteratively (≤ 4 passes, highest rank first, keeping every proxy sphere `skin` outside), as `HandClamp` ranks them. Planes live `plane_seconds` without a re-hit. `gap_m = |requested − target|`.

- [ ] **Step 1: Failing tests** (fake world: wall x = 1 normal −x, floor z = 0, answers one frame late like the hand clamp tests):
  - `wall_carry_0p1_to_3ms`: tank proxies 0.2 m radius carried at 0.1, 0.5, 1, 2, 3 m/s into the wall: after the hit arrives, no frame has a proxy surface more than 1 cm past the plane (spec 5A).
  - `floor_press_stops_at_floor`: requested target 0.3 m below the floor for 2 s: published proxy bottoms ≥ −0.005 + radius clearance (never below the floor by more than 1 cm).
  - `corner_two_planes_no_penetration` (Review Focus 5): wall plus floor corner, requested target inside both: clamped target is outside both planes within 3 frames and finite.
  - `free_space_untouched`: no hits → `target == requested` bit-exact, `clamped` false.
  - `unreadable_bounds_no_clamp` (Review Focus 1): `proxies` empty → `target == requested`, `no_bounds` true, no sweeps.
  - `held_proxies_long_axis`: a 0.3 × 0.3 × 0.9 box gets 3-4 spheres along the long axis; a cube gets 1; a box over 3 m or non-finite gets none.
  - `release_after_quarter_second`: requested held 0.4 m behind the clamped target for 0.24 s: not released; at 0.26 s `should_release()`; a 0.30 m gap for 2 s never releases.
  - `non_finite_requested_invalid`, `epoch_change_resets_planes`, `yield_clears_state`.
- [ ] **Step 2: Run, expect failure.** `... --only 'held_clamp_check'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/held_clamp.hpp tests/held_clamp_check.cpp tools/build-msvc.cmd
  git commit -m "feat: HeldClamp, plane-based clamp of a held object's drive target (Part 5A)"
  ```
- [ ] **Step 6: Mutation pass.** `skin=.005=>.05`, `release_seconds=.25=>.5`, `release_gap=.35=>.5`, `held_proxy_capacity=4=>1`: each fails.

---

### Task B2: `SafeRelease`, never restore the layer inside geometry (fallback)

On release the object keeps the DEBRIS layer and gets a small outward velocity until a zero-length cast from its pose reports free, then the RIGID_BODY layer returns. The tank fell through the floor because it was restored inside it (ledger 2026-10-19 23:10).

**Files:**
- Modify: `src/held_clamp.hpp` (add `SafeRelease`)
- Test: `tests/held_clamp_check.cpp` (extend)

**Interfaces:**
- Produces:
  ```cpp
  enum class ReleasePhase {idle,checking,depenetrating,done};
  struct SafeReleaseInput {double now{};bool released{};std::optional<ClampHit> overlap;bool sweep_answered{};};  // overlap: answer to the zero-length cast from the released pose
  struct SafeReleaseOutput {ReleasePhase phase{};Vec3 velocity{};bool write{};bool restore_layer{};bool unresolved{};std::uint64_t depen_frames{};};
  class SafeRelease {public:static constexpr double max_speed=1.5,max_seconds=.5;
      SafeReleaseOutput update(const SafeReleaseInput&);void reset();};
  ```
  Flow: `released` starts `checking` (layer not restored); an answered cast with no overlap → `done`, `restore_layer`; an answered overlap with a valid normal → `depenetrating`: `write` an outward velocity along the contact normal (`min(1.5, depth*20 + 0.3)` m/s, never above `max_speed`) every update until the cast reports free (→ `done`, restore) or `max_seconds` pass (→ `done`, `restore_layer`, `unresolved`); an overlap without a valid normal pushes straight up (+z). An unanswered cast waits (up to `max_seconds`, then restore + `unresolved`). The layer is never restored in `checking` or `depenetrating`.

- [ ] **Step 1: Failing tests:** `release_in_free_space_restores_immediately` (first answer free: restore, no write), `release_inside_floor_resolves_within_half_second` (lab-style fake: depth 4 cm, floor normal +z: free by ≤ 0.5 s, every written speed ≤ 1.5, restore only after free), `layer_not_restored_while_checking` (no answer for 0.4 s: `restore_layer` false), `never_frees_counts_unresolved` (0.5 s: restore + `unresolved`), `corner_release_resolves_or_counts` (Review Focus 5: two alternating normals: terminates within 0.5 s with either outcome, speeds ≤ 1.5), `no_normal_pushes_up`, `non_finite_overlap_ignored`, `reset_idles`.
- [ ] **Step 2: Run, expect failure.** `... --only 'held_clamp_check'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/held_clamp.hpp tests/held_clamp_check.cpp
  git commit -m "feat: SafeRelease keeps the DEBRIS layer and pushes a dropped object out of geometry"
  ```
- [ ] **Step 6: Mutation pass.** `max_speed=1.5=>3`, `max_seconds=.5=>1`, restore while `checking`: each fails.

---

### Task B3: Held-clamp lab (Tier A)

**Files:**
- Create: `tools/held_clamp_lab.cpp` (a separate binary: `push_lab.cpp` models a hand, not a keyframed hold; its wall/floor scene geometry is copied, not shared)
- Modify: `tools/build-msvc.cmd` (after the Task B1 registration)

**Interfaces:**
- Consumes: `HeldClamp`, `SafeRelease`, `held_proxies`. Produces: `build\held_clamp_lab.exe <out_dir>` (`scenarios.json` + CSVs), exit 1 on a judged failure.

- [ ] **Step 1: Write the lab; judged scenarios (smooth 90 Hz input):**
  - `wall_carry`: a 0.3 × 0.3 × 0.9 m tank-like box carried into a wall at 0.1, 0.5, 1, 2, 3 m/s: the body ends ≤ 1 cm past the plane (same style as `wall_cast`).
  - `floor_press`: held pressed 0.3 m into the floor then released: restored layer only after free; final height above the floor plane after 5 simulated seconds; `release_overlap_unresolved == 0`.
  - `release_inside_floor`: resolves within 0.5 s.
  - `release_free_space`: untouched (no write, restore on the first answer).
  - `corner`: wall + floor press, release: terminates, finite.
  - `blocked_hand_lag`: hand continues 0.5 m past the stopped body: the drawn-hand gap stays ≤ 0.35 m until the release at 0.25 s.
  - info: `stepped_30ms`.
- [ ] **Step 2: Run, expect failure** before the lab matches production defaults. `python3 tools/linux/run_build_cmd.py --keep-going --only 'held_clamp_lab' candidate d2vr_render_probe_arm_v601.dll`.
- [ ] **Step 3: Tune only the lab's model** (never silently loosen a limit): record each change.
- [ ] **Step 4: Run, expect pass.** `held_clamp_lab: N scenarios, 0 failed`. Mutation pass: `skin`, `max_speed`, restore-while-checking each fail a scenario.
- [ ] **Step 5: Commit.**
  ```bash
  git add tools/held_clamp_lab.cpp tools/build-msvc.cmd
  git commit -m "test: held-clamp lab: wall carry, floor press, release inside the floor"
  ```

---

### Task B4: Wire the held clamp and safe release into the carry (fallback engine integration, the tank fix)

The tank is a native HoldObject: keyframed toward `NativeHeldObject::Target`. The clamp replaces `t.pos` before `publish`; the drop uses the game's Use press. DeepSeek second-opinion review (concurrency across render and game threads, layer writes).

**Files:**
- Modify: `src/dxgi_probe.cpp`: `publish_held_object` (before `d2vr::NativeHeldObject::publish(t)`; bounds from `*model+0x1840` already read in that function), forced drop via `held_use_until` (`:7270`), drawn-hand shift in the publication block (the `carry_bits` path ~`:4211-4272`), status `,"held_clamp":`, `build/no-held-clamp` knob
- Modify: `src/native_interaction_aim.hpp:618-660`: held sweeps use the clamp slots (`0`, `tip_slot`, `wall_tip_slot`, `exclude_slot` of the holding side: 4 spheres, exclusion = the held entity), plus `post_release_probe(side, pose)` / `release_probe_hit(side)` for the zero-length cast (slot 0) and a `request_release_depen(entity, velocity)` consumed in `NativeGrab::apply_poke`'s style
- Modify: `src/native_grab.hpp` beside `apply_poke` (`:431-446`): `apply_release_depen`, same 100 ms staleness and `write_velocity` guard
- Modify: `src/native_held_object.hpp:97-101,198-212`: `clear()` no longer arms `layer_restore_` while a safe release is pending; `NativeHeldObject::allow_restore()` releases it when `SafeRelease` says `restore_layer`
- Test: `tests/held_clamp_routing_check.py` (source guard) and extend `tests/native_held_object_check.cpp`

**Interfaces:**
- Consumes: `HeldClamp`, `SafeRelease` (Tasks 14-15), `NativeHeldObject` layer API (`service_hold_layers`, `layer_settled`, `restore_layer`).
- Produces:
  ```cpp
  // NativeHeldObject
  inline static std::atomic<bool> restore_hold_{};               // true while a safe release is pending: clear() leaves layer_restore_ alone
  static void begin_safe_release() noexcept;static void allow_restore() noexcept;   // render thread; allow_restore arms layer_restore_
  // NativeInteractionAim
  static void post_held_sweeps(unsigned side,const std::array<ClampSweep,held_proxy_capacity>&,std::uintptr_t exclude);
  static std::array<std::optional<WorldTraceHit>,held_proxy_capacity> held_hits(unsigned side,std::uint64_t now);
  static void post_release_probe(unsigned side,Vec3 at,double radius,std::uintptr_t entity);
  static std::optional<WorldTraceHit> release_probe_hit(unsigned side,std::uint64_t now);
  static void request_release_depen(std::uintptr_t entity,Vec3 velocity);
  ```
  Status block `held_clamp`: `sweeps`, `hits`, `clamped_frames`, `cast_us`, `no_bounds` (`held_clamp_no_bounds`), `overlap_on_release`, `release_depen_frames`, `release_overlap_unresolved`, `drawn_hand_gap_m`, `layer_at_release`, `forced_drops`. Behaviour: while a native hold is active and `HeldClamp::update` returns `clamped`, the published `t.pos` is the clamped target (axes unchanged), and the drawn grip for that side is shifted by `target − requested` (bounded by `DrawnAnchor::bound_m`); `should_release()` sets `held_use_until = now + 100` (native drop) and counts `forced_drops`; when the hold ends (`clear()`), `SafeRelease` runs on the released pose: probe cast, then depen velocity via `request_release_depen` (consumed in the dispatcher), then `allow_restore()` and `layer_at_release` recorded. With `build/no-held-clamp` or unreadable bounds nothing changes (`no_bounds` counted).

- [ ] **Step 1: Failing tests:** `native_held_object_check.cpp`: `safe_release_defers_layer_restore` (`begin_safe_release`, `clear()`, `service_hold_layers(0)` does not write the restore; `allow_restore()` then `service_hold_layers(0)` restores: last write RIGID_BODY), `transient_clear_still_reapplies_debris` (the phase-1 task 10 case stays green). `held_clamp_routing_check.py`: in `publish_held_object` the clamp call sits after the pose computation and before `NativeHeldObject::publish(t)`; `set_filter`/layer restore appear only in `native_held_object.hpp`; the forced drop only writes `held_use_until`; `held_clamp` status key present; the held sweeps use `exclude=*e`; no call to `0xc85860`/`0xc90de0` in the files this task touches (grep).
- [ ] **Step 2: Run, expect failure.** `... --only 'native_held_object|held_clamp_routing|held_object_routing'`.
- [ ] **Step 3: Implement.** Engine unknowns are measured, not assumed: the status exposes `hits` against `sweeps` so Task B5 can show the held-body exclusion reports walls; a keyframed HoldObject whose target is clamped is expected to keep keyframing toward the substituted pose [U] (Task B5 S12 observes it).
- [ ] **Step 4: Run, expect pass.** Same command plus the full harness: 0 failed, 41 skipped; record the passed count.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/dxgi_probe.cpp src/native_interaction_aim.hpp src/native_grab.hpp src/native_held_object.hpp tests/native_held_object_check.cpp tests/held_clamp_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: clamp the carried object's target and keep its layer until a drop is clear of the world"
  ```

---

### Task B5: Tank-fix Tier B scenarios, Tier C checklist, closing run

**Files:**
- Create: `tools/linux/scenarios/tank_wall.py` (S12: carry the tank to the wall by `move_y`, then onto the floor), `tank_drop.py` (S13: drop from 1.5 m; drop pressed into the floor), `tank_drop_cycles.py` (S14: 25 drops per session), `docs/VR-bonelab-feel-held-clamp-TEST-CHECKLIST.md`
- Modify: `tools/linux/bonelab_run_check.py` (S12-S14 assertions), `tests/bonelab_scenarios_check.py`

**Interfaces:**
- Assertions: S12 `held_clamp.sweeps > 0`, `hits > 0`, `clamped_frames > 0`; S13 `release_overlap_unresolved == 0`, `layer_at_release` recorded, and the tank's final height read from `held_debug`/the status above the floor plane after 5 s (the scenario waits 5 s after the last release); S14 summed over 4 sessions: 100 releases, `release_overlap_unresolved == 0`, `corpse.fault_latch == false`, no new `carry` fault counters.

- [ ] **Step 1: Failing test:** scenarios load and compile like plan A Task 13 (it provides `tools/linux/scenario_lib.py` and `tools/linux/bonelab_run_check.py`; both must exist first); `bonelab_run_check` fixtures: S13 FAIL when `release_overlap_unresolved:1`, PASS when 0.
- [ ] **Step 2: Run, expect failure.** `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 3: Implement** the scenarios and assertions; write the Tier C doc (spec's four items: tank into a wall and the floor; drop from chest height; drop while pressed to the floor; carry a plank through a doorway; save `held_clamp` after each).
- [ ] **Step 4: Run, expect pass.** `python3 tests/bonelab_scenarios_check.py`.
- [ ] **Step 5: Tier B runs** (`~/d2vr-test-saves/tank-floor`; ledger "outstanding" when missing): S12, S13, S14 ×4; record `release_overlap_unresolved`, `cast_us`, `hits/sweeps`, and the observed keyframe reaction to a clamped target.
- [ ] **Step 6: Closing run.** `python3 tools/linux/run_build_cmd.py`: 0 failed, 41 skipped, passed = 483 plus the new steps (`grab_force_cap_check`, `grab_mass_check`, `ragdoll_safety_check`, `corpse_diagnostics_check`, `corpse_tow_check`, `drawn_anchor_check`, `held_clamp_check`, `corpse_hold_check`, `corpse_drag_lab`, `held_clamp_lab`, the Python guards); record the count in the commit message. Update the ledger with Tier A/B status, the outstanding Tier B list and the Tier C pointers.
- [ ] **Step 7: Commit.**
  ```bash
  git add tools/linux/scenarios tools/linux/bonelab_run_check.py tests/bonelab_scenarios_check.py docs/VR-bonelab-feel-held-clamp-TEST-CHECKLIST.md tools/build-msvc.cmd
  git commit -m "test: held-clamp Tier B scenarios, run checks and the Part 5A headset checklist"
  ```

---


### Task B6: Hand-body static RE: userdata contract, shapes, cinfo, filters (research open items 1, 3, 7, 8, 9, 11)

Document-only, but the document gates every later task: no encoder, creation call or userdata layout is written from a guess. Use a DeepSeek second-opinion review on the findings. Time-box two days; a question left open stays tagged [U] and becomes a runtime item for B7 or B11.

**Files:**
- Create: `docs/native-hand-body-re.md`
- Test: `tests/hand_body_re_doc_check.py`; register after `tests\live_session_tools_check.py` in `tools/build-msvc.cmd`

**Interfaces:**
- Produces: sections `Q1`..`Q8` in the doc, each with a verdict tag [V] / [I] / [U], the RVAs involved, a `bytes:` line with the pinned prologue bytes of every function a later task will call or hook, and a decompile excerpt reference (script and function). Questions:
  - Q1 **Userdata contract**: the fields and vtable slots read from `body+0x90` by 0x93a40 (`[+8]`, vtbl +0x20/+0x28), 0x8527e0 (vtbl[0], +0x98) and 0x94700 (`+0x60/+0x68`, vtbl +8); the minimum fake userdata layout that is safe, or an engine path that creates a component-owned body at runtime (0x1b6ec0's player second body, rigid-body components 0x737300/0x73b420, debris 0xcca020).
  - Q2 **Shape**: sphere constructor 0xca6550 signature, storage marker and refcount rules for a persistent body; box/capsule constructors if cheap.
  - Q3 **createBody call**: argument values (`addFlags`, activation) used by 0x1b6ec0 and one rigid-body caller; whether add flag 2 defers the add; cinfo field confirmation (research §2.1 table).
  - Q4 **Motion**: motion-properties ids (gravity factor 0), creating a motion from the cinfo (`+0x28`), `setBodyMass` 0x13, the `motion+0x26` inverse-mass encoding.
  - Q5 **Held-object pairing**: the disabled-pair insert routine (filter +0x20 hash, reader 0x93b00) so a hand ignores what it holds; the fallback is a layer swap with a mask that omits the held object's layer.
  - Q6 **Flags**: what body flag bits 7 and 19 mean (`OnContactImpulse` branches).
  - Q7 **World filter identity** (world +0x470: arkCollisionFilter 0x1ad4c08, the hkfx wrapper, or the group filter).
  - Q8 **Queue drain**: which thread drains `arkPhys+0x148288`, when relative to the step 0x97bc0, whether 0x89fb0 runs every frame without mod-queued commands.

- [ ] **Step 1: Failing test** `hand_body_re_doc_check.py`: the file exists; every `## Q1`..`## Q8` heading exists; each section has a verdict tag in `[V] [I] [U]`; every RVA written as `0x` followed by 5-7 hex digits in a section is accompanied somewhere in that section by a `bytes:` line (hex byte list) or by the word `known` with a cited earlier doc; the summary table at the end lists all eight with a column `blocks route R1` / `R2` / `R3` (B11 routes).
- [ ] **Step 2: Run, expect failure.** `python3 tests/hand_body_re_doc_check.py` fails (no document).
- [ ] **Step 3: Do the RE and write the document.** Use the repo's Ghidra scripts (`tools/D2StaticResearch.java` and the `D2*.java` family) on the pinned exe; reuse research §2 findings (mark them [V] only when this document re-derives or cites the decompile). Finish with a **route table**: for each of R1 (mod calls `createBody` with engine-shaped userdata), R2 (repurpose an engine-spawned prop: `NativeWeaponHands::spawn_def` at `native_weapon_hands.hpp:524` spawns a decl through 0x8ae9b0, then its layer is rewritten through 0x97600; earlier note: "0x8ae9b0 spawns do not start components (sim v407)", so R2 needs a decl that does start a body or it is dead), R3 (a body attached to an engine motor with 0xc8e7c0), the statically known blockers.
- [ ] **Step 4: Run, expect pass.** `python3 tests/hand_body_re_doc_check.py`.
- [ ] **Step 5: Commit.**
  ```bash
  git add docs/native-hand-body-re.md tests/hand_body_re_doc_check.py tools/build-msvc.cmd
  git commit -m "docs: hand-body static RE: userdata contract, shapes, createBody, filters, queue"
  ```

---

### Task B7: Hand-body runtime read-only probe (research open items 2, 3, 4, 5, 8)

Observation only. It answers what static RE cannot: the real filter words of the player's bodies, what a prop's userdata really holds, which filter the world installs, and the queue cadence.

**Files:**
- Create: `src/hand_body_probe.hpp`
- Modify: `src/dxgi_probe.cpp` (knob `build/hand-body-probe`; rows written by the status/trace thread, one set per second, at most 120 sets), `tools/build-msvc.cmd`
- Test: `tests/hand_body_probe_check.cpp`

**Interfaces:**
- Consumes: `NativeWorldQuerySource` `body_filter_info` (`native_world_query_source.hpp`), the player proxy body of 0x1b6ec0 (`param_1[0x86]`), pool records (stride 0xb0, world+0x28), `NativeMotion`.
- Produces:
  ```cpp
  struct FilterWord {std::uint32_t raw{};unsigned layer() const;std::uint32_t mask() const;};          // layer = raw & 0x1f, mask = raw >> 5
  struct BodyProbeRow {std::uint64_t tick{};std::uint32_t handle{};std::uint32_t filter{};std::uint32_t flags{};std::uintptr_t userdata{},userdata_plus8{},userdata_vtable_rva{};const char* what{};};
  struct QueueProbe {std::uint64_t callbacks{},callbacks_with_commands{};bool busy_at_stage0{},busy_at_dispatch{};std::uintptr_t world_filter_vtable_rva{};};
  std::string body_probe_csv_header();std::string body_probe_csv_row(const BodyProbeRow&);
  template<class Read> std::vector<BodyProbeRow> probe_player_bodies(Read&,std::uintptr_t base,std::uint64_t tick);   // proxy + query-source + capsule bodies that resolve
  template<class Read> std::optional<BodyProbeRow> probe_prop_userdata(Read&,std::uintptr_t base,const NativeRigidBody&,std::uint64_t tick);
  ```
  The userdata dump records pointers and the vtable RVA only (never raw memory beyond 64 bytes), and `userdata_vtable_rva` is 0 when the pointer is not inside the image.

- [ ] **Step 1: Failing tests:** `filter_word_split` (0xffffffe3 → layer 3, mask 0x07fffff; layer in the low 5 bits), `csv_row_field_count_matches_header`, `probe_prop_userdata_reads_fixture` (fixture record with userdata → +8 → vtable inside the image: rows filled; userdata null: row with zeros, no crash), `garbage_pointers_rejected` (userdata outside user space, misaligned, or crossing a page: `userdata_plus8 == 0`, no read beyond), `probe_player_bodies_skips_unresolved`.
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_probe'`.
- [ ] **Step 3: Implement** the header and the knob wiring (counters via atomics: `callbacks` incremented in the 0x89fb0 hook entry, `callbacks_with_commands` when the command batch is non-empty if the hook can see it; if not visible, only `callbacks` and `busy_at_*`, and the doc says so).
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe|native_grab_compile'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_body_probe.hpp src/dxgi_probe.cpp tests/hand_body_probe_check.cpp tools/build-msvc.cmd
  git commit -m "feat: read-only hand-body probe: player body filter words, prop userdata, queue cadence"
  ```
- [ ] **Step 6: Probe run (Tier B; `tank-floor` save, `build/hand-body-probe`; mark outstanding if the save is missing).** `linux/d2vr-linux sim --test-save ~/d2vr-test-saves/tank-floor --scenario oil_tank_drop --lead 60 --seconds 100`. Append to `docs/native-hand-body-re.md` a section `Runtime findings`: the player bodies' filter words (layer and mask), whether the mask arithmetic confirms that a mask omitting 6, 7, 8, 18 never pairs with them, a prop's userdata chain and vtable RVA, the world filter vtable RVA (Q7), callback cadence and busy flags (Q8). Update the Q-section tags that changed and rerun `tests/hand_body_re_doc_check.py`.
- [ ] **Step 7: Commit the findings.**
  ```bash
  git add docs/native-hand-body-re.md
  git commit -m "docs: hand-body runtime findings: player filter words, userdata chain, cadence"
  ```

---

### Task B8: Command-record encoders, filter words and the mutual-collision predicate (pure)

Written before any engine call so the byte layouts and the mask arithmetic are pinned by tests copied from the B6/B7 evidence.

**Files:**
- Create: `src/hand_body_commands.hpp`
- Test: `tests/hand_body_commands_check.cpp`; register after `native_rigid_body_check`

**Interfaces:**
- Consumes: `Vec3`; layer numbers and record header from the research §2.4-2.6 and `docs/native-hand-body-re.md`.
- Produces:
  ```cpp
  inline constexpr unsigned layer_world_low=0,layer_rigid_body=3,layer_debris=4,layer_mover=5,layer_player=6,layer_player_hitbox=7,layer_player_dead=8,
      layer_npc=9,layer_npc_body_animated=10,layer_npc_body_ragdoll=14,layer_interactable=15,layer_player_physics=18,layer_pickup=23;
  struct CommandRecord {std::array<unsigned char,0x40> bytes{};std::size_t size{};};
  CommandRecord encode_set_velocity(std::uint32_t body,Vec3 linear,Vec3 angular);                       // {0x30,0x0200,9,id,lin[4]@0x10,ang[4]@0x20}
  CommandRecord encode_set_filter(std::uint32_t body,std::uint32_t filter);                              // size 0x20, type 0x1a
  CommandRecord encode_set_motion_type(std::uint32_t body,unsigned type,unsigned activation);           // type 0x11
  CommandRecord encode_hard_keyframe(std::uint32_t body,Vec3 position,const std::array<float,4>& quat,float dt);   // type 0x32
  CommandRecord encode_set_transform(std::uint32_t body,const std::array<float,12>& transform,unsigned activation);   // type 6
  constexpr std::uint32_t filter_word(unsigned layer,std::uint32_t collides_with_mask);                  // layer | mask<<5
  constexpr std::uint32_t hand_collides_with(bool hit_debris);                                           // layers 0-5, 10, 14, 15 (+4 when hit_debris), never 6, 7, 8, 18, 23
  constexpr bool layers_collide(std::uint32_t a,std::uint32_t b);                                        // the 0x992c0 mutual test, without the owner check
  ```
  Add/destroy records (types 1, 2) are encoded only if B6 Q3 gives their payloads [V]; otherwise the code uses the direct API under `!busy` (B13) and this header omits them (the test asserts the header has no `encode_add`).

- [ ] **Step 1: Failing tests:** `set_velocity_layout` (size 0x30, u16 0x0200 at +2, type 9 at +4, id at +8, linear at +0x10, angular at +0x20), `set_filter_layout` (size 0x20, type 0x1a), `set_motion_type_layout`, `hard_keyframe_layout`, `set_transform_layout`, `non_finite_never_encodes` (any NaN/inf component throws `std::invalid_argument`; Review Focus 3), `filter_word_roundtrip`, `hand_mask_includes_world_layers` (0-5, 10, 14, 15), `hand_mask_excludes_player_layers_both_directions` (for each of 6, 7, 8, 18: `layers_collide(hand, player_word)` false even when the player's own mask is all ones), `debris_only_when_requested`, `mutual_requirement` (a body whose mask omits the hand layer does not collide), `no_encode_add_without_evidence`.
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_commands'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_body_commands.hpp tests/hand_body_commands_check.cpp tools/build-msvc.cmd
  git commit -m "feat: hand-body command encoders, filter words and the mutual-collision predicate"
  ```
- [ ] **Step 6: Mutation pass.** drop layer 18 from the exclusion, change the record size 0x30 to 0x20, drop the `isfinite` check: each fails.

---

### Task B9: Hand servo law (pure; Bonelab §2.1, §1.4)

Bonelab's hand is a physical body pulled by clamped forces, with braking so it does not overshoot, a jerk filter, and an effective mass that includes what it holds. Same law in velocity form, using plan A's `cap_velocity_step`.

**Files:**
- Create: `src/hand_servo.hpp`
- Test: `tests/hand_servo_check.cpp`; register after `hand_body_commands_check`

**Interfaces:**
- Consumes: `cap_velocity_step`, `GrabTuning`, `effective_mass` (plan A Task 2), `WorldTrackedPose`.
- Produces:
  ```cpp
  struct HandServoConfig {double gain=18,max_speed=10,max_spin=20,brake_accel=40,jerk_tau=.02,hand_kg=2,snap_gap=.35,snap_seconds=.25,dt_max=1./30;};
  struct HandServoState {Vec3 filtered_accel{};double gap_time{};bool has{};};
  struct HandServoOutput {Vec3 linear{},angular{};bool capped{};bool snap{};double gap{};bool valid{};};
  HandServoOutput servo_step(HandServoState&,const WorldTrackedPose& target,const WorldTrackedPose& body,Vec3 body_velocity,Vec3 body_spin,
      double held_kg,double dt,double time_factor,const HandServoConfig&,const GrabTuning&);
  ```
  Algorithm not fixed by the tests: desired velocity = `error·gain` limited to `max_speed` and by the braking profile `sqrt(2·brake_accel·|error|)` (no overshoot), then `cap_velocity_step` with arm axis = the target's forward row and mass `hand_kg + held_kg`; the commanded acceleration is low-passed by `jerk_tau`; `snap` rises when the gap to the target exceeds `snap_gap` for `snap_seconds`. `time_factor` scales the commanded velocities (1 when unknown). Non-finite or absurd (> 50 m) targets give `valid=false` and zero velocities.

- [ ] **Step 1: Failing tests:** `step_response_no_overshoot` (target 0.3 m away at rest: reaches within 1 cm without exceeding it by more than 1 mm, 90 Hz), `asymmetric_caps` (a 0.3 m jump along the arm axis accelerates harder than the same sideways, ratio ≈ 300:90), `heavy_held_lags_no_runaway` (held 20 kg: acceleration reduced, peak speed never above `max_speed`), `time_factor_scales` (0.5 halves the commanded speed), `non_finite_and_huge_rejected`, `hitch_dt_clamped` (dt 0.5 equals dt 1/30; dt ≤ 0 or NaN gives zero output; Review Focus 4), `snap_hysteresis` (gap 0.34 for 5 s never snaps; 0.36 snaps between 0.24 and 0.26 s; a gap that closes at 0.2 s resets), `snap_target_overlap_refused` (Review Focus 2: with `blocked_target=true` input the output never requests a snap; add the field `bool target_blocked` to the call through a trailing parameter), `jerk_filter_smooths` (a step in target produces monotonic acceleration ramp).
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_servo'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_servo.hpp tests/hand_servo_check.cpp tools/build-msvc.cmd
  git commit -m "feat: hand servo law: capped, braked, jerk-filtered velocity step with snap-back request"
  ```
- [ ] **Step 6: Mutation pass.** `snap_gap=.35=>.5`, `snap_seconds=.25=>.5`, drop the braking term, drop the dt clamp: each fails.

---

### Task B10: Hand-body lifecycle state machine (pure)

**Files:**
- Create: `src/hand_body_lifecycle.hpp`
- Test: `tests/hand_body_lifecycle_check.cpp`; register after `hand_servo_check`

**Interfaces:**
- Consumes: `ReleaseReason`-style reasons are not used; plain events.
- Produces:
  ```cpp
  enum class BodyState {absent,ready,destroying,faulted};
  enum class BodyAction {none,create,destroy};
  struct LifecycleInput {std::uintptr_t world{};std::uint32_t world_generation{};bool gameplay{};bool focused{};bool tracked{};bool physics_busy{};bool shutdown{};bool fault{};bool enabled{true};};
  class HandBodyLifecycle {public:
      BodyAction update(unsigned side,const LifecycleInput&);   // what the caller must do now
      void created(unsigned side,std::uintptr_t world,std::uint32_t handle);void destroyed(unsigned side);
      bool can_write(unsigned side,std::uintptr_t world,std::uint32_t handle) const;   // world and handle both match the body created in this world
      BodyState state(unsigned side) const;};
  ```
  Rules: `create` only when enabled, gameplay, focused, tracked and physics not busy and the state is `absent`; any change of `world` or `world_generation`, loss of gameplay (load, death reload), shutdown or `fault` → `destroy` (or `faulted` latch for `fault`: no recreate this session); loss of focus or tracking destroys after 1 s (so a menu blink does not churn); `can_write` is false in every state except `ready` and for any other world or handle.

- [ ] **Step 1: Failing tests:** `create_waits_for_idle_physics`, `world_change_destroys` and `stale_world_never_writes` (Review Focus 1: `can_write` false the instant the world changes, before `destroyed`), `recycled_handle_refused`, `load_screen_destroys_then_recreates_once_gameplay`, `focus_loss_grace_1s` (blink 0.5 s: nothing; 1.1 s: destroy), `tracking_loss_same`, `fault_latches_off_for_session`, `shutdown_destroys_even_when_busy` (caller must use the queue), `two_sides_independent`, `disabled_never_creates`.
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_lifecycle'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_body_lifecycle.hpp tests/hand_body_lifecycle_check.cpp tools/build-msvc.cmd
  git commit -m "feat: hand-body lifecycle state machine: create, destroy, never write to a stale world"
  ```
- [ ] **Step 6: Mutation pass.** grace `1`→`0`, let `can_write` ignore the world, allow create while busy: each fails.

---

### Task B11: Creation spike across the candidate routes (isolated, sim only)

The first engine-creating task. Dev knob only; nothing here runs in a normal session. Use a DeepSeek second-opinion review (engine writes, concurrency).

**Files:**
- Create: `src/hand_body_spike.hpp`, `tools/linux/scenarios/hand_body_spike.py`
- Modify: `src/dxgi_probe.cpp` (knob `build/hand-body-spike`, content `R1`, `R2` or `R3`; read once at start; logged), `docs/native-hand-body-re.md` (a `Route results` section), `tools/build-msvc.cmd`
- Test: `tests/hand_body_check.cpp` (fake-engine seam, created here, grown in B13)

**Interfaces:**
- Consumes: B6 route table, B8 encoders, B10 lifecycle, plan A `FaultLatch` pattern (a separate `HandBodyFault` latch here).
- Produces:
  ```cpp
  enum class SpikeRoute {R1,R2,R3};
  struct SpikeEngine {   // seam: real engine in the DLL, fakes in the check
      std::function<bool(SpikeRoute,Vec3 at,double radius,std::uint32_t& handle)> create;
      std::function<bool(std::uint32_t handle)> destroy;
      std::function<std::optional<Vec3>(std::uint32_t handle)> position;
      std::function<std::optional<std::uint32_t>(std::uint32_t handle)> flags;
      std::function<bool()> busy;};
  struct SpikeReport {SpikeRoute route{};bool created{},collided_with_floor{},destroyed{},faulted{};double settle_z{},player_drift_m{};std::uint64_t ticks{};};
  SpikeReport run_spike(SpikeEngine&,SpikeRoute,Vec3 at,double floor_z,double seconds);
  ```
  Steps of one spike: create a 5 cm sphere body near the player's feet with gravity on (the hand layer, mask from `hand_collides_with(false)`), let it fall, record `settle_z`, press it into the player's capsule position for 5 s and record the player root drift, destroy it, check the pool record's flags show it gone. Any SEH fault latches `HandBodyFault` and ends the spike.

- [ ] **Step 1: Failing tests** (`hand_body_check.cpp`, fake engine): `spike_creates_falls_destroys` (fake world with floor at z = 0: `settle_z` within 1 cm of the radius), `spike_refuses_while_busy` (retries when idle, never creates while `busy()`), `spike_fault_latches_and_stops` (fake create throws a fault flag: `faulted`, no destroy attempt on a handle never created), `spike_never_writes_after_world_change` (fake changes the world pointer mid-run: no further calls except `destroy` skipped by `can_write`), `spike_reports_player_drift` (fake pushes the "player" 3 mm: reported, not hidden).
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_check'`.
- [ ] **Step 3: Implement** the seam, `run_spike`, and the real-engine implementations of R1-R3 in the header, each pinned by the byte checks of B6 and each wrapped in `guarded_call`: R1 `createBody` with engine-shaped userdata per B6 Q1; R2 `spawn_def` of the decl B6 names, then `setBodyCollisionFilterInfo` (0x97600) to the hand filter and a velocity zero; R3 attached variant per B6. A route B6 marks statically impossible is implemented as `return false` with the reason in the report, not as a guess.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_body_spike.hpp src/dxgi_probe.cpp tools/linux/scenarios/hand_body_spike.py tests/hand_body_check.cpp tools/build-msvc.cmd
  git commit -m "feat: isolated hand-body creation spike across candidate routes (dev knob)"
  ```
- [ ] **Step 6: Route runs (Tier B; `tank-floor`; mark outstanding if the save is missing).** One session per route (`build/hand-body-spike` = `R1`/`R2`/`R3`), 90 s each: record per route in `docs/native-hand-body-re.md` `Route results`: created, collided with the floor, `player_drift_m` (must be ≤ 1 mm), destroyed cleanly, any crash or `HandBodyFault`, and one more session per passing route that triggers a load mid-run (pause menu, `when: "always"` presses; if not scriptable, a Tier C item). Commit the doc.
  ```bash
  git add docs/native-hand-body-re.md
  git commit -m "docs: hand-body route results: R1, R2, R3 creation spike"
  ```

---

### Task B12: Go/no-go checkpoint (controller task, no code)

**Files:** none (ledger entry only).

- [ ] **Step 1: Judge** the `Route results` against the criteria: **GO** when at least one route is created, crash-free for 60 s, collides with the floor, leaves the player drift ≤ 1 mm, and is destroyed cleanly on a world change; record the chosen route in the ledger (`Ruling: route Rn`, with the cost if wrong).
- [ ] **Step 2: If NO-GO** (no route passes, or every passing route needs an RE item still [U] after B6-B7): stop Tasks B13-B17, keep B1-B5 shipped as the delivered fix, and present the user with the evidence table, the missing facts, the size of the next RE step (for example the constraint route 0xc90de0, or a native component path), and the statement that held objects then collide through the clamp only (deviation 4). Ask the user; do not choose for them. This is the only place in plans A and B where the user's go/no-go is requested.
- [ ] **Step 3: Record** the decision in the ledger and, on GO, tell the implementers of B13-B16 which route and which B6 questions they may rely on.

---

### Task B13: Production `HandBody` manager (create, destroy, fallback switch)

**Files:**
- Create: `src/hand_body.hpp` (promotes the chosen route's code from `hand_body_spike.hpp`; the spike header stays for the dev knob until B17)
- Modify: `src/dxgi_probe.cpp` (knobs `build/no-hand-body` and `build/hand-body` (default off until B14's proofs pass; B17 flips it), status `,"hand_body":`, start/stop beside `carry_trace`), `tests/hand_body_check.cpp`, `tests/hand_body_routing_check.py` (new)
- Test: `tests/hand_body_check.cpp` (extend), `tests/hand_body_routing_check.py`

**Interfaces:**
- Consumes: B8, B10, the chosen route.
- Produces:
  ```cpp
  struct HandBodyStatus {bool available{};BodyState state{};std::uint32_t handle{};std::uint64_t creates{},destroys{},faults{},refused_writes{};};
  class HandBody {public:
      static constexpr unsigned sides=2;
      void service(const LifecycleInput& per_side[2]);       // dispatcher tick: runs the lifecycle, creates or destroys
      bool ready(unsigned side) const;                       // lifecycle ready, no fault latch, not disabled
      std::optional<WorldTrackedPose> pose(unsigned side) const;   // last read-back pose (dispatcher thread), mutex-protected copy for the render thread
      bool write(unsigned side,const CommandRecord&);        // refused (counted) unless lifecycle.can_write
      HandBodyStatus status(unsigned side) const;
      void shutdown();};
  ```
  Creation happens only from the dispatcher callback when physics is idle; destruction on world change, load, death reload, shutdown through the queue when busy; any SEH fault latches `HandBodyFault` (the fallback clamp then runs for the session; status `available:false`, `faults`).

- [ ] **Step 1: Failing tests:** extend `hand_body_check.cpp` with the fake engine: `creates_once_per_world`, `destroys_on_world_change_then_recreates`, `write_after_world_change_refused` (Review Focus 1), `fault_latches_off_and_reports_unavailable`, `busy_destroy_goes_through_queue` (the fake counts queue pushes versus direct calls), `disabled_by_knob_never_creates`. `hand_body_routing_check.py`: the creation calls appear only in `hand_body.hpp`/`hand_body_spike.hpp`; no `0xc90de0`; each engine call site is inside `guarded_call`; `build/no-hand-body` read in the knob block; status key `"hand_body":` present.
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_check|hand_body_routing'`.
- [ ] **Step 3: Implement.** The knob logic: body enabled when `build/hand-body` exists and `build/no-hand-body` does not; after B17, enabled unless `no-hand-body`.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe|native_grab'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_body.hpp src/dxgi_probe.cpp tests/hand_body_check.cpp tests/hand_body_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: production HandBody manager: lifecycle-gated creation, queue-aware destroy, fault latch"
  ```

---

### Task B14: Drive the hand body and prove it (the four proofs)

Needs DeepSeek second-opinion review (concurrency: dispatcher thread writes, render thread reads).

**Files:**
- Modify: `src/hand_body.hpp` (`drive`), `src/native_grab.hpp` (dispatcher `invoke` calls `hand_body.service` and `drive` after `update`, `:455-461`), `src/dxgi_probe.cpp` (raw palm targets published per side with the existing hand publication; status fields)
- Create: `tools/linux/scenarios/hand_body_soak.py`, `hand_body_saveload.py`
- Modify: `tools/linux/bonelab_run_check.py` (soak/save-load assertions)
- Test: `tests/hand_body_check.cpp` (extend), `tests/hand_body_routing_check.py` (extend), `tests/bonelab_scenarios_check.py`

**Interfaces:**
- Consumes: `servo_step` (B9), `encode_set_velocity` (B8), `HandBody` (B13), `native_motion` for read-back, the raw calibrated palm pose per side (C9: intent) as the servo target in world space.
- Produces:
  ```cpp
  // HandBody
  struct DriveInput {unsigned side{};WorldTrackedPose target{};double held_kg{};double now{};bool yield{};};   // yield: authored animation, tracking loss
  void drive(const DriveInput&);                // dispatcher thread: read back, servo_step, write the velocity (one write per side per tick)
  struct DriveStatus {double gap_m{};double speed{};bool capped{};std::uint64_t contact_frames{},snaps{},cost_us{},cost_max_us{},player_drift_um{};};
  DriveStatus drive_status(unsigned side) const;
  ```
  `contact_frames` counts ticks where the achieved velocity falls short of the commanded one by more than 25% (research §2.5: contact without events). `player_drift_um` is the player root's movement not explained by locomotion input while a hand is pressed into the capsule (the proof that the player is never pushed). A `snap` request writes `encode_set_transform` only when the clamp's last cast shows the controller target is free; otherwise the body keeps servoing.

- [ ] **Step 1: Failing tests:** `drive_writes_once_per_tick`, `drive_follows_target_within_1cm` (fake world with no obstacles, 90 Hz), `drive_stops_at_wall_and_reports_contact` (fake wall: `contact_frames` rises, body never passes), `readback_garbage_latches_body_off` (Review Focus 3: NaN/huge velocity read-back → `HandBodyFault`, no further write, `available:false`), `yield_writes_zero_velocity_and_holds`, `cost_budget_recorded` (the fake's timings feed `cost_us`), `player_drift_reported_not_clamped`. Scenarios: `hand_body_soak` (120 s of reach, press into table, props, a guard if present; `bonelab_run_check` asserts `hand_body.faults == 0`, `available == true`, `player_drift_um ≤ 1000`, `cost_max_us ≤ 200` per tick summed for both hands), `hand_body_saveload` (quicksave then quickload by menu presses with `when: "always"` if scriptable; otherwise a Tier C item and the ledger says so).
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_body_check|bonelab_scenarios|hand_body_routing'`.
- [ ] **Step 3: Implement.** The knob stays off by default; the run enables it with `build/hand-body`.
- [ ] **Step 4: Run, expect pass.** Same command plus the full harness: 0 failed, 41 skipped.
- [ ] **Step 5: The four proofs (Tier B; ledger lists any not reachable as Tier C).** (a) `hand_body_soak` ×5 sessions of 120 s = 10 cumulative minutes, sum `faults == 0`; (b) `player_drift_um` ≤ 1000 in a scripted pressed-into-capsule segment; (c) save/load once per session or Tier C; (d) frame-time delta: `cost_max_us` and the existing `cast_us` against the 0.2 ms budget, both hands. Record the table in the ledger. A failed proof stops the plan here and goes to the user with the evidence.
- [ ] **Step 6: Commit.**
  ```bash
  git add src/hand_body.hpp src/native_grab.hpp src/dxgi_probe.cpp tools/linux/scenarios/hand_body_soak.py tools/linux/scenarios/hand_body_saveload.py tools/linux/bonelab_run_check.py tests/hand_body_check.cpp tests/hand_body_routing_check.py tests/bonelab_scenarios_check.py tools/build-msvc.cmd
  git commit -m "feat: drive the hand body with the capped servo; soak, drift and cost counters"
  ```

---

### Task B15: The visible hand follows the body, the clamp guards it (research Phase 3)

**Files:**
- Create: `src/hand_visual_source.hpp`
- Modify: `src/dxgi_probe.cpp` (publication block ~4211-4453: choose the drawn grip per side after `hand_clamp.update` and before `publish_hand`), `tests/hand_body_routing_check.py`
- Test: `tests/hand_visual_source_check.cpp`; register after `hand_body_lifecycle_check`

**Interfaces:**
- Consumes: `HandBody::pose` (B13), the clamp output (`ClampOutput`), the raw controller grip, the held-object seat (plan A `DrawnAnchor` wins for motor holds).
- Produces:
  ```cpp
  enum class VisualSource {controller,clamp,body};
  struct VisualInput {double now{};bool body_ready{};std::optional<WorldTrackedPose> body;WorldTrackedPose clamp{};WorldTrackedPose controller{};bool clamp_contact{};bool yield{};bool body_behind_surface{};};
  struct VisualOutput {WorldTrackedPose drawn{};VisualSource source{};bool snapped{};double gap_m{};};
  class HandVisualSource {public:static constexpr double blend_seconds=.15,snap_gap=.35,snap_seconds=.25;
      VisualOutput update(unsigned side,const VisualInput&);void reset(unsigned side);};
  ```
  Rules: `yield` → controller (native animation owns the arm); body ready and sane → body pose, blended over 0.15 s on source changes; the body is distrusted (controller or clamp wins, `snapped`) when it is farther than 0.35 m from the controller for 0.25 s or `body_behind_surface` (the clamp's tunnelling guard); body missing → clamp (phase-1 behaviour), unchanged.

- [ ] **Step 1: Failing tests:** `body_wins_when_sane`, `fallback_to_clamp_without_body` (equals the phase-1 drawn pose bit-exact), `yield_gives_controller`, `blend_over_150ms`, `far_body_snaps_after_quarter_second` (gap 0.34 for 5 s never; 0.36 snaps between 0.24 and 0.26 s), `behind_surface_distrusted`, `non_finite_body_ignored`, `sides_independent`, `no_flicker_on_threshold_noise` (gap alternating 0.349/0.351 each frame for 2 s: at most one source change).
- [ ] **Step 2: Run, expect failure.** `... --only 'hand_visual_source|hand_body_routing'`.
- [ ] **Step 3: Implement** both. Raw controller poses still drive intent everywhere; the python guard asserts `raw_grips` is not read by this code path.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'hand_clamp_routing|dxgi_probe'`.
- [ ] **Step 5: Commit.**
  ```bash
  git add src/hand_visual_source.hpp src/dxgi_probe.cpp tests/hand_visual_source_check.cpp tests/hand_body_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: draw the hand from the hand body with the clamp as tunnelling guard"
  ```
- [ ] **Step 6: Mutation pass.** `snap_gap=.35=>.5`, `blend_seconds=.15=>.5`, drop the `body_behind_surface` rule: each fails.

---

### Task B16: Held things follow the hand body (Part 5; Bonelab §1.2, §2.2)

Needs DeepSeek second-opinion review (geometry, layer upkeep, concurrency).

**Files:**
- Create: `src/hold_coupling.hpp`, `tools/linux/scenarios/tank_hand_body.py`
- Modify: `src/native_grab.hpp` (motor holds target the hand body's pose when `HandBody::ready`; temporary layer swap on grab and restore through `SafeRelease`), `src/dxgi_probe.cpp` (`publish_held_object`: the requested native-hold target is derived from the hand body pose when ready, then clamped by `HeldClamp`; the body is driven toward the clamped result), `tools/linux/bonelab_run_check.py`, `tests/hand_body_routing_check.py`
- Test: `tests/hold_coupling_check.cpp`; register after `hand_visual_source_check`

**Interfaces:**
- Consumes: `HeldClamp`, `SafeRelease` (B1-B2), `HandBody`, `HandVisualSource`, `PhysicalGrab::anchor` (plan A Task 7).
- Produces:
  ```cpp
  enum class HoldKind {motor_prop,corpse,native};
  struct CouplingInput {HoldKind kind{};bool body_ready{};std::optional<WorldTrackedPose> body;WorldTrackedPose controller{};
      std::optional<WorldTrackedPose> clamped_native;double now{};};
  struct CouplingOutput {WorldTrackedPose target{};bool uses_body{};double lag_m{};bool release{};};
  class HoldCoupling {public:static constexpr double lag_release_m=.5,lag_release_s=.25;
      CouplingOutput update(unsigned side,const CouplingInput&);void reset(unsigned side);};
  ```
  Rules: motor props and corpses: the `PhysicalGrab` hand pose is the hand body's pose when `body_ready` (else the controller, today's behaviour), so the dynamic held body is pulled behind a hand that the world already stopped; native holds: `target` is the `HeldClamp` output for a requested pose derived from the hand body, and the body's own servo target becomes that clamped pose; `release` when `lag_m` exceeds 0.5 m for 0.25 s (kept equal to plan A's corpse rule; props keep their 1.5 m rule). While holding, the held body's layer and the hand mask are arranged so the hand does not collide with what it holds (B6 Q5: disabled pair, else a layer swap with `hand_collides_with(hit_debris=false)`), and the swap is undone only through `SafeRelease`.

- [ ] **Step 1: Failing tests:** `body_not_ready_is_todays_behaviour` (target equals the controller, bit-exact, `uses_body` false), `motor_hold_uses_body_pose`, `blocked_hand_lags_held_prop` (body stopped 0.3 m from the controller: target is the body, `lag_m` 0.3, no release), `lag_release_after_quarter_second` (0.6 m for 0.24 s: no; 0.26 s: release), `native_target_is_clamped_pose`, `hand_does_not_collide_with_held_layer` (filter arithmetic from B8: hand vs the held object's layer → false while holding, true after the restore), `layer_restore_only_via_safe_release` (routing guard: no `set_filter` outside `native_held_object.hpp`, `hand_body.hpp` and `SafeRelease` consumers), `released_inside_geometry_keeps_layer` (the B2 behaviour with the body on).
- [ ] **Step 2: Run, expect failure.** `... --only 'hold_coupling|hand_body_routing'`.
- [ ] **Step 3: Implement.** Fallback is by construction: `body_ready` false (knob off, fault latch, creation failed) gives exactly the B4 behaviour.
- [ ] **Step 4: Run, expect pass.** Same command plus the full harness.
- [ ] **Step 5: Tier B S15** (`tank-floor`; ledger "outstanding" if missing): `tank_hand_body` carries the tank into a wall and the floor and drops it, once with the body on (`build/hand-body`) and once with `build/no-hand-body`; `bonelab_run_check.py RUN --scenario tank_hand_body` asserts in both: tank final height above the floor plane after 5 s, `release_overlap_unresolved == 0`, `hand_body.faults == 0`, and with the body on `hand_body.contact_frames > 0`; record the keyframe reaction to a clamped target.
- [ ] **Step 6: Commit.**
  ```bash
  git add src/hold_coupling.hpp src/native_grab.hpp src/dxgi_probe.cpp tools/linux/scenarios/tank_hand_body.py tools/linux/bonelab_run_check.py tests/hold_coupling_check.cpp tests/hand_body_routing_check.py tools/build-msvc.cmd
  git commit -m "feat: held things follow the hand body; the clamp stays as the native-hold collision mechanism and the fallback"
  ```

---

### Task B17: Default-on decision, Tier C checklist and closing run

**Files:**
- Create: `docs/VR-bonelab-feel-hand-body-TEST-CHECKLIST.md`
- Modify: `src/dxgi_probe.cpp` (flip the default per the ledger decision: body on unless `build/no-hand-body`; retire the `build/hand-body-spike` knob if no longer needed), `tests/hand_body_routing_check.py`

- [ ] **Step 1: Failing test:** the routing guard expects `no-hand-body` to be the only off switch and `hand-body-spike` removed or still dev-only; `python3 tests/hand_body_routing_check.py` fails until the flip.
- [ ] **Step 2: Decide** from the B14 proofs table: flip the default to on only if all four proofs are met (or the unreached ones are Tier C items the user will run first); otherwise leave `build/hand-body` as the opt-in and say so in the ledger and the checklist.
- [ ] **Step 3: Write the Tier C checklist** (ordered by risk, status fields to save: `hand_body`, `held_clamp`, `hand_clamp`, `carry`): 1 build and boot, `hand_body.available`; 2 press hands into a table, wall, crate: the visible hand stops and the arm follows, no jitter; 3 push the player's own body with a hand: the player never moves; 4 carry the tank into a wall and the floor, drop it from chest height and while pressed to the floor, it ends on the floor; 5 carry a plank through a doorway; 6 pull a corpse (plan A) into a wall: it stops, the hand lags, no flying; 7 an awake guard touched by the hand: nothing worse than phase 1; 8 save, load, die and reload with hands in contact; 9 ten minutes of ordinary play, frame time with `hand_body.cost_us`; 10 `build/no-hand-body` fallback still fixes the tank.
- [ ] **Step 4: Closing run.** `python3 tools/linux/run_build_cmd.py`: 0 failed, 41 skipped, passed = baseline plus plan A's and this plan's new steps; record the count; update the ledger (Tier A/B status, outstanding Tier B list, Tier C pointers).
- [ ] **Step 5: Commit.**
  ```bash
  git add docs/VR-bonelab-feel-hand-body-TEST-CHECKLIST.md src/dxgi_probe.cpp tests/hand_body_routing_check.py
  git commit -m "feat: hand body on by default with a single off switch; Part 5 headset checklist"
  ```

---

## Headset checklists (user; not automated)

Fallback and tank fix: `docs/VR-bonelab-feel-held-clamp-TEST-CHECKLIST.md` (Task B5). Hand body: `docs/VR-bonelab-feel-hand-body-TEST-CHECKLIST.md` (Task B17). Keep the `hand_body` and `held_clamp` status blocks from each run.

## Self-review

- **Spec coverage (Part 5 and user decision 5):**
  - Held-body clamp and safe release as fallback and as the native-hold mechanism: B1-B5 (tank fix first, Tier B S12-S14, Tier C).
  - RE and probe first: B6 (static), B7 (runtime), B11 (creation routes), with the go/no-go checkpoint B12 (user asked only if infeasible).
  - Offline pure layer: B8 (command encoding, filter mask), B9 (servo law), B10 (lifecycle) = the research's `hand_command_encoding_check`, `hand_filter_mask_check`, servo-law check and `hand_body_lifecycle_check`.
  - The body itself: B13 manager, B14 drive and the four proofs, B15 visible hand (research phase 3), B16 held-object coupling and oil-tank Tier B scenario, B17 default and checklist.
  - Spec's open items 1-12 map to B6 Q1-Q8 and B7/B11 runtime items; AI reaction to guard contact (item 10) is a Tier C checklist item 7.
- **Step scan:** no "TBD"; the unknowns are explicit RE tasks whose output gates the next tasks (B6, B7, B11, B12). Tasks B8-B10 are written before any engine call and use only facts B6/B7 supply.
- **Type consistency:** `CommandRecord`, `filter_word`, `hand_collides_with` (B8→B13,B16); `servo_step`/`HandServoConfig` (B9→B14); `HandBodyLifecycle`/`BodyState` (B10→B11,B13); `HandBody`/`HandBodyStatus` (B13→B14,B15,B16); `HeldClamp`/`SafeRelease` (B1-B2→B4,B16); `GrabTuning`, `cap_velocity_step`, `FaultLatch`, `PhysicalGrab::anchor`, `DrawnAnchor` (plan A).
- **Review Focus** lines each have a named test in the owning task (B1, B2, B8, B9, B10, B13, B14).
- **Honesty about scope:** deviation 4 states that for keyframed native holds the clamp, not the sphere, is what stops the object; the user is told at B12.
- **Proportion:** 17 tasks; B4, B11, B14, B16 are the engine-heavy ones.
