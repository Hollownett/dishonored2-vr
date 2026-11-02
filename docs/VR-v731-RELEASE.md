# VR v731 (test candidate, 2026-10-31)

The user's v730 headset run: in the Good Doctor boat ride with Meagan (leaving Addermire) the boat still turned upside down and sailed backwards. v730 had already reverted every v721-v725 cutscene camera change, and the flip pose was identical to v724-v729 (-87.63, 50.84, -1.36). The camera code was not the cause.

## Root cause: the ocean's GPU queries never come back with two eyes
- The boat is the scene group `boatcontrol` of `debrief_gooddoctor.midnightscene`, with `m_motionOceanOption` (normalWeight 0.1). Each frame its consumer (0x4e3f70) registers 3 points with arkOcean and reads the results registered 3 frames earlier:
  - height = result + offsetZ, and 0 when there is no result;
  - tilt = the summed result normals, smoothed over 8 samples, turned into a rotation from +Z.
- Engine bookkeeping (Ghidra, `artifacts/boat-re/`):
  - Register 0x3032b0 queues points (ocean+0x12d70) for the next frame slot.
  - The UPDATE_OCEAN pass (0x305370 → 0x305270) calls Dispatch 0x3030f0 once per view. Dispatch evaluates the queue into staging[slot], then always stores dispatched[slot] = queued (+0x12d60) and clears the queue.
  - The backend frame (0x361d60) reads staging[slot] back to the CPU results only while dispatched[slot] > 0 (0x304c40).
- Both VR eyes are copies of the one game view, so both run Dispatch. The second finds an empty queue and stores dispatched[slot] = 0. The readback never runs.
- Live check, sim on the user's save (SLOT02, Addermire dock), v730, read-only probe (`artifacts/boat-re/`):
  - Each frame the slot advances and the first eye sets dispatched[slot] = 160; about 3 ms later the second eye resets it to 0.
  - All four CPU result buffers stayed 0.0 for the whole stereo session.
- With zero results the boat targets height 0, about 6 m above the water. Its 8-sample tilt filter drains to a zero vector after ~20 frames, and the rotation from +Z to a zero vector (0x10f840) is a zero matrix. The traced flip (a clean 180° turn ~20 frames into the ride, the same pose every run) fits an orientation rebuilt from that degenerate matrix; that last step is inferred, not traced.
- The theater mode and the flat game render one view, so they were fine. The same frozen results also fed swimming in the ocean and floating props.

## Fix
- `NativeOceanEval` (src/native_ocean_eval.hpp) hooks Dispatch 0x3030f0 (16 prologue bytes checked). Only the first call of each engine frame slot (0x3308d04, advanced once per backend frame) runs; later views of the same frame skip it. One view behaves exactly as before.
- Knob `build/no-ocean-eval-gate` turns it off. Status `ocean_eval` in xr-live-status.json: `dispatches`, `skipped` (about one skip per stereo frame).

## Checks
- Lab `tests/native_ocean_eval_check.cpp` replays the decompiled bookkeeping: flat game -6.464 m, paired eyes 0.000 (never read back), paired eyes with the gate -6.464 m.
- Live, sim on the same save with v731 (stereo gameplay, 50 s): every slot keeps dispatched = 160; the results update every frame (~1130 distinct values per slot): water height -6.06 to -6.2 m, normals about (0, 0, 1) with wave tilt. `ocean_eval`: 21202 dispatches, 5179 skipped (one per stereo frame). No faults.
- `cutscene_view_check` passes. The DLL builds (`build/compile-v731.cmd`, from `build/v731src` = v730 sources + this change).
- Headset (user, 2026-10-31): the boat ride with Meagan works.
