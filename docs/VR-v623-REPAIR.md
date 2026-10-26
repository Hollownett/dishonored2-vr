# v623 headset-feedback candidate

This candidate addresses the v622 headset feedback on native pickups/readables, object prompts and sprint hand lag. It is a headset test candidate, not a visually accepted release.

## Interaction

Hand-mode acquisitions use a 25 cm sphere centred on the raw controller grip. The closest eligible object surface wins without palm orientation, velocity, old-hover preference, mesh-contact admission or asynchronous LOS for standalone props. Native pickups, readables and carryables remain owned by the original native interaction action. Physics props retain dynamic-body ownership and generation/world checks. Occupied hands and actual held bodies remain reserved.

The complete pinned native eligibility function at RVA 3b80a0 is verified before installing its wrapper. The original function runs once with unchanged arguments. A scoped marker in the original range getter proves that native descriptor/FSM permissions have passed before hand-sphere geometry replaces the later native distance/side/height verdict. A permission refusal before that marker remains a refusal. Nested calls suspend their parent marker, native SEH restores TLS through `__finally`, and routing, publication, component generation and entity spawn are revalidated before accepting the result. Character actions retain their native policy.

Readable books can use grip through the generic engine-selected interaction component; no guessed book class or physics-body writes are introduced.

## Native prompt ownership

Native Add/Gauge UI updates run outside stage0, so a stage0-only ticket left every observed v622 prompt on a fixed HUD panel. The new binding joins the exact UI argument key to its retained native HUD entry, then resolves its entity spawn handle. Only an exact owner/spawn/component-generation/epoch join can place the captured native artwork beside an object. Missing or ambiguous receipts retain the native fallback. Full native code regions are verified against the pinned executable.

## Sprint and depth

Free-space sprinting above 3.5 m/s clears a previous physics-source visual blend immediately, rather than retaining its 150 ms offset. Contacts, surfaces, anchors and yields keep their collision path. Walking and turning retain their existing behavior. Out-of-domain physical-hand gaps retire the owned body through its lifecycle; recreation still requires a verified free target.

The gameplay feature catalogue enables hand/world depth repair and leaves power gestures off. Powers remain on the trigger. The test installation enables all listed gameplay features, with the graphics diagnostic census left off. Runtime counters and a new locomotion trace must verify the effective settings.

## Graphics status and test limits

Proton Experimental and the installed Hotfix have a verified mixed-layer OpenXR conversion defect. Stable installed Proton 11.0 forwards the layers correctly and is used for this headset candidate. Launch directly without a headset-availability preflight.

The user's low-resolution/no-AA centre with clearer edges is not yet explained by verified evidence. No speculative crop change is included. Native world viewport origins and post-tonemap input metadata are added to diagnose the actual eye image. Successful DLSS calls do not establish headset image quality. Re-test DLSS Quality in real VR, capture both eyes, and compare stationary/head-motion behavior.

Headset acceptance remains required for coin/book reach, immediate and correctly anchored prompts, sprint onset/stop, occlusion and both-eye quality.
