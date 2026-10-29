# v686 test release: the game's own Hypatia cure message, and the hint it was hiding

The v685 session ended with no readable cure instruction and no injection. The evidence in
`%LOCALAPPDATA%\DishonoredVR\artifacts` from 19:21–19:31 explains both halves.

## What the session showed

- `native-hud-child-0.png` (the game's own prompt capture) reads **"Alexandria Hypatia (dead) /
  Grip Carry"**. The same save's status reports `cure_prompt_state: 0` and
  `cure_serum_detected: false`.
- `native-hud-child-11` (the tutorial slot) is a 499x38 strip whose maximum alpha is **3/255** and
  whose RGB maximum is **1**. It is not text.
- `native_message_publishes` was 726 and `cure_prompt_frames` 3815, so the blank tutorial proxy was
  being published every 250 ms and v685 treated it as "the game's own hint is on screen". That
  suppressed the English fallback. The headset therefore got neither the game's message nor the
  fallback.

## What changed

- **The spawned NPC is `grimalex`.** `models/characters/female/grimalex` is Hypatia's character
  asset and the entity is spawned as `ld_grimalex`. `is_hypatia_entity` only matched
  `hypatia`/`alexandria`/`crown`. A spawned grimalex could therefore be reported as "no Hypatia"
  while the game's own label said Alexandria Hypatia. It is now accepted.
- **A visible element at alpha 0 is not a proposal.** `publish_native_hud_messages` reads
  `_tutorialMessage.alpha` through the verified native getter ABI. Below .02 the element is a fade
  frame, not the message the flat player sees, and it no longer counts as the cure proposal.
- **A near-transparent capture is not artwork.** The native alpha-bounds reduction now ignores
  pixels with alpha <= 16/255. The 3/255 strip is `empty`; the blank art cannot set
  `native_tutorial_art_content`, so the readable fallback is published. The hint is **not**
  deleted — it is again the last resort when the game's own art is absent.
- **The cure keeps the native choke reach.** The cure is the game's non-lethal choke
  (`arkPlayerStateCureChoke`, `m_cureEquipment = grimalex/syringe.inventoryitem`). The private-view
  choke scope narrows the shipped 3 m gate to 2.25 m; near the living cure NPC it now uses the
  native 3 m, and returns to .75 elsewhere.
- **The serum scan reads the real item.** The filled item is `Pickup_IS_syringe_cure`
  (`grimalex/syringe.inventoryitem`). The scan now checks the declaration path and title id and
  the instance name, not only `declaration+0x48`.
- **Grip still supplies the game's own RB.** `cure_prompt_state 5` joins 2 and 4: when the game's
  own simple tutorial message is on screen, holding either grip supplies the native right-shoulder
  hold. The game still owns eligibility, the animation and the cure.

## Status fields

`xr-live-status.json` now also carries:

```json
"cure_native_message_frames": 0,
"native_cure_message": {"visible": false, "alpha": 0, "publishes": 0, "art_content": false}
```

`art_content: true` means a real native message image reached the tutorial slot and suppressed the
fallback. `visible: true` with `art_content: false` means the game reported the message but its
capture had no ink; the fallback is shown.

## Test (headset)

1. Load a save **before** Hypatia dies (the Current save at 19:27 is after the fight; the game's
   own prompt says `Alexandria Hypatia (dead)`). Finish the lab counter-serum first.
2. Approach her from behind. Expect either the game's own localized message or the English
   fallback **"Sneak behind her and hold … grip to inject counter-serum"**.
3. Hold that grip until the injection plays. Report `cure_prompt_state`, `cure_serum_detected`,
   `native_cure_message` and `cure_r1_frames`.
4. If the card says **Hypatia is dead**, that save is after the fight; load an earlier one.

## Limits

- Not headset-tested. The checkout has no pre-death save at Hypatia with the filled serum.
- If the native `_tutorialMessage` capture is still blank when the message is genuinely on screen,
  the fallback remains the visible instruction; the input path is unchanged.
- The choke factor is restored when she is no longer the nearby cure target, so ordinary chokes
  keep the accepted tuning.
