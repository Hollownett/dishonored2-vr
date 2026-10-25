# v612 grab, fingers, grenades and cutscene work: checklist (Tier C)

Status: not run. Read this first.

**Sim-verified (Linux harness and simulator, Tier A and B):**
- Early squeeze on a corpse: caught 9 ms after the grip edge and held about 5 s; while it was held and moved the body slipped back to the hand instead of dropping (no 0.5 m / 0.25 s drop).
- Grip alone on a corpse: held 3.5 s.
- Fingers on a table: the drawn fingertips rest within tolerance on about 93% of table-contact frames (the deepest tip was about 23 mm; the thumb was the worst finger).
- Letterbox: the two bars were hidden on every immersive 3D cutscene frame (about 2 per frame); Bink was detected (1 open, hundreds of frames).
- Unit and routing tests for everything else (palm pick, grip decision, grab buffer, living hold, finger wants stack, throw gesture, gadget aim, skip hold, letterbox gate).

**Headset only (the sim did NOT exercise these):**
- Grenades: no grenade was equipped in the sim, so no hand-swing throw ever happened.
- Mines and spring razors: no mine was equipped, so the left-hand aim result was never observed.
- Cutscene skip: the sim opening did not shorten under a 0.6 s and a 7 s hold (that scene may not be skippable); a real skip is unverified.
- The dialog-choice hold (no 3D to flat flicker): policy tests only, no dialog choice was reached in the sim.
- Living guards and civilians, doors/levers/loot staying Use, hip draw timing, and everything about feel.
- Letterbox in both eyes (the per-eye cap is unit-tested; the sim ran one pass per frame).

Every new feature is on by default; each has a row under Settings, Features. Dev knobs (files in `build/`) turn them off: `no-grip-alone-grab`, `legacy-palm-select`, `legacy-corpse-lag-release`, `no-living-hold`, `no-finger-surface`, `legacy-finger-ease`, `no-hand-grenade-throw`, `no-hand-mine-aim`, `no-bink-theater`, `no-cutscene-skip-hold`, `no-letterbox-hide`. Remove them when done.

## Before you start

- Note the DLL hash and the runtime (Steam Link or Virtual Desktop).

## Grabbing

1. **Grip alone grabs.** Squeeze the grip next to a crate, bottle or other prop, no trigger. Expect: you pick it up.
2. **Block still blocks next to furniture.** With a sword in the other hand, raise the grip to block while standing next to a table or shelf. Expect: you block, nothing is grabbed.
3. **Doors, levers and loot still Use.** Squeeze the grip at a door, lever, or a coin. Expect: it opens, pulls or is taken, not grabbed.
4. **Hip gun draw is not delayed.** Draw a pistol from the hip. Expect: no extra wait.
5. **Early squeeze catches.** Start squeezing just before your hand reaches a prop. Expect: it is caught as you arrive, not missed.
6. **Paper stacks.** Grab a stack of papers or books. Expect: it lifts without sticking or flickering.

## Bodies

7. **Corpse by torso, limb and head.** Grab a dead body at each. Expect: it is held where you grasped it.
8. **Hold does not drop when the body snags.** Drag a corpse past furniture. Expect: it slips back to your hand rather than dropping.
9. **Living guard or civilian.** Grab one. Expect: he stumbles and notices you. A hard pull ragdolls him and you keep a limb.
10. **Choke, takedown and talk still win** over grabbing a living person.

## Fingers

11. **Grips ease in.** Close your hand on a prop. Expect: the fingers close over a moment, not a snap.
12. **Pre-curl and pinch.** Move a hand toward a small item. Expect: the fingers curl as you approach and pinch for a small one.
13. **Fingers rest on surfaces.** Lay your hand on a table, shelf and a sheet of paper. Expect: the fingers rest on top and do not pass through. The thumb may dip when the palm is pressed down: report it, with where you stood.

## Throwables

14. **Grenade by hand swing.** Equip a grenade, hold the left trigger, swing and release. Expect: it flies the way your hand moved. Headset-only. Note speed (too weak or too strong) and direction.
15. **Mine and razor placement.** Equip a mine or spring razor, hold the left trigger and aim with the left hand. Expect: it is placed where the hand points, not where the head looks. Headset-only.

## Cutscenes

16. **Pre-rendered video.** Start one (for example a logo or intro movie). Expect: it shows flat at once, without a black wait.
17. **Hold A or X 0.6 s skips a skippable cutscene.** Expect: the skip prompt fills and the scene ends. Release afterwards: nothing leaks into the next screen (no stray menu confirm or jump). Headset-only; a scene that cannot be skipped may do nothing.
18. **No letterbox in 3D cutscenes.** Expect: no black bars at the top and bottom in either eye.
19. **No 3D to flat flicker at dialog choices.** In a conversation with choices, expect the picture to stay put while the choices are up. Headset-only.

## Everything from v611 still applies

20. Run the v611 checklist (docs/VR-v611-TEST-CHECKLIST.md) and the v610 one it points to.

## Report

For each item: pass or fail, the DLL hash, the runtime, and where you stood. For item 13, say which finger sank and how far.
