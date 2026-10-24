# Bonelab Feel C: Touch-Sensor Finger Curl Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The visible fingers follow the player's real fingers, using the Quest controller touch sensors. A finger resting on the trigger bends the index a little. Lifting the index points. Taking the thumb off the stick, the buttons and the rest opens it (thumbs-up). A held object always shows a firm grip, and the `finger_ik` contact wrap still stops fingers at surfaces.

**Architecture:** Three pieces.
1. A pure `FingerTouchCurl` module turns the trigger, the grip and the touch booleans into five smoothed curls, using the Bonelab mapping.
2. Optional, fail-soft OpenXR `/touch` actions feed it. The simulated runtime gets matching support so the sim rig can script touches.
3. In `dxgi_probe.cpp`, the result replaces the `finger_wants` input of the contact wrap for empty and held hands. A weapon hand keeps the captured native fist. The contact wrap stays the last stage.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook) and the Linux sim rig.

**Spec:** `docs/superpowers/specs/2026-10-19-bonelab-feel-design.md`. Part 3 is "Touch-sensor finger curl". Also apply the Constraints section (C9 routing: curl is visual only) and the Verification model. The user's request, 2026-10-24: "I thought it will be a bonelab style hands with fingers curl … ok, do part 3 now".

## Global Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`. Plan workspace (git-excluded): `.superpowers/sdd/2026-10-24-bonelab-feel-c-finger-curl/`.
- Pinned exe `Dishonored2.exe`, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
- **Curl is visual only (C9).** No intent consumer reads it: grab, Use, attack, aim, gestures, Blink. Raw trigger and squeeze values keep driving intent unchanged.
- **Touch actions are optional and fail soft.** A runtime that rejects a `/touch` path or a profile must leave the current controls live. Never throw, and never lose an existing binding. The game's native input action set and its current bindings must not change.
- No swapchain or layer work. No engine writes. No new casts.
- **Bonelab values, verbatim from the spec.** Index rest touch value **0.3**, slewed at **10/s**. Middle, ring and pinky = grip × **1.3** − **0.1**, clamped to 0..1. Thumb closed if any thumb sensor is touched, else open, slewed at **25/s**. Smoothing on every finger: step toward the input by `clamp01(20·dt)` each frame. While holding, curl is floored at **0.6**. The mod's grab thresholds stay (0.45 / 0.35).
- **Tuning knob** `build/finger-touch.cfg` holds the touch value, both slew rates, the smoothing rate and the floor. A malformed file means defaults plus a note, never a throw.
- One behaviour knob: `build/legacy-finger-curl` restores today's mapping. Its `tools/default_features.json` row is kind `disable`, default true (on), labelled "Fingers follow the controller's touch sensors".
- Harness gate: no new failures against the current baseline of 625 passed / 1 failed (`fsr_taa_gpu_check --fsr4`, environment-only) / 28 skipped. Launcher tests ALL PASS. `linux/tests` OK.
- **Sim safety** (as in plans D–B):
  - `PAUSE-SIM` must be absent;
  - no `Dishonored2.exe` or `vrserver` running (`pgrep -x` guards that can't match their own shell line);
  - never kill by pattern;
  - after each run, the installed `d2vr_render_probe_arm_v600.dll` sha256 must be `ff0daf6566a62a44a31cef372a93114fc42790f2a13eb9887f3dfb3de63bc927`, with no knob left behind.
- Commit trailer:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01BJuGoAt65W8LH5YcXkajFq`.

## Review Focus

1. **A runtime that rejects the touch bindings.** Some SteamVR or Virtual Desktop layers reject `thumbrest/touch` or other paths, or report a touch action inactive. Expected: the mod drops back to today's bindings, keeps every control working, sets `touch_available=false`, and the fingers use the legacy mapping. Test (Task 2): `rejected_touch_bindings_keep_controls`.
2. **A runtime that reports touch actions active but never true.** For example, a Quest under Steam Link that doesn't forward touch. Expected: the thumb must not stay open forever, and the index must not rest straight. If no touch boolean has been true for 10 s while the trigger or grip moved, the side falls back to legacy (`touch_silent`). Test (Task 1): `silent_touch_falls_back_to_legacy`.
3. **A hand holding a weapon.** Expected: the native fist is kept and touch curl is never applied to the sword or gun hand. Test (Task 3): `weapon_hand_keeps_native_fist`.
4. **Frame hitches.** A hitch of dt 0.5 s, dt ≤ 0, or a NaN dt must not overshoot or freeze: smoothing clamps the step to 1, and dt ≤ 0 or NaN holds the last value. Test (Task 1): `hitch_and_bad_dt_safe`.
5. **Curl leaking into intent.** Expected: no grab, attack or gesture code reads the new curls. Test (Task 3): `curl_never_feeds_intent` (a routing check).

---

### Task 1: Pure `FingerTouchCurl` (Tier A curl check)

**Files:**
- Create: `src/finger_touch_curl.hpp`
- Test: `tests/finger_touch_curl_check.cpp`, registered in `tools/build-msvc.cmd` next to `finger_ik_check`

**Interfaces:**
- Produces:
  ```cpp
  struct FingerTouchParams {float touch_rest=.3f,index_slew=10.f,thumb_slew=25.f,smooth_rate=20.f,held_floor=.6f;
      static std::optional<FingerTouchParams> parse(const std::string& text);};   // "key=value" lines; any bad line -> nullopt
  struct FingerTouchInput {float trigger{},grip{};bool trigger_touch{},thumb_touch{};bool touch_available{};};
  enum class CurlMapping {touch,legacy};
  struct FingerTouchOutput {std::array<float,5> curl{};CurlMapping mapping{CurlMapping::legacy};bool floor_active{};bool touch_silent{};};
  class FingerTouchCurl {public:
      FingerTouchOutput update(unsigned side,const FingerTouchInput&,double dt,bool held,const FingerTouchParams&);
      void reset(unsigned side);};
  ```
  The curl order is thumb, index, middle, ring, pinky. `legacy` reproduces `rig::finger_wants(held,grip,trigger)` exactly, with no smoothing.

- [ ] **Step 1: Write the failing tests:**
  - `index_rest_settles_to_touch_value`: trigger 0 and touched for 2 s at 90 Hz gives an index of 0.3 ± 1e-4.
  - `index_adds_trigger`: trigger 0.5 and touched, settled, gives min(1, 0.5 + 0.3).
  - `grip_mapping`: grip 0.08 gives middle/ring/pinky 0 (±1e-4); grip 0.85 gives 1; grip 0.5 gives 0.55.
  - `thumb_touch_closes_and_release_opens`: thumb touched → thumb 1 after settling; released → 0.
  - `slew_times_to_63pct`: the time constants of the touch-simulation slew (10/s) and the thumb slew (25/s), combined with the 20/s smoothing. Measure the time to 63 % of the step and compare it with the analytic value of the two-stage filter, within 10 %.
  - `held_floor_0_6`: held, all fingers lifted (grip 0, no touch) → every curl ≥ 0.6, `floor_active`.
  - `legacy_when_touch_unavailable`: `touch_available=false` → output equals `finger_wants(held,grip,trigger)` bit-exactly, mapping legacy.
  - `silent_touch_falls_back_to_legacy`: `touch_available` is true, but no touch boolean is true for 10 s while the trigger moves → the mapping goes legacy and `touch_silent` is set. Any touch true → back to touch.
  - `hitch_and_bad_dt_safe`: dt 0.5 gives no value outside 0..1 and no overshoot. dt 0, a negative dt or NaN holds the last output.
  - `outputs_always_in_0_1`: a random sweep of 10k inputs, including NaN and inf trigger or grip, keeps every output finite and in 0..1. Non-finite inputs read as 0.
  - `params_parse`: the defaults; a valid file; a bad line → nullopt; values clamped to sane ranges (rates 1..100, touch_rest 0..1, floor 0..1).
  - `sides_independent`.
- [ ] **Step 2: Run, expect failure.** `python3 tools/linux/run_build_cmd.py --only 'finger_touch_curl' candidate d2vr_render_probe_arm_v601.dll`
- [ ] **Step 3: Implement.** The index target is `trigger + sim`, where `sim` slews toward `touch_rest` while `trigger_touch` is true (toward 0 otherwise) at `index_slew` per second. The thumb target is 1 when `thumb_touch`, else 0, slewed at `thumb_slew`. Every finger then steps toward its target by `clamp01(smooth_rate·dt)`. Held hands apply `max(curl, held_floor)`. Track `touch_silent` per side with a 10 s timer.
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Mutation pass.** Change 0.3 to 0.4, 1.3 to 1.2, 25 to 10 and 0.6 to 0.5, and drop the silent fallback. Each must fail a test; record the kills.
- [ ] **Step 6: Commit** `feat(fingers): Bonelab touch-sensor curl mapping (pure)`.

### Task 2: Optional OpenXR touch actions (fail-soft), simulated runtime support

**Files:**
- Modify: `src/game_xr.hpp` (`create_actions`, `sample_input`, `InputAction`), `src/controller_input.hpp` (`ControllerInput`), `src/simulated_xr.hpp` (`float_action`/`bool_action`, a test switch to reject touch paths)
- Test: `tests/game_xr_simulation_check.cpp` (extend)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: new `ControllerInput` fields `bool left_trigger_touch,right_trigger_touch,left_thumb_touch,right_thumb_touch; bool touch_bound{};`. `left_thumb_touch` is the OR of `thumbstick/touch`, `x/touch`, `y/touch` and `thumbrest/touch` on the left; the right side uses `a`/`b`. `touch_bound` means the touch suggestion was accepted. `GameXr::touch_status()` returns `{bool bound; bool create_failed;}` for the status block.

- [ ] **Step 1: Write the failing tests** (simulated runtime):
  - `touch_actions_report_booleans`: the scripted touches reach `ControllerInput`.
  - `rejected_touch_bindings_keep_controls`: the simulated runtime rejects any `/touch` path in a suggestion. The mod retries with today's binding list, every existing action still works (trigger, squeeze, buttons, stick, poses, haptic), `touch_bound=false`, and nothing throws.
  - `touch_action_create_failure_is_soft`: a failure to create an action skips the touch actions and leaves the rest working.
  - `existing_bindings_unchanged`: the non-touch binding paths and the action names are identical to before.
- [ ] **Step 2: Run, expect failure.** `... --only 'game_xr_simulation'`
- [ ] **Step 3: Implement.**
  - The touch actions are boolean actions in the same `game_controls` set: `trigger_touch`, `thumbstick_touch`, `first_touch`, `second_touch`, `thumbrest_touch`.
  - OpenXR replaces earlier suggestions for a profile on every call, so there is ONE suggestion for `/interaction_profiles/oculus/touch_controller`. First try today's list plus the touch paths. If that call fails, call again with today's list only, under today's `checked` error, so a failure there is still fatal exactly as now.
  - Action creation for the touch actions is guarded: a failure skips them and is recorded.
  - Don't add other interaction profiles. SteamVR remaps them from the Touch suggestion today, and a new profile suggestion would change their input.
  - The simulated runtime gains touch booleans in its control input, name-based `bool_action` branches, and a `reject_touch` switch for the tests.
- [ ] **Step 4: Run, expect pass.** Same command plus `--only 'dxgi_probe|controller_input|simulated'`.
- [ ] **Step 5: Commit** `feat(fingers): optional OpenXR touch actions, fail-soft`.

### Task 3: Wire the curl into the hand (empty and held hands; weapon fist untouched), status, knobs

**Files:**
- Modify: `src/dxgi_probe.cpp` (the `finger_wants` call before `finger_contact`; the finger-turn input; the status writer; knob reads), `tools/default_features.json`
- Create: `tests/finger_touch_routing_check.py`, registered in `tools/build-msvc.cmd`
- Test: `tests/arm_tracking_check.cpp` (extend if the legacy fallback path needs it)

**Interfaces:**
- Consumes: `FingerTouchCurl`, `FingerTouchParams` (Task 1) and the `ControllerInput` touch fields (Task 2).
- Produces: status block `"fingers":{"profile","mapping":["touch"|"legacy",…],"touch_available":[…],"touch_silent":[…],"raw":{"trigger_touch":[…],"thumb_touch":[…]},"curl":[[5],[5]],"floor_active":[…],"action_create_failed":bool,"touch_bound":bool}`.

- [ ] **Step 1: Write the failing tests** in `finger_touch_routing_check.py`:
  - `wants_from_touch_curl`: the `wanted` array given to `finger_contact` comes from `FingerTouchCurl::update` for empty and held hands, unless `legacy-finger-curl` exists. `finger_contact` stays the last stage.
  - `weapon_hand_keeps_native_fist`: no touch curl when the hand-state bits say the hand is a weapon hand (`hand_weapon_bits`) or the game's own hand animation owns it (`native_hands`). The captured native grip path is unchanged.
  - `legacy_knob_restores_finger_wants`.
  - `curl_never_feeds_intent`: the new curl variables are read only in the finger block and the status writer. Grab, aim, gesture, attack and Blink code never reads them.
  - `status_block_present`.
  - `feature_row_present`.
- [ ] **Step 2: Run, expect failure.**
- [ ] **Step 3: Implement.**
  - Read `build/finger-touch.cfg` and `build/legacy-finger-curl` once at start, logging the outcome.
  - The finger dt is the existing `fdt`.
  - A held hand uses `held=true` (the 0.6 floor) and its wants are capped at `rig::held_curl` for fingers 1–4, as today.
  - If `finger_turns` needs the per-finger curls rather than `hands.curls`/`index_curls`, pass the touch curls through, and say which you did.
  - The legacy mapping must give bit-identical wants to today's.
- [ ] **Step 4: Run, expect pass.** Run the same check plus `--only 'finger_ik|arm_tracking|dxgi_probe'`, then the full harness and the launcher tests.
- [ ] **Step 5: Commit** `feat(fingers): fingers follow touch sensors on empty and held hands`.

### Task 4: Tier B scripted touch (S10), Tier C checklist

**Files:**
- Modify: `src/simulated_gameplay_script.hpp` and `tools/simulated_gameplay_script.py` (touch columns, version bump with backward-compatible loading of older scripts), `linux/d2vr_linux/sim.py` (if segment keys need passing through), `tools/linux/bonelab_run_check.py` (S10 assertions), `tests/bonelab_scenarios_check.py`
- Create: `tools/linux/scenarios/finger_touch.py`, `docs/VR-bonelab-feel-finger-curl-TEST-CHECKLIST.md`

**Interfaces:**
- Consumes: the status block `fingers` (Task 3).
- S10 timeline, each segment ≥ 2 s:
  1. rest the index on the trigger;
  2. point (trigger untouched, grip 1);
  3. thumbs-up (grip 1, thumb untouched);
  4. full fist (everything touched, trigger 1, grip 1);
  5. open hand.
- Assertions, read from the status at each segment's end, so the scenario holds each pose ≥ 1.5 s before its status sample:
  - mapping `touch`;
  - index ≈ 0.3 at rest;
  - index < 0.1 when pointing;
  - thumb < 0.1 at thumbs-up;
  - all fingers > 0.9 in the fist;
  - all < 0.1 open.
  - If the simulated runtime reports `touch_bound=false`, the check fails, unless the run used the reject switch.

- [ ] **Step 1: Write the failing tests.** The scenario loads and compiles. The `bonelab_run_check` fixtures PASS on the expected values and FAIL on a legacy mapping or a stuck thumb. Older scripts still load.
- [ ] **Step 2: Run, expect failure.** `python3 tests/bonelab_scenarios_check.py`
- [ ] **Step 3: Implement** the columns, the scenario and the checks. Write the Tier C checklist with the spec's 4 items:
  - rest a finger on the trigger;
  - point;
  - thumbs-up;
  - hold a bottle and a body: the curl stays at least firm.

  Also add:
  - report any chatter;
  - the resting index half-bend feels right (tunable via `build/finger-touch.cfg`);
  - one item for `touch_available` / `touch_silent` on the user's runtime (Steam Link and Virtual Desktop);
  - one item checking the sword and gun hands still show the native fist.
- [ ] **Step 4: Run, expect pass.** Same command.
- [ ] **Step 5: Tier B run.** One sim run, `--scenario finger_touch --lead 45 --seconds 120`, on the current save. It needs no props. `bonelab_run_check.py RUN --scenario finger_touch` must PASS. Record the per-segment curls.
- [ ] **Step 6: Commit** `test(fingers): scripted touch scenario and headset checklist`.

---

## Self-review

- **Spec coverage.** Part 3 maps onto the plan as follows:
  - the index, middle/ring/pinky and thumb mapping, smoothing and held floor: Task 1;
  - the grab thresholds are unchanged: no task touches them;
  - scope (empty or held hands vs weapon): Task 3;
  - controllers, fail-soft and the profile: Task 2;
  - the final contact clamp: Task 3;
  - the tuning knobs: Tasks 1 and 3;
  - the `fingers` diagnostics block: Task 3;
  - Tier A: Task 1;
  - Tier B S10: Task 4;
  - Tier C: Task 4's checklist.
- **Deliberate deviations.**
  - The hover pre-curl is part of Part 4, as the spec says ("lands with part 4's candidate focus").
  - The Index trackpad touch and other profiles are deferred. A new profile suggestion would change those controllers' input under SteamVR, and the user plays on Quest. Those controllers keep the legacy mapping, as the spec allows ("Controllers without touch sensors … keep the current mapping").
- **Type consistency.** `FingerTouchCurl` and `FingerTouchParams` from Task 1 are used in Task 3. The `ControllerInput` touch fields from Task 2 are used in Task 3 and the simulated runtime. The `fingers` status block from Task 3 is used in Task 4.
- **Review Focus.** Each of the five has a named test: Task 2 `rejected_touch_bindings_keep_controls`; Task 1 `silent_touch_falls_back_to_legacy` and `hitch_and_bad_dt_safe`; Task 3 `weapon_hand_keeps_native_fist` and `curl_never_feeds_intent`.
