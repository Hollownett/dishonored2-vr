# Why the held weapon was not rendered as a normal object

Status: v1019-era investigation, 2026-11-05. The user asked why the weapon cannot be drawn like any
other world object, given that the black left-eye / flickering right-eye artifact only affects it.

## The timing that forces the current design

The engine builds a model's per-instance transform (`model+0x1860` -> the MVP rows in the
per-instance constant buffer) during the frame's scene setup, from the arm palette. The VR hand that
a render actually draws is solved at that render's **eye-0 begin**, after setup: the solve needs the
game's tick data (body root, arm animation, hand clamp), which does not exist at setup time.

So the engine always draws the weapon from the *tick* hand. To keep the weapon in the render-time
hand, round 6 rewrote every weapon draw's MVP rows (`VP(eye) * B * inverse(F) * M`), per eye, at the
draw hook. That is the only object in the frame whose current clip position, previous clip position
(motion vector), and colour are authored by the mod separately.

## Why that rewrite is what breaks the image

`advance_attachment` already moves an attached item to the last drawn hand plus one tick of hand
motion (`full_body.item_advance`). In the v1017 headset session (`%LOCALAPPDATA%\DishonoredVR\
artifacts\xr-live-status.json`, 16:46) the remaining distance between the engine's frame and the
render hand was:

| hand | weapon | engine-vs-bound gap | angle |
|---|---|---:|---:|
| 0 | native pistol | **1.030 mm** | 0.000 deg |
| 1 | crossed sword | 22.054 mm | 4.875 deg |

So for the pistol the per-draw rewrite was moving it by about a millimetre - and in exchange it
wrote depth and motion vectors that the engine's own FSR 4 history did not expect. FSR/TAA reproject
the previous frame by those vectors; a near, fast object with vectors authored outside the engine's
path is exactly where a wrong reprojection reads as black (left eye) or flicker (right eye). The
weapon is the only such object, which is why only the weapon shows it.

## The change

`ObjectMotion::weapon_mode` now defaults to **0**: the weapon is drawn by the engine at its own
transform, both eyes identical, with the engine's own depth, motion vectors and colour - a normal
object. The round-6 binder/placement stays in the DLL but is opt-in with `build/weapon-draw-place`
(A/B and lab); `build/no-weapon-draw-place` remains a hard off.

For a build that predates this default, the same switch is the file
`%LOCALAPPDATA%\DishonoredVR\build\no-weapon-draw-place` (it is read every 2 s). It is set in the
installed mod home now.

Trade-offs, stated honestly:

- The residual engine error is now visible instead of corrected: ~1 mm for the native pistol,
  ~22 mm / 4.9 deg for the crossed sword in the measured session.
- A mod-spawned held copy (dual wield) is teleported from the last drawn hand at game-tick rate, so
  it can trail a fast hand by up to one frame. Native weapons use the engine's item advance and do
  not. If the copy trail matters, the follow-up is one tick of hand-motion prediction in the copy
  service (`native_weapon_hands.hpp`), not a return to per-draw patching.
- `arm_flicker.weapon_draw` is all zeros in this mode by design (`published [0,0]`); the weapon is
  not placed by the mod.

## How to verify

1. The game must be started fresh (the knob is read at startup by `place_weapons`, but
   `no-weapon-draw-place` is polled, so a live session picks it up within 2 s).
2. Hold the native pistol, look at it with each eye in turn, then move it fast. Expected: no black
   in the left eye, no ghost trail, the pistol remains in the hand within a millimetre.
3. If the pistol now sits visibly off the hand, the engine's item advance is worse than the session
   measured and the next step is prediction in `advance_attachment`, not the draw patch.
