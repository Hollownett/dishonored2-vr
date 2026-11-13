# VR v1276 (release, 2026-11-13)

The Timepiece works in VR, things you hold stay in your hand while you walk and run, and performance mode is now a main
launcher setting. Headset-tested by the author on v1276: the Timepiece display and held props. Everything else in this list was
built and checked between v1132 and v1276; say what you see if something is off.

## 1. The Timepiece (A Crack in the Slab)

| change | what you see |
|---|---|
| Raising the Timepiece no longer breaks the eye pair (v1244-v1248) | no black screen and no flat panel in Stilton's manor |
| The lens is drawn once for each eye, from that eye's own position, with its own shadows (v1257) | the other timeline through the glass has real depth and is lit like the rest of the scene |
| The lens keeps following you while you turn and walk (v1269-v1273) | the picture in the glass no longer drops back to a flat image when you move |
| The device sits on the left grip like the other left-hand items: each squeeze unfolds or folds it; the left trigger is the time shift (v1252-v1254) | |
| Something held in the other hand is drawn in both eyes while the device is open (v1259) | |

While the device is raised the game renders the other timeline twice, so the frame rate is lower than with it folded.

## 2. Things you hold stay in your hand

| change | what you see |
|---|---|
| The game itself now draws a held prop at the pose of the hand you see, carried by your own movement every frame (v1258-v1274) | an ash tray, bottle or book no longer flickers between two positions; it stays put while you walk, run, start, stop and turn |
| Severed limbs go through the same path (v1266-v1274) | a held arm, leg or head stays in the hand on the move |
| Held props have their shadow placed with them (v1236-v1239) | |
| A helmet on a carried head moves with the head (v1237) | |

## 3. Performance mode (experimental)

Performance mode draws one eye per frame and rebuilds the other from the previous frame. It is now on the launcher's main
settings page instead of the Features list, marked experimental: some visual artifacts may appear, mostly around moving hands and
objects.

| change | what you see |
|---|---|
| Moving objects, hands, arms and held items are re-projected from the game's own motion instead of being left where they were (v1150-v1216) | far less doubling on things that move |
| Camera cuts and teleports are rebuilt from the fresh eye (v1170-v1172) | no frame of the old place after a blink |
| The bright band on the right and bottom edge is gone (v1168-v1174) | |
| The spyglass scope holds full two-eye frames (v1222) | |
| The selected anti-aliasing (FSR, DLSS, game TAA) keeps working per eye | |

## 4. Bodies, takedowns and prompts

| change | what you see |
|---|---|
| Grip picks up any body the hand is on, from 28 cm, also with a gun drawn; loot is on the trigger (v1141-v1147) | bodies are easier to grab |
| An unlooted body on the shoulder is looted, then carried (v1142) | |
| Kill and choke offers are held for half a second and their art stays through gaps (v1141-v1146, v1233) | steadier takedown prompts |
| A held or re-pressed grip takes the game's choke in combat (v1232-v1235) | |
| The carry prompt is offered while the hand is on a downed body, with its own picture per kind (v1228-v1242) | |

## 5. Smaller things

- The weapon wheel has no black frame, only the ring (v1144, v1145).
- The moon in the tutorial is round (v1141).
- Tracked arms hold their last pose after a hitch instead of showing a frame of the game's own arm pose (v1213, v1214).
- Arms no longer slip against the body on a stick turn (v1247).
- A gun caught while the game is still putting it away stays the same gun (v1224, v1225).
- A copied upgrade that cannot be attached is rolled back instead of crashing (v1145).

## Kill switches

Empty files in `%LOCALAPPDATA%\DishonoredVR\build`:

| file | effect |
|---|---|
| `legacy-held-render-goal` | held props and limbs as in v1256 |
| `lens-mono` | one lens image for both eyes (v1256) |
| `lens-shared-shadows` | lens shadows as in v1256 |
| `lens-follow-prepared` | the older test for whether the lens follows you |
| `no-lens-isolation` | lens draws handled as in v1257 |
| `no-afw2` | the older performance-mode warp |

## Known issues

- **Crossbow bolt type can be wrong:** after picking a weapon up from the floor or tossing a weapon from hand to hand, the
  selected bolt type may not be the one you chose.
- **Performance mode is experimental:** expect some artifacts around moving hands and objects.
- **Two-handed holds and carried bodies** do not use the new held-prop path yet and can trail the hands while you move.
- **The Timepiece costs frame rate while it is raised** (two extra views of the other timeline).
- **Crossbow aim in the tutorial's firing range looks wrong:** the tutorial puts an invisible wall in front of the range. Outside
  the tutorial the crossbow aims normally.
- Exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).

Includes v1132 (PSVR2 Sense, Index and Vive controllers) and everything before it.
