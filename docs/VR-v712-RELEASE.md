# VR v712 (test candidate, 2026-10-30)

Headset v711 (user):
- The big flashing hints are gone, and the small hints are fine.
- The game crashed twice.
- The tutorial messages should sit lower than the view centre.
- Dr. Hypatia's message did not show.
- The Talk indicator is unstable.

## The two crashes (v711 regression)
- The dump showed an execute fault through a heap pointer inside `WorldUiCapture::redirect` (native_hud_art_capture).
- `NativeHudChildCapture` keeps three slots per bank in a fixed `std::array<WorldUiCapture,5>`. v711's new slot 15 indexed `banks_[5]`, past the end, and corrupted memory whenever a second message was drawn.
- Fix: `bank_count=(slot_count+2)/3` for `banks_` and `completion_`.
- The other two records in that context file were first-chance reads inside the SEH-guarded `read_self`, which are handled and not the crash.
- The missing Hypatia message most likely ran into the same corruption, since messages used slot 15. This is not verified.

## Messages lower
- Messages are head-locked with the stage centre 12 degrees below the gaze, keeping the game's own layout and scale.

## NPC Talk indicator
- The v711 blink journal (node field) showed the same Talk window node published by two bindings in turn: the retained event entry and the hand target.
  - Each change dropped a frame.
  - The two bindings placed the window at two points: the retained one at NPC origin +1.95; the hand one also lowered and pulled within 10 degrees of the gaze.
- Fix, placement: one placement (`npc_near_gaze`) for both bindings of a standing NPC's prompt.
- Fix, hold: a held frame accepts the same element still drawn under the other binding (`same_live`).

## Checks
- native_hud_child_draw_check and native_hud_child_capture_check pass.
- Sim v712 aim: no eye-art failures and no exception records.
- Not headset-tested. The sim save has no tutorial message and no talkable NPC.
