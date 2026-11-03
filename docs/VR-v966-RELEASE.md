# VR v966 (2026-11-03)

v965 plus the two issues from the user's v965 headset session: weapons/hands flicker, and Talk/Pickpocket still ran
from the grip and showed its X hint.

## Weapons/hands flicker

`native_attachment_pose` refused an arm publication when the worker's solved pose was one tracking sample behind the
policy unless **both** its tick and its revision were older. In production the head-view thread advances the tick only
(the upload revision is shared by both hands), so `source_in_grace` never accepted: headset status showed
`grace_accepts 0`, `rejects_policy 6326`, `publish_refused ~3170` and `arm_flicker.fallbacks ~3200/68333 uploads`, with
fallback runs up to 1089 frames. Each fallback switches the hand and the sword to the native animation pose: the
visible flicker.

Fix: a strictly older **tick** is enough (still inside the 100 ms grace and the same epochs); an equal tick with an
older revision remains the replayed-sample case and is still refused. Lab:
`tests/native_attachment_pose_check.cpp` now also covers the same-revision/one-tick-behind publication.

## Talk and pickpocket on the trigger

The v963/v965 route required the strict `native_talk_offer_current` receipt, which the headset log shows is usually
absent (`missing_receipt 82859`, `hints 0`) even while the game draws the Talk prompt. So the grip kept doing the
native Use and the feature never claimed the hand.

Fix: a new `NativeInteractionAim::npc_offer` reads the game's **raw primary/secondary action descriptor**
(+0xe8/+0x110 Talk, +0xc0 Pickpocket), independent of the receipt. The trigger claim, the grip suppression and the
prompt now all use that offer. `native_offer_controls` adds `npc_trigger_interact_frames` and
`npc_prompt_trig_frames` so the next headset log shows whether the route is running.

The native prompt's X glyph is painted **Trig** (instead of the grip's Grip) whenever the raw NPC offer is live and the
feature is on; with `build/no-npc-trigger` it stays the old glyph.

## Builds

- DLL: `build/d2vr_render_probe_arm_v966.dll`, SHA-256
  `a050f817e3150cadfc4a844ad63f76373fe1f8dbc2b46b10009daa0ad50fce2d` (14,067,712 bytes).
- `%LOCALAPPDATA%\DishonoredVR\build` contains only v966; a direct Steam launch uses it.
- Launcher: `DishonoredVRLauncher-v966.exe` in Downloads, embedding v966.

## Checks

- `native_attachment_pose_check` (including the new grace case) and `vr_controls_check` pass; the DLL builds.
- No headset run here. The next session's `native_offer_controls` (`npc_trigger_interact_frames`,
  `npc_prompt_trig_frames`) and `arm_flicker` (`grace_accepts`, `fallbacks`) show whether the two routes work.
