# v676 test release: the cutscene's own arms and body

This build keeps the player's body and arms on screen in an in-engine cutscene while the game's
own animation drives them. The controllers do not move the hands there: the cutscene keeps the
game's control rule, and grabs, Use, attacks, turning and hand physics stay off until it ends.

## What changed

- A cutscene no longer moves the player's rendered body/arms out of view, and the mod keeps
  the owned visible-body material on (the same material the gameplay full-body render uses), so
  the game's own native cutscene animation is drawn with a visible body. This is the default.
- The mod uploads no tracked hand or full-body palette in a cutscene, so moving a controller does
  not move the cutscene's arms. The full-body solver stays off for that frame and the tracked hand
  block does not run.
- The wheel still hides the avatar, as before.
- Knobs (files in `build/`):
  - `cutscene-avatar` — the v643 mode: tracked hands and the body follows the controllers.
  - `hide-cutscene-avatar` — v672's hidden cutscene avatar (the render copy moves out of view).
  - `no-cutscene-body` — leave the game's own body material alone (no forced visible body).
- The status block `scripted_view.cutscene.avatar` records the cutscene body uploads as
  `native` (the game's own animation, not hidden), `hidden` or `tracked`, plus
  `body_material` (frames where the owned visible-body material was on), so a session can show
  which mode ran.

## Test (headset)

1. Play an in-engine cutscene where the player character is on screen (a third-person scene or
   one that looks down at the body). Expect the character's own animated arms and body to be
   visible, with no duplicated tracked hands.
2. Move the controllers during the cutscene. Expect the cutscene's arms not to follow them, and
   no grab, Use, attack or hand push to fire.
3. After the cutscene, check that tracked hands, the visible body, grabs and combat work as
   before. If the body looks wrong in a scene, add `build/no-cutscene-body` (leave the game's own
   material) and report the scene; that tells us whether the game or the mod's body material is
   the right source for it.
4. If a scene still shows only the game's separate cinematic arms (for example the Dreadful Wale
   waking scene), note which scene it is: that scene uses its own cinematic arm model, not the
   player's body.

## Validation and limits

- Offline checks pass: `cutscene_policy_check` (the new avatar policy), `cutscene_view_check`,
  `cutscene_skip_check`, `xr_frame_hardening_check`, `comfort_vignette_check`,
  `native_action_camera_check`, `wheel_avatar_check`, `head_calibration_check`,
  `cutscene_routing_check.py` and `authored_view_routing_check.py`.
- The DLL builds with MSVC `/W4` (the repository's strict `/WX` certification still stops on the
  pre-existing `native_hand_contact.hpp` C4267 warning, as in v672/v675).
- **Not headset-tested.** Whether the game's cutscene animation looks right on the player body,
  and whether every scene shows a body at all, is only known in the headset.
- Local candidate: `build\d2vr_render_probe_arm_v676.dll`,
  SHA-256 `7f7d507e9ed0ee89b562d42b0fa22a41d59ae3866777f9b726178bb8c44e0315`. This is a
  DLL-only candidate; the launcher has not been rebuilt with it.
