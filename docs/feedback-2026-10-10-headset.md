# Headset feedback: centered UI, selection, wrist HUD and frame pacing

## Consolidated live-session report, 10 October 2026

This section supersedes earlier completion assumptions below. The running test
is v205 on PID 47496 through Virtual Desktop, with genuine FSR 4.1.1 in both eyes
and native GPU timing enabled. It is a development build, not an accepted VR
release. The requested outcome is a VR adaptation of Dishonored2 using the
Dishonored1 VR mod's established ownership, camera and native-art practices.

### Complete feedback and required behavior

| Area | User-observed problem | Required result |
|---|---|---|
| Combat | Blocking/weapon animation fights tracked movement | Physical controller movement drives ordinary fighting; native animation owns only verified scripted actions |
| Arms | Forearms need calibration; latest wheel exposes shoulders/full arms and freezes hands | Hands follow controllers in ordinary play and immersive UI; full arms/forearms remain deferred |
| Camera | Earlier camera was improved; wheel and finishers now displace it | Preserve accepted locomotion, turning and ordinary camera behavior across UI transitions |
| Finishers | Camera and player animation do not match | Native first-person finisher movement with the requested slightly wider view; return smoothly to ordinary tracking |
| Weapon wheel | Initially too low; latest build shows character, lacks visible selector/cursor and cannot select | Readable native selection UI in the world in front of the face, with visible and functional selection controls |
| Reading interfaces | Text/journal/notes too low, misplaced or missing | Reuse native pages on readable, centered world surfaces; preserve scrolling, selection and close behavior |
| VR settings | Separate interface is unwanted | VR Settings belongs inside the game's pause menu |
| Both hands | Sword/support hand could not interact reliably | Symmetric grab/Use acquisition and grip actions for both hands, including when holding weapons |
| Grab acquisition | Narrow ray requires pinpoint aiming, especially near/in front of an object | Nearby-object volume around each hand; surface-distance ranking and stable target preference, with native eligibility and obstruction rules |
| Native pickups | Food, armor, money and other interactables are hard to select | Include native actionable pickups, not only loose physics props |
| Holding | Selected/held object flickers and may fail to follow the hand | Stable ownership until release, with valid tracking-loss and obstruction handling |
| Interaction prompt | Prompt appears on HUD or beside the player; invented circles/ink are unwanted | Reuse the actual small native actionable/hold-grip prompt at its object in the stereo game scene |
| Objective/game markers | Markers are displaced or on a separate HUD | Native art, label and distance at the actual world target in both eyes |
| Wrist HUD | New replacement HUD is unwanted | Reuse native health/mana art at the right wrist and ammo/info art at the left, revealed by a watch gesture |
| Damage art | Red square/visible hard vignette edges | Correct full-extent native damage effect without shrinking the whole HUD movie |
| Virtual Desktop switch | Black screen, later identified as an image in the top-left corner | Recover valid native viewport, eye textures, poses and input after desktop/VR, focus and reference-space changes |
| FSR/image quality | FSR softens/washes the image without useful FPS improvement; user reports severe frame-rate degradation | Verify actual reconstruction and rendering costs; restore image quality and explain the measured bottleneck |
| Resolution/performance | Even0.5 render scale does not deliver the requested FPS improvement | Prove every relevant pass's resolution and remove the remaining fixed/serial work, waits or pacing limits |
| Performance target | User reports about69–70 FPS rather than near 90 | Stable90 Hz behavior where possible:11.11 ms frame budget, controlled frame pacing and measured headset/compositor behavior |

### Measured facts and limits

- v204/v205 live provider queries and dispatch counters report FSR 4.1.1 in
  both eyes, with zero FSR3 fallback. Quality input1922 × 2070 and Balanced
  input1672 × 1801 upscale to2496 × 2688 per eye. The upscaler is active.
- The latest0.5 setting really changes output to1248 × 1344 per eye; Balanced
  input becomes836 × 900. This is half each linear dimension and one quarter
  of the output pixels. It does not halve the cost of all engine/driver work.
- v204 Quality median Present interval13.919 ms/P9519.548 ms agrees with the
  reported approximately 70 FPS cadence. It does not measure headset scanout.
- v205 earlier Quality samples had native command-list sum8.53 ms and elapsed
  span12.11 ms. The sum excludes FSR and immediate draws; the span also includes
  intervening work and queue idle. It is not isolated GPU busy time.
