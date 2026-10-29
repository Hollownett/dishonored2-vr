# v677 test release: the counter-serum (choke/cure) prompt

Fixes the missing VR prompt when Dr. Hypatia has to be injected with the counter-serum in
Mission 3, *The Good Doctor*. The game performs that injection as its non-lethal choke
(`arkPlayerStateCureChoke`).

This release also includes the work since v672: the v673 palm-placed stun mine/spring razor,
the v674/v675 corpse **Loot** fix for Vasco's note, and the v676 cutscene's own animated
player body and arms.

The mod already draws the native choke hint as world art over the
NPC, but four separate paths could make it disappear:

1. A downed or unconscious NPC's prompt was cleared as if it were the game's "Carry body"
   prompt. The counter-serum target can be in that state, so the only prompt that completes
   the quest was removed.
2. The world takedown prompt took its target only from the player-sensor poll. A scripted cure
   can be offered through the native HUD choke action while that poll has no target.
3. If the retained HUD prompt's NPC/model anchor or its world pose could not be read for one
   frame, the prompt was dropped instead of shown.
4. The actual counter-serum instruction is the game's script message `ui_sm_cure_hypatia`
   ("Press <BLOCK> to inject counter-serum"). It is a HUDScene `_tutorialMessage` /
   `_informationMessage`, not an InteractionWindow, so it only reached the HUD panel and was
   easy to miss in VR.

## What v677 changes

- The body-carry guard no longer clears a downed NPC's prompt while the game offers that
  exact NPC its native choke/cure action. Other body prompts keep the v637/v675 guard.
- The native choke/cure HUD offer (`arkHUDElementInteractives` action 9) now also publishes
  the world prompt's target, in addition to the player-sensor poll. `offered_choke_spawn`
  remembers it for 250 ms; `takedown_prompt()` holds a target for its existing 700 ms.
- HUDScene `_tutorialMessage` and `_informationMessage` are probed on the existing verified
  native UI thread. A visible message with complete geometry is published as a world prompt,
  so `ui_sm_cure_hypatia` appears ahead of the player instead of only on the HUD panel.
- A prompt whose anchor or pose is unavailable for a frame is placed ahead of the player
  instead of being cleared. The last-good art still covers a failed capture.
- New diagnostics in `artifacts\xr-live-status.json`:
  `prompt_anchor_fallbacks`, `prompt_guard_skips`, `native_message_publishes`,
  `offered_choke_spawn`. A successful cure attempt should show `native_message_publishes`
  rising near Hypatia; `prompt_guard_skips` rises if her prompt was the one the old guard
  would have removed.

## Install

Exit Dishonored 2, run the v677 launcher and choose **LAUNCH VR**. The launcher embeds
`d2vr_render_probe_arm_v677.dll`; v675 and v637 remain in `rollback`.

## Test (headset)

1. Load a save in Addermire before injecting the Crown Killer, with the counter-serum crafted.
2. Approach Dr. Hypatia / the Crown Killer and look at her. The game's non-lethal hint must
   appear over her in the world. Aim either hand at her if the hint is not already visible.
3. Hold the grip (the non-lethal RB hold) to inject the counter-serum. The whole scripted
   animation should play.
4. If the prompt is still missing, send `artifacts\xr-live-status.json` and the
   `native-prompt-*` files. `offered_choke_spawn` nonzero means the game offered the cure;
   `takedown_hints` and `world_prompt_frames` show whether the mod captured and placed art.

## Limits

- Not headset-tested. This checkout has no Mission 3 save at Dr. Hypatia, so the cure prompt
  was not observed in the live game.
- The native choke hint artwork must still be captured. v677 guarantees its target and
  placement once the art exists; it cannot invent the game's label.
- The DLL built with `/W4`; the focused prompt-placement and HUD checks pass. Strict `/WX`
  repository certification was not rerun.
