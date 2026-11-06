# VR v1022 (2026-11-05) — the shoulder carry stays on the shoulder

v1019 put a body placed on the shoulder onto the **game's own body carry** and that worked: the first
headset session's status shows the engine offering the Carry action (`shoulder_carry.offer: 112`) and
the body going onto the shoulder. The body then fell off at once. This release fixes that.

## The cause (from the session's own status)

```
shoulder_carry: {placed:24, presses:27, started:1, loot:0, failed:7, takeoffs:1, offer:112}
sword_grip.carry_release: [45, 1, 24]
native_carry_auto_drops: 45
```

`native_carry_auto_drops` equals the carry drops (45): the v637 rule "a native body carry the mod did
not start is put down at once" was still running. It presses the game's **Use**, and the game's Use on a
carried body is its **Drop** — so on the first frame of every carry it dropped the body the placement
had just handed over. The v1019 latch that should have excused the placement's carry only fired once
(`started: 1`), because it depended on the dock state, which the render thread clears as soon as the
game takes the body.

## The fix

- **The v637 auto-drop is off by default.** `build/legacy-corpse-auto-drop` restores it for testing.
  Nothing in the mod presses Use while a body is carried unless the player asks for a drop.
- **The carry is recognised by the held Use that started it** (`shoulder_press_at`, within 3 s), not by
  the dock state, so it can never be mistaken for a foreign carry.
- **The held Use is kept for 700 ms once started** (`shoulder_carry_min_hold_ms`) even while the locked
  query answers empty, so the engine gets one continuous hold (v613 measured ~0.55 s to its carry; the
  previous 200 ms target grace was far too short). 7 of the 24 v1019 placements never started a carry;
  this is the main lever for that.
- Also: `legacy-corpse-carry` is now read **before** the shoulder knobs that use it (the v1019 order made
  that knob ineffective).

To take a carried body off: the v636 gesture is unchanged — squeeze at the shoulder and carry that hand
away (the game's Drop), or a throwing motion (the game's Throw). A plain Use on the body while carrying
is the game's Drop too, as in the native game.

## Build and evidence (2026-11-05)

- `build/d2vr_render_probe_arm_v1022.dll`, 14,903,808 bytes, SHA-256
  `f3e1990a86ac61f703223e55ef3897a5932838393a69aaa3844010e7243917b3`, built by
  `build/compile-v1022.cmd` (`dll_exit=0`).
- `artifacts/repair-v1022-test-candidate.json` (newest candidate; its sha256 matches the DLL).
- PASS: `shoulder_carry_check` (new minimum-hold case), `grip_decision_check`, `held_release_check`,
  `shoulder_carry_routing_check` (now also pins "no auto-drop unless the knob is present" and the knob
  read order).
- The launcher: `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1022.exe`, 83,182,997 bytes, SHA-256
  `2e69ab39e08b1177cb04ec0214810f5714c020337358f4075b9876f5d4eaeaf6`, published with
  `dotnet publish launcher -c Release`; the publish log printed
  `Embedding d2vr_render_probe_arm_v1022.dll from repair-v1022-test-candidate.json`. A
  `--page play --screenshot artifacts/v1022-launcher-play.png --exit` run shows
  `Mod  d2vr_render_probe_arm_v1022.dll (verified)` and installed it into
  `%LOCALAPPDATA%\DishonoredVR` (its `build\d2vr_render_probe_arm_v1022.dll` matches the hash above).

## Not verified

- No headset run of v1022. The drop cause is established from the v1019 session's counters, not from a
  new session.
- The start rate (`shoulder_carry.presses/started/failed`), the carry pose, the view blocked by the body,
  the tracked arms against the game's animated carry arms, the Drop/Throw gestures on the game's carry,
  and Sokolov's objective/skiff.

## What to check

1. Put a body on a shoulder: it must stay there. `native_carry_auto_drops` must stay **0**, and
   `sword_grip.carry_release[0]` must not grow while you carry.
2. `shoulder_carry.started` should now grow with `placed`; note `presses`/`failed`.
3. Take it off with the shoulder squeeze or a Use press; it should go down, not fall.
4. Sokolov: carry him out; the objective/skiff should react.

The full routine is still `docs/VR-v1019-TEST-CHECKLIST.md`.
