# Headset Feedback (Plan G) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix what the user's 2026-10-21 headset session showed: the crash from the vignette layer, low fps and an unstable image, stealth kills that don't read intent, missing takedown prompts, and a thrown oil tank that falls through the floor.

**Architecture:** Each task is independent. Several of them replace the mod's own judgement with the engine's, as the reference Dishonored 1 VR mod does: the mod forwards player intent, and the engine decides the outcome.

**Tech Stack:** C++17 DLL (D3D11, OpenXR, MinHook), and the Python sim rig.

**Spec:**
- The user, 2026-10-21 17:18: "head turn still provides unstable image, fps still low … Stealth kils indicators dont appear for me, no difference. Stealth kill still don't read palyer intents correctly (any atack it can be a stab or swing from behind is a stealth kill look for VR mod reference for the first part most problems are solved in this mode). when turinig head camera sill renders unstable image, same for wolking, oil tank still goes thougth floor when I thorw it."
- Session data: `.superpowers/sdd/2026-10-21-vr-framerate-e/progress.md`, section "HEADSET SESSION RESULT".
- Reference technique notes, written in our own words: `.superpowers/sdd/plan-d-intake/vr-mod-reference.md` and `vr-mod-reference-techniques.md`. The reference is zlib-licensed, but we still write our own code and never paste theirs.

## Global Constraints

- Same as plan D's global constraints (`.superpowers/sdd/2026-10-20-comfort-performance-d/global-constraints.md`): worktree, branch, pinned exe, knobs, harness, sim rig rules, commit trailer.
- Every behaviour change has a knob that restores the old behaviour.
- Sim runs only happen when the PAUSE-SIM file (session scratchpad) is absent, the game and SteamVR are closed, and the install hash is verified after the run.
- Only the headset can confirm these fixes. Each task adds its own row to the "Headset checklist (Task 11)" in `docs/VR-PERF-LAPTOP.md`.

## Review Focus

1. **Every layer we submit must pass SteamVR's validation.** The simulated backend accepted the layer that SteamVR rejected. Expected: `simulated_xr` validates quad layers the way the spec requires (Task 1 test `simulated_runtime_rejects_unnormalized_or_spaceless_quad`).
2. **An awake guard must get a normal attack, never a forced kill.** Expected: the mod only presses attack; the engine decides (Task 3 test `attack_intent_never_forces_kill`).
3. **A slow or accidental hand movement must not attack.** Expected: below 3.0 m/s with head motion subtracted, there is no press (Task 3 `slow_hand_no_press`).
4. **The prompt panel must not cover the view during fights.** Expected: the takedown or interaction prompt appears only while the engine shows one (Task 4).
5. **A thrown object must keep its momentum and collide.** Expected: release hands the object back to the engine's own throw (Task 5).

---

## Task 1: The vignette layer is valid on SteamVR

The session's first launch crashed. Every rejected `xrEndFrame` returned -39 (`XR_ERROR_POSE_INVALID`) and carried the vignette quad: 96x96, view space, pose `(0,0,0,1) / (0,0,-1)`, size 3x3.

**Files:** `src/game_xr.hpp` (vignette chain and layer), `src/simulated_xr.hpp` (validation), `tests/game_xr_simulation_check.cpp` or a new pure check.

- [ ] **Step 1: Find the cause** by reading the code and the OpenXR spec. Check:
  - the quad's `space`: is a VIEW reference space created, and is it valid at submit time?
  - the pose and the quad size;
  - `eyeVisibility`;
  - whether the `color_scale_bias` next-chain is attached when it was never enabled;
  - layer flags.
  Write the cause in the report.
- [ ] **Step 2: Fix.** Prefer submitting the quad in the same space as the other quads, with a pose built from the frame's located head pose. The brief's own fallback allows this. Keep the knob.
- [ ] **Step 3: Pin it.** `simulated_xr` validates every quad layer:
  - the space handle is valid;
  - the orientation is normalized within 1e-3;
  - the size is finite and greater than 0;
  - `subImage` lies inside the swapchain.
  It returns the matching XR error. Add the test `simulated_runtime_rejects_unnormalized_or_spaceless_quad`. Check whether the old vignette code would now fail the simulated check; if the cause turns out not to be modelable, say so.
- [ ] **Step 4:** Full harness, then commit `fix: comfort vignette layer is valid on SteamVR`.

## Task 2: A cheaper anti-aliasing default for VR

The headset spent 2.84 ms of GPU per eye on FSR 3 "Native" (FSR used as AA at full resolution).

**Files:** `src/vr_preferences.hpp` (default `temporal_aa`), `launcher/Core.cs` (default), `docs/VR-PERF-LAPTOP.md`, `docs/PLAYING.md`.

- [ ] **Step 1: Measure** on the sim rig: `--vr-setting temporal_aa=N` for every mode the probe supports (see `vr_preferences.hpp` and `aa_mode_fraction`), with the same scenario and `--knob native-gpu-timing`. Record for each mode:
  - interval p50/p95;
  - `fsr_gpu_left_ns` and `fsr_gpu_right_ns`;
  - list sum.
  Also convert one `dxgi-stereo.rgba` per mode to PNG and look at it: is there shimmer, are edges acceptable?
- [ ] **Step 2: Rule.** The default becomes the cheapest mode whose image is not visibly broken. Write the table and the choice in the doc. An existing `vr-settings.cfg` keeps the user's choice. Tell the user in the report how to switch modes in the VR menu.
- [ ] **Step 3:** Commit `perf: cheaper anti-aliasing default for VR`.

