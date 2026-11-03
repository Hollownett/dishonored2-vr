# VR v969 (2026-11-03)

v969 hardens the Talk/Pickpocket remap after the v965 session still showed the grip doing them.

## Why the trigger route could still lose

The game's Talk/Pickpocket action is the native **X (Use)** action. v966-v968 sent the trigger as a 220 ms X, but several
other control paths also write `input.x` in the same frame: the grip's `InteractOwner` latch, the gaze ray, the legacy
corpse Use and the Talk-Y route. The last write before `controller_bridge.publish` decided, so the grip could still
press X even when the raw NPC offer was present.

v969 adds a final arbitration immediately before publish:

- while an NPC Talk/Pickpocket offer (`NativeInteractionAim::npc_offer`, raw primary/secondary descriptor) owns a free
  hand, `input.x` can only be **true** for that hand's trigger claim (the 220 ms X) and is forced **false** otherwise;
- the grip, gaze, legacy Use and Y routes therefore cannot talk or pickpocket while that offer is live;
- if no trigger claim is active, the prompt is drawn with the **Trig** glyph and the grip does nothing there.

The trigger claim itself still requires a hand free of a weapon/gadget/copy, because that hand's trigger is its weapon's.

## Also in this build

- `npc_prompt` falls back to either hand's raw NPC offer when the prompt is a retained/zero-target entry, so the glyph
  changes even when `refresh_prompt` has no target identity.
- New status fields: `native_offer_controls.npc_trigger_interact_frames` and `npc_prompt_trig_frames`.
- The v966/v968 arm grace fix remains (one-tick-behind poses accepted; the model frame no longer has to match for the
  grace source), which removes the hand/sword fallback flicker.

## Build

- DLL: `build/d2vr_render_probe_arm_v969.dll`, SHA-256
  `b01f6dc42cb96007dbed9d985792922f8eee82f94e605c4b9b2ada312fa06076` (14,068,224 bytes).
- Installed as the only DLL in `%LOCALAPPDATA%\DishonoredVR\build`; launcher `DishonoredVRLauncher-v969.exe`.
- `native_attachment_pose_check` and `vr_controls_check` pass. No headset run here; the next session's
  `npc_trigger_interact_frames` / `npc_prompt_trig_frames` and `arm_flicker.grace_accepts` / `fallbacks` prove the
  routes.
