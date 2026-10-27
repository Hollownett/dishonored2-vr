# v636 test build: corpse drag, physical shoulder, thumb

Headset status: not run. Built on v631 (the v628-v631 corpse and carry repairs, never headset-tested). Feedback from the v627 headset run:
- the hold on a corpse dropped once the player walked;
- the player wants to put a body on the shoulder with the hands;
- the thumb on the controller sank into the palm.

The user chose a physical shoulder: the game will not take a body held up in the air into its own carry (sim v632).

## Changes

- **Thumb.** A touched thumb sensor asks the thumb to close fully, and with the index and middle fingers open nothing stopped it, so it went into the palm. It now closes halfway and lies beside the index on the controller. Once a finger folds, the thumb lies over it as before, so the fist is unchanged. Hand lab: `artifacts/hand-lab/thumb-before` vs `thumb-after`.
- **Dragging while walking.**
  - The runaway guard now measures limb speed against the holding hand. At a sprint, a limb whipping past 12 m/s in the world had made it let go and lock the body for 1 s.
  - The pelvis tow adds the hand's speed away from the body, up to 8 m/s instead of 4.
  - The tow no longer stops while the limb motor is at its force cap. In sim v632 the cap bound on 43 of 126 writes of a walking drag, and each of those writes switched the tow off.
  - When the hand stops, it holds the coasting body back.

  `tools/corpse_drag_lab.cpp` walk scenarios: at a 3 m/s walk the lag stays at 3-7 cm. Sim v633 walking drag: no lag release.
- **Physical shoulder (`grip_decision.hpp` ShoulderDock).**
  - *Putting it on:* hold any part of a body, lift it to either shoulder and let go there. The hold must be at least 0.25 s old, with the grasp point within 0.6 m of the hand.
  - *On the shoulder:* the hold stays, pulled to the point where the hand let go. That point is fixed to your head position and heading, so the body comes along when you walk, turn or crouch, and the hand is free again.
  - *Taking it off:* a fresh squeeze of that hand at the shoulder takes the body back into the hand. That squeeze is never the game's Use. From the hand, letting go puts it down and a swing throws it, as with any held body. The other hand can also grab the body and carry it off; pulling it away hard releases it.
  - The game's own carry is no longer started by the mod. The v613 rule that a torso held 0.5 s goes onto the shoulder is gone.
- A right hand holding a body at the right shoulder is no longer taken by the sword holster.

## Headset checks

1. **Thumb.** Hold the controllers normally with the thumb on the thumbrest, stick or buttons and the fingers relaxed. The thumb should rest on top, not inside the palm. Make a fist: the thumb should wrap over the fingers.
2. **Dragging.** Drag a body by a wrist, an ankle and the collar while walking, then sprinting, then turning (snap and smooth). The hold should not drop. When you stop, the body should stop near you, not slide into your legs.
3. **Putting it on.** Lift a body to your right shoulder and let go, then try the left shoulder, then lift with two hands.
   - It should stay where you put it.
   - Walk, turn and crouch: it should come along.
   - Note how much it blocks the view, and whether it pushes you or jitters.
4. **Taking it off.** Squeeze at the shoulder: the body should come back into your hand. Lower it and let go, or swing to throw it. Also try taking it off with the other hand.
5. **Doors.** With a body on the shoulder, use the free hand on a door.

Diagnostics:
- In the status: `sword_grip.carry_release` = [native carry drops, throws, shoulder docks]. The corpse diagnostics `releases` say why holds ended (`lag`, `runaway`, `open_fist`, ...).
- `native-physical-grab.csv` logs the held bone and its target, including while docked.
