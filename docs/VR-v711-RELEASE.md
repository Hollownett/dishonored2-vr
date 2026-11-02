# VR v711 (test candidate, 2026-10-30)

Built on v697 (927fe75) plus the uncommitted v698/v699 HUD work. Numbers v700-v709 belong to the finger session; this session uses v710-v719.

Headset v699 (user): object prompts (doors, pickups) work well. Two problems remained:
- The tutorial messages were large white letters that went past the view and jumped between big and small. The bottle's throw hint, the counter-serum message and "Rune or Bonecharm nearby" each overlapped the others.
- An NPC's Talk indicator showed for a moment, then went.

## Tutorial and information messages
- **Flat copy.** `native_hud_keep_flat` let a tutorial message draw flat into the eye whenever its world art was not ready yet. At the eye target's full-screen scale, that was the huge white text.
  - Tutorial messages are now VR-only, like prompts and vitals.
- **Shared slot.** Two messages at once took slot 11 by turns, so the art was never ready and the flat copy kept flashing.
  - Each message node now holds its own slot while drawn: slot 11, and new slot 15 (art slot 12).
- **Size and layout.** Each message is head-locked 1.2 m ahead at its own stage position, with the stage centre straight ahead.
  - It uses the HUD screen's angular scale without the 1.4 prompt boost, and is capped at 55 degrees wide.
  - Two messages keep the game's own spacing.
  - The offset is resolved at draw time (`GameXr::native_art_lock`), so the art has no pose lag.
- **Tutorial movie cards.** They are no longer HUD-panel content and go through the same path.

## NPC Talk indicator
- The v699 blink journal showed slot 0 still alternating between Hypatia's retained Talk entry and an unretained binding.
- The takedown hint is now recognised before the general prompt identification, so `identify()` can no longer claim it for slot 0.
- A held prompt frame accepts the same drawn element (`native_hud_child_same_element`: same node, movie and owner) across a binding change.
- The blink journal now also logs the node and the crop width.

## Prompt size jumps
- While the same prompt keeps landing within 400 ms, its crop only grows (union). A fade-in no longer shrinks it to the visible part.

## Lab
- `build/final-eye-capture` writes `xr-final-N-left.rgba`: the eye image with every native widget composited, every 2 s.
- Sim eye captures do not show composited widgets.

## Checks
- native_hud_child_draw_check is updated and passes.
- Sim v711 aim: no eye-art failures. The final frames are clean.
- The sim save has no tutorial message, so the new message layout and the Talk fix are not verified in the sim.
- Not headset-tested.
