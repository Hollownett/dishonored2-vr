# v675 test release: corpse Loot via the sensor's raw action offer

Fixes the v674 test failure on Vasco's note. v674's strict native Loot receipt never validated:
the v674 session left `body_loot_passes: 0` and `body_use_blocked: 578`, so the carry guard still
blocked every Use on the body. The game's own prompt was on the body throughout.

## Why v674 failed

v674 admitted a dead body's Use only when a full `native_talk_offer`-style receipt validated. That
receipt checks a long chain (selected reference → link → selected owner, component/selector
identity, sensor/machine/interaction ownership, and the primary/secondary sensor slots). One link
in that chain is not satisfied for a body's Loot interaction, so the receipt stayed empty even
though the engine offered Loot. The raw sensor state still held the action.

## What v675 changes

- Every stage-0 interaction cycle now records the **sensor's own action offers** for the hand:
  `sensor+0xa8` (primary) and `sensor+0xb0` (secondary), resolved to offsets from the target
  component's declaration. A body's Loot is the selector's primary at **+0x98**; Carry is the
  secondary at **+0x70**.
- The carry guard stands aside when the primary offer is **Loot at +0x98**, revalidated against
  the live sensor and the hand's player each time. The strict receipt is still accepted when it
  validates, but it is no longer required. A body whose primary is Carry at +0x70 keeps the v637
  hand-carry guard.
- New `corpse_loot` block in `xr-live-status.json`, per hand:
  `primary`/`secondary` offsets, `raw` (the raw Loot check), `receipt` (the strict check) and the
  target component's vtable RVA. It keeps the last cycle with a real offer, so a status sample
  after the attempt still shows it. This is the evidence to read even when the trace CSVs are off.

The native loot event remains the engine's: the mod does not grant items, call engine functions
or write interaction fields.

## Install

Exit Dishonored 2, run the v675 launcher, choose **LAUNCH VR**. v672 remains available for
rollback. The v673 palm-placed stun-mine/spring-razor change is included.

Built files in this checkout (hashes):

- `launcher\dist\DishonoredVRLauncher.exe` — `a8065280ab8f601b5479e19efcdf164a03df2abb7be8c840cd5121ec4a51e077`
- `build\d2vr_render_probe_arm_v675.dll` — `2a1ebacb7dd8aa03cfa03054eaa358bb39174d940a6109b362c333ad30a7fe99`

## Test (headset)

1. Load the save near Vasco's body. Aim the palm at the body/note and squeeze grip. Expect the
   body to be looted and the safe-code note to open as the left-hand tablet; no shoulder carry.
2. Check `artifacts\xr-live-status.json` after the attempt. `corpse_loot[side].primary` should be
   `152` (0x98) while the body is targeted; `raw: true`; `body_loot_passes` should rise.
3. If `primary` is `112` (0x70 Carry) instead, the engine is offering only Carry from the hand;
   send that status block (and the `native-hud-child-0` prompt capture, if present).
4. Loot an ordinary guard body and drag a body by a limb; a body with nothing to loot still must
   never be shouldered by a grip.

## Limits

- Not headset-tested. The raw primary-offer semantics are from the pinned selector
  (`0x747840`: Loot primary +0x98, Carry secondary +0x70) and the live v672/v674 offers.
- The offer diagnostics are always on and cheap (two pointer reads per stage-0 cycle); the trace
  CSVs remain off unless their knob is present.
- Focused checks pass: `grip_decision_check`, `native_talk_offer_check`, `gadget_aim_check`; the
  DLL built `/W4`.
