# v221 repair candidate and issue ledger

**REJECTED / LAUNCH BLOCKED (2026-10-11).** The headset rejection overrides the historical candidate/check claims below. See `artifacts/live-v221-review/20261011-headset-rejection/review.json` and `docs/v221-confirmed-defect-repairs.md`. The 43 fixtures did not verify native callback order, visible gait, player dimensions, item stability or sustained headset performance.

v220 failed headset acceptance. v221 contains implemented repairs and a bounded
observer for the remaining failures. It is a controlled-test candidate, with
`headset_acceptance=false` and `release_ready=false`. Actual native gameplay,
lens visibility, comfort and sustained speed remain unverified.

Candidate identity and complete build/source paths are in
`artifacts/repair-v221-test-candidate.json`. The installed executable remains
pinned to SHA256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
The build compiles the working tree; its archive is verified unchanged during
compilation. It is not compiled directly from an extracted immutable snapshot.
The normal demo candidate, numbered builds, user settings and saves are preserved.

## Evidence and implementation

The observation baseline is
`artifacts/live-v220-review/20261010T211523Z/`, plus the completed v220 run at
`artifacts/runs/20261010T213747.038079Z-pid38936-arm-v220/`.
The complete private-desktop run passed 43/43 checks in 75.4767 seconds:
`artifacts/playground/runs/20261011-001257-4310fe0f/results.json`.
Its Corvo/Emily replay sheets were visually inspected as offline fixture output.
The full required build log is `artifacts/playground/build-v221-final.log`.
Earlier failed interface/shadow attempts remain archived with their logs.
Final validation is `artifacts/repair-v221-validation.json`: the DLL/provenance
hashes, ZIP integrity and all 556 archived input files were rechecked. Current
compiler inputs match that archive, and preserved v217/v220 manifest hashes match.

| Issue / user observation | Confirmed evidence and hypothesis | Actual v221 change | Offline validation | Remaining acceptance |
|---|---|---|---|---|
| Calibration displaces body; joints/wrists misalign | Parent-rotated joints were adjusted through local Z as if it were world Z. Fixed palm attachment was counted in limb scaling. The actual floor remains unknown. | Apply world displacement through inverse parent rotation, preserve camera, fit true upper-arm shoulders and limb-only palm span. | Driver checks the accepted v220 dimensions, requested spans, camera preservation and mapping; both native character replays pass. | Same-frame palms/controller comparison before/after fit, physical body placement, skinning and floor. |
| Legs trail even before fitting | The driver capped elapsed time at 1/30 s, so 10 Hz/5 Hz advanced only1/3 or1/6 of wall time. | Consume monotonic wall time with interpolated substeps<=1/30s, max15; reset/replant on gaps>.5s or reversed ticks. |90/30/10/5Hz timing, gait progression, gap/reference resets pass. | Natural walking, turning/crouching and foot placement in the headset. |
| Aiming hand disappears with tracking | Arm-before-body callbacks could use stale revision/age output. Culling/material causes remain possible. | Reject stale output; use a fresh complete tracked arm outside current/prior authored ownership. Record actual submitted fallback landmarks. |30-frame arm-first sequence, sleeve bases, loss/stale age and CarryCorpse ownership veto pass; original five-argument weapon compute signature retained. | Actual callback order, geometry visibility, culling and either-eye hand visibility. |
| Sustained5–7FPS and unresponsiveness | v220 later CPU intervals were~90ms without capture.14,591 material failures are not IK/GPU failures. No matched FSR comparison exists. Repeated refresh/discovery are plausible costs. | Same-owner native refresh retry bounded to 250 ms; owned visibility-only path avoids refresh. Physics discovery 256 slots/publication/hand with 64 lifetime-revalidated nearby candidates. Add branch and hook cost diagnostics. | Refresh/deferred/restoration/foreign edit fixtures and bounded discovery/lifetime checks pass. | Matched native scene/configuration timing. No speedup or complete root cause established. Full discovery takes 64 slices; new targets can be delayed. |
| Pickup registration/prompt mismatch | Native Use and physical hover have different identities; zero ranked sorts did not mean zero hits. Two coins were acquired in v220. | Query outcomes distinguish incomplete/empty/single/multiple/rejected. Qualified native target exports identity+anchor together, revalidating generation/spawn/freshness. Prompt placement uses that target or a matching native marker. | Native range/order/two-hand/lifetime/blocked permission fixtures pass. | Actual cue and acquired object agree for coins/container loot/usables/either hand. Dynamic physics LOS seam remains unproved. |
| Markers drift and objectives duplicate flat HUD | Native prompt/marker frame, epoch and target identity matter. Projection acknowledgement alone does not prove lens presentation. | Bind prompt anchor to current qualified native target/reference publication; preserve target-specific both-eye presentation acknowledgement and fallback. | Native child binding/capture/world-UI/fallback fixtures pass. | Correct guard/object/objective anchors and no persistent flat duplicate, including occlusion. |
| Guard carry fully obscures RIGHT eye | Separate stereo geometry/render ownership defect. No captured same-frame palette proves its cause. | Preserve authored carry/attachment priority; new observer records palette/model/owner/callback evidence. No speculative carry geometry mutation. | Authored ownership/range/attachment regressions pass. | **Unresolved:** controlled left/right-eye carry/turn/drop and choke/kill prompt checks. Abort on obstruction. |
| Health/mana uncertain attachment; health/ammo too large | Wrist native layer has separate physical scale and fitting. Projection captures omit wrist quads. | Halve physical wrist scale .22 to .11 exactly once before fit; keep native canvas/crop and both-eye containment. | Half-width simulation and recovery/resource ownership pass. | Correct wrist/hand attachment and usable lens placement/readability; health/mana always visible as requested. |
| Pistol reticle large; wheel keyboard glyphs | Exact native gun constructor and wheel keyboard branch identified in extracted movies. Quest texture swaps alone cannot replace text glyphs. | Exact full-hash/same-size owned-load edits set native gun root scale .5 and route wheel through existing Xbox bitmap assets/Quest replacements. | Actual movie hash, edit isolation, path normalization, parse/idempotence/mismatch checks pass. | Native movie load counters, reticle appearance and correct VR glyphs. Already loaded instances are not retroactively changed. |
| Blue pointer duplicates native cursor | Extra XR pointer quad added a second visual. | Remove extra menu/wheel/native-interface blue quad; preserve native cursor UV/trigger routing. | Simulated native cursor active with zero extra pointer layers; wheel controls pass. | One effective cursor; hold X/select/release equips once, R3 zoom only, fresh-trigger alternate path. |
| VR Settings must use actual game assets | Native OptionsScreen/OptionsData/widgets identified. Hub0x34800 returns dispatch success, not typed AS3 data. Provider callback marshalling remains unproved. | Add full native movie identity guards and preserve current settings behavior. No guessed bridge or new imitation. | Native menu hash/lifecycle and existing dispatch checks pass. | **Unimplemented:** replace GDI VR Settings with native assets/widgets and verified data/update bridge. Research: `artifacts/native-vr-settings-research-20261010.json`. |
| Hard red borders and blackouts/stop restoration | v220 wrist recoveries were initial headset-unavailable retries; no later wrist-triggered recovery logged. This is not acceptance. Old process restoration was still pending. | Preserve damage isolation, typed wrist cache/nonblocking acquire, state restoration/GPU retirement. Repair interrupted owned material restoration retry without foreign writes. | Wrist fault, native HUD fallback/child isolation, interrupted restoration and resource-state fixtures pass. | Heavy damage edges, pause/load/focus, clean stop/resume/start with explicit restoration result; no blackouts. |

