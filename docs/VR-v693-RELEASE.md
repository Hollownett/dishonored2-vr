# VR v693 (test candidate, 2026-10-30)

Headset v691 feedback: grenade, HUD stability, readable text and the Hypatia cure with its prompt all work. Three issues remained.

## Prompt text huge for 1-2 frames
- Cause: a prompt frame that cannot place itself (anchor lost for a frame, e.g. when "Pick up" becomes the held bottle's prompt) re-shows the last good art through `NativeHudPromptHold`. The hold kept the old *width in metres* while the compact texture had already been replaced by the new prompt's crop, so a smaller crop was stretched to the old width.
- Fix: the hold keeps the art's scale (metres per texel, `width_for`). The stage scale now comes from the capture surface's own fit (`min(w/1920, h/1080)`), not the last hud.iggy root drawn. The second hand's indicator uses the same angular scale (it was a fixed 0.24 m).
- Proof if it recurs: `native-prompt-jump-N.rgba/json` (any slot-0 crop change over 1.5x).

## A grip on an item put the sword in the hand
- Evidence (v691 session): 30 game draws of the sword, 0 shoulder draws, 13 drops and 7 pickups of the dropped sword.
- Cause: while the game had drawn the sword by itself (after a carried object, etc.), `SwordGrip` owned the right grip (no Use), and a grip edge adopted that sword. Releasing then dropped it, and the next grip within 45 cm of it picked the sword up instead of the item.
- Fix: a squeeze on something the game offers to use is that Use. It neither adopts a game-drawn sword nor picks up the lying copy, and `owns` yields (`SwordGrip::In::use_target`, tests/vr_controls_check.cpp).
- Proof if it recurs: `native-sword-draw-journal.jsonl`, the last 1.5 s of the pad state before any sword draw SwordGrip did not request.

## FSR 4 Quality looked like a plain lower resolution
- The FSR 4 dispatch runs and its output reaches the swapchain unscaled (static trace).
- Fix 1, jitter: FSR got the engine's eight 1/16 px jitter phases. FSR upscaling now owns a Halton(2,3) jitter like DLSS, with AMD's phase count `ceil(8*(display/render)^2)` (14 at 0.77). Status `dlss.jitter_phases`. Knob `build/fsr-engine-jitter` restores the old pattern.
- Fix 2, mip bias: the texture mip bias is `log2(render/display) - 1` (was -0.5), per the AMD and NVIDIA guidance. Knob `build/mip-bias`.
- Open suspects: the LDR post-tonemap input, and the motion-vector scale at fraction < 1 (`build/fsr-mv-full`).

## Checks
- vr_controls_check, native_hud_child_draw_check and dlss_mode_check pass.
- Sim aim (v692) and sim FSR4 Quality (v693): 14 phases, jitter owner on, 3684 FSR4 dispatches, 0 fallbacks, 0 eye-art failures.
- Not headset-tested.
