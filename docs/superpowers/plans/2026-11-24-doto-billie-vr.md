# DOTO VR: Billie's powers, dual Voltaic Guns, grip fixes, mines: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the Death of the Outsider VR port's gameplay layer: Billie's powers in VR, polished dual Voltaic Guns, the grip-cancel bug (GitHub #6, both games), the Hook Mine mode switch and mines that stick when thrown, each proven in a simulated session.

**Architecture:** All gameplay changes live in the shared `src/` (D2 base) and reach DOTO through `tools/doto/port.py`; DOTO-only behaviour sits behind `d2vr::game_traits` and keep-as-is rules in `tools/doto/offsets.json`. New pure logic gets a host check in `tests/`; engine-facing changes get a status block in `xr-live-status.json` and a gameplay-script scenario in `tools/linux/scenarios/`.

**Tech Stack:** C++17 DLL (MinHook, OpenXR, D3D11), clang-cl + xwin + Wine harness on Linux, Python 3 tools, Proton for simulated sessions.

**Spec:** `docs/superpowers/specs/2026-11-24-doto-billie-vr-design.md` (research: `docs/doto-research/2026-11-24-R{1..4}-*.md`).

## Global Constraints

- Build checks: `python3 tools/linux/run_build_cmd.py --only '<check>'`; DOTO DLL: `python3 tools/linux/build_doto.py <ver>` (Task 1). Every changed check passes under `/W4 /WX`.
- DOTO DLL versions continue from 145751 (first build of this plan = 145752); D2 candidate versions continue from 1459.
- Every new feature knob gets a default-on row in `tools/default_features.json` (user requirement since v612).
- Hypothesis fixes ship default-off; fixes proven in sim ship default-on.
- Never edit `build/doto-src/`; DOTO-only RVAs in `src/` need a keep-as-is rule in `tools/doto/offsets.json` with `why`.
- Sims: never while the user plays; saves and `dishonored_DOConfig.cfg` backed up and restored by the runner; screen unlocked.
- Helper models: Sonnet for implementation and reviews, Haiku for mechanical edits, Opus for engine hooks and the final review. Medium effort.
- Claims of "fixed" need an A/B sim against the baseline of Task 2.

## Review Focus

- Grip pressed while NO power aim is held (plain fist, grab, sword): must behave exactly as before (fist never fires, grab works). Test in Task 4 (`no_aim_grip_keeps_fist_rule`).
- Power aim held, grip squeezed, trigger released, grip still held: no cast, no second cancel, LT stays released. Test in Task 4 (`release_after_cancel_never_casts`).
- Mine equipped and trigger held while the hand moves fast (throw gesture during a long hold): one action only (mode switch OR throw), never both. Test in Task 6 (`long_hold_then_open_does_not_throw`).
- Sword drawn and holstered quickly in the copy hand (grip flutter at the shoulder): ring must not flash. Test in Task 8 (`ring_release_hysteresis`).
- Displace swap during a sword swing / authored camera: fade must not stick on (fade bounded to its window). Test in Task 12 (`fade_window_expires`).

---

## Phase A: Lab and simulation rig

### Task 1: Build DOTO DLLs on Linux

**Files:**
- Create: `tools/linux/build_doto.py`
- Modify: `tools/doto/target.py` (`steam_libraries()`: add `~/.local/share/Steam`, `~/.steam/steam` roots)
- Modify: `tools/linux/README.md` (one section: "DOTO build")

**Interfaces:**
- Produces: `python3 tools/linux/build_doto.py <ver> [--python <venv python>]` -> `build/doto/d2vr_render_probe_arm_v<ver>.dll` (+ `.pdb`, `.map`); exit 0 only on a linked DLL with no `/force:unresolved`.

- [ ] **Step 1:** Implement: run `tools/doto/port.py` (with the RE venv python), read `tools/doto/target.py folders`, ensure `build/md/*.obj` and `build/imgui/*.obj` exist (else run `run_build_cmd.py --only 'minhook|imgui'` steps), then translate the cl line of `tools/doto/build-doto.cmd` through `run_build_cmd.cl_to_clang` (import it), cwd = target `SRC`.
- [ ] **Step 2:** Run `python3 tools/linux/build_doto.py 145752`. Expected: `dll_exit=0`, DLL exists, no "degraded" note.
- [ ] **Step 3:** Run `python3 tools/linux/run_build_cmd.py --only 'dxgi_probe\.obj'` (D2 candidate). Expected: PASS (D2 still builds).
- [ ] **Step 4:** Commit `tools/linux/build_doto.py tools/doto/target.py tools/linux/README.md` ("Linux: DOTO DLL build (port + clang-cl)").

