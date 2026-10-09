# Avatar calibration and same-session checkpoint verification

Tracked arms now require an accepted measured fit from the start of each VR
session. An unmeasured or invalidated fit cannot publish retargeted poses.
On the first eligible native Pause page, the calibration page opens once;
press A and hold the instructed pose. Cancelling leaves native menus usable
and the arm gate closed. A newly invalidated accepted fit prompts again on
the next Pause page. Loading, Journal and other native pages do not open it.
Unit checks cover the initial gate and prompt/retry lifecycle; native v91
verification is pending. Earlier pre-wizard arm evidence used the old policy.

Measurements and runtime fit status are separate. A retained ready measurement
may be suspended while the current native graph/resources are checked. The VR
settings row and calibration page now show `Checking avatar - fit suspended`,
`Measured fit active`, or `Recalibration required`. Capturing/tracking/error
instructions remain visible while a wizard runs. Floor height remains unknown
without an independent datum. Native model-unit conversion is still unverified.

`avatar-fit-events.csv` records changes only, at most256 rows per probe, and is
archived by `inject_probe.py`. It contains event sequence/time/present/reason,
XR reference/pose epochs, accepted reference, game logic mask, runtime and
wizard statuses, pose gate, resource-pending flag, accepted/active/template
flags, accept/reuse/invalidate/suspend counters, native dimensions and current
scale/shoulder drop. Each arm records player/body/animation/model/skeleton,
body/animation generations/flags and resource source/primary/surface identity
fingerprint/count. The surface fingerprint is diagnostic FNV, not a cryptographic
asset certification. Header/runtime status integers are unmeasured0, verifying1,
active2, recalibration-required3; wizard enum follows `avatar_calibration.hpp`.

Suspension, owner replacement, template verification, acceptance, reuse,
invalidation and gate updates are recorded at their actual transition points.
Changed reference/pose epoch or wizard state is also recorded. Stable frames
produce no rows. The result JSON reports event rows/dropped changes and an
explicit500ms resource-check bound. Dropped changes make chronology incomplete.
Resource/scale mutation detection is periodic; this is not immediate callback
revocation. Native upload owner verification remains independent.

## Parent-operated native verification

Use one running XR probe session. Reinjecting constructs fresh GameXr/reuse
state and cannot prove same-session reuse. Capture the explicit wizard first,
then load a native checkpoint while that same session continues. Parent owns
all game input and loading actions.

The existing simulated native wizard fixture requires gameplay at initial
injection, tracked arms, no physical-grab/native-left-aim/gameplay-script option,
and native_menu_mode other than2. It pauses8–8.2 seconds; publishes raw T-pose
8.5–14; opens settings at9; selects the avatar row at9.5/10/10.5; starts capture
at11; completes after at least20 samples and one second; closes at14 and resumes
at14.5. A paused initial state or gameplay-script mode skips this wizard fixture.
Allow at least15 seconds before parent checkpoint input and enough remaining
duration for native loading plus verified resource refresh.

Alternatively use the actual VR settings wizard with tracked physical poses:
upright, looking forward, controllers held sideways level at shoulder height
for one stable second. Record the resulting measured span/shoulder drop and
accept event before initiating reload. Do not substitute the presence of arm
uploads or a template verification event for accepted calibration.

Expected positive sequence is acceptedfit+active+gate, loading/owner suspension
with gatefalse, fresh owned model/animation tuple, matching complete template in
the same reference epoch, reuse counter increase, then active+gatetrue with
identical accepted physical dimensions/scale/drop. Shared resource addresses,
69 labels/hierarchy/inverse/bind mappings, model scale and native proportions
must match. Reallocated otherwise-identical mesh resources deliberately reject
reuse. Pointer/address change of the runtime model alone is permitted only after
fresh complete owned verification.

Expected negative sequence for a changed template or XR reference is fit
invalidation and recalibration-required, no active fit or pose gate until a
new accepted wizard result. Incomplete capture interrupted by loading/focus/page
exit must cancel and never finish later while hidden; an interrupted same-space
retry may retain the earlier accepted measurement while still subject to the
runtime verification gate. Recenter is not calibration.

Acceptance requires event_dropped0, actual owner replacement/loading evidence,
same session/reference epoch for the positive path, chronological gate states,
matching current/accepted metrics, and resumed native pose output afterward.
The event log records control/gate state, not a frame-atomic proof of every
worker upload during a gap. Final ever-applied and accumulated counters alone
are insufficient. v82 had accepts0/appliedfalse and is not calibrated reuse
evidence.

## Isolated validation

Current avatar calibration/reuse tests pass MSVC /W4 /WX. Event tests cover
transition order, reference/owner/resource changes, tick/present de-duplication,
256-row cap/reset and dropped-change reporting. Settings tests render runtime
states despite ready measurements; the verifying preview is visually checked.
Production GameXr WARP tests cover retained ready measurement with suspended
runtime status, explicit wizard, retry cancellation, STOPPING, focus/reference
changes and1000-frame resource cleanup. Their reports/previews are isolated in
the ignored `native-arm-assets/calibration-reload-review` directory. Actual
native wizard-then-checkpoint acceptance remains parent-operated evidence.

## Late loading-ready confirmation fixture

