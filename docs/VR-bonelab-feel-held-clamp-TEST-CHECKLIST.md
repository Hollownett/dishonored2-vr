# Bonelab feel Part 5A: held-object clamp, headset checklist (Tier C)

Status: not run. Tier A (logic) and Tier B (sim, crash safety) cannot declare feel or comfort; only this list can. Run it on the
build under test with the held clamp on (no `build/no-held-clamp` file) and again once with that file present for the A/B.

## Before you start

- Note the DLL hash and that `/tmp`-style dev knobs are absent from `build/`.
- Stand in a room with a whale-oil tank (or any carryable prop), a wall within 2 m, a doorway and a long plank or similar prop.
- After each item below, save the status: copy `xr-live-status.json` and keep its `held_clamp`, `release_watch` and `layer`
  blocks (sweeps, hits, clamped_frames, releases, release_overlap_unresolved, layer_at_release, layer pending).

## Items

1. **Tank into a wall, then the floor.** Pick up the tank, walk it into a wall, push the hand through the wall, then press it
   down onto the floor. Expect: the tank stops at the surface and does not jitter through it; the drawn hand stays within about
   0.35 m of your real hand; no ejection when you let go. Save `held_clamp` (sweeps, hits and clamped_frames above 0).
2. **Drop from chest height.** Lift the tank to chest height and open the hand. Expect: it falls and lands once, comes to rest on
   the floor, no pop or launch. This 1.5 m drop is judged only here: the sim's `release_watch` keeps just the last release. Save `held_clamp`.
3. **Drop while pressed to the floor.** Push the held tank into the floor so the clamp holds it, then release. Expect: it
   settles out of the floor within about half a second at a gentle speed (at most 1.5 m/s), never fired away.
   `release_overlap_unresolved` stays 0. Save `held_clamp`.
4. **Carry a plank through a doorway.** Hold a long prop and walk through a door frame, turning your wrist. Expect: the prop
   scrapes or stops at the frame, never passes through it and never snags you or throws you. Save `held_clamp`.
5. **Release-in-corner.** Release the tank with it jammed into a wall/floor corner. Expect: it comes out of the corner softly;
   if it cannot, `release_overlap_unresolved` counts it and the layer is still restored (`layer.pending` 0).
6. **Comfort.** Fifteen minutes of picking up, carrying and dropping props: note any nausea, hand lag you can feel, or hand
   and object disagreeing (drawn hand away from the tank).

## Pass

All six behave as described, `release_overlap_unresolved` is 0 (or each counted case recovered), `layer.pending` is 0 after
every release, and the A/B with `no-held-clamp` shows the clamp is what stops the tank. Anything off is a finding, not a skip.