### Task 2: `d2vr-linux --game doto` and a baseline sim

**Files:**
- Modify: `linux/d2vr_linux/steam.py` (a `GameProfile` dataclass: appid, exe, build, sha256, folder, saves_rel, config_name, mod_home_win/rel, process names; `D2` and `DOTO` instances)
- Modify: `linux/d2vr_linux/sim.py`, `modhome.py`, `doctor.py`, `cli.py` (thread the profile through; `--game d2|doto`, default d2)
- Test: `linux/tests/` (add profile cases next to the existing tests)
- Create: `tools/linux/scenarios/doto_idle_look.py` (60 s: stand, look around, look down, both grips/triggers once)

**Interfaces:**
- Produces: `linux/d2vr-linux --game doto install|doctor|sim ...`; sim artifacts in `~/d2vr-test-builds/<run>/` (status JSON snapshots, logs).

- [ ] **Step 1:** Write tests: profile constants for DOTO (`appid=="614570"`, `exe=="Dishonored_DO.exe"`, sha `be73d979fdb5a6eacbcd385944c6060b97c997d06ab9d72fb39f275a866b955d`, saves rel `.../Arkane Studios/Dishonored_DO/base`, config `dishonored_DOConfig.cfg`, mod home `DishonoredVR-DOTO`); `conflicts()` detects `Dishonored_DO.exe`; D2 defaults unchanged.
- [ ] **Step 2:** Run the linux test suite (`python3 -m unittest discover linux/tests`). Expected: new tests FAIL.
- [ ] **Step 3:** Implement the profile threading; default behaviour for D2 byte-identical.
- [ ] **Step 4:** Run the suite. Expected: PASS.
- [ ] **Step 5:** Build the proxy (`python3 linux/proxy/build.py`), install the Task-1 DLL into the DOTO prefix (`--game doto install --zip` from a locally made candidate zip/manifest, `tools/doto/make_candidate.py`), `--game doto doctor` all green.
- [ ] **Step 6:** Baseline: build 7ef5612 as v145752-base, `sim --game doto --scenario doto_idle_look` in the newest chapter-2 save. Expected: gameplay frames paired = total, hand aim applied > 0, `full_body.visible` 1, 0 faults. Save the status JSON as the A/B baseline (`~/d2vr-test-builds/doto-baseline/`).
- [ ] **Step 7:** Commit linux/ changes + scenario ("Linux sim: Death of the Outsider profile").

### Task 3: Read-only power and gadget observer

**Files:**
- Create: `src/native_power_observer.hpp` (reads only; SEH-guarded `rig::value` reads)
- Modify: `src/game_profile.hpp` (traits: `swapper_vtable`, `theeye_vtable`, `mimic_vtable`, `hook_mine_vtable`, `weapon_states_vtable`; D2 values 0 = observer parts off)
- Modify: `tools/doto/offsets.json` (keep-as-is rules for the DOTO vtables), `src/dxgi_probe.cpp` (status block `powers`, call site in the control loop at the existing `native_player_state` reads)
- Test: `tests/native_power_observer_check.cpp` (fake memory image: component list -> found by vtable, fields decoded)

**Interfaces:**
- Produces: `struct PowerSnapshot { bool aim_active, cast_flag; int usepower_state; std::uint32_t equipped_power_vtable; bool swapper_aiming, swapper_has_target; float swapper_target[3]; int theeye_state; float theeye_body[3], theeye_body_yaw; bool mimicking; int weapon_state_index; bool state73, state93, state41; float voidstrike_commit_s, voidstrike_mana; }`; `PowerSnapshot NativePowerObserver::read(reader, image_base, player)`; atomics `power_snapshot_` readable by the render/control threads; status `powers` object with these fields plus counters.