- Latest retained full-size Balanced:1950 tracked samples, median Present
  interval17.856 ms, native-list sum8.069 ms, elapsed15.274 ms. Latest half-size
  Balanced:8413 tracked samples, interval13.481 ms, native-list sum4.415 ms,
  elapsed11.277 ms. These are different scenes/epochs, so they are not a valid
  before/after speedup benchmark. They still miss the 90 Hz budget.
- In tracked samples, median XR wait/submit and texture-transfer CPU spans
  were small compared with the engine/interop gap. CPU work, driver/GPU
  backpressure, FSR4 handoff, queue idle and native fixed-size passes still need
  attribution. Low total CPU utilization alone cannot exclude a thread limit.
- Most counted empty/dropped XR layers occurred in VISIBLE state while the
  runtime requested no rendering. They are not evidence of missed GPU frames.
- The top-left defect was reproduced: native cached size841 × 485 while eye
  textures remained2496 × 2688. v205 performs nonblocking restoration only from
  its captured original pair, rejects that damaged frame and resets history.
  Live focus return recorded automatic recovery and no deferred corrections;
  the later0.5 cache also matches its1248 × 1344 output. General headset
  acceptance and all switch/recenter combinations remain outstanding.
- An earlier physical pickup acquired one right-hand object, moved19 times,
  and released after297 ms without ownership loss. Native vertical movement
  was0.00562 versus requested0.11183 native units. This is not a successful
  lift test; collision, motor response and release cause need correlation.
- Native selection logs contained genuine no-selection gaps. The bounded
  trace filled before the physical pickup, so raw grip/selection/physics events
  cannot yet be correlated for the demonstrated failure.

Evidence: `artifacts/session-feedback-20261010-171008/xr-frame-timing.json`,
its copied live status and raw traces; `artifacts/feedback-v205-build-validation.json`;
`artifacts/live-review-20261010-164245-black-screen/native-cache-mismatch.json`.

### Source findings: confirmed behavior versus hypotheses

1. **Wheel hands/shoulders:** tracked hand publication is gated on gameplay
   logic2. Wheel logic256 renders the native world, but the hand publication
   expires after250 ms. The fallback supplies the complete native palette,
   bypassing the tracked-pose arm-hiding path. Setting `full_arms=false` alone
   does not hide native fallback arms. This directly explains the regression
   path; the correction still needs live verification.
2. **Missing wheel cursor:** the GameXr pointer path explicitly excludes the
   wheel. A separate dial/stick path exists, but it does not establish visible
   pointer or working native selection in this session. Test selection events,
   highlighted sector and release-to-equip together, rather than just drawing
   the wheel.
3. **Finisher camera:** BodyAnchor levels native pitch/roll unconditionally,
   then prepare_head_view adds calibrated headset orientation and roomscale/
   crouch offsets. This is not the native first-person authored camera.
   The ordinary pose converter requires a level anchor, so preserving authored
   basis in BodyAnchor alone is insufficient. A separate composition path and
   verified finisher/action discriminator are required.
4. **Native Use remains ray-oriented:** loose physics props already have a
   proximity envelope. Static native pickups use separately owned geometry,
   permissions and obstruction queries. The geometry factory and asynchronous
   lifetime must be verified before creating broader native sensor shapes.
5. **Native UI is not yet adapted at objects:** prompt/MasterHUD bindings are
   identified, but child draw isolation, stage transforms, scene placement and
   duplicate suppression are unfinished. The custom GDI wrist card currently
   violates the clarified native-art requirement.
6. **Performance:** FSR4 currently has an avoidable cropped-color copy before
   copying again into the DX12 shared input. Removing it is a concrete small
   optimization candidate, not a promised90 FPS fix. The larger elapsed-minus-
   native-list gap needs stage measurements. A viewport at the TAA stage does
   not prove all native scene passes use the requested resolution.

### Ordered repair plan and acceptance gates

**1. Freeze a reproducible baseline and repair interaction-breaking regressions.**
Keep candidate/source hashes, saved settings and the accepted ordinary camera/
turning behavior. Record a short fixed-route scenario with native output,
AA/backend, render fraction, XR focus, native action and hand state in every
sample. Fix hand publication/hiding across wheel/reading states and restore
the wheel's native selection path with a visible selector. Gate: open, select,
equip and close repeatedly with both sticks/controllers; no full shoulders,
frozen hands or camera jump. Keep v205's owned viewport recovery.

