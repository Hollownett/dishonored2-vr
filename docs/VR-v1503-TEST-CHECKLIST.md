# Headset checklist: solid arms, body holsters, gadgets and the Heart back, natural fingers (v1497 / v145799, 2026-11-26)

Branch `feat/doto-vr`. Everything below was built and checked on Linux (host checks + simulated sessions of the real games under
Proton, `tools/linux/scenarios/*`); none of it has been in a headset yet. Each line: what to do, what you should see, the knob that
turns the change off if it feels wrong (`%LOCALAPPDATA%\DishonoredVR\build\<knob>` for Dishonored 2, `...\DishonoredVR-DOTO\build\<knob>`
for the Outsider, or the launcher's feature list). Release notes: `docs/VR-v1497-RELEASE.md`.

## Arms (both games)

| # | Do | Expect | Off switch |
|---|---|---|---|
| 1 | Stand beside a wall, lean your head toward it (the game leans), then move the arm on that side: down by your side, forward, up, the hand to your chest with the elbow out | The elbow turns away from the wall instead of entering it; your hand stays where your controller is; no shaking while you hold still | `no-arm-collision` |
| 2 | Push your hand into an inside corner (one wall in front, one at the side), then raise and lower it | The elbow stays out of the side wall; moving along the corner the elbow glides, it does not snap | `no-arm-collision` |
| 3 | Reach through a door frame or over a table with the elbow low | The elbow bends around the frame / table edge | `no-arm-collision` |
| 4 | Stand normally at a wall and move your arms around | Nothing changes from the previous build (the body keeps your arms clear) | — |
| 5 | Hold a sword or gun in one hand and sweep it through your other forearm | It stops at the forearm instead of passing through | `no-forearm-contact` |
| 6 | Choke or carry a guard | Your arms are not pushed by his body | — |

## Holsters, grenades, mines and the Heart

| # | Do | Expect | Off switch |
|---|---|---|---|
| 7 | Hold a grenade or a mine (Outsider: the Hook Mine), bring it slowly to the front of your belt and let go | It goes back into the inventory (no drop, no throw); a tick | `no-gadget-stow` |
| 8 | With the hand empty, squeeze at the front of your belt and pull out | The last grenade/mine you put back is in your hand | `no-holster-draw` |
| 9 | Let go of a grenade or mine at normal speed away from the belt, and throw one | Dropped / thrown as before | — |
| 10 | Dishonored 2: let go of the Heart at your left chest (or a hip) | It is put away | `no-heart-pocket` |
| 11 | Dishonored 2: squeeze at your left chest and pull out | The Heart is back in your hand | `no-heart-pocket` |
| 12 | Look down while turning your head: reach over either shoulder for the sword | The holster is where your shoulder is drawn, also when you look down | `legacy-shoulder-zone` |
| 13 | Dishonored 2: holster the pistol at the hip, look down at it, then walk, sprint, strafe and turn | The pistol stays on your hip; it does not trail or shake | `legacy-hip-place` |

## Fingers (both games)

| # | Do | Expect | Off switch |
|---|---|---|---|
| 14 | Put your open hand flat on a wall, then squeeze the grip a little | The fingers lie flat on the wall and stay open on it; no finger inside the wall; the thumb not inside it either | `legacy-finger-surface` (opening), `no-finger-put-out` (sinking), `legacy-finger-timing` (timing) |
| 15 | Slide the flat hand along the wall slowly, then fast; walk sideways with it on the wall | The fingers stay level and still on the surface; no flicker | `legacy-finger-probe-order`, `legacy-finger-plane`, `legacy-finger-surface` |
| 16 | Lower your hand quickly onto a table | The fingers stop on the top, they never dip in | `no-finger-put-out` |
| 17b | Press the back of your hand on a wall, then squeeze a little | The fingers straighten along the wall instead of closing | `legacy-finger-surface` |
| 17 | Rest the hand on a table with the fingers past its edge | The fingers past the edge droop over it; the thumb stays on top | `legacy-finger-slab` (edge), `legacy-finger-surface` (thumb) |
| 18 | Make a fist and open it in the air; point; thumbs up | Quick and natural, not sluggish; no overshoot | `legacy-finger-timing` |
| 19 | Grab a bottle, a sword hilt, a body | Holds look as before | — |

| 20 | Load a save during play (pause menu, load) | No crash; afterwards the belt offers nothing until you hold a gadget again | — |

## Regressions to look at
- The grip power cancel, thrown mines sticking, the Hook Mine lethal switch (trigger hold), drawing the sword from the shoulder:
  all passed again in the simulator. A quick try of each in the headset is enough.
