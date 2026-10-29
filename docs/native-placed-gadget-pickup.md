# Picking a placed stun mine or spring razor back up with the hand (v684)

User report (headset v682): "fix mines UI pickup prompt, it is not displayed to me when I trying
to pickup with hands" — clarified as a **placed** stun mine / spring razor, with the pickup
itself "sometimes very hard".

## What the mod does with a world interaction

The mod routes the game's own stage-0 interaction query from the tracked hand and only changes
*geometry* (where the sphere and the LOS start). The game still decides permissions, action
type and the prompt/action it offers. Two gates belong to the mod:

1. **The hand sphere** (`NativeInteractionAim::sphere_owner_current`, 25 cm,
   `grab_acquire_distance` in `src/selection_policy.hpp`): a non-character owner the palm is
   on is kept as a target even when the head ray or the native eye LOS says otherwise.
   Its proximity reference was the owner's rendered static-mesh box
   (`owner_bounds`: `*(entity+0xf0)` typed `0x1b0dd88`), or a skinned box for an
   `arkSeveredLimb` (v638).
2. **The grip-Use gate** (`NativeInteractionAim::grip_use_target_body`): the same 25 cm test
   decides whether the squeeze may press the native Use on that target.

## Why a placed gadget failed both

A placed stun mine / spring razor is a small deployed object with no readable static-mesh box
on the entity's model link. `owner_bounds` therefore returned nothing, `sphere_owner_current`
returned false before the sphere was even measured, and `grip_use_target` hit the same
missing-box branch. The mine then became a target only when the hand ray itself landed on it
(the native selection changed), which explains both the missing prompt and the "very hard"
pickup — the prompt is world art published for a known target and flat prompt copies are
suppressed by design (`native_hud_keep_flat`).

This is exactly the v638 shape (`docs/VR-v638-RELEASE.md`), where severed limbs had no static
box either.

## v684

`NativeInteractionAim::sphere_reference` takes the readable model box when there is one and
otherwise the **native interaction point** of the same call:

- eligibility (`0x3b80a0` hook): the point argument of the call itself;
- LOS (`0x3b7b90` hook): the hit point the call reports;
- the hover offer and its revalidation carry that point (`SphereHoverOffer::point`,
  `SphereEligibility::point`), so a later ownership check measures the same reference.

The point is read with the guarded reader and must be finite; a missing or unreadable point
leaves the old behaviour (no sphere qualification). Nothing about native permission, the
character classifier, the 25 cm gate or the ranking changed.

`grip_use_target_body` uses the same rule through `target_point_locked` (the recorded native
contact point), matching `target_bounds`, which already had that fallback.

### Scope of the relaxation

The same rule therefore also covers any other non-character world usable whose box the mod
cannot read: the palm must be within 25 cm of the point the game itself reported for that
candidate, and the native permission/descriptor and FSM checks still decide whether the action
exists at all. Doors, levers and other static-model usables keep their readable box, so their
native reach and obstruction policy is untouched (`hand_pickup_in_range` is measured against
the same box as before). Character entities are excluded before the sphere is measured.

## Diagnostics

`sphere_point_fallbacks`: sphere references taken from the native point (model box missing).
`sphere_missing_reference`: sphere evaluations with neither a box nor a usable point (refused).
Both appear in `artifacts\xr-live-status.json`. If a mine still shows no prompt while
`sphere_missing_reference` stays 0, the game never presented that mine to the hand sphere and
the next step is the native sensor/action trace, not this sphere.