- [ ] **Step 1:** Write `tests/native_power_observer_check.cpp`: a fake component list with a Swapper (vtable trait) at slot k, `+0xa4=1`, sensor target set -> `swapper_aiming && swapper_has_target`; TheEye `+0x19c=4` -> `theeye_state==4`; unknown vtables -> defaults; null player -> all defaults, no fault.
- [ ] **Step 2:** Run `--only native_power_observer_check`. Expected: FAIL (missing header).
- [ ] **Step 3:** Implement per R2 §2 offsets (Swapper `+0xa4`, `+0x259`, sensor `power+0xc8` `+0x148`; TheEye `+0x19c/+0x358/+0x364`; Mimic component `+0x4c==2`; state actives via the existing `native_player_state`; slash decl values when present).
- [ ] **Step 4:** Run the check. Expected: PASS. Build both DLLs (D2 `dxgi_probe.obj` step, DOTO 145753).
- [ ] **Step 5:** Sim (DOTO, `powers_probe.py`: equip each power via wheel test command, hold/release LT, tap again): `powers` block shows Swapper aim during the hold and a target after release; state 93 edge after the second tap. Record answers to R2 open questions 1-3, 5, 7 in `docs/DOTO-VR.md` §3.
- [ ] **Step 6:** Commit.

## Phase B: Grip and mines

### Task 4: Grip cancels a held power aim (issue #6, both games)

**Files:**
- Create: `src/power_cancel.hpp` (pure: `PowerCancelLatch`)
- Create: `src/native_power_cancel.hpp` (MinHook detour of the UsePower release handler D2 `0x8566f0`; DOTO via port map; prologue pin; status `power_cancel`)
- Modify: `src/dxgi_probe.cpp:17682` (+ `:17151`, `:17805`) fist-rule exemption while `latch.aim_held()`; grip Use/grab off for that hand while aiming; install near the blink-aim install
- Modify: `tools/default_features.json` (row `power-grip-cancel`, default on), `tools/build-msvc.cmd` (check lines)
- Test: `tests/power_cancel_check.cpp`

**Interfaces:**
- Produces: `class PowerCancelLatch { void update(float trigger, float grip, bool power_in_hand, std::uint64_t now_ms); bool aim_held() const; bool cancel_pending() const; bool take_cancel(); }` (aim = trigger >= .55 pressed while grip < .55 and power in hand; cancel on grip rising edge >= .55 during aim; cleared when trigger <= .25); `NativePowerCancel::install(image_base)`, `request_cancel()`, status fields `installed, releases_seen, cancels_applied, casts_passed`.

- [ ] **Step 1:** Write tests: `aim_then_grip_cancels` (trigger 1.0 for 200 ms, grip 1.0 -> `take_cancel()` true once); `no_aim_grip_keeps_fist_rule` (grip first then trigger -> `aim_held()` false); `release_after_cancel_never_casts` (cancel, release trigger with grip held -> no second cancel, `aim_held()` false); `grip_flutter_single_cancel`; `no_power_no_aim`.
- [ ] **Step 2:** Run `--only power_cancel_check`. Expected: FAIL.
- [ ] **Step 3:** Implement `PowerCancelLatch`; then the detour: if `request` latched and `power=list(state+0x58,2)` non-null and `power+0xa4` set, call `[vt+0x128](power)` then the original. Pin the handler prologue (D2 bytes from the exe; DOTO bytes via `fn_diff.py`), refuse quietly on mismatch.
- [ ] **Step 4:** Run the check. Expected: PASS. Build D2 + DOTO DLLs.
- [ ] **Step 5:** Sim A/B (DOTO Displace, R4 scenario S1: aim 2.5 s, grip 0.8 s, release; control: plain press/release). Baseline build: cast within 60 ms of the grip edge. New build: `power_cancel.cancels_applied` +1, Swapper target unchanged, no state 93/no marker; control cast still works. D2 run (Emily save with Far Reach, if a D2 save with Far Reach exists in the D2 prefix; else Corvo Blink) same assertions.
- [ ] **Step 6:** Commit ("Both games: the grip cancels a held power aim (#6)").

### Task 5: Measure the Hook Mine mode input

**Files:**
- Create: `tools/linux/scenarios/hook_mine_modes.py` (equip hook mine; LT holds 0.3/0.8/1.5 s with 2 s gaps; then 150 ms pulses of x, y, b, a, R3, RB)
- Modify: `src/native_power_observer.hpp` only if Task 3 lacks the weapon-states index

