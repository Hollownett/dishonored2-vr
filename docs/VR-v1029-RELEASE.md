# VR v1029 (test candidate, 2026-11-05)

Flat props stop being gripped as if they were handles. A wide, thin thing — a book, a folder, a plank, a sheet — is not closed on: it lies on the palm,
so the hand now lies on it.

## Why (user, headset v1024: "grip is unnatural on books")

v1024 sized the grip ball as the prop's thin side. For a book that is a 2 cm radius ball, and the fingers wrap a 4 cm ball at the palm: a fist, with the
book's cover through the knuckles (renders `artifacts/hand-lab/gripball-v1024/book_ball-views.png`). The ball is the right answer for a shaft — a bottle,
a candlestick, a spoon's handle — but a wide flat face is not something a hand wraps at all.

## What v1029 does (`src/finger_sphere.hpp`, `src/native_grab.hpp`, `src/dxgi_probe.cpp`)

- `grip_is_flat`: at most 3 cm on the thing's thinnest side, at least 4 cm on its middle side, and the thin side at most half the middle one. A book
  22x16x4, a folder 32x22x2, a thin plank: flat. A bottle 7 cm across, a candlestick's 2.2 cm shaft, a spoon's 1.5 cm handle, a head, a crate: not flat.
- A flat hold gets **no contact shape at all** and a relaxed reach (`grip_flat_reach = 0.35` of the hold's closed curl): the four fingers rest slightly
  curled, the thumb alongside, as a hand lying on a book. Nothing is re-read per frame, so nothing can twitch, and the solver has one shape fewer than the
  ball — this is also the cheapest grip.
- The decision is made once at the bind from the thing's own local box (`NativeGrab::begin_hold`, `lane.flat`; `hold_flat(side)` for the render thread).
  `build/no-grip-slab` restores the v1024 ball for flat props; `build/legacy-sphere-grip-whole-object` still restores v642.
- Status: `finger_ik.hold[6]` counts the frames a physical hold drew the flat grip (`finger_flat_holds`).

## Evidence (offline; no headset run)

- `finger_sphere_check` passes. Flat/not-flat table (book, folder, thin plank, wide sheet flat; candlestick, bottle, spoon, head, crate, thick plank not),
  and the relaxed hand: mean flexion of the four long fingers **30.5 deg** with `grip_flat_reach`, against **67.3 deg** on the book's 2 cm grip ball and
  **79/60 deg** for a fist — the hand lies on the book instead of closing through it.
- Hand lab, same rig and solver: `artifacts/hand-lab/flat-v1029/book_flat-views.png`, `folder_flat-views.png`, index.html (the v1024 ball renders are in
  `artifacts/hand-lab/gripball-v1024/` for the comparison).
- `finger_sphere_routing_check` and `eye_capture_routing_check` pass.
- DLL: `build\compile-v1029.cmd` (MSVC 2022 x64 `/W4`), dll_exit=0, no new warnings; 14,921,728 bytes, sha256
  `7fea3e9e3468165a96811af703236b14046cc6fa8e7988fc4f56232ca4f4adc5`.

## Limits

- **Not headset-tested.** How a flat hand reads in the headset is what this build asks.
- The hand's own pose is still the player's; only the *fingers* relax. If a book still reads wrong, the next step is to lower the hand's grip-drawn offset
  for flat props, not to add shapes.
- A very large flat prop (a 2 m plank) and a book grabbed by its spine edge both use the same relaxed hand: there is no per-face analysis.
- Flatness is decided from the box read at the bind; a prop whose box is unreadable stays on the ball.
- The build also carries the worktree's other in-flight changes (parallel session) and no full `tools\build-msvc.cmd` pass (pre-existing `/WX` stops).

Checklist: grab a book, a folder or a note sheet, then a bottle, a candlestick and a spoon; the flat things must read as lying on the hand (no fist
through the cover), the shafts must still close on the ball. Report `finger_ik.hold[6]` and `grab_focus[i].sphere_mm`.
