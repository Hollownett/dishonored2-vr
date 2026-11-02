# VR v714 (test candidate, 2026-10-30)

Headset v713 (user): the counter-serum message still did not show. Only "Rune or Bonecharm nearby" was visible.

## What the v713 journal showed
- `native-message-slots.jsonl`: near Dr. Hypatia, two hud.iggy message nodes held slots 15 and 16. No slot was taken from another.
- `native-hud-child-15/16.rgba`: slot 15 captured "Rune or Bonecharm nearby. Equip the Heart." and slot 16 captured "Press [Grip] to inject counter-serum." just below it.
- So the message was captured. It was lost between the capture and the eye.

## Two general causes on that path (all hud.iggy script messages)
- **The binding expired between publishes.** `publish_native_hud_messages` runs at most every 250 ms, on the next UI call, and `identify()` dropped a message binding older than 250 ms.
  - At each edge the node was not recognised and the capture's scope aged out.
  - Now a message binding is renewed by its own Draw, so it lives exactly as long as the game draws it, as retained prompts do.
- **An animated message only updated on its bounds pass.** The per-frame pixel refresh required an unchanged node matrix.
  - A message whose node animates (a pulsing hint) waited for its round-robin bounds pass. With about 13 active slots that pass can land later than 250 ms, and the art was then cleared.
  - A static message, like the rune hint, kept refreshing.
  - Message slots now refresh every frame through their crop. That crop is the union of the measured sizes while the same message keeps landing, as prompts already do, so a pulse is not cut.
- Which of the two hid the counter-serum message is not proven. Both are fixed.

## Diagnostics
- `native-message-shows.jsonl` logs every show and hide of message slots 11, 15, 16 and 17 with:
  - the node and its art age;
  - the crop;
  - whether a live capture exists and is the same element, and its scope age.

## Checks
- native_hud_child_draw_check and native_hud_child_capture_check pass.
- Sim v714 aim: no eye-art failures and no exception records.
- Not headset-tested. The sim save has no script message.