- [ ] **Step 1:** Run the scenario (DOTO, chapter-2 save with Hook Mines; `equip` test command). Record which input made `powers.state73` rise and `weapon_state_index` toggle, and whether a throw happened after the long hold.
- [ ] **Step 2:** Write the result into the spec §2 ("measured: ...") and commit the scenario.

### Task 6: Hook Mine mode switch gesture

**Files:**
- Modify: `src/throw_gesture.hpp` (`GadgetGrip`: long trigger hold >= `mode_hold_ms` (default 800) with a mode-capable gadget passes the native input measured in Task 5 and suppresses the release throw)
- Modify: `src/dxgi_probe.cpp:17815-17832` (route), `src/game_profile.hpp` (`gadget_mode_switch` true DOTO / false D2), `tools/default_features.json` (`hook-mine-mode-switch`)
- Test: `tests/throw_gesture_check.cpp` (extend)

**Interfaces:**
- Consumes: Task 5 measurement; `PowerSnapshot::state73`, `weapon_state_index`.
- Produces: `GadgetGrip::Out::mode_hold` (true while the native long hold must be sent), `GadgetGrip::Out::mode_switched` (edge), status `gadget_mode {holds, switches, throws_suppressed}`.

- [ ] **Step 1:** Tests: `tap_still_throws` (trigger 120 ms -> tap, no mode_hold); `long_hold_switches` (trigger 900 ms -> mode_hold true from 0 ms, mode_switched edge at release or at engine ack, no tap); `long_hold_then_open_does_not_throw` (long hold, then grip opens with velocity -> no tap/throw); `mode_switch_off_trait` (trait false -> old behaviour exactly).
- [ ] **Step 2:** Run `--only throw_gesture_check`. Expected: new cases FAIL.
- [ ] **Step 3:** Implement; haptic double tick on `mode_switched`.
- [ ] **Step 4:** Run check PASS; build both DLLs; sim `hook_mine_modes.py` variant with the gesture: one switch per long hold, 0 throws during it, `weapon_state_index` toggles; tap still throws.
- [ ] **Step 5:** Commit.

### Task 7: Thrown mines stick (engine placement along the throw)

**Files:**
- Modify: `src/throw_gesture.hpp` (`GadgetGrip`: grip open with hand speed > 1.5 m/s -> `throw_place` instead of `drop`)
- Modify: `src/gadget_aim.hpp` (`gadget_aim_wanted` throw-aim override: origin hand, direction velocity, pitch clamp +-85 deg, valid 250 ms; ThrowHit z not forced 0 during the override)
- Modify: `src/dxgi_probe.cpp:17815-17832` (route: placement tap; fallback to `NativeGadgetDrop::drop` when no item consumed in 400 ms), `tools/default_features.json` (`mine-throw-stick`)
- Test: `tests/throw_gesture_check.cpp`, `tests/gadget_aim_check.cpp` (extend)

**Interfaces:**
- Produces: `GadgetGrip::Out::throw_place`, `GadgetAimWanted::throw_override(origin, dir, until_ms)`; status `mine_throw {throw_place, fallback_drops, hook_mine_state}`.

- [ ] **Step 1:** Tests: `fast_open_places` (speed 2.0 m/s -> throw_place, no drop); `slow_open_drops` (0.5 m/s -> drop); `override_direction_is_velocity` and `override_expires_250ms`; `pitch_clamped`.
- [ ] **Step 2:** Run checks. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Checks PASS; build; sim R4 S3 (wall ahead, glide throw) A/B: baseline `gadget_drop.flights` 1; new `mine_throw.throw_place` 1, `gadget_drop.flights` 0, hook mine state reaches STUCK_READY (observer), inventory count -1; S3b slow open still drops.
- [ ] **Step 5:** Commit.

## Phase C: Dual Voltaic Guns

### Task 8: Sword hand blocks its gun crosshair; sword block restored

**Files:**
- Create: `src/hand_reticle_policy.hpp` (pure: which reticles show)
- Modify: `src/dxgi_probe.cpp` (publish `hand_sword_bits`/`copy_armed_bits` atomics at `:16427` and the copy loop `:17208`; ring at `:12329-12365`; main crosshair `:12291-12296`; `:17169-17170` add `!weapon_owns(s)`; copy ring uses `copy_gate.reticle`), status `crosshair.blocked_reason`, `copy_ring.suppressed[2]`, per-hand `last_ndc`
- Test: `tests/hand_reticle_policy_check.cpp`; scenario `tools/linux/scenarios/voltaic_sword_hand.py`

