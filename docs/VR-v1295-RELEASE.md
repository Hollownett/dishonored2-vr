# VR v1295 (release, 2026-11-13)

Performance mode no longer loses frame rate when you hold weapons, each crossbow and pistol keeps the ammunition you chose
for it, and a held weapon moves calmly against walls and corners. Headset-tested by the author up to v1291: frame rate with
weapons in performance mode, ammunition per weapon, weapon parts placed in the hand. The last change (v1295, weapon parts in the
rebuilt eye), the hound carry and the Timepiece stereo change were checked in simulation or offline only; say what you see if
something is off.

## 1. Performance mode with weapons

| change | what you see |
|---|---|
| The player's arms and held weapons are written to the warp's mask and target without switching render targets for every part (v1286-v1288) | holding a weapon no longer costs frame rate; with two crossbows the frame time is the same as with empty hands |
| A dual-wield copy is recognised as a held weapon by the warp (v1281) | the second weapon no longer breaks up into blocks in the rebuilt eye |
| A weapon's loaded bolt and upgrade parts are found and placed in the hand with the weapon (v1290) | bolt and attachments stay on the crossbow instead of trailing it |
| Everything the mod places in the hand is treated as held by the warp (v1295) | bolt and attachments are rebuilt with the weapon body instead of tearing |
| The check that runs for every nearby draw call is far cheaper and takes no locks (v1284) | |
| A weapon's grip offset is remembered per weapon and restored the moment the weapon returns to the hand (v1277), and it is learned while you walk (v1284) | a weapon no longer sits beside the hand for a moment after a swap, a catch or crossed arms |

Performance mode is still marked experimental. Known: the bolt and attachments can still look thin for a frame when the weapon
turns quickly at the edge of the view.

## 2. Ammunition belongs to the weapon

| change | what you see |
|---|---|
| Every crossbow and pistol instance keeps its own ammunition type (v1279-v1282) | choosing a bolt with one hand changes only that hand's crossbow |
| A second crossbow chosen with the other hand starts with the bolt that hand picked; the first keeps its own (v1278) | |
| A weapon keeps its type through a toss, a catch and a change of hands (v1279) | |
| The game may switch a weapon's type by itself only when that type has run out (v1279) | picking up bolts does not change what is loaded |

## 3. Hands and items

| change | what you see |
|---|---|
| Only a power moves to the other hand when you choose something else; the Timepiece, the Heart, gadgets, guns and the sword are put away instead (v1284) | the Timepiece no longer jumps to the other hand |
| A held weapon pressed into a wall, an edge or a corner follows the hand smoothly: contact surfaces are remembered, the pose that moves the weapon least is chosen, and the weapon glides to it (v1289) | no more thrashing from side to side in a corner |
| A hound's body keeps the game's Carry, and a downed hound can be held in the hand like a prop (hound v1291, v1292) | hounds can be picked up and carried |

`legacy-weapon-contact` (an empty file in the mod's `build` folder) restores the old weapon contact.

## 4. The Timepiece

| change | what you see |
|---|---|
| The lens keeps its per-eye picture for up to half a second when the "lens follows the head" test fails for a frame (v1291) | in normal stereo the picture in the glass no longer drops to a flat image while you move |

## 5. Diagnostics

With Diagnostics on, the mod writes `weapon-perf.csv` (frame time, what each hand holds and the cost of each weapon path, twice a
second for the whole session) and counters for weapon contact, weapon binding and the warp's replay path into the status file.

## Not in this release

- The hand itself can still step a few centimetres in one frame when a gun hand touches a sharp edge.
- The warp's target outline is not available on every frame; the warp falls back to its mask on those frames.
