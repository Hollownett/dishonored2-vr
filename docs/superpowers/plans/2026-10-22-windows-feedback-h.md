# Windows Feedback (Plan H) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Act on the user's v603 Windows test.

**Spec:** user 2026-10-22 14:56 (verbatim): "add dlss support, image and fps is good on fsr3 native. Game menu still not opened on hold the menu button (click conflicts with steamVR dock button). make corpses 2 times easier to move, corpses grab work good, just need to make it easier to move. Stealth kills and chockes works good. Need to make teleport skill better calibrated, sometimes I teleporting instead of holding to select teleportation target. camera when climbing and killing sometimes can be off place need better calibration, Need more smooth on the aim now it takes all of the hand micromovements and it leats to bad aim. Player movent camera is good - no more motion sicness. oiltank test passed, works good."

## Global Constraints

- Plan D global constraints apply (`.superpowers/sdd/2026-10-20-comfort-performance-d/global-constraints.md`): worktree/branch `feat/bonelab-feel`, pinned exe, knob per behaviour plus a `tools/default_features.json` row, harness, sim rig rules, commit trailer.
- Don't regress what the user confirmed works:
  - movement camera (no motion sickness);
  - stealth kills and chokes;
  - corpse grab;
  - oil tank;
  - FSR 3 native image.
- The user tests on Windows. Every change must be correct for native Windows D3D11 as well as Proton.

## Tasks

### H1: Corpses 2x easier to move

Use the existing tuning, `GrabTuning` in `src/grab_force_cap.hpp` and `build/grab-tuning.cfg`. The corpse motor's force and velocity caps and the tow limits are tuned to the lab model. Make the effective corpse resistance half as much: either double the force and catch-up caps used for corpses, or halve the effective hanging mass.

Choose the lever that keeps runaway and explosion protection valid, and prove it in `tools/corpse_drag_lab.cpp`:
- each lab scenario reaches its target in about half the time, or with half the hand lead;
- no runaway or fling scenario regresses.

The new defaults go in `GrabTuning`, and the old values are restorable with a knob or cfg. Write a test that pins the 2x ratio.

### H2: Smoother aim

The aim ray drives the crosshair, Blink, the hand shots and the interaction ray. It currently follows every hand micro-movement.

Add a speed-adaptive low-pass filter (a One Euro filter), applied per hand to the aim ray's origin and direction, with these properties:
- strong smoothing when the hand is still;
- little lag on fast motion;
- defaults: `min_cutoff` 1.0 Hz, `beta` 0.02, `d_cutoff` 1.0 Hz;
- a knob `no-aim-smoothing` and a strength file;
- applied once per displayed frame (`per_frame`), with filter state reset on epoch, teleport or calibration.

The visible hand and the sword are NOT filtered; only the aim is.

Tests:
- `still_hand_jitter_reduced` (1 mm noise reduced at least 5x);
- `fast_sweep_lag_bounded` (lag at most 2 frames at 3 m/s);
- `reset_on_epoch`.

### H3: Blink: hold-to-select must not teleport immediately

Investigate the Blink input path:
- `src/native_blink_aim.hpp`;
- the hand-blink feature;
- the pad composer mapping for the power button;
- the game's own press vs hold semantics. In Dishonored 2, holding the power button shows the target marker and releasing it casts.

Find why a hold sometimes casts at once. Candidates:
- a tap emitted on press;
- a debounce or flicker on the trigger axis crossing the threshold;
- the gesture path (`vr_gestures.hpp`) firing a cast;
- a release seen during the hold.

Fix it so that only a release after a hold casts, and a quick tap behaves as the game's tap. Add tests for the input state machine with a noisy analog trigger.

### H4: Game pause menu on a hold that doesn't conflict with SteamVR

The menu button on the left controller opens the SteamVR dock, so the game never sees a usable hold. Plan C Task 4 made "hold Y" open the mod's VR settings panel, not the game's pause menu.

Ruling:
- hold Y (0.6 s) in gameplay opens the game's own pause menu, by sending the native pause/Start input through the pad composer;
- the VR settings panel opens from hold Y while the pause menu is open (or from the existing panel entry);
- a tap of Y keeps its game meaning (talk);
- the knob `legacy-hold-y-settings` restores the old mapping.

Find the native pause input the mod already knows (grep `menu_tap_`, `Start`, `pause`). Write tests for the routing state machine.

### H5: Camera off-place when climbing or killing

Authored scenes (takedowns, cutscenes) use `authored_head_view` and `AuthoredHeading`. Mantle and climb use the gameplay anchor with `AnchorSmoother` (bounded 0.20 m lag). Investigate both paths from code and the headset data, `~/d2vr-test-builds/headset*/` (`authored_*` counters, comfort, frame timing). Also use a sim trace if a scenario can mantle.

Look for:
- the heading snap rule at 30 degrees;
- the authored position offset (head offset vs native centre);
- the smoother's 0.20 m lag during a fast mantle;
- the transition back from authored to gameplay (recentre and epoch).

Fix what the evidence shows. Every change gets a knob and a test.

### H6: DLSS (research first, then its own plan)

The user wants DLSS. Their Windows machine has an NVIDIA RTX GPU; the Linux laptop has an RTX 3070.

Research:
- NGX/DLSS on D3D11, including the NVIDIA DLSS SDK license and redistribution of `nvngx_dlss.dll`;
- per-eye contexts, the same shape as `src/fsr3_eye_upscaler.hpp`;
- the inputs: motion vectors, depth, jitter and exposure, using what the FSR 3 path already extracts;
- whether DLSS works under Proton with dxvk-nvapi (`PROTON_ENABLE_NVAPI=1`);
- quality modes as AA modes in `vr_preferences.hpp`.

The output is a design doc plus a plan. Implementation follows only if the license allows shipping.
