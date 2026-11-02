# VR v722 (test candidate, 2026-10-31)

Four problems from the user's headset session with v721.

## 1. Dead NPC: no indicator when the hand is on the body
- The user wants it to work like Talk (Lila) or a loot pickup.
- In the v721 session `prompt_guard_clears` was 1157. The v637/v721 body prompt guard (`body_carry_prompt`) still cleared the body's prompt whenever no Loot offer was live.
- Fix: the guard is removed. A body shows the game's own prompt on the body, like every other prompt.
- `body_carry_action` still keeps the Use press off a body, so the hand carry is unchanged.

## 2. Dialogue choices: a black, head-locked window
- Log:
  - logic 512 at 52372562-52389562.
  - `dialogue_keyed` [0,0], so the v721 world panel never drew.
  - No `world-ui-3` capture in that process.
  - No menu layer during 512.
- So the choice movie was never captured, and the game drew it into the eyes itself with its dark backdrop.
- Probable cause:
  - `begin_world_ui` required `find_native_player_camera`.
  - In the boat ride that camera does not validate (v720 log: theater fallback `camera_unreadable`).
- Fix:
  - The gate now requires only that the game and player exist (`native_player_present`).
  - The owner lookup and `native_menu_state(..., any_batch)` also find the movie in any Iggy batch, not only the newest. This is exact, because the file's own Iggy pointer must match. Menu input still uses only the newest batch.
- Placement (user: "next to the person, exactly like the Talk prompt"):
  - The last prompt shown on a standing NPC (the `npc_near_gaze` placement) is remembered.
  - Choices that open within 30 s of it stand there, 0.8-3 m away, facing the eyes, at the same angular size (`world_ui_speaker_pose`).
  - Otherwise they open 1.6 m ahead, as in v721.
- New status fields:
  - `dialogue_frames` [stale, fresh];
  - `world_ui_hook` [calls, identified, scopes, rejects(logic), rejects(no player), target redirects, draws wheel/notes/journal].

## 3. Boat cutscene still bobbed
- The rendered eye height (`dxgi-camera-probe.csv`) swung 1.3 m between samples during the ride. x/y were smooth, and the eyes kept their IPD, so these were the mod's written views.
- The v721 cutscene smoother ran only when a cutscene view opened. Without the authored camera (mode 10) no view opens, and the frame used the gameplay smoother with its 0.2 m lag bound.
- Fix:
  - Every cutscene frame uses the cutscene height smoother.
  - Without a cutscene view, its own `detect_cut` (horizontal and yaw) resets it.
- Status `cutscene_untracked` = [frames, cuts].

## 4. Shadow Walk: kill indicators drawn high and gone in reach
- The user's simplest fix: a kill by grab.
- In Shadow Walk, closing either hand's grip is an attack press, one per press, like the trigger (`CombatFrame::grab`). A grip held across the mode change is not a press.
- The game's own rule decides whether it is a kill.
- The indicator placement itself was not changed: there are no Shadow Walk records to show which HUD path draws it.

## Checks
- These pass:
  - `world_ui_pose_check` (speaker placement);
  - `native_world_ui_check` (choice movie under a newer batch);
  - `native_menu_state_check`;
  - `vr_combat_check` (grab press in Shadow Walk only, held grip is one press);
  - `cutscene_view_check`;
  - `anchor_smoother_check`.
- The DLL builds.
- No sim run, and not headset-tested.
