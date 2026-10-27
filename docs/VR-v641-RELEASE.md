# v641 test release

This launcher embeds the exact v641 DLL tested locally. It is a prerelease that fixes the four problems from the v638 headset run. Everything else is unchanged from v638.

## Changes

- **Bodies are never put on the game's shoulder.** A grab on a torso could still fall back to the game's Use, and the game's held Use on a body is its shoulder carry. Now no Use reaches the game while it has a dead or unconscious body selected. Dragging and the hand-made shoulder are unchanged. Knob: `build/legacy-corpse-carry`.
- **Hands no longer get stuck.** After a table or a corpse, a hand could stop following your controller. The visible hand waited without end for a collision answer that never came (or a check that started inside a corpse limb); now it waits at most 0.25 s and then follows your controller, still kept out of known walls. The invisible physics hand is reset when it stays wedged for 1 s. Knob: `build/legacy-hand-stuck`.
- **Stealth kills from behind.** Any stab or slash that touches a calm, unaware NPC from behind or from the side kills him, whether or not the game shows its kill prompt. When the game offers its own takedown on him, its assassination animation plays instead. A guard who has noticed you still takes an ordinary hit. A strong pulse on the sword hand confirms the kill. Knob: `build/legacy-sword-contact`.
- **Chokes with the free hand.** With the sword drawn, the left hand chokes (before, only the right grip could, and it was holding the sword). The sword-in-front block no longer swallows the choke's press.
- **Sphere grip.** The fingers wrap one invisible ball sized to the object when you take it: books, folders, candlesticks, spoons, forks, corpse limbs you drag and severed parts. The object sits inside that ball, and the fingers no longer twitch while it swings. Bottles and glasses keep their grip. Knob: `build/legacy-finger-grab`.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled.

The DLL alone, with its manifest and notices, is in **d2vr-v641-dll.zip**.

## Validation and open items

- Simulated game, guard 9258 (40 hp, unaware):
  - the sword swung through him from the side: the contact was held, the game offered him, and the contact's press became the game's own Assassination (40 to 0 hp) in three runs;
  - left hand squeezed with the sword drawn and held in front (Block on): the game's Choke started (8 s, 27 choke input frames). Before v641 the left grip never routed to the choke.
- The kill without a game offer (the mod's own kill) is covered by `sword_contact_policy_check` only: in every sim run the guard was already offered or dead when the blade met him.
- Simulated game, sphere grip: a dragged corpse wrist and a severed part were wrapped on the ball; the finger curls stayed constant across every 0.5 s snapshot once settled, all five fingers touching.
- Offline: `hand_clamp_check` reproduces the frozen hand (answers lost in free air, a sweep starting inside a loose body) and its recovery after 0.25 s; `hand_body_check` the wedged-body release; `finger_sphere_check` 0 deg per-frame finger change on the ball; `grip_decision_check`, `finger_ik_check` and the touched routing checks pass. The 14 Python checks that fail also fail on v638's commit.
- The body-Use block is not reached by the sim scenarios (no Use landed on a body there); the corpse drag still works (`drag`: body dragged).
- The test save's nearby guards are mostly dormant (1 hp, or not standing where listed); combat tests need `--near npc-side --pre-teleport 77.38 58.24 25.91`.

- The DLL was built with Windows MSVC (DLL only). The full required build was not run. **Not headset-tested.**
- The checklist is in `VR-v641-TEST-CHECKLIST.md` inside the zip.

The v638 release remains available for rollback.
