# VR v1129 (release, 2026-11-10)

The weapon wheel selects again with dual wield. Headset-tested by the user on v1129 (four wheel choices across both hands, each
equipped a new item).

## Weapon wheel

| change | what you see |
|---|---|
| With dual wield the wheel selects only through the game's menu pointer. The mod's pointer adapter turned itself off for the rest of the session on a single refused cursor move or viewport-margin read, or once its 64 menu input slots filled over a long session. A refused frame is now skipped (clicks still go, so press and release stay paired), and a full slot table reuses a slot with no click held | pointing at an item moves the wheel highlight and the trigger equips it, also late in a long session; before, the highlight could stay frozen on one item |
| With "Save diagnostic logs" on, `dual-wield-events.jsonl` gets one `wheel_pointer` line per wheel opening: which pointer check failed while the hand pointed, and whether the game's menu took, rejected or dropped the pointer | nothing in game; one log now shows where a frozen wheel loses the selection |

Includes v1127 (parry by a quick push of the blocking sword) and v1126 (tutorial fixes).

## Known issues

- **Crossbow aim in the tutorial's firing range looks wrong:** the tutorial puts an invisible wall in front of the range. Outside the
  tutorial the crossbow aims normally.
- As v1123: exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).
