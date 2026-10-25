# v611 hands and gun-arm work: checklist (Tier C)

Status: not run. Read this first.

**Proved only in the sim (Linux harness, Tier A and B):** the palm pick logic (unit tests); a coin on a desk next to bigger props is picked on about 97% of frames (70% with the old forward beam); a closed commode door still blocks loot; the loot prompt never named an item a squeeze would not take (0 mismatches); the gun-arm cause (a 100 ms socket lease against a 250 ms tracked-pose window) was confirmed from live captures and the fix is unit-tested.

**Needs the headset:** everything about feel. The sim could NOT exercise these: a coin on the floor or under a bed or chair (the sim's arm cannot reach it, the palm stays about 0.7 m away; this is the main headset item), a real open drawer (the saves had none), how the palm pick feels when you reach quickly, and real frame hitches during play.

If something misbehaves, note the item and where you stood. Dev knob (in `build/`): `legacy-palm-select` restores the old forward-beam pick. Remove it when done.

## Before you start

- Note the DLL hash and the runtime (Steam Link or Virtual Desktop).

## Picking coins and loot

1. **Coin on a desk next to bigger props** (typewriter, books). Reach toward the coin with your palm. Expect: the prompt appears on the coin, and a grip takes it, not the neighbouring prop.
2. **Prompt matches the squeeze.** Expect: the prompt never shows an item a squeeze will not take. Note any prompt that shows but takes nothing.
3. **Coin on the floor.** Headset-only. Crouch and reach down. Expect: prompt and pickup work.
4. **Coin under a bed or chair.** Headset-only. Reach under with your hand. Expect: it works, because line of sight now starts at your hand, not your eyes.
5. **Closed drawer or door.** Expect: loot behind a closed drawer, commode door or wall still cannot be taken.
6. **Palm facing away.** Hold the back of your hand toward a coin. Expect: it is not picked.
7. **No flicker.** Hold your hand still between two coins. Expect: the prompt stays on one and does not jump.
8. **Reach distance.** Expect: the prompt still appears at about arm's reach, not only on touch.

## Gun arm

9. **Gun hand during hitches.** Hold a pistol or crossbow and play through loading or busy moments. Expect: the gun hand stays on the controller. Before, it could jump for a frame or two to a flat-screen pose (a bug since v562). Note any jump.

## Everything from v610 still applies

10. Run the v610 checklist (docs/VR-v610-TEST-CHECKLIST.md): scripted-moment recenter and VR cutscenes.

## Report

For each item: pass or fail, the DLL hash, the runtime, and where you stood for items 1 to 4. For fails on items 3 and 4, say how far the palm was from the coin.
