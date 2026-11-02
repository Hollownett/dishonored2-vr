# VR v719 (test candidate, 2026-10-30)

Headset v717 (user): walking up behind Dr. Hypatia showed no hint that she can be cured. The choke from behind did start the cure animation.

## What the v717 proof showed
- The game shows "Press [Grip] to inject counter-serum." only during the choke.
  - It took slot 15 at tick 37456281.
  - The camera became authored at 37456279 (the choke animation) and returned at 37458515 (`xr-frame-timing.csv` authored_camera).
- `object_art_slots`:
  - art slot 12 (the cure message) had 0 eye draws, 0 misses and 0 lock skips;
  - art slot 10 (the rune hint, shown outside the choke) drew 416 times.
- GameXr drew native object art only under `gameplay && !authored_camera && tracked_pair_()`. A message shown during a takedown animation was published every frame and never drawn.
- Art slot 12 itself works: a sim build that routed the message to slot 15 drew it 1068 times.
- The art pixels were correct in the headset (`native-message-art-*`): both texts.

## Fix (general)
- Head-locked art draws in any gameplay camera, authored ones included. It needs no world anchor: its point comes from the same tracked views as the eye layer.
- World-anchored art (prompts, markers) and the reticle still draw only in the player's own camera.
- Cutscenes are not gameplay and are unchanged.

## Diagnostics kept
- Per-slot eye-art counters in `xr-live-status.json`.
- `native-message-art-N` dumps.
- `message_refreshes`.
- The final eye image after a message shows: at most 2 per session (27 MB each).

## Checks
- native_hud_child_draw_check and native_hud_child_capture_check pass.
- Sim v719 on the user's save:
  - the rune hint drew 1064 times;
  - no eye-art failures;
  - no exception records;
  - only the usual sim failures (diagnostic-thread timeout, HUD edge coverage 0.031).
- The choke on Dr. Hypatia is not reproducible in the sim. Headset test needed.
