# VR v698 / v699 (test candidate, 2026-10-30)

v696 and v697 are the finger session's numbers. v698 was built from HEAD v696 fingers; v699 is the same change on top of v697 (927fe75) and is the one to test.

Headset v695 (user):
- In Dr. Hypatia's office, ammo and blueprints could not be picked up and the newspaper could not be read. Newspapers elsewhere read fine.
- A prompt still went very big for a few frames, then vanished.
- No Talk offer on Hypatia, as with other NPCs.

## Use in Hypatia's office (the separate cure block)
- The v695 journal showed the cause: every right grip there was RB and never X (Use). RB with an empty hand is Block, which is why the game kept trying to draw the sword (37 vetoes).
- Cause: DeepSeek's cure block (v677-v690). While Hypatia was near and her cure state was "offer" or "native message", it turned every grip on either hand into RB and cleared Use.
- Removed: that block, `publish_cure_hint`, `nearby_hypatia`, `player_has_cure_serum`, `is_hypatia_entity` and the cure status fields.
- The cure now goes through the general choke route. The game's own choke sensor offering an NPC (`sensor_choke`) is a choke offer for the grip, like the HUD's offered choke. It applies to any NPC; it was Hypatia-only.

## Prompt blink on an NPC: Talk vs the choke/kill hint
- The v695 blink journal showed Hypatia's retained Talk prompt (key 7) and the takedown hint alternating in slot 0 every 30-100 ms. Each change was a missing frame.
- The takedown hint now has its own capture slot (14, art slot 11). It is placed with the same NPC anchor logic and its own last-good hold, one hint-height above the Talk prompt, as in the flat game.

## "Huge" prompt frames
- A prompt that appeared before its object was known went to the HUD screen panel for those frames (`!prompt_world`).
- Prompts are now never panel content in the HUD window. The world path puts an unanchored prompt 0.9 m ahead.
- The partial crops in native-prompt-jump-* (a lone "Tap", the name line) are the game's own fade-in.

## Checks
- native_hud_child_draw_check is updated for world-only prompts and passes.
- Sim aim on v696/v698: HUD and captures pass, 0 eye-art failures. The v698 run missed "reached gameplay" on timing.
- Not headset-tested: the cure via the general choke, Talk with the takedown hint, and Use in Hypatia's office.
