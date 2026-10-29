# v681 test release: native R1/RB fallback for the Crown Killer

v680's stealth prompt still did not produce an injection in the last session. The status from
that session showed:

- `cure_prompt_state: 3` — living Hypatia was found, but the filled counter-serum was not
  detected in the player inventory;
- `offered_choke_frames: 0`, `choke_input_frames: 0` — the game never offered or received the
  cure action.

v681 is a diagnostic/fallback build for that exact combination:

- The stealth card is no longer gated on the serum scan. For a living Hypatia it shows
  **“Sneak behind her and hold RIGHT/LEFT grip to inject counter-serum.”**
- If neither the native NPC-interaction tutorial nor the choke HUD hint exists, holding the
  free-hand grip while the mod identifies the living Crown Killer is passed through directly
  as native **R1/RB**. This is scoped to the authored Hypatia entity, not a general proximity
  action.
- New `cure_serum_detected` status reports the filled-item scan without suppressing the card.

The filled counter-serum is still a game prerequisite. The empty syringe does not cure her.
If the serum was already used, reload a save from before using it; if the journal does not
show it, finish the three lab steps.

## Test

1. Load a save before Hypatia dies, with the filled counter-serum.
2. Crouch behind her and hold the grip named by the card.
3. Report `cure_serum_detected`, `cure_prompt_state`, `offered_choke_frames` and
   `choke_input_frames`.

## Limits

- Not headset-tested. The last recorded status predates v681.
- The fallback does not grant the cure item or bypass the game's own eligibility; it only
  supplies the native R1/RB input when the HUD path is absent.
