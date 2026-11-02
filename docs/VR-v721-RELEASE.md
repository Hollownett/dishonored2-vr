# VR v721 (test candidate, 2026-10-30)

Four problems from the user's headset session with v720.

## 1. Dead NPC indicator does not appear when the hand aims at the body
- The problem matches the old Talk indicator problem.
- Blink journal:
  - `carry_guard=1` rows on dead NPCs (body flags 136/160/176).
  - The retained entry (key 10) and the hand target (key -1) published the same node in turn.
  - The slot was shown and hidden every 15-600 ms.
- Cause:
  - `body_carry_prompt` hides a body's prompt unless the hand's live Loot offer validates (`loot_offered`, 250 ms live window).
  - That offer lapses for frames while the hand query re-selects.
  - Each lapse cleared the prompt for both bindings.
  - These are plain `clear()` exits, so the v711 same-element hold never applied.
- Fix (general, every body):
  - A Loot offer seen on a body holds for 700 ms.
  - A body that offers only Carry still shows no prompt (v637 rule).
- New status field `prompt_guard_clears` counts the frames the guard still clears.

## 2. Dialogue choice shown like a theater screen
- The user wants it in the game, in the world at the person being talked to.
- Log of the boat cutscene:
  - logic 32, then 512 (dialogue choices) for 21 s, then 32.
  - The scene stayed stereo.
  - The choices were a 1 m world-UI panel 1.05 m ahead, showing the choice movie's 16:9 band with its dim full-screen backdrop.
- Fix, for logic 512 only:
  - The panel stands 1.6 m away at conversation distance and 0.2 m below eye height.
  - It keeps the old angular size and is fixed in the world when it opens.
  - The movie's dark translucent backdrop is keyed out on the GPU (`NativeEyeArt` keyed replace), so only the lit choice art shows over the scene.
  - A failed key falls back to the plain copy.
- Status `dialogue_keyed` = [keyed images, key failures].

## 3. Boat cutscene camera jumped up and down
- Data from `dxgi-camera-probe.csv` for the boat ride (presents 57888-60196):
  - The scene camera's x/y moved smoothly.
  - Its height jumped 0.6-2 m between samples (-0.61 to -2.27).
  - The headset was still: `head_y` std 3.7 cm.
- Each height jump of more than 0.5 m counted as a camera cut (`detect_cut` used 3D distance). A cut:
  - reset the vertical smoother;
  - re-aimed the view instantly.
- 867 cuts over 9 cutscenes.
- The gameplay smoother also bounds its lag to 0.2 m, so it could not hold such a camera.
- Fix:
  - Height alone is never a cut: `detect_cut` uses horizontal distance and yaw.
  - An immersive cutscene view holds its height with its own slow low-pass (tau 0.5 s, 0.6 m/s, no lag bound or teleport reset). It resets only on a real cut.
  - Gameplay smoothing is unchanged.
- The source of the raw height swing was not identified. No per-frame raw scene z was logged.

## 4. Shadow Walk stealth attack is hard to do
- In Shadow Walk the empty hand drives the native Slash through `VrCombat`.
- It needed the full sword swing: 2.8 m/s, 25 cm of tip travel, 15 cm of hand travel, 12 m/s² acceleration.
- Fix, in Shadow Walk only:
  - Every swing threshold is ×0.55.
  - A right-trigger press also attacks (one attack per press).
  - A trigger held across the mode change is not a press.
- Reach: the kill is the game's own shadow-walk assassination rule, distance ≤ 3 m from the player origin (`docs/native-takedown-range.md`).
  - The mod's takedown scale (0.75) never limited Shadow Walk.
  - Widening the rule means editing shared rule-condition instances. The v536 audit removed those writes after a cross-rule cache-poisoning case.
  - Not done here.

## Checks
- `vr_combat_check`: gentler Shadow Walk swing and trigger attack, and neither in ordinary sword play.
- `cutscene_view_check`: a 1.5 m vertical-only jump is not a cut.
- `anchor_smoother_check`: the boat's recorded height pattern settles to less than 10 cm of spread.
- `native_eye_art_contract_check` (WARP GPU): a keyed replace drops a dim translucent backdrop and keeps lit art.
- `attack_intent_check`, `native_hud_child_draw_check` and `native_hud_child_capture_check` pass.
- DLL builds.
- No sim run, and not headset-tested.