`SimulatedLoadConfirm` now handles each new focused native logic32768 Ready
appearance independently of diagnostic uptime. It holds neutral input for
500ms, emits one200ms native A confirm window, then remains neutral until Ready
leaves. Focus loss, leaving Ready or a backwards clock resets the window. With
sparse frames the pulse starts on the first observed sample after the wait,
rather than being skipped because the first700ms of the session elapsed.

Integration is restricted to `simulated_input_requested`: it replaces the old
absoluteelapsed500–700ms loading A line, resets when that simulated session
starts, and leaves native pause/menu commands and gameplay-script dispatch
unchanged. A later Ready after checkpoint loading works even when the fixture
initially started paused. Real input/backend and camera paths are unchanged.
No extra injector options or configuration fields were added.

`tests/simulated_load_confirm_check.cpp` passes initial/late/repeated Ready,
neutral priming, single pulse, focus/leave/backwards-clock reset and sparse-frame
checks. It also exercises the actual PadComposer activation-release gate for
an initial and second checkpoint confirm. MSVC /W4 /WX and main syntax checks
pass. Live same-session loading confirmation remains parent-operated evidence.

## Death startup and first eligible Pause (v96 audit)

Run `20261009T043813.501232Z-pid44016-arm-v96-failed` reports logic8192→0→32768→2→8192→8200, no exactPause4. Runtime status3 means `recalibration_required`, not an unavailable latch. Wizard state0 remains idle; no settings layers or fit accepts occurred. Two verified templates have different primary players, showing that the new rig was discovered. Template geometry is not a measured human T pose and cannot create a fit. This run does not exercise wizard reopening at all.

`tests/avatar_death_reload_wizard_check.cpp` exercises actual GameXr with WARP and simulated OpenXR: initial non-pause Death/Loading pages, suspension/new rig availability during gameplay, first eligible focusedPause4 neutral priming then automatic prompt, explicitA start with invalid default grips (no fit), and explicit measured T-pose retry (ready fit). Isolated MSVC/W4/WX test passes. No runtime change was needed for the tested startup transition. Machine proof: `artifacts/avatar-death-reload-wizard-audit-v96-20261009.json`.

The next actual acceptance step is a focusedPause4 after restart with active tracked input: allow a neutral frame, expect the required calibration page, then pressA and hold the instructed T pose for at least1s. `fit_accepted` and active pose gate must follow a real measured result; no scripted T pose means zero accepts remains correct. Production currently does not automatically pause gameplay to show calibration. Whether launch should require that initial pause is a separate UX decision.

One distinct untested edge remains: `VrSettings::calibration_prompted_` intentionally survives `suspend()` to avoid nagging after focus loss or deliberate dismissal. Only an active fit clears it. A previously displayed unavailable/dismissed wizard followed by a new owner while runtime status remains required has no explicit prompt rearm. This is not demonstrated by the run above (its prompt never opened). If this behavior should re-prompt on actual owner/resource/reference change, add an explicit identity-change notification; do not reset it every frame or on ordinary focus loss.

## Explicit identity prompt rearm (structural edge fixed)

The separate edge above was reproduced in actualGameXr/WARP before the fix: show a required wizard without native measurements, pressA (unavailable), dismiss it, suspend, make new measurements available, return to focusedPause. Ordinary suspension correctly left the dismissal latch intact, so the simulated new-owner path did not reopen. This was an isolated structural test, not a claim that the043813 actual run displayed or dismissed a prompt.

`GameXr::avatar_identity_changed()` now clears native measurements and the stale wizard candidate/result, then calls `VrSettings::calibration_identity_changed()` to rearm the required prompt and neutral-control priming. Ordinary `suspend_avatar()` and `VrSettings::suspend()` still preserve dismissal across focus/loading suspension. Qualified active fit reuse remains authoritative and suppresses a prompt, even if the old wizard state records rig_changed. GameXr also detects actual XR reference epoch changes before updating settings, clears old-reference capture via synchronization and rearms once; native dimension measurements are retained for this same-geometry reference change.

Root integration points (main changes owned by root):

- In `last_avatar_rig != rig` / owner_replaced, notify for a confirmed nonnull new owned rig. Missing rig/loading/focus alone only suspend. A newly acquired rig after load is a confirmed identity to notify.
- In fit_invalidated, a verified complete different template/resource identity can notify. A failed resource read alone is uncertainty, not a confirmed change: suspend and wait. If there is no accepted fit, retain the last verified resource/template key across the pending retry so the new verified key can be compared before notifying.
- Apply compatible accepted fit reuse and its active runtime status before the next settings update, so an accepted same-assets fit does not unnecessarily reopen a wizard.
- Do not call the identity notification every frame, at everyPause, or on ordinary focus return.

`avatar_identity_prompt_check.cpp` now passes: ordinary suspension does not nag; explicit new identity prompts; actual measured stale candidate is cleared; validated active reuse suppresses prompting; an actual simulated XR reference event rearms a dismissed notice. Registered alongside `avatar_death_reload_wizard_check.cpp`. Existing VrSettings, AvatarCalibration, AvatarFitReuse, XrContinuity and production GameXr WARP/1000-frame checks pass underMSVC/W4/WX. No native writer or camera/query change is included.
