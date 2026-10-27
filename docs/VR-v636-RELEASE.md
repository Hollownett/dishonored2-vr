# v636 test release

This launcher embeds the exact v636 DLL tested locally. It is a prerelease for headset testing of the v627 feedback: corpse holds dropped while walking, putting a body on the shoulder by hand, and the thumb sinking into the palm. It also carries the v628-v631 corpse, hand and wrist repairs, which have not been released before.

## Changes

- **Thumb on the controller.** With your fingers relaxed, the thumb rests beside the index on top of the controller instead of inside the palm. The fist is unchanged: the thumb wraps over the folded fingers.
- **Corpse hold while walking, sprinting and turning.** Bodies follow the hand instead of being dropped:
  - limb speed is judged against your hand, so a fast run no longer counts as a physics blow-up;
  - the pelvis is towed along at your walking speed, up to sprint;
  - the tow keeps working when the body is heavy;
  - when you stop, the body stops near you instead of sliding into your legs.
- **Physical shoulder.**
  - *Putting a body on:* hold any part of it, lift it to either shoulder and let go there. It stays on that shoulder and comes along when you walk, turn or crouch. Your hand is free.
  - *Taking it off:* squeeze again at the shoulder with the same hand and the body is back in your hand. Let go to put it down, or swing to throw it. The other hand can also grab it and carry it off.
  - The mod no longer starts the game's own body carry: the game refuses a body held up in the air. The old "hold the torso 0.5 s" pickup is gone.
- Earlier repairs since v627:
  - a corpse limb follows the hand without pulling it off the controller;
  - the wrist no longer twists thin;
  - both arms stay tracked during carries;
  - a light haptic tick when the open hand is on a grabbable corpse bone;
  - a sword the game draws by itself is taken only on a fresh squeeze.

## Install

Exit Dishonored 2, download **DishonoredVRLauncher.exe** and run it. It installs the mod and starts VR from **LAUNCH VR**. You need Steam game build 17576867 and the VC++ 2015-2022 x64 runtime. No game assets are bundled. Existing settings and feature choices survive the upgrade.

The DLL alone, with its manifest and notices, is in **d2vr-v636-dll.zip**.

## Validation and open items

- Offline labs:
  - hand lab: the thumb is out of the palm with the fingers relaxed, and the fist is unchanged;
  - `corpse_drag_lab`: 40 scenarios, 0 failed (3 m/s walk: the hold stays, 3-7 cm lag).
- Focused checks pass (grip/shoulder logic, finger IK, tow, corpse/grip routing). `body_ik_check` fails only its known reverse-grip hilt check, which has failed since v612.
- Simulated game:
  - walking drag keeps the hold;
  - lift a body to the right shoulder, let go and walk 2 s: it stays at shoulder height (5 cm lag) with both hands free;
  - squeeze to take it back, then put it down or throw it (6.6 m).
- The DLL was built with Windows MSVC (DLL only). The full required build was not run.
- **Not headset-tested.** Please check:
  - how much a body on the shoulder blocks the view;
  - whether it pushes you or jitters;
  - long walks and sprints with a held body;
  - the thumb.
- The checklist is in `VR-v636-TEST-CHECKLIST.md` inside the zip.

The v627 release remains available for rollback. Exit the game before changing launcher versions.
