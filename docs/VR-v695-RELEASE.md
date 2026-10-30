# VR v695 (test candidate, 2026-10-30)

Headset v693: prompts fine. The sword still came into the hand on a grip at Dr. Hypatia and at a newspaper. Rule (user): the sword is in the hand only when taken from the back or picked up from the floor.

## The game never draws the sword by itself
- Every right-hand equip goes through EquipChange state 53, the same class and processor `8363f0` as the left's 64 (docs/native-weapon-equip-research.md 2b/2c).
- The game asks state 53 to "draw the remembered item" (code 4/8) in several places, and with an empty hand that falls back to the default sword:
  - the restore after Choke and the cure (6 / 0x46);
  - HoldObject exit (0x16);
  - the Undraw toggle (long Interact);
  - Draw 54, attack with an empty hand (8);
  - a left Draw's other-hand request;
  - the wheel's explicit item.
- `NativeManualEquipRuntime::veto_right_draw_locked` strips those bits (and the explicit item) in the existing processor hook. Lock and unlock bits are kept.
- Only the mod's own SwordGrip draw (0x04) and pickup (0x44) pass. `NativeWeaponHands::equip` authorizes their request ids.
- Draw 54 waits for the hand to fill unless its follow-on (+0x30) is 0x5b (update `8319e0`, disassembled). A vetoed code-8 request sets 0x5b, so Draw stops itself instead of waiting.
- SwordGrip no longer adopts a drawn sword on a squeeze. One that slips through is put away after 150 ms.
- Status: `sword_grip.game_sword_draws {vetoed, allowed, draw_ends, veto}`. Knob: `build/game-sword-draw` restores the game's draws.
- `native-sword-draw-journal.jsonl` now records draws in any logic (v693's needed gameplay logic, so the draws after the cure and note screens were never logged).

## Hypatia body prompt blinking (diagnostics)
- `native-prompt-blinks.jsonl` records every world-prompt show/hide with its owner, body flags, carry guard, hand target, takedown and art age.

## Checks
- vr_controls_check passes.
- Sim swordgrip (v694): 3 own draws/pickups allowed, 0 vetoed.
- Sim script with B holster taps (v694): 1 game draw vetoed, the sword never appeared, nothing hung.
- Sim aim (v695) is clean.
- Not headset-tested.
