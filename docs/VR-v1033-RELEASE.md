# VR v1033 (2026-11-05) — the carrying hand keeps its fingers

The shoulder carry (v1019-v1022) works: the body goes onto the game's own body carry and stays there.
This fixes the one thing the headset run showed: the hand that put the body on the shoulder went slack
— straight fingers — and only came back when the body was taken off.

## The cause

`NativeCarrySnapshot::hands()` reports a **carry of a body** as the right hand (`2`), because the game's
carry clip poses the right arm over the shoulder. The mod publishes that as an occupied hand:

```
hand_empty_bits = carry_hands ? ~carry_hands & 3 : ...      // = 1 for a body carry: the right hand is not empty
```

`body::Inputs::hand_empty` is the gate for the player's finger IK in `body_ik`:

```
procedural = !native || (weapon_incoming ? remembered : hand_empty[i]) || weapon_grip[i];
finger_mode = procedural ? (has_finger ? 1 : 2) : 3;        // 3 = the native clip's fingers
```

With the right hand "not empty", `procedural` was false for the whole carry, so the drawn right hand
played the **game's carry-clip fingers** — a relaxed, straight hand — while its position stayed the
player's. The arm itself was already the player's for `CarryCorpse` (`native_ownership` returns arms
`{0,0}` for it), which is why only the fingers looked dead.

## The fix

While the game carries a **body** (and no native object is held), the body inputs publish **both hands
as empty** and clear the mirrored-hand and gun-copy routing, so nothing re-takes the fingers:

- `finger_mode` is the player's again (`1` with a tracked finger pose, else `2`), for the whole carry;
- the carry clip no longer drives the hand's wrap or fingers;
- right after the body is put down the hand is unchanged (it was always the player's then).

The body itself is unaffected: it is bound to the player's spine by the game's `CarryCorpse` state, not
by the mod's hand.

## Build and evidence (2026-11-05)

- `build/d2vr_render_probe_arm_v1033.dll`, 14,926,848 bytes, SHA-256
  `fa2259d800afadb48c34f5dccd591111f7f26a96e4afb5d5919c78eaaa3915d1`, built by
  `build/compile-v1033.cmd` (`dll_exit=0`).
- `artifacts/repair-v1033-test-candidate.json` (newest candidate; its sha256 matches the DLL).
- PASS: `shoulder_carry_check`, `grip_decision_check`, `held_release_check`,
  `shoulder_carry_routing_check` (the shoulder path is untouched by this change).
- The launcher: `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1033.exe`, 83,189,632 bytes, SHA-256
  `649ac7f3813981005a8063ace4c66f1431f1024433704a0c699304949c38464d`, published with
  `dotnet publish launcher -c Release`; the publish log printed
  `Embedding d2vr_render_probe_arm_v1033.dll from repair-v1033-test-candidate.json`. A
  `--check-package` run into an empty home installed exactly `d2vr_render_probe_arm_v1033.dll` with
  SHA-256 `fa2259d8…3915d1`, and a `--page play --screenshot artifacts/v1033-launcher-play.png --exit`
  run shows `Mod  d2vr_render_probe_arm_v1033.dll (verified)` and installed it into
  `%LOCALAPPDATA%\DishonoredVR`.

## Not verified

- No headset run of v1033. The cause is the code path above plus the headset report; the fix is proved
  by the compile and the unchanged carry path, not by a new session.
- Finger curl while the other hand holds a limb of the body the game carries, and the carry pose
  itself, are unchanged and not re-tested here.

## What to check

1. Put a body on a shoulder: the placing hand's fingers must follow your grip/trigger while the body is
   carried (open when you open, closed when you squeeze), not sit straight.
2. The free hand is unchanged: fingers, props, doors, the sword draw.
3. Take the body off: both hands are as before (they always were the player's after the drop).
