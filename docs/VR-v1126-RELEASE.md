# VR v1126 (release, 2026-11-10)

The game's tutorial can now be finished in VR. Headset-tested by the user on v1126 (tutorial completed).

## Tutorial and combat

| change | what you see |
|---|---|
| The tutorial hands Emily a fresh sword when the combat lesson starts (its own loadout, a game draw). The mod blocked every sword draw the game makes by itself, so that sword never arrived and the shoulder draw had no sword to take. A game draw of a sword the right hand has no record of now passes; the mod puts it away at once (hold-to-hold) | the sword can be drawn from the shoulder in the combat lesson |
| The sword-in-front Block waits until the sword is really in the hand. Block with an empty hand is the game's own sword draw, which the mod refused, and the refusal put away the sword that was just being drawn | a grabbed sword no longer drops out and vanishes |
| Right stick pushed down holds crouch for as long as it stays down (it was a short tap) | swimming: hold the stick down to dive, push it up to rise; on land crouch still switches on the first push |

## Known issues

- **Parrying is hard.** A parry is a fresh Block press just as the enemy swings. With the sword in front of you Block is held the
  whole time, so the tutorial's parry step takes several tries.
- **Crossbow aim in the tutorial's firing range looks wrong.** The tutorial puts an invisible wall in front of the range; bolts and the
  crosshair stop on it. Outside the tutorial the crossbow aims normally.
- As v1123: exiting to desktop ends in a fault inside the Virtual Desktop OpenXR runtime's teardown (harmless, saves are written first).
