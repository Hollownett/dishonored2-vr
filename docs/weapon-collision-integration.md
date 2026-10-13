# Weapon / hand collision integration

## What exists
`src/weapon_collision.hpp` (pure, header-only, `d2vr::WeaponCollision`), checked by `tests/weapon_collision_check.cpp`. Nothing is wired into the DLL yet.

Per hand, per frame: `update(side, dt, anchor, grip, bladeDir, bladeLength, query)`.
- `anchor`: a point known to be in free space (chest or shoulder from `body_anchor`/`body_ik`). The palm sweep goes anchor -> grip.
- `query(from, to)` returns `optional<CollisionHit{point, normal (toward free space), fraction, entity}>`: first solid on the segment.
- Palm: stops one `palm_radius` (6 cm) off the surface. Blade: sweep from the corrected grip to the tip; the grip is pushed out along the normal by the tip penetration. Shift is capped at `max_shift` (45 cm).
- Hysteresis: contact stays latched for `hold_seconds` after the last hit, so there is no event/haptic chatter. The shift engages instantly (never draws inside the wall) and eases out at `release_speed` (1.5 m/s) after the controller backs out.
- New-contact result: `event{point, normal, speed (m/s into the surface), entity, blade, side}` and `haptic{amplitude 0.2-1, seconds 0.03-0.12}`, rate-limited by `haptic_cooldown`.
- `bisect_segment(from, to, blocked)` turns a binary "blocked" test into a `CollisionHit`. Its normal is only the reversed segment direction (approximate).

Applying the result: add `result.shift` to the tracked grip pose before it is written to the weapon attachment and arm IK wrist target. The aim ray and fire seam should keep the RAW controller pose, so you can still aim while the visual is clamped.

## Where the native query would come from (evidence from repo)
1. `0x95550` "occlusion" wrapper. It is used today by `NativeInteractionAim::service_obstruction` in `src/native_interaction_aim.hpp` (about lines 251-275) as `physical_occlusion_(sensor, entityBody, origin[3], axis[9], target[3], 0, &continuation)`. `(result & 0xff) == 0` means clear. It is a binary segment test from origin to target, so use it with `bisect_segment` (about 6 calls per hit; call it only for segments that pass a cheap AABB or velocity gate). It tests one specific body (`obstruction_request.body.entity`). Whether passing a different or null entity gives a general world test is UNVERIFIED.
2. `0x6c6760` direct ray: `direct_(manager, const InteractionRay*, entity*, float* distance, void* normal)`. It returns an entity node, distance and a normal (`interaction_hit_point`, comment at about line 74). This is the better source: a real normal and entity. UNVERIFIED: the normal's exact layout (float3 vs float4) and whether the ray filter (the flags in `f[11]`, validated as `(flags & ~7) == 0x3f000000` in `controller_interaction_ray`) lets arbitrary segments hit world geometry rather than interaction targets only. The 48-byte `InteractionRay`: origin f[0..2], f[3]=0, direction f[4..6], range f[7]. Verify in Ghidra before use.
3. Thread/stage: both are only called today inside the owned stage-0 sensor scope (`Stage0` hook with `context_`, `valid()` checks of sensor/camera identity, `logic_()==2`, not authored view). Reuse that same scope. Do NOT call from the render/XR thread.

## Integration points (patch sketch, not applied)
1. In `NativeInteractionAim` stage-0 (the same place `service_obstruction` runs), after `self->target(*scope)`, add a `WeaponQueryService` that, inside the validated scope, drains per-hand requests (`from`, `to`, token) published by the render-side and writes back the `CollisionHit` (or none) tagged with token and `GetTickCount64()`. Use the same freshness rule as `request_current` (`grab_pose_freshness_ms`).
2. Render-side, where `dxgi_probe.cpp` computes the tracked hand/weapon pose before `NativeAttachmentPose` and `ArmIk`:
   ```cpp
   // per side, per frame
   auto q=[&](Vec3 a,Vec3 b){return weapon_query.latest(side,a,b);}; // last fresh native result; posts request for the next frame
   auto r=weapon_collision.update(side,dt,body_anchor_pos,grip,blade_dir,blade_len,q);
   grip=r.grip; // also feed r.shift into the wrist target
   if(r.haptic) xr.pulse(side,r.haptic->amplitude,r.haptic->seconds); // GameXr::pulse, game_xr.hpp ~752
   if(r.event) combat_hook(*r.event);
   ```
   `GameXr::pulse` already guards focus and tracking.
3. `blade_len`: per weapon (sword about 0.9-1.0 m, pistol/crossbow about 0.25 m, bare hand 0). Source: `motion_sword*.hpp` / weapon attachment tables. I did not find a measured length in the files I read; treat as UNVERIFIED.
4. Add a VR setting toggle and `max_shift` tuning in `vr_settings.hpp`.

Because the native query results arrive from the game thread one frame late, the `query` adapter must reuse the last result for the same side (the sweep changes little frame to frame). A one-frame latency of a fast swing means a few cm of visible penetration; palm_radius and the instant-engage rule mask most of it. Alternative if latency is poor: run the query on the render thread with a pure-geometry cache of recent native hits (planes), which keeps this policy unchanged.

## Risks
- Native normal ABI and generic-world-ray behaviour are unverified (see above); the bisect fallback has only an approximate normal, which affects the push direction on slanted surfaces.
- Query cost: up to 2 sweeps per hand per frame (about 12 binary calls with bisect). Gate by distance to anchor or recent hit.
- Self-hits: the ray must exclude the player's own body and held weapon; the filter used by `0x6c6760` does so only for its interaction filter, so unverified for a world query.
- Clamping the visual while gameplay swings still use the raw pose can feel inconsistent; keep melee hit detection on the raw pose and use the events only for feedback.
- Only unit-tested against synthetic geometry. Headset comfort and visual quality are not verified.
- Mirror/one-handed two-hand grips: the policy is per hand; a two-hand weapon needs one shared shift (use the larger).
