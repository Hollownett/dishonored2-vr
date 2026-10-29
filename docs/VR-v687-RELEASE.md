# v687 test release: the native cure hint, and a real grip press

The 22:07 headset session answered both v686 failures.

## What the session showed

- The game's own message **was captured correctly** this time:
  `native-hud-child-11.rgba` is 503x38, alpha 0..255, and reads
  **“Press [Grip] to inject counter-serum.”** It is the flat player's element.
- The custom card was still what the headset showed. `publish_cure_hint` wrote the GDI card into
  the **same tutorial art slot** whenever the `_tutorialMessage` alpha probe was not fresh, so it
  overwrote the native art every frame. `cure_prompt_frames` was 496; the native art never survived.
- Grip did not start the cure. The 22:07 controller trace has `right_grip` 1 for long stretches,
  while `cure_r1_frames` rose to 196 and `offered_choke_frames` stayed 0. The grip was held through
  focus/menu/modifier transitions; the input composer's blocked set suppresses RB until the analog
  value is released. The normal choke path already creates this release edge
  (`AttackPress::lead_ms`); the v686 cure path did not.

## What changed

- **The native hint is the only cure hint.** The GDI fallback is disabled by default; it is opt-in
  only with `%LOCALAPPDATA%\DishonoredVR\build\cure-fallback-card`. The native
  `Press [Grip] to inject counter-serum.` art now stays in the tutorial slot instead of being
  overwritten.
- **The native art itself selects state 5.** `cure_prompt_state 5` now means “the game's own cure
  message is on screen” based on the captured native art, not only the alpha probe. State 5 pulses
  the cure input.
- **A real RB press.** On entering the cure state, the native message appearing, or a fresh grip,
  the cure path releases RB for 90 ms and then holds it. This clears the composer's blocked set and
  gives the game a press it can see, exactly like the normal choke path. A re-press is limited to
  one per 400 ms so a hold still holds.
- The static `grimalex` NPC match, native alpha bounds, serum scan and native choke reach from
  v686 are unchanged.

## Status

- `cure_prompt_frames` should now be **0** (fallback off).
- `cure_native_message_frames` should rise while the game's message is on screen.
- `cure_r1_frames` should rise after the 90 ms release; `offered_choke_frames` may still be 0
  because the cure is an NPC interaction, not the HUD choke icon.

## Test (headset)

1. Load a save **before** Hypatia dies with the filled counter-serum.
2. Sneak **behind** her, close enough for the game’s own message. Confirm the native
   “Press [Grip] to inject counter-serum.” appears — no English card.
3. Hold the grip. The 90 ms release is part of the press; keep holding until the injection plays.
4. Report `cure_prompt_state`, `cure_native_message_frames`, `cure_r1_frames` and whether the
   scripted moment started.

## Limits

- Not headset-tested. The native capture, the overwrite and the held-grip block are proven by the
  22:01/22:07 artifacts; the scripted start itself still needs the headset.
- The game still requires its own cure eligibility (behind, unaware, correct quest state). This
  build cannot start the scene from the front or after Hypatia is dead.
