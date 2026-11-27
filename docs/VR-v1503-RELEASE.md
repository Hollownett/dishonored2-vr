# v1503 test build: solid arms, body holsters, grenades, mines and the Heart back in the inventory, hands that rest on surfaces, natural fingers, no crash on loading a save (pre-release, 2026-11-27)

A test build from the branch `feat/doto-vr`: Dishonored 2 **v1503** and Death of the Outsider **v145806**. It follows v1483 / v145785 (the v1491 / v145793 and v1497 / v145799 packages were never published: this one replaces them).
**It has not been played in a headset yet.** Everything below was checked in simulated VR sessions of the real games: scripted head and
controller input, the game's own state read back, and eye captures. The design follows the reference VR mod (its arm collision,
its body holsters and its finger contact), read side by side with this mod (`docs/research/2026-11-26-holsters-collisions/`).

Each item says how far it was checked:
- **sim-proven:** seen working in a simulated session.
- **built:** in the build, but not seen working yet.
- **not done.**

## Fixed: loading a save during play no longer crashes the game (both games)

Every earlier build made on Linux could crash the game while a save loaded during play (in the simulator: Death of the Outsider, every
time). The mod's guarded memory reads are meant to fail quietly when the game frees memory under them, but the Linux compiler (clang-cl)
did not catch the fault inside them the way the Windows compiler does. They now go through a call the guard covers in both compilers
(`src/seh_copy.hpp`; a host test reads a released page and must get "failed", not a crash). Sim-proven: a save loaded mid-game in both
games, no crash. One engine hook (`bodyless_motion_guard.hpp`) keeps its old read on purpose: a fault there has to reach the engine's
own handler. Catching it made the guard pass a body that is no body to the engine, which crashed 3 s into play (found by bisecting
build by build).

## New: solid arms (both games)

Your elbow now bends around walls, door frames and furniture instead of passing through them. The mod sweeps the arm the body would
draw (forearm and upper arm) against the world every frame. When it meets something, the elbow turns about the line from your shoulder
to your wrist, the way your own elbow would, until it is clear. Your hand stays exactly where your controller is.

- **Leaning into a wall** (sim-proven, both games): with the collision off, the drawn elbow sat inside the wall for 142 frames, up to
  11 cm deep (Death of the Outsider). With it on, the arm would have been in the wall for 209 frames and the drawn elbow was in it for
  7: the first frames of a contact while the elbow turns out (at most 12 rad/s, so a fast lean still shows it in the wall for a moment).
  Across runs and both games: 5-14 such frames. The captures show the forearm and the gun on it moving off the wall.
- **Standing beside a wall or in a corner** (sim-proven): nothing changes. The game's body keeps your shoulder at least 27 cm from any
  wall, so a normal pose never touches it: about 5,000 arm sweeps a side, no contact, no movement.
- **Guards** do not push your arms (by design, as in the reference mod), so you can still hold or choke one. Your hands, blades and
  barrels already stop at guards: a test confirmed the mod's casts see a living guard (sim-proven).
- **A sword, a gun or your hand cannot pass through your other forearm** (sim-proven): the right hand pushed down through the left
  forearm held across the chest stopped on it (305 frames of contact); with `no-forearm-contact` it passed through.

## New: holsters on the body (both games)

The holsters now sit on the body the game draws, not around your head. Your belt is at your hips, the shoulder holster is over your
shoulder, and both stay there when you look down or turn your head.

