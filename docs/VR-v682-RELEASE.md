# v682 test release: live inventory and either-hand Crown Killer press

v681 still could not see the filled counter-serum in the last session and the direct R1/RB
path was addressed to a computed hand. v682 tightens both:

- The serum scan now reads the player state machine's live inventory pointer (`machine+0x328`,
  the path verified by `native_manual_equip`) before falling back to the component list.
- Both equipped/held item slots are checked as well as the inventory list.
- The native R1/RB pass-through behind the living Crown Killer accepts **either hand's held
  grip**, so a left/right mismatch cannot silently swallow the press.
- `cure_r1_frames` reports how many frames the native press was actually issued.

The filled counter-serum is still a game prerequisite. The empty syringe does not cure her.
The last recorded v681 session reported `cure_serum_detected: false` and no native cure offer;
if that is still true, reload a save before the serum was consumed or finish the lab crafting.

## Test

1. Load a save before Hypatia dies with the filled counter-serum.
2. Crouch behind her and hold either grip.
3. Report `cure_serum_detected`, `cure_prompt_state`, `cure_r1_frames`, `offered_choke_frames`
   and `choke_input_frames`.

## Limits

- Not headset-tested.
- The native R1/RB pass-through is scoped to the living authored Hypatia; it does not grant the
  cure item or bypass the game's eligibility.
