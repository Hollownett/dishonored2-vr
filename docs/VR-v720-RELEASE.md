# VR v720 (test candidate, 2026-10-30)

User, after v719: the cure hint belongs before the choke. Sneaking up behind Dr. Hypatia should say that choking her cures her. A hint during the cure animation is useless.

## Cause
- `takedown_child` recognises the takedown hint (choke/kill glyphs) by position: any MasterHUD child at nesting 3, stage x 960±60, y 600-820, while a takedown is offered.
- The game's script messages lie in the same region. In the v714/v717 headset captures they were at stage y ≈ 650 (rune) and ≈ 686 (counter-serum), x ≈ 920-960.
- In window passes `takedown_child` runs before `identify`. While the game offered the choke from behind, it claimed the message node as the takedown hint.
- The message the game raises with the offer (v691: Kiscule "Cure notification") was therefore never shown as a message.
- When the choke began, the offer ended and the message was recognised again.
  - v717: the right grip went to 1.0 at 37456234, the authored camera started at 37456279, and the message's first show was at 37456281.
  - `_tutorialMessage` was visible with alpha for about 23 publishes (about 5.7 s), longer than the 2.2 s choke.

## Fix (general)
- A node that the game has published as a script message (`_tutorialMessage` / `_informationMessage` binding) is never claimed as the takedown hint.
- This holds for every NPC and every message.
- v719 is kept: head-locked messages also draw during authored cameras.

## Checks
- native_hud_child_draw_check covers this case: a takedown is offered, a node at stage (960, 700) is the takedown hint, and the same node published as a script message stays a message.
  - Without the fix the check fails at that assertion.
- native_hud_child_capture_check passes.
- No sim run: the user tests in the headset (2026-10-30).