**2. Restore authored first-person actions.**
Verify native finisher/assassination/mantle state ownership and camera basis.
Compose native animation camera plus correct stereo eye offsets in a separate
path, without the ordinary roomscale/crouch/body-follow compensation. Implement
the slightly wider authored view through a valid projection/FOV policy, not a
stretched image. Gate: native body, weapon and camera agree throughout start,
action and exit; ordinary tracking/turning resumes without a discontinuity.

**3. Replace grab acquisition with per-hand nearby-object selection.**
Use a hand-centered volume and nearest eligible object surface, independent
of narrow pointing and including objects just in front of or beside the hand.
Start from the existing.45 m acquisition/.55 m retention envelope and tune in the
headset; these are provisional, not accepted ergonomic measurements. Preserve
an eligible target with bounded hysteresis, deterministic ties and separate
ownership per hand. Keep the other hand's held object excluded. Verify native
query-shape construction, queue retention and retirement before integrating
food/money/armor Use. Keep native permissions/obstruction/action ownership.
Correlate raw grip, qualified candidate, hold owner, native action, target and
actual body pose plus explicit release reason. Gate: both hands acquire nearby
items without pinpoint aiming or flicker; objects follow and release reliably;
blocked/ineligible items are never advertised or activated.

**4. Reuse native UI artwork in the stereo game scene.**
Measure native prompt, health, mana and ammo child transforms on the owned UI
thread. Establish individual native draw ownership and capture only those
native elements, retaining content, glyphs, gauges and animation. Render the
prompt at the selected object's native world point; render native objective
art at its world target. Render wrist-native health/mana on the right and
ammo/info on the left using the watch gesture. Suppress only the corresponding
duplicate HUD draws. Preserve damage/vignette and unrelated native UI. Native
world wheel/reading art must remain centered and selectable. Gate: correct
stereo placement/occlusion, readable small prompts, no side HUD/duplicate UI,
no invented circles, lines, bars, fonts or replacement stat artwork.

**5. Attribute and optimize the performance bottleneck.**
Run the same scene/route with native AA, FSR3.1 and FSR4 at identical settings;
then vary Quality/Balanced and1.0/0.75/0.5 linear output scale separately.
Keep scene, mirror, refresh, runtime/SSW configuration and profiling overhead
recorded. Time native passes, both FSR4 handoffs, producer flush/consumer wait,
input/output copies, command-list CPU submission, Present and XR/compositor.
Verify every relevant native render target/viewport; identify fixed-size
effects/shadows and serialization. Remove the proven duplicate color copy,
then compare it against an unchanged matched control. Address whichever CPU,
GPU, interop or pacing limit the stage data actually establishes. Gate: report
median/P95/P99 CPU/GPU frame costs and actual headset/compositor statistics;
near 90 FPS with consistent pacing, or a measured remaining limit. Preserve
image quality; no mode is called a performance fix based on softer imagery.

**6. Run integrated headset acceptance before claiming a polished iteration.**
Test combat/block, both-hand pickup/use, throw/release, wheel, notes/journal,
native pause settings, wrist gesture, damage, objective marker, finisher,
death/reload, recenter and Virtual Desktop switch. Use focused correctness/GPU
checks, full build and source provenance, then real user visual/gameplay
acceptance. Publish a new test version with retained fallback only after its
claimed features pass; source fixtures alone do not establish headset quality.

### Work completed this session and work not deployed

- **Running v205:** owned native output-cache recovery, completed-frame
  rejection/history reset, full build/source validation and live focus-reset
  evidence. Genuine FSR4 and native GPU telemetry are active; saved user mode/
  resolution choices are retained. Headset acceptance is incomplete.
- **Focused source correction, not in running DLL:** restored centered native
  queries instead of angularly offsetting native range/shape slots. Tests pass.
- **Tested policy, not integrated:** per-hand native-qualified proximity policy
  with acquisition/retention/hysteresis. It does not yet select static pickups.
- **Tested probe, not integrated:** native prompt/MasterHUD geometry and global
  affine-transform getter helper. It does not yet capture/relocate native art.
- **Not implemented:** wheel/hand transition repair, explicit first-person
  authored camera, native-art wrist/object rendering, full static-volume Use,
  duplicate-copy optimization or validated 90 FPS performance improvement.

