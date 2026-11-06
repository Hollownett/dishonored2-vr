# v1019 headset checklist — corpse on the shoulder

What this build changes: a body you put on a shoulder goes onto the **game's own body carry**
(`arkPlayerStateCarryCorpse`), so a place that expects a carried body (Sokolov out of the Jindosh
mansion) sees a carried body. Read `VR-v1019-RELEASE.md` for the mechanism and its limits.

Use a save with a dead or unconscious body, and a save in the Clockwork Mansion with Sokolov.

## 1. The handoff

1. Grip a dead guard anywhere (chest, collar, arm) and lift him to your right shoulder. Let go there.
   - **Expected:** after about half a second the game takes him; he moves to the game's carry pose on
     your right shoulder. Your hand is free.
   - **If he just stays where your hand let go:** the game did not take him. Note whether the body's
     **Loot** prompt was on it (the engine offers Loot first on a body with items; loot it with a plain
     grip and put him on the shoulder again).
2. Repeat on the left shoulder and with two hands.
3. Walk, run, crouch, open a door with the free hand.
   - **Expected:** the body comes with you on the shoulder (the game's carry); nothing pushes you.
4. Take him off: squeeze at the shoulder and move that hand away.
   - **Expected:** he is put down (the game's Drop). A throwing motion throws him.
5. Try a body that has **loot** on it: put it on the shoulder without looting.
   - **Expected:** no carry (the status `shoulder_carry.loot` counts it); he stays on the physical
     shoulder. Loot him, then put him on again.

## 2. Sokolov

1. In the Clockwork Mansion, knock Sokolov out and put him on your shoulder.
   - **Expected:** the game's carry, as in the native game. Walk him to the skiff / the exit.
2. Check that the mission's own steps react (the objective, the exit, the skiff), and whether the
   guards react to the body.
3. Take him off at the skiff and put him down.

## 3. Regressions (only the shoulder path changed)

1. A plain grip on a dead or unconscious body still drags it by the bone; it never starts a carry by
   itself.
2. Looting a body's items with a grip still works (a body with a Loot offer is never carried by the
   mod's press).
3. A choke that ends with the body on the shoulder (not placed by you) is still put down at once.
4. The right-shoulder sword draw still works; the shoulder zone does not eat the draw.
5. A body dropped (not placed) still falls normally.

## Status keys

`artifacts/xr-live-status.json`:

| key | meaning |
|---|---|
| `shoulder_carry.placed` | bodies put on a shoulder (the dock) |
| `shoulder_carry.presses` | held-Use episodes sent to the game |
| `shoulder_carry.started` | placements the game took into its carry |
| `shoulder_carry.loot` | placements refused because the body's Loot was still offered |
| `shoulder_carry.failed` | placements where the game carried nothing before the window closed |
| `shoulder_carry.takeoffs` | placements the game's carry took over from |
| `shoulder_carry.offer` | what the engine itself offered the placing hand on the body: `112` = Carry (0x70), `152` = Loot (0x98), `-1` = nothing |
| `shoulder_carry.enabled` / `.native` | the knobs in effect |
| `sword_grip.carry_release` | `[carry drops, carry throws, shoulder docks]` (unchanged) |
| `native_carry_auto_drops` | native carries the mod put down itself (should stay 0 while you carry) |

Diagnostic files: `%LOCALAPPDATA%\DishonoredVR\artifacts\native-interaction-targets.csv` and
`native-physical-grab.csv` show the placing hand's target and the held bone.

## If the game does not take the body

Read `shoulder_carry` after the attempt:

- `placed` does not grow when you let go at the shoulder — the dock never engaged. Check `dock_diag`
  (a release while holding a corpse, in the shoulder zone, the hold 250 ms old, the grasp point within
  0.6 m), and whether the body's bone was actually held (the hand may have slipped).
- `loot` grows — the body still offers its Loot; loot it with a plain grip, then put it on again.
- `failed` grows and `started` stays 0 — the mod pressed the game's Use and the game did not take the
  body. This is the case v632 recorded ("a body held up in the air"). To confirm the engine still
  starts its carry on this save at all, create the file
  `%LOCALAPPDATA%\DishonoredVR\build\legacy-corpse-carry` and try a plain grip on a body lying on the
  floor: if the game carries it then, the press works and the shoulder position is what the engine
  refuses; report that. Remove the file afterwards.

Do not read PASS/FAIL from the checks as gameplay proof: they only prove the policy and the wiring.
