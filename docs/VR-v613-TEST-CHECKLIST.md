# v613 fixes from your v612 headset test: checklist (Tier C)

Status: not run. Read this first.

**Sim-verified (Linux harness and simulator, Tier A and B):**
- Sword draw: the hilt sat 1.5 to 1.9 cm from the hand from the first frame; wrong-pose reads went from 1 per draw to 0.
- Right-eye ghosting (item 2): reproduced in the sim by delaying the right eye's upscaler. With the fix the right eye is upscaled from frame 1 (sharpness ratio right/left 0.97 to 1.03, against 1.1 to 1.5 for the first minute before).
- Shoulder carry: a quick pelvis grab stays a physical drag; a long pelvis or spine hold hands off to the game's carry at +0.51 s; releasing ends the carry 14 ms later; limbs never hand off.
- Unit and routing tests for the AA defaults, the launcher AA save and the performance-mode label.

**Headset only (the sim did NOT exercise these):**
- Gun holster and draw (no gun in the sim save), crosshair after a weapon swap, a live in-game AA switch.
- Sprint hand lag: not reproducible in the sim (it cannot sprint long enough).
- Mine placement marker (no mine equipped).
- The feel of everything.

Every new feature is on by default; each has a row under Settings, Features. Dev knobs (files in `build/`) turn them off: `legacy-corpse-carry`, `legacy-run-gate`. `build/barrel-aim-trace` is a diagnostic, off by default. Remove knobs when done.

## Before you start

- Note the DLL hash and the runtime (Steam Link or Virtual Desktop).

## 1. Launcher AA applies

1. **Launcher choice reaches the game.** In the launcher pick an Anti-aliasing mode, save, press LAUNCH VR. Expect: the game starts with that mode (check the VR settings panel).
2. **GPU default is preselected on a fresh install.** Expect: RTX with DLSS shows DLAA, an AMD GPU that runs FSR 4 shows FSR 4 native, anything else FSR 3 native. No switch happens a moment after the level starts.
3. **An in-game change still wins.** Change AA in the VR panel, quit, reopen the launcher. Expect: it shows your in-game choice and the game keeps it.

## 2. No right-eye ghosting

4. **First minute with FSR 3, FSR 4 and DLSS.** Start a level with each (as available). Expect: no ghosting or soft right eye in the first minute.
5. **Performance mode uses SMAA.** With performance mode on (default), the game draws one eye per frame and uses SMAA instead of FSR/DLSS. Expect: the VR panel's Anti-aliasing row says "(SMAA in perf. mode)" and its help says so; the launcher's Anti-aliasing help shows the same note while the performance-mode row is on. Turn performance mode off to use FSR/DLSS.

## 3. Sword catch

6. **Throw a sword up and catch it.** Expect: it is in your hand from the first frame, no flicker. Known residual: the angle on the catch frame can be a little off.
7. **Redraw with B.** Expect: at most one open-hand frame.

## 4. Shoulder carry

8. **Torso held 0.5 s.** Grab a corpse by the torso and hold. Expect: after about 0.5 s the game's shoulder carry starts.
9. **Quick or other grabs drag.** Quick torso grab, limb, head or collar. Expect: a physical drag, no shoulder carry.
10. **Grip near a corpse.** Squeeze the grip near a body. Expect: it never shoulders it instantly.
11. **Release drops.** While carrying, release the grip. Expect: the body drops.

## 5. Crosshair and mine marker

12. **Crosshair after a weapon swap.** Swap weapons a few times. Expect: the crosshair still lines up with where the weapon aims. Unverified in the sim. If it drifts, create an empty file `build/barrel-aim-trace`, repeat, and send `artifacts/barrel-aim.csv`.
13. **Mine marker.** Equip a mine or spring razor. Expect: the placement marker follows your left hand.

## 6. Sprint

14. **Run with the game's sprint.** Expect: hands and held objects stay with the controllers, no lag then catch-up cycle. Headset-only.

## 7. Holster and draw

15. **Holster then draw a sword.** Expect: no flicker before it fits the arm.
16. **Holster then draw a gun.** Expect: the same. Headset-only.

## Everything from v612 still applies

17. Run the v612 checklist (docs/VR-v612-TEST-CHECKLIST.md) and the v611 one it points to.

## Report

For each item: pass or fail, the DLL hash, the runtime, and where you stood.
