# Ranged aim and crosshair candidate

The render update now computes barrel correction, aim offset and the existing smoothers once, then applies the current hand-clamp origin guard. A validated float origin/frame receipt supplies both native shot publication and the reticle's 30-unit target. The reticle no longer independently samples the barrel, filter or clamp. The receipt is local to the render update and records player/camera, render frame, pose epoch and reference epoch; invalid input clears ranged/reticle intent.

A focused, tracked, active left hand can keep its ranged pose fresh while gaze retains native interaction/takedown camera aim. Authored action-camera ownership, grip/holster and melee restrictions still reject ranged takeover. Native launch arguments, target entity, spread and projectile physics are unchanged.

The exact live crosshair child draws at `0.65 × live HUD scale`. This changes its copied draw matrix, preserving the aim pivot and caller memory. Flat-HUD ancestor layout is undone for that owned reticle before applying its glyph scale, avoiding a second HUD-scale multiplication and shifted aim point. Panel and flat HUD paths use the same live setting, including 100% and larger HUD sizes. Unrelated, expired, detached or replaced movie/node bindings retain their native draw path.

Offline checks cover float launch/target reuse, invalidation, native setter behavior, live owner/movie/list identity, nested parent-to-child HUD transforms, pivot/argument/return preservation and existing smoothing. CPU fixtures do not establish visible alignment or comfort.

The accepted one-eye reticle placement remains. Setup order, latest view and Present parity do not establish which eye a native child draw belongs to, so no per-eye draw selector was added. Gun spread, crossbow gravity/trajectory, stereo parallax and headset visual alignment still need real evidence and user feedback after runtime restrictions are lifted. This is a source candidate, not a complete runtime alignment claim.
