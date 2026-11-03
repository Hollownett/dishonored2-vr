# VR v980 (test candidate, 2026-11-03)

The v979 source plus the sword power attack the user asked for, now reachable at all. No headset or sim run: the DLL
builds and the listed lab checks pass.

## 1. Sword power attack on grip + trigger

- User (headset): *"when I hold grip and trigger with a sword in hand it should charge a power attack. please add it."*
- The charge itself was added in v963, but it could never start. The sword hand's trigger was also the attack gate's
  `explicit_block` (the v562 "right trigger blocks with the sword" rule), and `attack_allowed` requires
  `!explicit_block`. On the trigger's very first held frame the block closed the gate the charge runs on, the charge
  reset, and the press became a block. The lab check only exercised the pure timer, not the gate.
- v980 gives that press to the charge: while the hand holds the sword (the grip machine keeps it), the trigger is not a
  block, so the attack gate stays live. Grip + trigger held still charges for up to 0.8 s; one haptic pulse per third
  rises in the hand. A real swing (hand speed 2.5 m/s or more), a sheathe, a menu, the wheel or losing the grip cancels
  it. Releasing past 0.6 (0.48 s of holding) fires the game's **heavy attack** (an `AttackPress` held 340 ms instead of
  the ordinary 120 ms) and powers the next sword contact to **25 damage** (the player's sword decl is 10; a stealth kill
  keeps its 1000).
- **The trigger is not a block with the sword in the hand.** Blocking stays the sword-in-front pose (the game's own
  Block) and the right grip when it holds no prop. The docs (README, PLAYING.md, VR-CONTROLS.md and the design page)
  now say the same thing; PLAYING.md still promised a trigger block.
- A trigger already claimed by the NPC Talk/Pickpocket Use (v963) is never a charge. A sword hand's trigger never goes
  to that owner, but the claim is checked anyway.
- Lab: `sword_charge_check` now also pins the press policy (`sword_trigger_owns_press`) and the v963 failure
  (`attack_allowed` with `explicit_block`).

## 2. Diagnostics

- New status counter `attack_intent.charge_starts` (charge presses recognized), next to `power_attacks` (heavy attacks
  fired). If a headset log shows `charge_starts` but no `power_attacks`, the release was too early or a swing cancelled
  it; if both stay 0, the press never reached the charge.

## Checks

- `sword_charge_check` passes (the charge, its cancels, the press policy, the gate).
- The DLL builds (MSVC 2022, `/W4`, the project's warning-only policy).
- Not run: the game, the sim and the headset. The charge feel, the three pulses, the heavy attack's damage and how a
  held trigger now behaves while blocking need the headset.