More source/research detail is retained in `docs/body-v221-repair.md` and
`docs/interaction-ui-v221-repair.md`. Native ownership/ABIs stay pinned to the
installed PE; no borrowed-resource layout or call convention was guessed.

## Observer contract

Enable with `-SynchronizedDiagnostics`. Raw head/grip/aim poses, flags,
LOCAL space, prediction time, reference/pose epochs, tracking revision and
sampled native counter are published together. The owned native consumer records
its exact submitted palette, native owner/model/world frame, final reconstructed
palms/wrists/pelvis/feet, solver timing/fit/ownership and interaction/cost samples.
The intended palm target is the mapped world grip; grip-to-attachment transform
is recorded. Fallback palette landmarks are reconstructed from actual submission.

The 128-record ring uses try-lock producer publication,10Hz per consumer kind and
dropped-record counters. Worker-only serialization has a 64 MiB file limit.
Finite-to-null conversion keeps JSON valid. The helpers record exact selected
PID/game/DLL path/base/hash and stop cleanly on identity change, stale status or
owned `stop.request`. They run at Idle priority; no periodic captures by default.

`publication_consistent` is raw/solver publication consistency, **not** actual
same native frame receipt: `consumer_frame_verified=false`. The consumer counter
and Present are separate. Culling, measured floor and lens visibility remain
explicitly unobserved. Separate grab/native-Use snapshots retain their own
timestamps. Native permission/LOS and actual XR presentation acknowledgement are
not all present in one serialized atomic record. This observer is not a complete
scene replay, continuous video or compositor capture.

## Controlled next test

Follow `docs/VR_REPAIR_TEST.md`: normal exit of the old stopped v220 game first,
then a fresh exact v221 session. Start with 120 seconds of performance/body
checks. Abort broader acceptance immediately on sustained slowdown, broken fit
or right-eye obstruction. Keep the PC usable during development; all validation
in this repair used sequential bounded low-priority fixtures without live-game
input, save loads, real XR session or foreground control.