**Interfaces:**
- Produces: `struct ReticleInputs { bool sword_held[2]; bool busy[2]; bool copy_at[2]; unsigned voltaic_arms; int aim_hand; bool pinned; }`; `ReticlePolicy::update(const ReticleInputs&, std::uint64_t now_ms)`; `bool ring_visible(int h) const`; `bool main_visible() const`; `const char* main_blocked_reason() const` (`"sword"`, `"gun_hidden"`, nullptr); 150 ms release hysteresis.

- [ ] **Step 1:** Tests: `ring_hidden_while_sword_held`; `ring_release_hysteresis` (sword released -> ring back only after 150 ms; flutter 40 ms never shows); `main_hidden_when_aim_hand_holds_sword`; `main_hidden_when_engine_gun_hidden`; `busy_hand_hides_ring`; `d2_ring_unchanged_without_sword`.
- [ ] **Step 2:** Run `--only hand_reticle_policy_check`. Expected: FAIL.
- [ ] **Step 3:** Implement policy + wiring; sword block fix.
- [ ] **Step 4:** Check PASS; build both; sim `voltaic_sword_hand.py` A/B: ring `published[1]` flat while the right sword is held, rises after; RT block reaches the game with sword + right copy (baseline: block counter flat); `fire.fired` flat while the sword is held.
- [ ] **Step 5:** Commit.

### Task 9: Bolt type per hand

**Files:**
- Modify: `src/native_ranged_fire.hpp:311-340` (DOTO `wheel_pick`: `select_ammo` on the copy component, `set_own`), `:788-846` (`track_ammo` for Voltaic copies), `src/dxgi_probe.cpp:17185` (`note_copies` with the crossbow kind for each `voltaic_copy_at[h]` in DOTO), test command `voltaic bolt left|right <i>`
- Test: `tests/native_ranged_fire_check.cpp` (extend)

- [ ] **Step 1:** Status-only sim first (current build): right copy shots, read `fire.v1094[4]` own_shots vs `fire.fired` to know which branch applies (R3 G-b1). Record.
- [ ] **Step 2:** Tests: `doto_right_pick_selects_copy_ammo` (fake component: `select_ammo` called with the picked entry, engine gun keeps its own); `left_pick_keeps_engine_gun`; `copy_bolt_tracked_in_status`.
- [ ] **Step 3:** Run; FAIL. Implement. Run; PASS.
- [ ] **Step 4:** Build; sim: `voltaic bolt right 1` then right shots -> projectile def differs from left shots; `fire.bolts` non-negative for the copy.
- [ ] **Step 5:** Commit.

### Task 10: Copy gun render polish

**Files:**
- Modify: `src/dxgi_probe.cpp:7983-7986` (`publish_voltaic_copy()` also at eye-1 Begin), `src/native_weapon_hands.hpp:1857-1859` only if the capture still shows a gap; mirrored scale only if the capture shows handed details

- [ ] **Step 1:** Baseline capture: sim with `voltaic both`, right arm swinging, per-eye mirror frames (`tools/doto/burst.py` equivalent on Linux: window capture of the mirror) -> measure the copy's position difference between eyes vs the native gun's.
- [ ] **Step 2:** Implement eye-1 placement; capture again; the copy's inter-eye offset matches the native gun's within 1 px.
- [ ] **Step 3:** Commit with both captures in `docs/images/doto/`.

## Phase D: Billie's powers

### Task 11: Displace by hand

**Files:**
- Modify: `src/dxgi_probe.cpp:10686-10701` (publish gate OR `power_snapshot.swapper_aiming`)
- Create (only if Step 2 shows head steering): `src/native_displace_aim.hpp` (clone of `native_blink_aim.hpp` on DOTO `0x3e21b0`, trait `displace_aim_rva`, keep rule)
- Scenario: `tools/linux/scenarios/displace_aim.py` (hand yawed 30 deg from the head; hold 1.5 s, release, tap)

- [ ] **Step 1:** Implement the gate term; build; sim `displace_aim.py`.
- [ ] **Step 2:** Expected: Swapper target along the hand ray (< 5 deg from hand forward, > 20 deg from head forward); swap after the tap (state 93 edge, origin moves to within 1 m of the target). If the target follows the head: implement the hook (Opus task), rebuild, rerun.
- [ ] **Step 3:** Commit.

