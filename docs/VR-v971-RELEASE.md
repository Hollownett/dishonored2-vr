# VR v971 (2026-11-03)

v971 addresses the two remaining headset reports: the held weapon still flashing black under temporal AA, and the prompt's
"Trig" hint being redrawn as the old grip hint.

## Held weapon black flash under TAA/FSR/DLSS

The v930-v962 draw-time weapon placement rewrites the weapon's **current** MVP rows in its per-instance constant buffer,
but the engine's depth prepass and velocity/reprojection were already written with the tick-frame rows. Temporal AA,
DLAA, FSR and DLSS then sample depth/history for one position and colour for another — the black, untextured-looking
flash when the weapon is in the hand.

v971 keeps the engine's **current** rows for every draw under a temporal mode (`temporal_live_mode >= 1`); only the
general per-eye previous-row correction still runs, so depth, colour and motion stay consistent. The placement remains on
for temporal-AA-off (SMAA/native) sessions. `build/weapon-draw-place-temporal` forces the placement back on for A/B.
Status `object-motion.json`: `weapon_temporal_skip`.

## Prompt hint reverts to "Grip"

The prompt's button icon is bound by a different HUD child than the prompt context callback sees, and the game can
redraw it. The per-child `talk_prompt_context` therefore sometimes returned false and `GlyphSwap` used its default X
replacement ("Grip").

v971 adds a 600 ms `GlyphSwap::npc_prompt_until` latch, set while an NPC Talk/Pickpocket offer is live in the control
loop or the HUD observer. The X glyph stays **Trig** for the whole prompt even when the per-child callback is false; it
returns to Grip when no NPC offer is latched. The lab `glyph_swap_async_check` now checks the latch and the Trig label.

## Also

- `native_offer_controls.npc_trigger_interact_frames` / `npc_prompt_trig_frames` remain for the next session.
- Arm grace, raw NPC offers and the v969 last-write X arbitration are unchanged.

## Build

- `build/d2vr_render_probe_arm_v971.dll`, SHA-256
  `e17ba4753e70919476acbfe00bbb2ee081fdd79542d77b1b0f996a4550109f5e` (14,069,248 bytes).
- Installed as the only DLL in `%LOCALAPPDATA%\DishonoredVR\build`; launcher `DishonoredVRLauncher-v971.exe`.
- `native_attachment_pose_check`, `vr_controls_check` and `glyph_swap_async_check` pass.
- No headset run: the black flash and the hint latch need the user's headset.