- **Grenades and mines back to your belt** (sim-proven in both games): let go of a grenade or mine slowly at the front of your belt and
  it goes back into the inventory. Squeeze there and pull out to take it again. The Outsider's Hook Mine and Dishonored 2's grenade each
  went back and came out again twice, and a normal drop still worked afterwards. (The Dishonored 2 lab save holds no grenade: the test
  gave it one with the game's own inventory add, `d2vr give item`.)
- **The belt forgets on a load** (sim-proven): load a save or die, and the belt no longer offers the gadget you held before (a save from
  before you had it would not have it): a pull there gives the "empty" tick.
- **The Heart at your left chest** (sim-proven, Dishonored 2): let go of it there (the hip works too) to put it away. Squeeze there and
  pull out to take it again: twice each, no failures.
- **The holstered pistol or crossbow stays on your hip while you move** (Dishonored 2): it used to trail your body by a frame or two,
  about 2 cm walking and 6 cm sprinting. It is now carried to where your body will be after the game's movement step, as the weapons
  in your hands already are (sim-proven): walking, sprinting, strafing and turning, it sat 3.4 mm from the hip the body is drawn with on
  average, against 10.5 mm before, over about 1,600 frames each.
- **The shoulder holster follows your body** (built): over either shoulder, where the drawn body's shoulder is. Drawing and sheathing
  the sword there works as before (sim-proven, the same results as the previous build).

## New: your hand rests on a surface instead of stopping 4 cm short (both games)

Until now the mod kept your empty hand out of walls as if it were a ball 5 cm round the controller: whichever way the hand faced, it
stopped with its skin about 4 cm off the surface. A palm "on" a wall floated in front of it, and the back of the hand could never touch
one. The hand now has the shape the reference mod gives it: the palm is a flat box (1 cm deep on the palm side, 1.5 cm on the back),
the fingers are the fingers as they are drawn. After the old stop, an empty hand moves on toward your controller until that shape meets
the surface, a few millimetres off it (sim-proven, both games):
- the palm or the back of the hand comes to rest on the wall; a fist stops at its knuckles, a thumb that sticks out stops the hand
  until the fingers move it;
- it follows the controller in smoothly (a host test presses in and out in 2 mm steps: never more than 2 mm a frame) and lets go the
  way it did before;
- only an empty hand, and only against walls, floors and things that do not move: a hand holding a weapon, a gadget, a body or a prop,
  a hand the game animates, and anything loose are exactly as before (sim: the gun hand pressed into a wall never moved);
- `legacy-palm-sphere` brings the old stop back.

Palm flat on a wall, slid slowly and fast (sim, against `legacy-palm-sphere` in the same build):
- Dishonored 2: fingers past the depth limit on 10 of 1,544 frames, 1.07 mm deep on average (1.99 mm before); the thumb more than 5 mm in
  on 42 frames (244 before); the palm skin never inside the wall (17.5 mm before, a measuring artefact of the old probes, see below);
- Death of the Outsider: fingers past the limit on 0-2 frames (4 before), the same steadiness (1.9 mm average step, no flicker).

The probes that find surfaces for the fingers were also cleaned up: a probe that started inside something, met the mod's own hand
body, or met a surface facing the wrong way used to become a "surface" through the hand (a resting hand's index finger flickered,
and in Dishonored 2 the palm measured 18 mm into a wall that was not there). Such answers are now ignored.

## New: fingers that touch things naturally (both games)

Your fingers now follow the reference mod's finger rules on walls, tables and props:

- **They never sink into a surface.** After the fingers ease toward their pose, a finger that would be inside something is set out of
  it at once.
- **A palm on a wall lies flat.** When your palm rests on a surface the fingers open onto it first instead of clawing at it. If you
  squeeze while it rests there, the fingers stay open on the surface.
- **Edges.** A surface the fingers touch is only as big as the hand felt it, so a finger past a table edge droops over it instead of
  resting on an invisible extension. (Checked by the host tests only: no table in the lab saves.)
- **The thumb stays on top of an edge.**
- **The back of your hand pressed on a surface straightens the fingers**, up to 35% of the way open, from 4.4 cm off to 4 mm, as in
  the reference mod. Sim-proven with the back of the hand pressed on a wall: engaged on 549 frames (Death of the Outsider) and 949
  (Dishonored 2). Before the hand could rest on a surface it never engaged in that test (the hand stopped 4 cm short).
- **Quick and steady.** Fingers close onto a surface or around a squeeze about twice as fast as before (about 35 ms) and open as
  before (about 70 ms), at most 6 rad/s: the reference mod's timing. The fingers resting on a surface are checked first and agree on one surface, so
  they stay level and still while your hand slides or you walk.

In the simulator, an open hand pressed flat on a wall, slid along it slowly and fast and carried along it while walking (sim-proven,
compared with the build before this work):
- fingers past the depth limit in 1-3 frames instead of 22; the deepest long finger 4-9 mm instead of 18.5 mm (slow) and 10-20 mm
  instead of 28 mm (fast); without the put-out step 51 frames and 24 mm;
- no flicker between a finger bending and lifting (the previous build flipped 4 times in the same run);
- the quicker timing moves the fingers a little more per frame (2.2 mm against 1.3 mm on average), with no flicker; `legacy-finger-timing`
  brings the old timing back;
- the wall rules (palm open, thumb, steady contact) against `legacy-finger-surface` in the same run: the largest frame-to-frame finger
  step 43 mm against 147 mm, the average 2.1 mm against 4.2 mm, and no flicker against 6 flips.
- **The thumb on a wall.** The thumb is now drawn in the pose the contact solve finds for it (it used to be drawn from its relaxed
  pose, which points into the wall): in the host test from 20 mm in to 9 mm, the least the thumb's joints allow with the palm flat on
  the surface (it cannot lie fully flat), and 5.8 mm where a clear pose exists. Measured frame by frame in the simulator, the thumb is
  more than 5 mm in on 1-4% of the frames the hand is at the wall (the "2.5 cm" of the last notes was the single worst moment of a
  fast slide).

## Checked for regressions (simulated, new build against v1483 / v145785)

The same results as the previous build: the grip power cancel (3 of 3), thrown mines sticking, the sword in fast slashes, a sword and a
gun pushed into a wall (the same contact counts), drawing the sword from the shoulder, and the Hook Mine's lethal switch (the new build
passed it; the old build's run did not register the holds), and the finger poses on the controller (rest, point, thumbs up, fist, open:
the same results with and without the new finger rules).

