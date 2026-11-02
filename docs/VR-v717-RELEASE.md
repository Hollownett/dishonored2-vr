# VR v717 (diagnostic test candidate, 2026-10-30)

Headset v714 (user): neither the counter-serum message nor the rune hint was seen. Only the takedown indicators showed.

## What the v714 headset journal showed
- `native-message-shows.jsonl` covers both messages:
  - the counter-serum message (slot 11) was published to the eye for about 10 s, six times;
  - the rune hint (slot 15) was published in many shows, one of them 10 s long.
- `native-hud-child-11/15.rgba` hold clean captures of both texts.
- So capture and publication work. The loss is at or after the eye draw.

## Sim on the user's own save (Dr. Hypatia's office, rune hint at load)
- `vr_selftest --launch --inject-at-title` loads the user's newest save, with saves backed up and restored.
- v715 added per-slot eye-art counters. Art slot 10 (the message) drew 1124 times. Its art 600 ms after the show held the text (6380 opaque pixels).
- The final eye frame shows the text, small, below the centre.
- Message refreshes all came from the measured matrix (`message_refreshes` [556,0]). So the v714 per-frame refresh copied no moved pass in the sim.
- Not explained: in the sim the message drew at about 0.62 of its computed width.
- The headset-only cause is not found.
- Note: the sim `xr-t*` images come from the transfer observer, before the HUD overlays. They never show eye art. Only `xr-final-*` does.

## Diagnostics in this build
- `object_art_slots` in `xr-live-status.json`, per art slot:
  - eye draws;
  - fresh art with no rect (off view);
  - fresh art skipped because its head lock was unresolved;
  - the last NDC rect.
- `message_refreshes` [same matrix, moved], and `native-message-moved.jsonl` with the moved matrices.
- `native-message-art-N.rgba/.json`: the message art 600 ms after each show (at most 12), with an opaque pixel count.
- `xr-final-N-left.rgba/.json`: the final projection eye image, taken right after that dump (at most 6 per session).

## Checks
- Sim v717 on the user's save: one message show, one art dump and one final eye image written. No exception records.
- No behaviour change from v714.
