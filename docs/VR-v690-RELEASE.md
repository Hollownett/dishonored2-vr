# v690 test release: cure with either hand, stable HUD art, grenade on the throwing motion

From the v689 headset session (2026-10-30 10:00-10:08).

## What the session showed

- The cure worked with the left grip while the sword was in the right hand, but with empty hands a grip on her did nothing.
- The game's own cure message was reported on screen for 3134 frames (`cure_native_message_frames`), yet the user saw it
  only once the cure animation had started: a display problem, not a missing message.
- HUD art blinked, jumped in size and read as low resolution.
- Grenade: `grenade_throw.last_delay_ms` 281 - the game's own throw animation between its trigger release and the launch.
- After the cure, the cured Hypatia had no prompt although the grip started the scene.

## Changes

- **Cure with either hand.** The grip is the game's RB also while the game's own choke sensor offers Hypatia (behind her,
  unaware), whatever the hands hold. v689 relied on the message/prompt states only.
- **HUD size no longer jumps.** Art size used `root_scale`, which the HUD, tutorial-card and panel movies all overwrite with
  their own roots; it now uses hud.iggy's root only. Art is 1.4x the HUD screen's angular scale (text was ~6 px per letter).
- **No more blinking markers.** The per-frame refresh copies only elements drawn at the same place as their last measured
  crop; moving ones (objective markers) wait for their bounds pass, as before v689.
- **Sharper eye art.** Eye-drawn art (prompts, messages, markers, crosshair) averages 4x4 taps over each pixel's footprint
  instead of one bilinear tap of a texture about twice the drawn size (no mips): text no longer shimmers or loses strokes.
- **Cured Hypatia's prompt** is no longer hidden by the downed-body carry guard (v637/v677); her Use is not blocked either.
- **Grenade.** A held grenade (not mines or spring razors) is let go to the game on the throwing motion itself (held 250 ms,
  hand faster than 1.5 m/s), so the game's ~0.28 s windup runs with the arm. The flight uses the peak hand velocity of the
  whole throw and the hand's position at the launch. Knob `build/grenade-early-mps` (metres per second, 0 = only at the
  release). Status `grenade_throw.early_releases`.

## Verification

- Sim `aim`: game crosshair art drew in 3590 of 3826 reticle draws, no eye-art failures, 1392 per-frame refreshes (7959 in
  v689 with the moving markers), HUD screen and captures pass.
- `tests/throw_gesture_check.cpp`: early release, follow-through peak, launch position, no new hold until the fist opens.
- Not headset-tested: the cure paths, the prompt after the cure, HUD readability and the grenade timing.