## Spacewarp: what could still improve it (research, no change in this build)

A survey of open-source frame generation and reprojection, ranked by how much it would help here:
1. **Hands while walking:** keep the hands' own motion out of the head-motion warp while you walk (the largest visible artefact).
2. **Thin, fast edges:** spread a moving arm's motion one pixel outward, nearest depth wins, before the warp.
3. **Holes behind moving things:** fill them from a blurred lower-resolution copy instead of stretching the edge.
4. **Depth to the headset** (`XR_KHR_composition_layer_depth`): could let the runtime's own reprojection use real depth; whether
   VDXR, SteamVR or Quest Link actually use it is not documented, so it needs a measurement first.
5. **AFW sharpening:** a light sharpen after AFW to undo its softness.
6. **Snap turns:** tell the warp a snap turn is a cut, so it does not smear across it.
Not worth it here: interpolating frame generation (adds latency), optical flow as the main source, neural methods, `XR_FB_space_warp`
on PC, and GPL code. Details: `docs/research/2026-11-26-holsters-collisions/spacewarp-improvements.md`.

## If something feels wrong

An empty file with one of these names undoes that change. Put it in `%LOCALAPPDATA%\DishonoredVR\build` for Dishonored 2 or in
`%LOCALAPPDATA%\DishonoredVR-DOTO\build` for the Outsider. Each is also a row in the launcher's feature list.
- `no-arm-collision`: the solid arms
- `no-forearm-contact`: a weapon blocked by the other forearm
- `no-gadget-stow`: putting grenades and mines back
- `no-holster-draw`: taking them from the belt again
- `no-heart-pocket`: the Heart's chest pocket
- `legacy-shoulder-zone`: the old shoulder holster around the head
- `legacy-hip-place`: the old hip gun placement
- `no-finger-put-out`: fingers set out of surfaces
- `legacy-palm-sphere`: the hand stops 4 cm short of surfaces again (the old ball-shaped stop)
- `legacy-finger-surface`: the palm-open, thumb, back-of-hand and steady-finger rules
- `legacy-finger-slab`: the old large surface planes
- `legacy-finger-timing`: the old, slower finger timing
- `legacy-finger-probe-order`: every finger checked in turn
- `legacy-finger-plane`: each finger on its own plane

## Known issues
- Not tested in a headset. What to check: `docs/VR-v1503-TEST-CHECKLIST.md`.
- The arms only react when they really meet something. Standing at a wall, the game's body keeps them clear, so most of the time you
  will not see it. Leaning in is where it shows.
- Palm flat on a wall: the thumb's joints cannot lay it fully flat, so it can sit a few millimetres in (see the fingers section).
- Both DLLs and the launcher were built on Linux, not with the usual Windows build.

## Linux (Proton)

Dishonored 2: `linux/d2vr-linux install --zip d2vr-v1503-dll.zip`. Death of the Outsider:
`linux/d2vr-linux --game doto install --zip d2vr-doto-v145806-dll.zip`. See `linux/README.md`.
