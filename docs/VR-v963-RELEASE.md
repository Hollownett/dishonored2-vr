# VR v963 (test candidate, 2026-11-03)

The v962 source plus the four issues from the user's headset session and the requested sword power attack. No headset or
sim run: the DLL builds and the listed lab checks pass.

## 1. SMAA is a real, separate anti-aliasing method

- The v962 pipeline ran SMAA **before** CAS sharpening. The default sharpening (Medium) steepened every edge SMAA had
  just smoothed, which is why the Edge anti-aliasing row looked like it did nothing.
- v963 sharpens first, then SMAA is the **last spatial step**.
- SMAA now runs only when the eye has **no temporal AA** (`effective_temporal_aa() <= 0`, i.e. Anti-aliasing **Off** with
  Edge anti-aliasing on). It is never stacked on Game TAA, FSR or DLSS; those modes already skip it.
- The settings rows say this: the Off mode is "Off (SMAA when Edge AA is on)", and the Edge row says SMAA runs on its own
  in that mode and is skipped in a temporal mode.
- Lab: `eye_antialiasing_check` (edge coverage, independent eyes, resize, state restore) and `alternate_eye_check`
  (the gating: SMAA only for effective 0).

## 2. Talk and pickpocket on the hand's trigger

- New launcher feature **Talk and pickpocket on the trigger** (default on; off writes `build/no-npc-trigger`).
- A free hand whose trigger is pressed while the game offers **Talk** (declaration+0xe8) or **Pickpocket** (+0xc0) takes
  that offer as a 220 ms native Use on the hand's trigger. The press is consumed: it never fires a gun, casts a power,
  or blocks. The claim lasts until the trigger is released.
- The **grip** still does doors, keyholes, loot and the deliberate neck choke. It no longer presses Use on an NPC, so a
  grip near a person cannot turn into a choke by accident.
- Y still talks (a second route); the game's Talk prompt glyph now says **Trig** (feature off: **Y**).
- The existing `no-npc-trigger` knob is the feature flag; the launcher writes it.

## 3. Keyhole peek while holding the grip

- A door is the one native Use target where a **held** Use is the peek. While the grip is squeezed and the hand's live
  target is a door entity (vtable 0x1cb7c68), v963 keeps Use down until the grip is released.
- Every other held grip stays the v691 200 ms Use press: a held pad X is the game's holster/draw toggle on non-door
  items, and holding it there drew the sword instead of reading a newspaper.

## 4. Weapon black flicker under TAA/FSR/DLSS

- The v930-v962 draw-time placement writes both the current and the previous MVP rows of a held weapon. For a **tagged**
  draw (the engine could not certify its eye; 35% of the v951 headset's placed draws) the previous rows were set equal to
  the current rows, i.e. zero motion.
- Temporal AA then reprojected the weapon's history from its own pixel while the hand moved: it sampled the background
  the weapon had just covered. That is the dark/black, untextured-looking flash.
- v963 uses the hand-relative previous world frame under the current view
  (`vp * previous placed * inverse(current tick frame) * model`). It is the weapon's own motion; it is zero only when the
  hand really did not move relative to the camera, not merely when a view was missing.
- Lab: `weapon_draw_place_check` now also checks this fallback (previous rows match the hand's previous placed frame and
  are not zero).

## 5. Sword power attack (hold the trigger to charge)

- With the sword drawn and held, holding the hand's trigger still for up to 0.8 s charges a strike; three haptic pulses
  mark the thirds. A real swing (hand speed >= 2.5 m/s), a sheathe, a menu/wheel or losing the grip cancels it.
- Releasing past 0.6 fires the game's **heavy attack** (an AttackPress held for 340 ms, instead of the ordinary 120 ms)
  and powers the next sword contact to **25 damage** (the player's sword decl is 10). A stealth kill still uses its 1000.
- Lab: `sword_charge_check`.

## 6. Heart and the opposite hand; wheel state at open

- The wheel now captures what slot 1 held, and whether the other hand held a power/gadget, when it **opens**. v962 read
  those at the commit, after the game may already have changed the item, so the displaced gun was never restored.
- The other hand keeps its sword or its own mod-owned weapon. A displaced gun no longer replaces a weapon in the opposite
  hand when the Heart takes slot 1 (the user: "the weapon in the opposite hand disappears").
- Lab: `hand_loadout_check` (Heart case and the guard for an occupied other hand).

## Checks

- These pass: `sword_charge_check`, `hand_loadout_check`, `weapon_draw_place_check`, `vr_controls_check` (including the
  new NPC-trigger/keyhole policy), `alternate_eye_check`, `eye_antialiasing_check`, `gun_grip_check`, and
  `controls_guide_image_check` (the three guide pages with the new bullets, both characters, all art cases).
- The DLL builds (MSVC 2022, /W4; the DLL link keeps the project's warning-only policy).
- Not run: the game, the sim and the headset. The guide/README text and the settings wording are changed, but the actual
  prompt, the charge feel and the keyhole hold need the headset.