## Task 3: Stealth kills read intent: the physical attack presses the game's attack

**Files:** Create `src/attack_intent.hpp` and `tests/attack_intent_check.cpp`. Modify `src/dxgi_probe.cpp`: the sword and hand input path, the native Slash press, and the stealth-hold and contact-routing call sites (`NativeInteractionAim`, `sword_contact_policy`). Also modify `docs/PLAYING.md`.

**Interfaces:**
- `struct AttackSample {double t_s; Vec3 hand_pos; Vec3 head_pos; bool tracked;};`
- `class AttackIntent { bool update(const AttackSample&, bool crouched, bool weapon_drawn) noexcept; std::uint64_t presses() const; };` returns true on the frame a press should start. The values come from the technique study, implemented our own way:
  - **Speed:** hand speed relative to the head, from successive samples, filtered with a median of 3. A jump over 20 m/s is a tracking glitch and is ignored.
  - **Slash:** speed ≥ 3.0 m/s with the weapon drawn.
  - **Re-arm:** the speed must fall below 1.0 m/s for 2 consecutive samples, and there is a 300 ms cooldown.
  - **Stab:** only while crouched. It starts at 1.5 m/s and needs at least 0.20 m of travel within 400 ms, a straightness ratio of at least 0.75 (net distance / path length) and an alignment of at least 0.5 with the hand's forward axis.
- `struct AttackPress` holds the native attack button for 120 ms, and keeps holding until the game has polled the pad at least twice, with a 500 ms cap. Reuse the existing pad composer and native-press path.
- The engine decides the outcome. On an unaware guard seen from behind, the native attack is an assassination. On an aware guard, it is a normal attack.
- When the knob `legacy-sword-contact` is absent, the old contact-routing gating (calm-contact drops, stealth hold, `arms_native_attack` gating) is bypassed for drawn-sword attacks. The old path stays behind that knob.
- A honour check logs whether a kill happened within 600 ms of the press, using the existing takedown or fatality state reads if the mod has them (grep `Assassinate|Fatality|takedown`). Otherwise it logs press counts only. Status: `"attack_intent":{"presses","slash","stab","honoured","not_honoured"}`.

- [ ] **Step 1: Failing tests:**
  - `slow_hand_no_press`
  - `slash_at_3mps_presses_once`
  - `rearm_needs_two_slow_samples_and_300ms`
  - `head_motion_subtracted` (walking forward fast with a still hand gives no press)
  - `tracking_jump_ignored`
  - `stab_only_when_crouched`
  - `stab_needs_travel_and_straightness`
  - `attack_intent_never_forces_kill` (a routing check: the new path calls only the native attack press, never a direct takedown or kill function)
- [ ] **Step 2: Implement and wire.** Make the tests green. Run the full harness.
- [ ] **Step 3: Headset checklist rows:**
  - "from behind on an unaware guard: a fast swing kills; a crouched stab kills";
  - "an aware guard: a swing is a normal attack";
  - "no accidental attacks while walking".
- [ ] **Step 4:** Commit `feat: physical attacks press the game's attack; the engine decides stealth kills`.

## Task 4: Takedown and interaction prompts are visible in gameplay

The user sees no takedown indicators. In the reference, these prompts are simply the game's HUD interaction prompt, drawn on a panel quad.

**Files:** `src/game_xr.hpp` and `src/dxgi_probe.cpp` (HUD and world-UI routing, `native_object_art_`, the hint layer). Find which HUD element carries the interaction and takedown prompt and why it doesn't reach a submitted layer in gameplay. Grep `prompt|interact|hud` and read `docs/` on the HUD.

- [ ] **Step 1: Find the cause.** Use the code and the sim rig: `artifacts/*hud*` and `dxgi-stereo.rgba` near an interactable, with the oil_tank_drop scenario. Write it down in the report.
- [ ] **Step 2: Fix.** The engine's prompt appears in gameplay, either on the existing HUD quad or on a small panel near the bottom of the view. Add a knob to hide it.
- [ ] **Step 3: Test and verify.** A routing or pure test pins the fix. A sim screenshot shows the prompt next to the oil tank. Add the headset checklist row "the takedown prompt appears behind an unaware guard".
- [ ] **Step 4:** Commit `fix: interaction and takedown prompts show in VR gameplay`.

## Task 5: A thrown object leaves with the engine's throw (oil tank through the floor)

**Files:** `src/physical_grab.hpp`, `src/native_grab.hpp`, and the release path in `src/dxgi_probe.cpp`. Find where a held prop is released when the hand opens or throws. Also read the plan A ledger notes on tow and release.

- [ ] **Step 1: Find the cause** of the fall-through. Candidates:
  - a release velocity applied while the body is still kinematic or keyframed;
  - the object teleported inside the floor on its release frame;
  - collision disabled during the carry and not restored;
  - a release velocity so large that the object tunnels through.
  Use the code. If one sim run with the oil_tank_drop scenario helps, use it. Write the cause in the report.
- [ ] **Step 2: Fix.** Release must restore the engine's own collision state. The throw uses the engine's throw, with only the aim direction taken from the hand, as the reference does. Alternatively, cap the velocity and use the engine's continuous collision, if the code shows that path is safer. The old behaviour stays behind the knob `legacy-throw`.
- [ ] **Step 3: Test and verify.** A pure test of the release-state helper. The sim scenario drops and throws the tank, and the tank's final height is not below the floor. Add the headset checklist row "throw the oil tank at the floor and at a wall: it bounces and stays in the level".
- [ ] **Step 4:** Commit `fix: thrown objects use the engine's throw and keep collision`.
