# v625 headset feedback candidate

This candidate follows the v623 headset report: nearest hand-object selection works, but object prompts appear near the view centre, body carry loses its hold circle, and sprint hands lag/jitter. It preserves the accepted 25 cm hand sphere, locomotion and turning. Limbs drag; holding the torso hands off to native carry after 500 ms.

## Native prompt and gauge ownership

A native HUD entry, resolved from the exact Add/UpdateGauge key and entity spawn, now owns the captured InteractionWindow for its full active lifetime. It does not require an optional current hover/component join. The full label/gauge subtree remains captured. Pending Add entries wait for native activation. Native removal, key replacement, entity reuse and changed movie/node retire the source; native array compaction retains it. Last-good artwork cannot survive a replacement source or renew old pixels.

Retained native entries use the verified model world bounds centre, falling back to the entity world origin when the model requires an unverified transform. Placement no longer applies the generic centre-view or 0.7 m native-only offset to these entries. Ownership and model reads are revalidated. No native carry timer or limb/torso semantics are changed.

## General hand synchronization

Walking and sprinting share the collision/source policy. The v623 speed-specific visual bypass is removed. The clamp predicts root displacement separately from capped controller-relative motion and sweeps over the longest recent frame interval. A translation fallback is allowed only on an answered sweep centre-line; it does not transport world collision planes or establish new collision proof. Sudden acceleration beyond covered space waits for the real answer. Variable cadence, onset/stop, unseen walls, corners and rotation are checked. The rejected v624 unchecked root-translation fallback is launch-blocked and must not be installed.

## Validation limits

Focused Windows-target fixtures run under clang-cl/xwin/Wine. The production DLL compiles with /W4 /WX; this environment does not provide the complete native Windows MSVC gate. These checks do not establish headset visual quality or comfort.

The next direct VR launch uses installed stable Proton 11.0, DLSS Quality, alternate-eye enabled by default, gameplay features enabled, world-hand depth enabled and power gestures disabled. Trigger powers are preserved. Alternate-eye continuity after scene changes and the previously reported low-resolution centre/right-eye distortion remain unresolved until fresh headset evidence. The v623 runtime recorded 35,843 DLSS evaluations at 1549×1656 to 2323×2484 with no evaluation/create failures, fallback or owner refusals; FSR4 dispatched zero jobs. Alternate-eye fell back after 6,874 alternated frames. Current menu viewport observations cannot be compared to stale gameplay viewports to infer a mismatch. Loaded DLLs and successful AA API calls are insufficient acceptance.

Locomotion output includes clamp validation hold/gap alongside visual/body gap. Carry-handoff tracing and exact Add/Gauge keys distinguish transition failures from UI anchoring failures. Headset acceptance remains pending.
