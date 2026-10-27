# v646 test release

This launcher embeds the exact v646 DLL tested locally. It is a prerelease that fixes the items from the v642 headset run. Built on v642.

## Changes

- **The free hand picks things up while the sword is held.** With the sword in the right hand and in front of you, the mod holds the game's Block, and the game then ignored the left hand's grab of a severed part or a bottle. Now Block lets go while the left hand grips something to use, and comes back after. The left-hand choke, the gun hand and the block itself are unchanged.
- **Stealth kills from behind.** The test for "the player is behind or beside the guard" read the guard's facing backwards. A stab in the back counted as a frontal hit and got the ordinary, alerting slash. Only side hits, or the game's own kill offer at the neck, killed. Now any deliberate stab or slash from behind or the side kills a calm, unaware guard. Measured live: a patrolling guard walks along the direction the mod now treats as his front. Knob: `build/legacy-sword-contact` (the older contact rules).
- **Heavy objects lift.** Props now get the same hand strength as bodies (15x instead of 6x). Before, the pull across a level forearm was weaker than gravity for the heaviest props, so a big crate could not leave the floor. `prop_strength_scale 6` in `build/grab-tuning.cfg` restores the v642 feel.
- **Thumb on the sphere grip.** Holding a book, a stamp or another prop, the thumb no longer closes into the palm. It rests on the outside of the grip ball, opposite the fingers. On very small objects the thumb stays out of the palm but cannot quite reach the ball.
- **Cutscenes keep your hands and body** while the scene's camera is at your character. Takedowns, finishers and mantles still play the game's own animation. Grabs, Use and combat stay off during a cutscene. Knob: `build/legacy-cutscene-hands`.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled.

The DLL alone, with its manifest and notices, is in **d2vr-v646-dll.zip**.

## Validation and open items

- Simulated game, left hand on a severed body part (crouched, grip alone):
  - v642, sword held: no pickup (reproduces the report).
  - v646, sword held (Block on): HoldObject.
  - v646, no sword: HoldObject.
- Simulated game, guard 9258 from behind (v645): no hit was rejected as frontal; the guard died (40 to 0 hp).
- Live measurement (v644): a patrolling guard walked along +row0 of his frame (median cosine +1.00 over 161 one-second windows), so +row0 is his forward.
- The game's transition table, read live: Block does not forbid HoldObject or Interact, but it does forbid LootObject. So with the sword in front, loot (coins) could not be taken either; the v646 change covers that too.
- Cutscene (the Dreadful Wale waking scene, SLOT22): the tracked hands and body ran for 1171 frames (v643: 0, because a load cleared the arm binding and it was never refreshed in a cutscene). The eye images are unchanged in this scene: it draws its own cinematic arm model (khaki sleeve), not the player's. Scenes that use the player's own model are not tested yet.
- Hand lab: on the built-in book and on a recorded 15 cm hold from the v642 session, the thumb moves out of the palm onto the ball. The lab poses a right hand only, so the left-hand recordings are not a valid check.
- Unit checks: `sword_contact_policy_check`, `grip_decision_check`, `native_action_camera_check`, `finger_sphere_check` (new thumb case), `finger_ik_check`, `corpse_strength_routing_check` pass. In `grab_force_cap_check`, the new heavy-prop case passes. Its temp-file section, and `corpse_hold_check`, fail in this sandbox on v642's commit too.
- The DLL was built with Windows MSVC (DLL only). The full required build was not run. **Not headset-tested.**
- The checklist is in `VR-v646-TEST-CHECKLIST.md` inside the zip.

v642 and v641 remain available for rollback.
