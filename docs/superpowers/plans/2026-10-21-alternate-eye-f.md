# Alternate-Eye Rendering (Plan F) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An opt-in "performance mode": the engine renders one eye per displayed frame, alternating left and right, and each eye is submitted with the pose it was rendered from so that the runtime reprojects it. This removes one engine view per frame.

**Architecture:** The work builds on the plan E Task 5a mono-view diagnostic: one engine view per frame through the single-eye descriptor, with eye identification by view address and both swapchains fed. Plan F swaps the centre pose for the current eye's pose and alternates the eye by frame parity. It keeps the other eye's last image and pose. It also makes per-frame mod work run once per displayed frame instead of once per eye-0 render.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook) and the Linux sim rig.

**Spec:**
- The user, 2026-10-21 10:39: "I need atleast 72 stable fps, 90 fps should be a base line for a gaming pc."
- Plan E gate data:
  - The mono view saves 26% of the frame on the sim rig (14.43 to 10.70 ms).
  - One view costs about 3.9 ms of GPU on the sim and more at the headset's 2020x2160.
  - The headset runs at 22 fps (45 ms), and SteamVR throttles it to 1/3 or 1/4 of the refresh rate.
- Design notes: `.superpowers/sdd/plan-d-intake/framerate-findings.md` §5.

## Global Constraints

- Same as plan D's global constraints (`.superpowers/sdd/2026-10-20-comfort-performance-d/global-constraints.md`).
- Knob `alternate-eye` is opt-in. Without it, behaviour is byte-identical to today, and the review proves this (as in plan E Task 5a).
- Any validation failure falls back to normal paired rendering for the session and is logged once. The mod never presents a mismatched eye.
- Each eye's projection view carries the exact pose and FOV it was rendered with. An eye image older than 2 displayed frames is never submitted. In that case the frame falls back to paired rendering.
- Comfort risk (one-frame interocular delay on moving objects, Pulfrich-style shimmer) is real. Only the headset decides whether the mode is usable, and the launcher row says "performance mode (may shimmer on fast motion)".

## Review Focus

1. **The stale eye's pose must match its image.** Expected: the layer for eye k uses `rendered_views_[k]` from the frame that eye was rendered in (Task 1 test `stale_eye_keeps_its_render_pose`).
2. **Per-frame mod work runs on every displayed frame.** This covers comfort, the listener, hands, input and turning. Expected: it runs once per Present, not once per left-eye render (Task 2 routing check).
3. **The temporal AA history does not mix eyes.** Expected: each eye has its own history, jitter sequence and dt, or temporal AA is forced off in this mode (Task 3).
4. **Fallback after a hitch or level load.** Expected: after an epoch change or a frame with no view, the next frame renders paired once, then resumes alternating (Task 1 test `epoch_change_renders_paired_once`).
5. **Menus and authored scenes.** Expected: alternation applies only in free gameplay. Menus, the settings panel, loading and takedowns render paired (Task 1 gating test).

---

## Task 1: Eye alternation and per-eye persistence

**Files:**
- Create `src/alternate_eye.hpp` and `tests/alternate_eye_check.cpp`.
- Modify `src/game_xr.hpp` (`rendered_mask_`, `rendered_views_`, the per-eye copy and layer fill).
- Modify `src/dxgi_probe.cpp`: the mono-view path from plan E Task 5a (single-eye descriptor, view matching, `perf-mono-view`), plus `prepare_head_view` for the per-eye pose.

**Interfaces:**
- `class AlternateEye { int eye_for_frame(std::uint64_t frame, bool gameplay, bool authored, bool epoch_changed) noexcept; bool can_submit(int eye, std::uint64_t now_frame) const noexcept; void rendered(int eye, std::uint64_t frame) noexcept; void reset() noexcept; };`
  - `eye_for_frame` returns 0 or 1 by parity while gameplay is on and authored is off. Otherwise it returns -1, meaning paired.
  - After an epoch change or `reset()`, the first frame returns -1 (paired, both eyes fresh).
  - `can_submit` is false when that eye's image is more than 2 frames old.
