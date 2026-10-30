# v689 test release: Hypatia cure, game crosshair, HUD scale and rate, grenade release

From the user's v686 headset session (2026-10-29 22:01-22:07) and the follow-up report. (v688 was an internal sim build.)

## Hypatia (Crown Killer) cure

- **No custom hint.** The GDI "Hold grip to inject counter-serum" card is removed from the code (v687 only made it opt-in).
  The only hint is the game's own tutorial message, captured live: "Press [Grip] to inject counter-serum."
- **Grip = the game's RB only while the game offers the cure** (its own message on screen, or its prompt on her).
  v686/v687 also turned every grip into RB whenever she was within 4.5 m (state 4). With the sword held in the right grip
  that was a permanent block through the whole fight.
- **She stays found.** `nearby_hypatia` reset its cache every 500 ms and its 4096-slot window found her only once per full
  16384-slot sweep, so the cure state blinked off three calls in four. It now keeps her while she stays within 4.5 m.
- **An unrelated prompt no longer hides her.** The offer owner used to be the first prompt owner found (any prop); only a
  Hypatia owner counts now.
- While the cure is offered and a grip is held, the grip's Use (X) is not sent, so it cannot compete with RB.
- Unchanged from v687: on the message appearing (or a fresh grip) RB is released for 90 ms and then held, so the game sees a
  real press even with the sword already gripped.

## Crosshair

The white dot is replaced by the game's own crosshair element, captured live every frame (dot, red dot, grenade, mine,
power and crossbow states). It is drawn exactly where the old reticle was (the hand's aim point, per eye) at the game's
own pixel size. The static dot remains only as a fallback when no capture is available.
Status: `crosshair.game_art_captures`, `crosshair.game_art_draws`.

## HUD scale

Prompts, the tutorial message and objective markers are sized by their own captured width as a share of the game's 1920 px
stage, at the HUD screen's angular scale. Before: fixed metres whatever the image (the cure message was 41 deg wide;
a marker icon and a marker with distance text got the same 1.7 deg; markers and NPC prompts nearer than 4 m grew).

## HUD update rate

Captured prompts, markers, tutorial and wrist art changed only when the asynchronous bounds pass returned (about every
55 ms per element, 15-20 Hz). Every newer capture of the same element is now copied through the last measured crop each
frame (wrist art capped at 30 Hz). Status: `native_hud_children.frame_refreshes`.

## Grenade release

- A fist (grip + trigger) now ends when either finger opens; before, an opening grip handed the squeeze to the index
  finger alone, so a lagging index held the throw. After an opened fist the gadget stays released until the index lets
  go too (no accidental re-cook).
- The gadget's left trigger skips the power-trigger release debounce (30 ms + 2 frames); it is already a clean 0/1.
- `grenade_throw.last_delay_ms` reports release -> the game's launch. The game itself launches after its throw animation
  (about 0.3 s, not measured on this build); this release does not change the animation.

## Verification (sim, 2026-10-30)

- `aim` and `pistol` sim runs: the game crosshair art drew in 4386 of 4672 reticle draws (the rest fell back to the dot
  while a capture was not yet fresh), no eye-art failures, 7959 per-frame HUD refreshes. The captured art
  (`artifacts/native-crosshair-art-*.rgba`, written three times per session) is the pistol's own dispersion crosshair,
  centred on its anchor.
- `tests/throw_gesture_check.cpp` covers the fist release and the no-re-cook rule.
- Eye captures are taken before overlays, so the composited crosshair in the eye is not in any capture.

## Limits

- Not headset-tested. The cure needs a save before the cure with the filled serum.
- The crosshair capture box is 384 px around the element's anchor; a larger crosshair state would be clipped.
