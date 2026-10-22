# H5 action camera alignment candidate

Experimental, headset verification pending. The default-on `action-camera-alignment` feature has one rollback file,
`build/legacy-action-camera-alignment`, read once during StartProbe. The independent `legacy-authored-camera` file still
selects native authored rotation and its existing FOV policy. TAA, input, native ownership, ordinary movement and turning
are unchanged.

## What changes

Before H5, head-owned authored eyes used native centre plus eye-minus-head-centre only. Entering an action removed all
physical lean and crouch compensation even when the native centre/heading did not change. H5 passes the already bounded,
carry-adjusted and crouch-compensated gameplay `head_offset` to `authored_head_view`. The first per-frame callback freezes
that offset for the two authored eyes and the listener. It is transformed once through recenter yaw and held action heading,
using the same mapping as the pupil displacement. No absolute standing HMD height or second crouch lift is added.

The optional physical-offset argument defaults to zero, preserving the old helper API. Pupil validation remains independent:
eye-minus-centre must be finite and at most 0.15 m. Physical displacement must be finite, horizontal radius at most 0.45 m,
and vertical in [-0.7, 1.2] m (with 1 micrometre numerical tolerance). The vertical envelope follows the existing gameplay
clamp [-0.7, 0.3 + lift], with native crouch lift capped at 0.9 m. Invalid values reject the authored view.

A context key includes player, camera, generation, pose epoch, reference epoch and authored ownership. Each change reseeds
height smoothing from the new source and supplies a fresh history epoch to held heading. A camera change below the teleport
or heading-snap thresholds therefore cannot inherit another source's lag/heading. Same-context smoothing and the 30-degree
snap rule remain unchanged, including authored in-mode smoothing. Snap counters remain cumulative. Explicit smoother resets
use the existing reset API, which does not increment its automatic-reset counter.

The original per-frame callback eligibility remains intact (paired eye 0, or the alternate single-view callback). Only that
callback may start a context. Same-frame callbacks share the context; a changed owner/reference/mode second eye is rejected
before anchor preparation rather than mixing two contexts in one pair. Repeated callbacks do not advance heading/history;
height remains cached by frame as before. The next eligible frame can accept the new context. The rollback file skips the
new context policy and passes a zero authored physical offset, retaining pre-H5 behavior.

## Evidence and limits

Source evidence establishes loss of physical offset at authored handoff and missing owner/reference/mode invalidation in
heading/height history. Saved captures contain aggregate authored counts and no matched per-frame placement proof; they do
not establish the cause of the user's particular climb/kill. No new engine state or native ABI was inferred or introduced.

Equal native centres, equal level headings and equal calibration now give equal gameplay/authored physical displacement.
This is conditional: gameplay turning compensation or a different authored heading can rotate a nonzero offset at handoff.
The preserved 30-degree heading re-aim can likewise move the offset around the native centre. CPU tests deliberately show
these remaining differences rather than claiming universal world-space continuity.

The smoother still allows up to 0.20 m of lag during ongoing gameplay or authored motion. Context resets remove inherited
lag at entry/exit but do not bypass in-mode smoothing. Gameplay-owned mantle detection and smoothing retuning are outside
this candidate. Existing bounds/carry adjustment do not establish clearance from victims or walls during authored actions.
Headset placement, collision acceptability, comfort and complete gameplay need human verification.

## Future evidence

The existing `native-camera-trace` hook's `rendered-head-anchor.csv` appends `authored`, `action_camera_alignment`,
`action_history_epoch`, and `physical_x/y/z`. Existing source/anchor/render fields remain in place. The physical columns are
the shared authored argument (zero under rollback), in XR metres; they are not an additional transform or necessarily the
final gameplay displacement. Correlate with `native-authored-camera.csv` for the validated raw native midpoint, since the
rendered source Z is already smoothed. Startup status exposes `action_camera_alignment_enabled`. No new telemetry subsystem
or default trace collection is added.

## CPU checks

`authored_view_check` covers identical-centre/heading gameplay parity with nonidentity origin yaw, compensated lean/crouch,
64 mm stereo, centre midpoint, default-zero behavior, independent invalid physical/pupil input, divergent heading/turning
compensation and re-aim residuals. `action_camera_alignment_check` covers each identity component, entry/exit, changed second
eye rejection, duplicate callbacks, bit-identical ordinary in-mode smoothing against baseline, and actual audio conversion.
Existing authored-heading and anchor-smoother regressions remain applicable. The routing check verifies the integration
passes the same compensated argument to eyes/listener, gates context before anchors, retains the rollback/native paths and
uses frame-shared filter state. The new C++ test is registered with existing project MSVC flags.
