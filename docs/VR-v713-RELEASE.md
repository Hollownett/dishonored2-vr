# VR v713 (test candidate, 2026-10-30)

Headset v712 (user): the crash is gone and the messages are lower. "Rune or Bonecharm nearby" kept Dr. Hypatia's counter-serum message from showing.

## A message never takes another drawn message's slot
- v710 gave messages two capture slots (11, 15), keyed by node. A third message node drawn at the same time found both slots busy and took slot 11 on every draw.
  - The two nodes sharing slot 11 swapped in it several times per frame. Their captures never settled, so neither showed.
  - The node in slot 15 stayed stable. That fits the rune hint showing while the counter-serum message did not.
- Messages now have four slots: 11, 15, 16 and 17 (art slots 10, 12, 13, 14). Slots 16 and 17 share bank 5 with slot 15, so no new capture bank is allocated.
- `NativeMessageSlots` (native_hud_child_capture.hpp):
  - a node keeps its slot while it is drawn;
  - a new node takes the slot idle longest (more than 300 ms);
  - a fifth simultaneous message gets no slot, instead of taking one that is still drawn.
- `native-message-slots.jsonl` logs each new assignment:
  - the node;
  - whether it comes from ui/tutorials.iggy or hud.iggy;
  - its slot, and the node it replaced.
- General rule: no message-specific code.

## Checks
- native_hud_child_capture_check now includes three messages drawn every frame keeping three distinct slots, a fourth fitting, a fifth refused, and reuse after 300 ms idle. It passes, as does native_hud_child_draw_check.
- Sim v713 aim: no eye-art failures and no exception records.
- Not headset-tested. The sim save has no tutorial message.