Primary reference practices: [Dishonored1 native HUD draw routing](https://raw.githubusercontent.com/VR-Stereo-Hub/Dishonored-VR/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/core/gfx/hud_layout.cpp)
and [menu immersion ownership](https://raw.githubusercontent.com/VR-Stereo-Hub/Dishonored-VR/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec/src/game/dishonored/menu_immersion.cpp).
The output recovery uses the existing verified native cache and [64-bit atomic
compare-exchange semantics](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedcompareexchange64).
D1 memory layouts and crop coordinates must not be transplanted into D2.

## Earlier feedback and implementation history

User feedback supersedes the earlier hand-mounted world UI and ink-line design.
The v199 camera now looks better and is accepted. Preserve its camera, movement
and turning behavior, and keep arms/forearms deferred.

## Work order

1. Preserve the real-headset v199 run and retain its DLL as fallback. The game exited before a restoration report was written; clean shutdown of that run is unverified.
2. Place wheel and journal/notes at eye height in front of the face, upright and
   stable while open; clear pose on close/recenter/focus changes. Match pointer
   placement to the same surface.
3. Remove hand-to-object lines and custom grab circles. Reuse the game's native
   interaction prompt near the actionable object, with stable selection. Widen the
   symmetric physical reach envelope and add target hysteresis; broaden native
   loot/food/money interaction only through verified existing sensor queries.
   Keep the engine responsible for eligibility, collision and the actual Use action.
4. Keep damage/vignette effects full extent. Fix owned world marker placement.
   Move native health/mana information to the right wrist and ammunition/info
   to the left wrist, visible with a watch gesture; never fabricate values.
5. Verify FSR's actual input size and GPU cost at Native, Quality and Balanced;
   reduce redundant full-size input work where valid. Investigate desktop
   presentation pacing, pose timing and frame-budget misses separately from FSR.
6. Run focused correctness/GPU tests and the full build, then matched native-game
   runs. Publish a new version only with source provenance, exact evidence and
   retained fallback. Headset appearance and comfort remain user acceptance.

## Baseline findings

- v199 world UI uses the left grip's position, which explains panels at chest height.
- NativeHudRect shrinks a complete movie, including any full-screen damage art.
- Current headset output is 2496 × 2688 per eye at a reported 90 Hz display period.
- The first captured gameplay/tracked intervals have median 13.94 ms and P95
  20.30 ms, exceeding the 11.11 ms budget. These are CPU frame intervals, not
  GPU timings or a matched FSR comparison.
- The 20,000-row timing cap omits later user mode changes. Add AA-mode/render-size
  identity and continued bounded observations before judging FSR performance.
- Saved user changes (edge AA off, sharpening zero, FSR Native) must be retained.

Implementation results and acceptance limits will be appended after verification.

## v200 test session started

Full compiler/check pipeline passed. Its initial source archive was invalidated
only by an unrelated added Python rendering tool; a fresh DLL-only recompilation
then verified unchanged source and published exact provenance. Real Virtual
Desktop OpenXR session on PID 47496 reached FOCUSED, submitted native game frames
and tracked both controllers at startup. v199 is retained as fallback. User
preferences were preserved. See `artifacts/feedback-v200-test-session.json`.

Headset appearance and gameplay outcomes are being tested by the user. Native
HUD widgets remain a fallback alongside wrist displays. Removal of whole-movie
shrink/coordinate compensation is verified; residual portrait vignette edges and
per-eye target-marker projection still need visual/native-game confirmation.
Standalone GPU wrapper checks do not establish whole-game performance gains.

## Additional objective-marker feedback

The user reports that both gameplay interface markers and the main objective
marker are completely out of place. This remains an open rendering defect.
Retain the native marker artwork, label and distance information. Render the
interaction prompt beside its actionable object and the objective marker at its
native world target inside the stereo game scene. These indicators must not use
the default HUD surface or a separate OpenXR composition overlay. Merely changing
HUD x/y does not satisfy this requirement. Head turning, FSR mode
changes and Virtual Desktop focus return must preserve that alignment.

The current marker helper observes native updates and projection calls; it does
not correct objective placement. Whole-HUD scaling is already disabled and is
not sufficient to fix world-marker projection. Verify the native marker update
cache and UI placement thread before changing engine state. Preserve marker
diagnostics in each archived run. Headset alignment remains unverified while
live computer control is stopped.

Required rendering path: establish individual native prompt/marker draw ownership,
reuse that native artwork on a world-positioned surface in each game eye, and
suppress only its duplicate HUD draw. Keep native eligibility, objective state,
labels and distance updates. Preserve the full-screen HUD damage effects and
unrelated UI. Scene depth/occlusion behavior must be checked in-game; it must not
be inferred from a compositor overlay or an automated pose test.

## Latest headset feedback and v207 work

The user confirmed that focus selects the safe instead of its contents. Food
and loose loot improved, mantle and ordinary camera/movement are accepted,
blocks and combat finishers improved, and Virtual Desktop away/back is accepted.
Preserve those working paths. The weapon selector remains cropped and shows the
character, silent assassination camera is displaced, native guardian/item/goal
widgets still need world placement, and Quality/Balanced reduce image quality
without a perceived FPS improvement.

The next test build implements these focused changes:

| Feedback | Implementation | Remaining native/headset check |
| --- | --- | --- |
| Safe takes focus before contents | Rank complete native object hits around the active grip, retain a selected owner within2cm, remove mixed camera/hand direct-ray priority. Continue after an opaque rejected object while keeping its native LOS failure. | Open safe: both hands acquire loot. Closed safe/wall: blocked contents never become eligible. Native contact geometry may still favor a nearer shell; no invented item priority. |
| Weapon wheel cropped, character visible | Use the full native wheel image and matching pointer mapping. Hide only owned copied20/69joint render palettes while wheel logic256 is active. | All sectors/cursor visible, selection works, hands/attachments restore immediately on close. |
| Silent assassination camera displaced | Freeze a verified native first-person stereo centre and basis for each authored frame/owner, with unchanged ordinary camera behavior. | Silent assassination and combat finisher align; invalid native eye pairs pass through unchanged. |
| Prompts and markers belong near targets | Capture individual native display children; copy exact measured alpha pixels and place them into both game eye images at the owned object/marker anchor. | Actual native child scope includes its GPU draws; original prompts, labels and distances remain complete. Distant markers and movement stay aligned. |
| Reuse native wrist HUD | Capture original health/mana/equipment children, combine original health/mana pixels for the right watch gesture and equipment/ammo for the left. | Native pixels are complete, correct and legible during actual watch movements. No replacement circles/fonts/meters. |
| FSR blur without FPS gain | Accept compatible native SRGB/typeless colour textures and mip-zero inputs for FSR4. Observe actual native world viewport after scaling and record coherent mode/frame/time receipts. | Compare matched gameplay Native/Quality/Balanced with no menu, loading, capture readback or scene changes. Genuine dispatch success alone is not FPS proof. |

The capture bounds helper performs a GPU alpha reduction and reads only16 bytes
asynchronously. A three-surface shared ring copies each completed crop into a
small widget texture before releasing its lease. It avoids reading a full HUD
back to the CPU each frame. Queues are rate limited and round-robin; empty art
temporarily falls back to the original native draw and retries, rather than
permanently disabling a transiently culled widget. One compact diagnostic image
per slot is retained and its readback is marked in timing data.

Targeted checks passed for native candidate ordering/LOS continuation, hook
lifecycle and unowned passthrough, wrist/marker identity, deferred child capture,
GPU crop-to-texture matching after ring reuse, full wheel pointer/pixels,
distant markers in both eyes, authored camera pair qualification, and real
FSR4 Native/Quality/Balanced GPU wrapper dispatches with SRGB/typeless inputs.
The full frozen compiler run and live startup are recorded separately in
`artifacts/feedback-v207-build-validation.json` after successful publication.

Full-body material visibility remains staged. The native setter and refresh
ABI are verified, but their safe render phase is not yet proven; activating
them from an animation worker can race rendering. The calibrated full-body
solver also requires native integration and headset acceptance. This build
must not be described as a completed full-body implementation or a verified
90FPS release.

### Live test order

1. Open an already-open safe, reach to loot with each hand, and pick it up.
   Repeat against a closed safe/wall to verify that obstruction still rejects.
2. Open the wheel, inspect every sector and cursor, select a weapon, close it,
   and confirm immediate restoration of the hands and weapon attachments.
3. Aim at food/loot and a guardian. Confirm the game's original prompt/marker
   is beside that object, stable while moving the head and either hand.
4. Look at an objective both nearby and far away; check both eyes and head turns.
5. Use each wrist watch gesture, then perform a silent assassination, a combat
   finisher and a mantle.
6. Stay in one gameplay scene and compare Native, Quality and Balanced for
   equal intervals. Match actual geometry viewport, FSR generation/fallbacks,
   native-list timing, FSR GPU cost and delivered frame intervals. Finally
   switch Virtual Desktop away/back and verify the accepted recovery path.