- In alternate mode, the engine renders the single-eye descriptor with that eye's pose and FOV (the per-eye path that `prepare_head_view` already computes for paired eyes).
- The image is copied into that eye's swapchain. The other eye's swapchain image and `rendered_views_[other]` stay as they are.
- `GameXr::present` submits both views: the fresh eye with its new pose, and the stale eye with the pose recorded when it was rendered. `rendered_mask_` becomes per-eye freshness instead of a must-equal-3 rule.

- [ ] **Step 1: Failing tests.**
  - `parity_alternates_in_gameplay`
  - `menus_and_authored_render_paired`
  - `epoch_change_renders_paired_once`
  - `stale_eye_keeps_its_render_pose`, using a pure helper that builds the two layer views from per-eye records
  - `eye_older_than_two_frames_not_submitted`
- [ ] **Step 2: Implement and wire behind the knob.** Run the full harness.
- [ ] **Step 3: Sim proof.** Run M0 (paired) and M2 (`--knob alternate-eye`) on oil_tank_drop with `native-gpu-timing`. Record:
  - the interval p50/p95 for both;
  - the share of frames that alternated (status `"alternate_eye":{"frames","fallbacks"}`);
  - two consecutive `dxgi-stereo.rgba` frames converted to PNG, showing one fresh eye per frame.
- [ ] **Step 4: Commit** `feat(perf): alternate-eye rendering behind a knob`.

## Task 2: Per-frame mod work keyed to the displayed frame

**Files:** `src/dxgi_probe.cpp`: every `eye==0`-guarded per-frame site listed in findings §1, which covers the comfort meter, authored heading, anchor smoother, audio listener, crosshair, hands and body writes, turning, and the frame-split stamps.

- [ ] **Step 1: Routing check** `tests/alternate_eye_routing_check.py`. Each listed site is guarded by "first view of this displayed frame", meaning a frame counter compare, and not by `eye==0`.
- [ ] **Step 2: Implement.** In paired mode the behaviour is unchanged, because the first view is eye 0. Then run the full harness.
- [ ] **Step 3: Sim check.** With `alternate-eye`, `comfort.samples` is about equal to the number of Presents, and the hands and body update every frame (see `arm-upload` counts).
- [ ] **Step 4: Commit** `fix(perf): per-frame work runs every displayed frame in alternate-eye mode`.

## Task 3: Temporal AA in alternate-eye mode

**Files:** `src/fsr_taa.hpp`, `src/game_stereo.hpp` (jitter index), `src/dxgi_probe.cpp` (`view_setup_hook`, previous matrices), and `src/native_taa_history.hpp`.

- [ ] **Step 1: Decide by evidence.** Log the view object's address and its previous-matrix and jitter fields (`+0x7850`, per findings §5) over 100 frames in alternate mode, on the sim. Write down whether the engine keeps one view object, and what its previous matrices hold.
- [ ] **Step 2: Implement one of the two paths.**
  - **Path A:** per-eye FSR history with a per-eye jitter counter and a 2-frame dt, plus correct per-eye previous matrices written in `view_setup_hook`.
  - **Path B (fallback):** if the previous matrices cannot be made per-eye safely, alternate-eye mode forces temporal AA off (SMAA) and says so in the launcher row.
  Either way, add tests for the jitter counter and history selection.
- [ ] **Step 3: Sim images.** Convert consecutive frames to PNG. Expect no ghost of the other eye's viewpoint on moving edges.
- [ ] **Step 4: Commit** `feat(perf): temporal AA keeps per-eye history in alternate-eye mode` or `… forces SMAA in alternate-eye mode`.

## Task 4: Launcher row, docs, headset checklist

**Files:** `tools/default_features.json` (row `alternate-eye`, kind enable, default off, "performance mode (may shimmer on fast motion)"), `docs/PLAYING.md`, and the `docs/VR-PERF-LAPTOP.md` checklist.

- [ ] **Step 1: Write the rows, docs and launcher test.**
- [ ] **Step 2: Add headset checklist rows.**
  - "performance mode on: fps at 72 Hz vs off (same area, 90 s)"
  - "turn head and strafe past a guard: shimmer acceptable?"
  - "fast hand swings: hands look solid?"
- [ ] **Step 3: Commit** `docs: performance mode`.
