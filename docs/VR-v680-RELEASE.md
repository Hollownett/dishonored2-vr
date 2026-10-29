# v680 test release: the Crown Killer stealth injection and the correct grip

The cure can be given from **stealth behind the Crown Killer** with the game's non-lethal R1/RB
action. v679's block-only instruction was wrong and was never published. v680 fixes the prompt
and the input path for the stealth case.

## What changed

- The fallback card now says **“Sneak behind her and hold RIGHT grip to inject
  counter-serum”**, or **LEFT** when the sword is in the right hand (the mod gives the native
  non-lethal RB/choke to the free hand).
- Once the game offers the action it changes to **“Hold RIGHT/LEFT grip to inject
  counter-serum.”**
- The Crown Killer's stealth path can report a choke-sensor target without the
  `arkHUDElementInteractives` choke hint. For Hypatia only, v680 admits that exact sensor
  target as the same native R1/RB action.
- The stealth press accepts the raw held grip, because behind her back the hand may have no
  normal Use target.
- Only the filled cure item counts; the empty syringe no longer produces a false “serum”
  state.
- `cure_prompt_state`: `1` dead, `2` live offer, `3` no serum, `4` pre-offer stealth hint.

## Test (headset)

1. Load a save before Hypatia dies; have the **filled counter-serum**.
2. Crouch/approach her from behind. Expect the **“Sneak behind her…”** card even before the
   game's own hint appears.
3. Hold the grip named on the card (right normally, left with the sword out) until the injection
   plays.
4. If the card says **dead**, load an earlier save; the cure cannot be applied after death.

## Limits

- Not headset-tested. The stealth route is admitted only when the native choke sensor reports
  Hypatia; it is not a generic proximity action.
- The card is English; the game's own localized tutorial remains preferred when it appears.
- The mod still relies on the game's own cure eligibility and animation.