### Task 12: Teleport fade (Displace swap, Foresight exit)

**Files:**
- Create: `src/teleport_fade.hpp` (pure: fade window from edges)
- Modify: `src/game_xr.hpp` (full-view dark quad layer alpha), `src/dxgi_probe.cpp` (edges from `PowerSnapshot`: state93 rising, theeye 4->5/0; anchor smoother + hand clamp reset on those edges), `tools/default_features.json` (`teleport-fade`)
- Test: `tests/teleport_fade_check.cpp`

**Interfaces:**
- Produces: `TeleportFade::edge(std::uint64_t now_ms, int duration_ms)`, `float alpha(std::uint64_t now_ms) const` (ramp 0->1 in 30% of the window, hold, 1->0), status `teleport_fade {fades, last_ms}`.

- [ ] **Step 1:** Tests: `fade_shape` (alpha 0 before, 1 at 40%, 0 after); `fade_window_expires` (no edge for 10 s -> alpha 0); `retrigger_extends_not_stacks`.
- [ ] **Step 2:** FAIL -> implement -> PASS; build; sim `displace_aim.py`: `teleport_fade.fades` 1 per swap, frames still paired, 0 faults.
- [ ] **Step 3:** Commit.

### Task 13: Void Strike from the sword charge

**Files:**
- Modify: `src/attack_intent.hpp:119-153` (per-press cap), `src/dxgi_probe.cpp:17602-17620` (press length `voidstrike_commit_s*1000+60` when owned, else 340)
- Test: `tests/attack_intent_check.cpp` (extend: `press_length_override_respects_cap`)
- Scenario: `tools/linux/scenarios/void_strike.py`

- [ ] **Step 1:** Read the decl values in the Task 3 sim (`voidstrike_commit_s`, mana). If 340 ms already commits, record and skip Step 2-3.
- [ ] **Step 2:** Test FAIL -> implement -> PASS.
- [ ] **Step 3:** Build; sim: charged release -> slash committed flag 1, mana drop >= cost.
- [ ] **Step 4:** Commit.

### Task 14: Semblance during a choke

**Files:**
- Modify: `src/dxgi_probe.cpp:17682` (fist-rule exception while `state41` active), scenario `tools/linux/scenarios/semblance_choke.py`

- [ ] **Step 1:** Implement (one condition, shares Task 4's exemption code path).
- [ ] **Step 2:** Build; sim (guard from behind, choke, LT 1 s): states 95/102 then 96; `mimicking` true.
- [ ] **Step 3:** Commit.

### Task 15: Foresight: no body under the spirit, comfortable exit

**Files:**
- Modify: body renderer gate (`src/full_body_driver.hpp` / body renderer draw gate), physical hand contact and grab gates (`native_grab`, hand body), turning (treat exit yaw as a snap), `src/dxgi_probe.cpp` wiring from `PowerSnapshot::theeye_state`
- Scenario: `tools/linux/scenarios/foresight_scout.py`

- [ ] **Step 1:** Sim the current build in Foresight (scenario) to see what draws and what the camera mode is; record.
- [ ] **Step 2:** Implement gating for states 3/4 and the exit edge (fade from Task 12, smoother reset).
- [ ] **Step 3:** Build; sim: `full_body.visible` 0 and hand-contact pushes 0 in states 3/4; origin restored within 0.05 m; one fade at exit; frames paired.
- [ ] **Step 4:** Commit.

## Phase E: Integration

### Task 16: Regression pass, docs, launcher rows, review

- [ ] **Step 1:** Full Linux harness: `python3 tools/linux/run_build_cmd.py --keep-going`. Expected: no FAILED (skips listed).
- [ ] **Step 2:** DOTO regression sim A/B vs Task 2 baseline with `doto_idle_look.py` + `voltaic_fire.py`: paired frames, hand aim, fire counts not worse.
- [ ] **Step 3:** Update `docs/DOTO-VR.md` (§3 powers table "state", §4 plan, §5 runs), `tools/default_features.json` rows, a headset checklist `docs/VR-DOTO-v1457xx-TEST-CHECKLIST.md`.
- [ ] **Step 4:** Final whole-branch review (Opus), fix findings, push `feat/doto-vr`; ask the user before any release.
