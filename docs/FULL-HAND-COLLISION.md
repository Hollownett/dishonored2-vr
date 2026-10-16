# Full hand contact: experimental query response

The current coordinated v553 work tests a query-driven hand response. It does not create native physical hand actors or grip constraints. The main repair chat owns integration, the exact numbered candidate, export and game control. v552 remains a preserved engineering baseline. Headset acceptance and release readiness remain false.

## Implemented transaction

The arm driver freezes the real body/arm state and preserves the independent raw controller palette and procedural finger state. `HandPaletteContact` fits 24 palm/back/digit spheres from the actual raw palette. Each side captures a producer receipt before preparation, preserving original tracking/source clocks and owner, binding, fit, pose, reference and mapping identities. Clear, stop, restart and side revocation invalidate old producers, including preparation still in flight.

Inside the existing admitted Stage0 callback, `NativeFullHandContact` requests 24 local overlap probes through the owned native query path. These are penetration observations, not proof of swept startup clearance. The reducer limits correction to 8cm. An arm-only solve uses the same frozen state, then all 24 spheres are fitted again from its actual output and queried again. Any final witnessed penetration over 0.5mm, incomplete query, unreachable target, changed geometry, cancellation or stale input rejects the batch. A newer failed side cannot leave another side partially published.

Every observed raw and final body/subshape is captured by `FullHandTargetWitness`. The numeric signature includes the full body ID, current world/pool, pose, body flags, fresh shape identity and fixed header, and applicable component/entity identity. Each validation derives a fresh record and shape from the current full ID. Saved address values are comparison data, never addresses for a later read. Both target sets are rechecked after the final queries and after full admission validation. The host must recheck both sets at final consumption. A service caller without a witness adapter fails closed on any hit.

The response is an immutable tested palette. A final consumer receipt spans the original native upload; rebasing between model frames must preserve all tested world-space spheres. There is no later IK solve or old-plane reprojection. Corrected native attachment reads are allowed only inside the same thread's already validated and committed original-upload callback, using its exact receipt. Independent attachment reads fall back to native even while the mailbox is fresh; mailbox freshness alone does not validate live source, empty-hand ownership or targets. Public numeric acquire and validation-only scopes cannot authorize corrected attachment reads. Already committed nested readers may finish using that exact palette after cancellation; cancellation closes genuinely new readers immediately. Explicit C++ and native SEH cleanup retain ownership until every admitted reader returns. Full stop drains these borrowers before restart. `attachment_continued` and `attachment_rejected` count this boundary.

Actual clocks are refreshed after external validation rather than relying on a caller's entry timestamp. The service measures whole-batch cost and defaults to a 3ms between-query budget, with at most 48 queries per side and 96 per pair. A single native query cannot be preempted. Budget rejection, transaction states, consumed outputs and fallbacks need separate runtime reporting; activity counters do not establish visible contact or compositor performance.

## Host integration contract

Prepare and submit only a currently eligible empty hand. An equipped opposite hand does not automatically disqualify it. Preserve existing weapon, carry, ordinary grab, door, Use, locomotion and turning owners. The experimental response flag must not globally require fitted acquisition data or disable existing pickup paths. A corrected palette is applied only when the result contains an actual correction. Eligible uncorrected frames use the current raw palette; a legacy palm approximation cannot count as a successful full-hand result.

`publish(sample,now,&admitted)` returns the exact successful post-publication producer receipt under the same lock. A finger-state change may advance the side epoch, so the input producer cannot substitute for this output. Failure clears the output. Final uncorrected raw upload must check this receipt with `producer_current` plus the live source, empty-hand, binding/model and Driver gates. A clear, side revocation or restart closes that raw admission too.

Stage0 supplies `service(owner, now, cast, fullValidate, lightValidate, watch)`, where `watch(sample, fullID, shapeKey)` calls `FullHandTargetWitness::capture`. Final upload uses `candidate/consume`, validates current arm/binding/ownership plus `result.raw_targets` and `result.final_targets`, and keeps the admission through the original upload. The old pre-IK `resolve/resolve_checked` APIs intentionally return no result.

## Evidence and limits

Focused fixtures cover the actual arm driver and finger state; 48-query exact final-geometry transactions; model-frame rebasing; independent sides; producer invalidation; real threads; deterministic clock expiry; cancellation, faults and drain; and both C++ and actual Windows SEH. The separate owned native query arena exercises copied native dispatcher/filter/collector code and records unsupported shape paths and explicit service bridges. These are offline implementation and ABI evidence, not live-world or headset acceptance.

Target witnesses are numeric change detectors, not native pins or an engine-wide geometry lease. Changes between a query and its first snapshot, arbitrary in-place mesh payload edits, new unobserved obstacles and later motion remain limitations. Empty local probes do not prove fast thin-wall safety or every native shape pair. Query budget and dynamic-target latency require real runtime measurement.

The general body-local anchor/session/IK core is documented in [GENERAL-PHYSICAL-GRIP.md](GENERAL-PHYSICAL-GRIP.md). Safe native hand actor creation, mass/inertia, layers/pairs, force-limited drive and grip constraints, solver mutation phase and complete teardown/save ownership remain necessary for a physical hand attached to an individual corpse part. They are not enabled by this query response.

## Headset acceptance

Test palm, back, knuckles and every digit against world surfaces, doors and props; open/half/fist poses; corners, sliding, slow pressure, fast approaches and deep starting overlap; moving/rotating targets; opposite-hand weapons and carrying; tracking/focus loss, release/regrip, snap turns, reload and exit. Record actual admitted outputs, missing receipts, unsupported geometry, fallback frames and worst contiguous compositor cost. Preserve accepted locomotion, turning, wheel and VR entry/exit. Headset acceptance and feature-complete claims require these results.
