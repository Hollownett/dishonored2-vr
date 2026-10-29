# v684 test release: placed stun mine / spring razor pickup prompt and grip

Fixes the v682 report: a stun mine or spring razor that was already placed on a wall or floor
showed no pickup prompt, and taking it back was "very hard" — it only worked now and then.

## Cause

The hand sphere is what lets a hand take something it is *on*, rather than only something its
ray happens to hit. It needed a readable static-mesh box for the object. A placed gadget has no
such box, so the mod never qualified the mine as a hand target: the game's own prompt was then
only shown while the hand ray landed exactly on the small mine, and the squeeze only took it on
that same frame. It is the same class of failure as v638's severed limbs (skinned models, no
readable static-mesh box).

## Changes

- **Placed gadgets are hand targets.** When no model box is readable, the hand sphere uses the
  native interaction point the game itself reports for that candidate — the exact argument of
  the same eligibility/LOS call, never a guess. Native permission, the character classifier and
  the 25 cm palm gate are unchanged.
- **The squeeze uses the same point.** The grip-Use gate required a readable box as well, so a
  prompt could have appeared while the squeeze still failed. Both now share the reference.
- **Diagnostics.** `sphere_point_fallbacks` counts sphere references taken from the native point
  (the fix in use); `sphere_missing_reference` counts sphere evaluations that had no reference
  at all and were refused. Both are in `artifacts\xr-live-status.json`.

## Test

1. Place a stun mine on a wall and a spring razor on the floor.
2. Reach for one with an open hand, *without* putting your head or the ray on it, then close the
   hand. Expect: the game's prompt appears over the gadget and the grip takes it back.
3. Report `sphere_point_fallbacks`, `sphere_missing_reference`, `world_prompt_frames` and
   `prompt_shows_hides` from `artifacts\xr-live-status.json`.

## Limits

- Not headset-tested. The focused fixture `tests/native_interaction_sphere_check.cpp` covers the
  new reference (inside the palm sphere, outside it, an unreadable point, and the grip gate) and
  passes; the DLL compiles with `/W4`.
- The fix cannot make the game offer an action it does not offer: if `sphere_missing_reference`
  stays 0 while aiming at a mine, the game never handed that mine to the hand sphere and the
  cause is elsewhere.
- This DLL was built from a shared working tree that also carries the v683 bottle/glass pickup
  sphere change (`docs/VR-v683-RELEASE.md`). Both changes are in this binary; testing one
  exercises the other.
