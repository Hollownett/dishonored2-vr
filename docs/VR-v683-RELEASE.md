# v683 test release: bottles and glasses are hand-range pickups

Bottles and glasses could be picked from across the room while food, ammo and
coins required the hand. Both are takeables, but the old pickup sphere was
skipped in the grip's gaze/reach fallback (and for any carryable whose class the
character classifier could not decode), so the native Use could select a bottle
with the head ray at the game's own Use distance.

v683 makes every takeable use the same 25 cm sphere around the tracked grip:

- **Loot** (`arkPickup`: food, ammo, coins) and **native carryables**
  (`arkCpntPickable_Simple`: bottles, glasses) share one gate.
- The sphere is measured from the **tracked grip centre**, never from the gaze
  pose that the grip fallback publishes as `volume_centre`.
- The gate applies in **hand and reach/gaze mode**. A takeable native Use is
  accepted only while the hand is on it; a bottle across the room no longer is.
- An unclassified carryable is still recognised and gated through its
  `Pickable_Simple` component.
- Doors, NPCs, guards and other world usables keep their native reach and
  obstruction policy; this change is only the pickup sphere.

## Test

1. Stand in front of a bottle or glass on a table. Do not reach for it.
2. Look at it and squeeze the grip. Confirm it is **not** picked.
3. Put the hand on it and squeeze. Confirm it is picked, as before.
4. Repeat with food, ammo and coins: near-hand pickup works, distant gaze does
   not.
5. Check that doors, levers, talk and body loot still use their native reach.

## Limits

- Not headset-tested. The focused sphere/LOS fixtures and the DLL compile pass;
  headset pickup feel is not established by those checks.
- The deliberate gaze fallback no longer takes loot or carryables beyond the
  hand sphere. That is the requested behavior, but it is a change from v682.
- The same working tree also carries an in-progress v683 placed-mine /
  spring-razor sphere-reference change; this candidate compiles it, but it is
  not part of the bottle/glass fix or its acceptance.
