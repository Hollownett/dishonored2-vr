# Code-Review Fixes (2026-10-17) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the verified crash, session-teardown, judder and feel defects found in the 2026-10-17 five-part code review, without changing accepted locomotion/turning behaviour.

**Architecture:** Small, local fixes inside the existing headers and `src/dxgi_probe.cpp`; no restructuring. Pure-logic fixes get behavioural C++ checks in the existing `tests/*_check.cpp` style; `dxgi_probe.cpp`/hook-only fixes get a `tests/*_routing_check.py` source guard (the repo's existing convention for that file) plus a headset verification line.

**Tech Stack:** C++17 (MSVC `cl`, `/EHsc /W4 /WX`), MinHook, OpenXR, D3D11; tests built by `tools\build-msvc.cmd`; Python 3 source guards.

**Spec:** The review findings in this session (summarised per task below with file:line on `origin/integration/v59x`).

## Global Constraints

- Base branch: `origin/integration/v59x` (12 commits ahead of `main`; all findings re-checked there). Work on a new branch `fix/review-2026-10-17`.
- Preserve user-accepted locomotion and turning (AGENTS.md). No change to snap/smooth turn amounts or movement mapping.
- Every new check must be registered in `tools\build-msvc.cmd` next to its neighbours, same flags (`/std:c++17 /utf-8 /EHsc /W4 /WX /O2`), and fail the build on non-zero exit.
- Pure headers with no `<windows.h>` (`physical_grab.hpp`, `vr_perf_governor.hpp`) must stay `<windows.h>`-free; their checks also run on Linux: `g++ -std=c++17 -Isrc tests/<name>.cpp -o /tmp/<name> && /tmp/<name>`.
- Engine calls guarded against access violations must use SEH (`__try/__except`), never `catch(...)` — the build is `/EHsc`, which does not translate SEH into C++ exceptions.
- Diagnostic file I/O must never throw into the XR present path.
- Automated checks do not establish headset behaviour; each task with a headset line must be confirmed in the headset before the branch is merged.

## Review Focus

1. Tracking dropout mid-carry (hand pose NaN for 1–3 frames) → held object stays put or is released; game never crashes or launches the object. Pinned by Task 3 `nan_target_rejected`.
2. Level load / death reload while gripping a physics prop or with the crosshair visible → no crash. Pinned by Task 2 `guarded_call_catches_av` + headset line.
3. Sword thrown off a ledge / out of the map → sword stops after `max_flight_s` and lands at last valid position. Pinned by Task 4 `flight_times_out`.
4. Borderline-performance scene (≈4–6 % missed slots) → resolution settles instead of oscillating every few seconds. Pinned by Task 7 `no_oscillation`.
5. Status JSON locked by an editor/antivirus or disk full → VR keeps running, status flag reports the write failure. Pinned by Task 6 routing check + manual test.

---

### Task 1: Branch setup

**Files:** none (git only)

- [ ] **Step 1:** `git -C ~/github/dishonored2-vr fetch && git -C ~/github/dishonored2-vr worktree add ../dishonored2-vr-fixes -b fix/review-2026-10-17 origin/integration/v59x`
- [ ] **Step 2:** On the Windows dev box run `tools\build-msvc.cmd` on the untouched branch. Expected: completes with exit 0 (baseline green). Record any pre-existing failure before changing code.

---

### Task 2: Real crash guards around engine writes (native_grab, native_crosshair)

Finding: `catch(...)` under `/EHsc` does not catch access violations; the velocity setter (`native_grab.hpp:284,305,407,422`) and AS3 setter (`native_crosshair.hpp:147-148`) write into engine objects that can be torn down during load/death.

**Files:**
- Create: `src/native_guarded_call.hpp`
- Modify: `src/native_grab.hpp` (4 `velocity_(...)` call sites), `src/native_crosshair.hpp:135-150`
- Test: `tests/native_guarded_call_check.cpp`; register in `tools\build-msvc.cmd`

**Interfaces:**
- Produces: `namespace d2vr { template<class F> bool guarded_call(F& f) noexcept; }` — runs `f()` inside `__try/__except(EXCEPTION_EXECUTE_HANDLER)`; returns `true` if it returned normally, `false` on any SEH exception. The function body must hold no objects with destructors (MSVC C2712).

- [ ] **Step 1: Write the failing test** `tests/native_guarded_call_check.cpp`:
  - `guarded_call_runs_normally`: a lambda that sets `int hit=1` → returns `true`, `hit==1`.
  - `guarded_call_catches_av`: a lambda writing `*reinterpret_cast<volatile int*>(0x10)=1` → returns `false`, process continues.
- [ ] **Step 2:** Build/run via `tools\build-msvc.cmd`. Expected: compile error (header missing).
- [ ] **Step 3:** Implement `src/native_guarded_call.hpp` (`#pragma once`, `<windows.h>` with `NOMINMAX`).
- [ ] **Step 4:** In `native_grab.hpp`, wrap each of the four `velocity_(...)` calls: `auto call=[&]{velocity_(...);}; if(!guarded_call(call)) {++contact_.faults; /* existing release-all-lanes path */}`. Extract the existing catch body at `invoke()` (`lane.grab.release(false)…++lost_`) into `void release_all_locked()` and call it on a `false` result (taking `mutex_` the same way the catch does).
- [ ] **Step 5:** In `native_crosshair.hpp` `place()`: change the `object` check to `rig::pointer(object)` (`game_rig.hpp:11`), and wrap both `setter_` calls in one `guarded_call`; on `false` → `++rejected_`, `applied=false`, and `failed_=true` so the hook stops writing until reinstall.
- [ ] **Step 6:** Run `tools\build-msvc.cmd`. Expected: all checks incl. `native_crosshair_check`, `native_guarded_call_check` pass.
- [ ] **Step 7: Headset line:** grip a physics prop, then Pause > Load; die while crosshair visible and Restart Checkpoint (×3 each). Expected: no crash; `xr-live-status.json` shows grab `faults` ≥ 0 and game continues.
- [ ] **Step 8: Commit** `fix: use SEH guards for engine velocity and crosshair setter calls`

---

### Task 3: Reject non-finite / non-orthonormal held-object targets

Finding: `NativeHeldObject::publish` (`native_held_object.hpp:63`) and `commit` memcpy unvalidated `pos`/`axis` into engine memory; `anchor` (`:328-339`) writes a quaternion from `quat_from_rot` without a finite check.

**Files:**
- Modify: `src/native_held_object.hpp`
- Test: `tests/native_held_object_check.cpp` (add cases)

**Interfaces:**
- Produces: `static bool NativeHeldObject::target_valid(const Target&) noexcept` — true iff all of `pos[3]`, `axis[9]` are finite, `|pos[i]| < 1e5`, each axis row has length within `1e-2` of 1, pairwise |dot| `< 1e-2`, and determinant `> 0`. Counter `inline static std::atomic<std::uint64_t> rejected_targets{}`.

- [ ] **Step 1: Write failing tests** in `native_held_object_check.cpp`:
  - `nan_target_rejected`: publish a valid target, then publish one with `axis[4]=NAN` → `snapshot` still returns the first target's `pos`; `rejected_targets==1`.
  - `scaled_axis_rejected`: axis rows scaled ×2 → rejected.
  - `anchor_rejects_nan_quat`: call `NativeHeldObjectCheck::anchor` with a target whose `axis[0]=NAN` (bypassing `publish`) → returns `false` and fixture bytes at `rf+0x3d0..0x3f0` unchanged.
  - `reflection_rejected`: axis rows orthonormal but det −1 (`{1,0,0, 0,1,0, 0,0,-1}`) → `publish` rejects (add `det>0` to `target_valid`).
- [ ] **Step 2:** Run `tools\build-msvc.cmd`. Expected: new asserts fail.
- [ ] **Step 3:** Implement `target_valid`; in `publish()` return early (incrementing `rejected_targets`) before taking the lock when invalid; in `commit()` re-check `target_valid(t)` after `carry()` and return 0 if invalid; in `anchor()` return `false` unless all four `q` components are finite and `|q|` within `1e-3` of 1, then normalise before writing.
- [ ] **Step 4:** Run `tools\build-msvc.cmd`. Expected: PASS, including existing held-object and `held_object_routing_check.py`.
- [ ] **Step 5:** Add `rejected_targets` to the held-object section of the status JSON in `dxgi_probe.cpp` (next to `placed`).
- [ ] **Step 6: Commit** `fix: reject non-finite held-object targets before engine writes`

---

### Task 4: Bound thrown-sword flight and validate launch input

Finding: `flight_since_` (`native_weapon_hands.hpp:179`) is never read; flight never ends off-map; `launch()`/`spawn()` accept NaN.

**Files:**
- Modify: `src/native_weapon_hands.hpp` (`SwordFlight`, `request()`, `launch()`, `fly()`)
- Test: `tests/sword_flight_check.cpp`

**Interfaces:**
- Produces: `SwordFlight::age` (`double`, seconds), `static constexpr double SwordFlight::max_flight_s=6.0`, `static bool SwordFlight::finite(const SwordFlight&) noexcept`. `step()` returns `false` and sets `flying=false` when `age>=max_flight_s` or the state is non-finite; on timeout `pos` stays at the last finite position.
- `NativeWeaponHands::request(const Request&)` returns `false` for any op whose `pos`/`vel`/`axis` contain non-finite values.

- [ ] **Step 1: Write failing tests** in `sword_flight_check.cpp`:
  - `flight_times_out`: launch over an empty world (cast always `{}`) with `vel={5,0,0}`; step at `dt=.02` 400 times → `flying==false` by step 300, `age>=6.0`, `pos` finite.
  - `nan_velocity_stops`: `vel.x=NAN` → first `step` returns `false`, `pos` unchanged.
- [ ] **Step 2:** Run `tools\build-msvc.cmd`. Expected: FAIL.
- [ ] **Step 3:** Implement: `age+=dt` at the top of `step()`; finite/timeout check before integrating; reject non-finite in `request()`; delete the unused `flight_since_`.
- [ ] **Step 4:** Run `tools\build-msvc.cmd`. Expected: PASS.
- [ ] **Step 5: Headset line:** throw the sword off a balcony edge into a void area. Expected: it stops and lies at its last valid position within ~6 s; game keeps running.
- [ ] **Step 6: Commit** `fix: end thrown-sword flight after 6 s and reject non-finite throws`

---

### Task 5: Don't hold the turning feedback lock across the game's physics step

Finding: `NativeTurning::invoke` (`native_turning.hpp:96`) holds `feedback_mutex_` across `original_(...)` (`:132`); the XR/render thread (`publish_head`, `head_compensation`) and input thread (`orient_movement`) block on it for the whole physics step.

**Files:**
- Modify: `src/native_turning.hpp:89-140`
- Test: `tests/native_turning_lock_routing_check.py`; register in `tools\build-msvc.cmd` next to `physical_grip_routing_check.py`

- [ ] **Step 1: Write the failing source guard** `tests/native_turning_lock_routing_check.py`: within the body of `void invoke(`, assert the text `feedback_lock.unlock()` appears before `original_(delegate,` and `feedback_lock.lock()` appears after it; include the repo's mutation layer (replace `feedback_lock.unlock();` with empty → `check()` must fail), as `physical_grip_routing_check.py` does.
- [ ] **Step 2:** Run `python tests\native_turning_lock_routing_check.py`. Expected: FAIL.
- [ ] **Step 3:** Implement: `const bool relock=feedback_lock.owns_lock(); if(relock) feedback_lock.unlock(); original_(...); if(relock) {feedback_lock.lock(); if(feedback_.pending()) {/* existing acknowledge/cancel block */}}`. Nothing else in `invoke` changes.
- [ ] **Step 4:** Run `tools\build-msvc.cmd`. Expected: PASS.
- [ ] **Step 5: Headset line:** snap-turn 20× and smooth-turn 10 s while walking; compare `frame-timing` missed-slot % against a v59x baseline run in the same spot. Expected: turning feels identical (user confirms); missed slots ≤ baseline.
- [ ] **Step 6: Commit** `perf: release turning feedback lock across native physics step`

---

### Task 6: Diagnostics can't tear down VR; glyph hook is never unhooked mid-call

Findings: `dxgi_probe.cpp:6263,6271` throw on status/lifecycle write failure → `recover_xr_session`; `remove_glyph_hook()` (`:312-315`) calls `MH_RemoveHook` and nulls `original_create_texture2d` while streaming threads may be inside `create_texture2d_hook` (`:279`).

**Files:**
- Modify: `src/dxgi_probe.cpp:139` (new flag), `:6263`, `:6271`, `:305-315`, status JSON near `:6259`
- Test: `tests/diagnostic_isolation_routing_check.py`; register in `tools\build-msvc.cmd`

**Interfaces:**
- Produces: `static std::atomic<bool> xr_status_write_failed{}` reported as `"xr_status_write_failed"` in the status JSON and reset where `frame_timing_write_failed` is reset (`:7203`).

- [ ] **Step 1: Write the failing source guard**: (a) the strings `Cannot save live XR session state` and `Cannot save XR lifecycle state` must not appear inside a `throw`; (b) the body of `remove_glyph_hook` must not contain `MH_RemoveHook` nor `original_create_texture2d=nullptr`; (c) `install_glyph_hook` must re-enable via `MH_EnableHook(create_texture2d_target)` when `create_texture2d_target` is already set. Include mutation checks.
- [ ] **Step 2:** Run the guard. Expected: FAIL.
- [ ] **Step 3:** Replace both throws with `xr_status_write_failed=true;` (skip the rest of that diagnostic block). Change `remove_glyph_hook` to `MH_DisableHook` only, keeping `create_texture2d_target` and the trampoline (matches the repo's "never free trampolines" policy); in `install_glyph_hook`, if `create_texture2d_target` is set and belongs to the same device vtable slot, just `MH_EnableHook` it.
- [ ] **Step 4:** Run `tools\build-msvc.cmd`. Expected: PASS.
- [ ] **Step 5: Manual test:** during VR, open `artifacts\xr-live-status.json` in a program that holds an exclusive lock (e.g. `powershell -c "$f=[IO.File]::Open('artifacts\xr-live-status.json','Open','Read','None'); Start-Sleep 20; $f.Close()"`). Expected: VR keeps running with no session recovery in `xr-recoveries.log`; afterwards the JSON shows `"xr_status_write_failed":true`. Then stop and restart VR twice from the launcher while in a level. Expected: no crash.
- [ ] **Step 6: Commit** `fix: keep diagnostics out of XR recovery and never unhook CreateTexture2D mid-call`

---

### Task 7: Resolution governor anti-oscillation

Finding: each scale change costs a ~0.5 s stall (`vr_perf_governor.hpp:19-21`); the down 0.06 / up 0.02 after 4 calm windows loop oscillates in borderline scenes.

**Files:**
- Modify: `src/vr_perf_governor.hpp`
- Test: `tests/vr_perf_governor_check.cpp`

**Interfaces:**
- Produces: `PerfGovernorConfig::up_lockout_after_down` (`unsigned`, windows, default `60` = 30 s at 90 Hz); `up_hold` default changes `4 → 8`. After any down step, no up step happens for `up_lockout_after_down` decision windows (replaces the role of `down_cooldown` for up-steps; keep the field for compatibility).

- [ ] **Step 1: Write failing tests**:
  - `no_oscillation`: loop 20×: `run(g,45,1.0,.10)` then `run(g,45*8,.5,0)` → `g.changes<=3` (measured on current code: 40 changes). The lockout starts on a down step that actually changed `scale_`.
  - `recovers_after_lockout`: one down step, then `run(g,45*(60+8+1),.5,0)` → `g.scale()>d`.
  - Update the existing cooldown case (`run(g,45*6,.5,0); CHECK(g.scale()>d)`) to `run(g,45*(60+8),.5,0)`; this is the intended behaviour change.
- [ ] **Step 2:** `g++ -std=c++17 -Isrc tests/vr_perf_governor_check.cpp -o /tmp/vpg && /tmp/vpg`. Expected: `FAIL <line>: g.changes<=3`.
- [ ] **Step 3:** Implement the lockout counter in `feed()`.
- [ ] **Step 4:** Re-run the g++ command. Expected: exit 0, no output. Then `tools\build-msvc.cmd` on Windows.
- [ ] **Step 5: Headset line:** stand in a heavy scene (Karnaca harbour) for 2 min with dynamic resolution on. Expected: ≤ 3 resolution changes in `xr-live-status.json` `perf_governor.changes`, no repeating half-second freezes.
- [ ] **Step 6: Commit** `fix: stop dynamic-resolution oscillation after a down step`

---

### Task 8: Throw velocity loses motion on duplicate-tick samples

Finding: `PhysicalGrab::sample` (`physical_grab.hpp:74-85`) sets `previous_=hand` even when `dt<1 ms` (same `GetTickCount64` quantum), so that motion never enters `velocity_`; throws come out ~30 % weak at 90 Hz.

**Files:**
- Modify: `src/physical_grab.hpp`
- Test: `tests/physical_grab_check.cpp`

**Interfaces:**
- Produces: `inline constexpr std::uint64_t grab_velocity_min_ms=8;` — when `0 <= tick-tick_ < grab_velocity_min_ms`, `sample()` still runs the discontinuity check and grip press/release logic and returns `true`, but leaves `previous_`, `tick_` and `velocity_` untouched.

- [ ] **Step 1: Write failing test** `duplicate_ticks_keep_motion`: hand moving +0.011 m per call along x with ticks `1000,1000,1016,1016,1032,1032,…` (16 ms quanta, two frames per quantum) for 20 calls → `grab.velocity().x` within 15 % of `0.011*2/0.016 = 1.375 m/s`.
- [ ] **Step 2:** `g++ -std=c++17 -Isrc tests/physical_grab_check.cpp -o /tmp/pg && /tmp/pg`. Expected: assertion failure on the new case.
- [ ] **Step 3:** Implement the early branch after the discontinuity check.
- [ ] **Step 4:** Re-run g++ command. Expected: exit 0. Then `tools\build-msvc.cmd`.
- [ ] **Step 5: Headset line:** throw a bottle with the same arm motion 10× before/after. Expected: user reports throws travel noticeably further and feel consistent.
- [ ] **Step 6: Commit** `fix: keep throw-velocity baseline across same-tick samples`

---

### Task 9: Launcher checks the game build before launch

Finding: `Probe.cs:17` pins the exe hash but only checks it after starting the game and sleeping 45 s; `Env.RunChecks` (`Core.cs:149-172`) shows all green on an updated game.

**Files:**
- Modify: `launcher/Core.cs` (`Env.RunChecks`, `Ready()`), `launcher/Probe.cs` (make `GameHash` `internal`)

**Interfaces:**
- Produces: a setup check row "Dishonored 2 (supported build)" computed once per game path (cache by path + file length + last-write time) via SHA-256 of `Dishonored2.exe` compared with `Probe.GameHash`; `Ready()` is false when it fails; the row's detail text says "Game was updated by Steam; this mod build supports only the pinned version."

- [ ] **Step 1:** Implement the check row and gating.
- [ ] **Step 2:** `dotnet build launcher -c Release`. Expected: 0 warnings as errors.
- [ ] **Step 3: Manual test:** point the launcher at a copy of the game folder with one byte of `Dishonored2.exe` changed. Expected: the new row is red, LAUNCH VR disabled, no game start. With the real install: row green.
- [ ] **Step 4: Commit** `fix(launcher): verify game build before launch`

---

### Task 10: Full verification and release candidate

- [ ] **Step 1:** `tools\build-msvc.cmd` on Windows → exit 0. Linux: run the two g++ checks from Tasks 7–8 → exit 0.
- [ ] **Step 2:** Run every headset line from Tasks 2, 4, 5, 6, 7, 8 in one session; record results in `docs/VR-review-fixes-TEST-RESULTS-2026-10-XX.md`.
- [ ] **Step 3:** Use superpowers:finishing-a-development-branch to merge into `integration/v59x` and cut a pre-release with a rebuilt `DishonoredVRLauncher.exe`.

---

## Deliberately out of scope (follow-up plans)

- Dedicated OpenXR thread / event polling decoupled from Present; device-removed (TDR) handling; splitting `log_mutex` (review M1, M3, M4). Larger design change, needs its own plan.
- Fixed-size FSR contexts so resolution changes don't stall at all (root cause behind Task 7).
- Replacing `GetTickCount64` with a QPC millisecond clock project-wide (swing detection, freshness gates) — touches many tuned thresholds; needs a headset-tuning pass.
- Snap turn dropping held props, crouch baseline/lift, menu height follow, comfort vignette.
- Stale NPC pointers in `NativeWeaponHands::yank`/queued requests; `native_menu` 64-slot exhaustion; door-owner graph caching.
- Compiling diagnostics out of shipping builds, `vr-command.txt` channel, `.git` gate.
- Repo LICENSE, README rewrite, single test runner + CI, reproducible builds, Linux/Proton support.
