# Bonelab-feel: Tier B simulated-VR test saves

The Linux simulated-VR rig (`linux/d2vr-linux sim`, see `linux/README.md`
section 9) needs a few hand-made saves so Tier B scenarios start in a known
placement. Saves live under `~/d2vr-test-saves/<name>/`.

## Making a save

1. Play to the spot described below.
2. Make a manual save, then quit the game.
3. Copy the `SLOT*` folder(s) and `profile.bin` from the game's `savegame/`
   directory into `~/d2vr-test-saves/<name>/`.
4. Validate: `python3 tools/linux/check_test_saves.py ~/d2vr-test-saves/*`
   (exit 1 and a clean message on any missing or incomplete save; a missing
   save never fails a build).

## Saves and the placement scenarios assume

- `tank-floor` (used by plan B): standing 0.5 m from a whale-oil tank on the
  floor, in front of the knees; a wall within 2 m ahead; open floor to the
  right.
- `corpse-dead`: a dead guard lying on open floor, right wrist 0.5 m in front
  of the knees, head toward the player's left, 3 m of free floor ahead, no
  other guard awake within 15 m.
- `guard-sleeping` (optional): an unconscious guard in the same placement as
  `corpse-dead`.
- `guard-awake`: an unaware guard standing still (or seated), upper body 0.4 m
  from the right hand's rest position, 3 m of free floor behind him, no wall
  within 2 m behind him, no other guard within 15 m.

## Running a scenario

```
linux/d2vr-linux sim --test-save ~/d2vr-test-saves/tank-floor --scenario <name> --status-field corpse --status-field held_clamp
```

Rule: scenarios need a settled corpse (woken by the squeeze, S1), so no
pre-wake step is required.
