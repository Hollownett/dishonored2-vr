# v626 headset feedback candidate

v625 was rejected in headset testing: sprint still lagged/jittered, and pickup labels appeared only after grip, including after a two-second open-hand hover. This candidate requires new headset acceptance.

## Sprint timing and collision receipts

The v625 trace localizes large sprint gaps to contactless clamp holds, before the arm solve. Arm IK residuals remain small. A reproduction with native movement publications every 25.4 ms produces the same approximately 17.8 cm spikes at 7 m/s while walking does not exhaust coverage.

Root motion prediction now measures changed native positions over the time between changes and retains that motion estimate across repeated render poses. Query look-ahead includes observed submission-to-consumption latency. These predictions extend actual sweeps; they do not grant collision proof or transport collision planes.

Each palm/chest/tip/wall/exclusion request has a process-lifetime unique ID. Native completion returns that ID, original geometry, producer, original completion timestamp and optional hit atomically. Missing, stale, replayed, unknown, evicted and old-session replies grant no new proof; a completed receipt with no hit is a real clear cast. An incomplete native cast is not published as a clear completion. The clamp folds its cached request geometry/role/probe rather than pairing a previous answer with its latest post. Strict mode waits for real coverage instead of exhausting a hold timer into unchecked space.

Released-shot guards and hand-body creation probes consume actual completion geometry/timestamps. Creation needs three distinct matching clear replies for the current epoch, palm, radius and excluded body. Replaying one old miss cannot create a body or refresh clearance. Existing walking, turning and limb-drag/torso-carry policy remain subject to regression checks.

## Pickup previews

Fresh v625 evidence shows several missing hand previews were discovered by gaze/reach mode only after grip was pressed. Capture rejected no native UI events. Native sphere hover receipts now publish readiness independently of the active-use FSM component, but this alone cannot create HUD artwork when native selection has emitted no Add.

A separate native head-gaze preview is available while both resting hands have fresh, complete empty sphere queries and no input/action/occupation is active. It runs through the same native query stages and produces genuine HUD entries before grip; it never emits Use or supplies action readiness. Actual XR controller movement, action input, occupation, tracking loss or owner changes return priority to hand interaction. No periodic hand/gaze alternation is used. The next test distinguishes native hand qualification from this input-free gaze preview and active gaze Use. Native permissions, nearest-hand sphere geometry, exact retained object ownership and trigger powers are preserved. No text or object label is reconstructed or fabricated. New target trace fields identify original native refusals, semantic/publication failures, sphere qualification and final hover offers.

## Evidence and limits

Meaningful fixtures exercise 90 Hz rendering with 30 Hz native completions, batched root motion, in-flight A taken / B posted / A completed, wall constraints, missing and duplicate answers, auxiliary probes and session replacement. Native scanner output ABI at RVA 3b8b05 is independently verified over 338 bytes against the pinned executable. Windows target checks use clang-cl/xwin/Wine; complete native Windows MSVC validation remains unavailable.

The test uses installed stable Proton 11.0, DLSS Quality, alternate-eye enabled, all requested gameplay features enabled, and power gestures disabled. Headset visual quality, centre/right-eye defects, prompt timing, and sprint comfort remain unverified. Successful compilation or simulated fixtures do not establish these results.
