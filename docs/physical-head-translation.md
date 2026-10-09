# Physical head translation: staged math and collision requirements

The runtime reports roomscale_translation_applied=false accurately. GameXr
locates eyes and controllers in LOCAL, but native rendering retains only eye
separation and orientation. The same head-centre subtraction applies to hands,
aim, interaction rays and grabbing. Head-relative alignment is coherent;
stationary physical controllers move oppositely in game during head translation.

For native anchor A, yaw/axis/scale mapping R, calibrated head h0, current head h,
eye e and controller g, current positions are A+R(e-h) and A+R(g-h).
The missing common offset D=R(h-h0) yields A+R(e-h0) and A+R(g-h0).
The physical displacement D must remain coherent across eyes and every
controller-derived interaction. A future collision result governs body movement
and obstruction fading; it must not clamp D by moving the tracking origin.
HMD tracking does not measure torso lean. See the primary references and design
inference in `vr-embodiment-reference-decisions.md`.

## Staged source only

tracking_offset.hpp supplies pure request geometry and owner/reference/time
qualification. It performs no engine calls and has no production consumers.
Tracked head position/orientation and focus are required. The owner includes
player, camera and camera generation. Consumers must correlate asynchronous
collision results with the current owner, reference and request serial/frame.
The default100ms freshness bound is staged policy, not measured latency.

tracking_offset_check.cpp covers stationary and common controller translation,
crouching, XR/native yaw, an arbitrary shared offset, invalid tracking/focus,
backward/stale time and owner/reference rejection. Isolated MSVC/W4/WX passed.
Main, controllers and build registration remain with their source owners.

## Verified callback, unverified sweep

Pinned executable SHA256:
b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180.
Existing Ghidra demo-camera-update/function-1407f6140.txt proves camera update
RVA0x7f6140 takes a camera pointer in RCX on Windows x64 and returns void.
Caller0x94f9a0 iterates16-bit component IDs, resolves through0x4900b0 and moves
RAX into RCX before calling it. NativeCameraTrace already observes this callback.
Static engine-call provenance does not prove collision-world query permission
on that thread; avoid a competing hook and verify native query call-through.

Interaction0x6c6760 is a finite-ray query over three accelerator lists through
0xfd7e80, with the ray bound at+0x1c. It is not a head/body shape sweep.
The genuine native Lean shape-query path is now statically traced separately:

- Constructor0x3b9100 installs vtable0x1b40648, initializes query IDs at+0x78
  and+0x7c to-1 and limits at+0x80/+0x84 to1.
- Setup0x3b9340 reads three half-extents from declaration+0x70/+0x74/+0x78,
  constructs a bounds-derived shape through0x0ef880 and stores it at sensor+0x40.
  Shutdown0x3b93c0 releases that shape through0xa9e180, then clears the pointer.
- Producer0x3b9650 computes origin at+0x48 and axes at+0x54/+0x60/+0x6c.
  Its engine component-loop caller0x954940 resolves IDs through0x4900b0,
  passes the result in RCX, and calls the producer once per component.
- Two native calls to0x095550 queue left/right shape-bearing requests. Windows
  x64 arguments are RCX=query manager (game+0x60280), RDX=query-ID output,
  R8=request, R9=48-byte axes; stack arguments include declaration+0x84 mask
  and flags0x40. The wrapper returns the output pointer in RAX. It allocates
  a0x140-byte query job and calls0x08ffb0 with job type1. Finite-ray wrapper
  0x095280 instead uses type2. Both copy112 request bytes plus48 axis bytes.
- In this producer the112-byte request includes shape pointer+0x28, origin+0x30,
  direction+0x40, distance bound+0x4c and tolerance+0x60. IDs are stored at
  sensor+0x78/+0x7c. These offsets describe this native call, not a public ABI.
- Consumer0x3b9460 (engine caller0x950030) retrieves results through0x092630,
  reads result distance+0x20 when count is nonzero, divides by declaration+0x7c
  and writes the left/right limits. Result records are0x70 bytes and use native
  container lifetime rules. Clamp0x3b9220 takes sensor in RCX and float pointer
  in RDX; it clamps against negated+0x80 and+0x84 and returns bool in AL.
- Disable0x3b93f0 cancels queued IDs through0x091a50 and resets limits to1.

Exports are in direct-ghidra/hands-head-lean-* and hands-head-collision-*.
The adjacent0x3b8d60 five-query producer has a DIFFERENT object layout and must
not be attributed to Lean. Likewise a successful native Lean query does not
certify an arbitrary HMD head shape, collision mask or callback invocation.
There is no approved native-call wrapper or live sweep validation yet.

## Bounded runtime inspection and passive observer proposal

inspect_native_lean_queries.py checks the exact owned Lean sensor vtable,
started identity, current player/state machine, declaration, stored shape and
query-manager world pointer twice. It reports native mask, extents, distance,
IDs and limits; raw shape/world words remain uninterpreted. Four fixtures pass
exact-offset and owner/resource-race rejection. The first1s PID44016 sample
rejected all21 reads because the owned Interaction dependency was not started.
It proves no live collision resource, mask or thread observation.

The subsequent gameplay sample native-lean-query-state-v91-gameplay-44016.json
accepted21/21 owned reads. It found sensor generation59/flags536870922,
declaration half-extents0.05/0.20/0.10, query distance0.8 native units and mask
0x802b. Sensor enabled=false, query IDs=-1/-1 and limits1/1 throughout; it does
not prove active casting, collision response or engine thread execution.

NativeInteractionAim already hooks producer0x3b9650 (stage2) and queue0x095550.
Extend that existing owner with a default-off passive observer and route=false;
do not create another hook. Preserve the original callback exactly once and
observe before/after; original behavior must run even when inspection rejects.
Filter exact sensor vtable0x1b40648, current player+0x10, current owned machine
at+0x20, started identity, non-null unchanged declaration/shape and enabled+0x38.
Qualify the queue observation by thread-local owned producer scope plus native
return addresses0x3b9962/0x3b9a49: the exported CALLs are0x3b995d/0x3b9a44.
Log at most256 state changes: thread, tick, producer/consumer phase, player,
machine, sensor generation/flags, shape/declaration/world identity, query IDs,
mask/flags,112-byte request and48-byte axes. Worker results must be correlated
to issued IDs/generations; pointer continuity alone is not query lifetime proof.

For Lean, queue R9 is48-byte axes, not a finite interaction ray. The existing
Queue typedef names it InteractionRay; hand-ray substitution must not be reused
for this collision path. Preserve native R8 request and R9 axes byte-for-byte.
The existing stage context requires vtable0x1b403b8, so it currently rejects
genuine Lean vtable0x1b40648 and preserves the original. Keep that separation;
the proposal uses a distinct typed passive context, never the ray() context.
The exact mask semantics, ignored objects, shape type/radius, engine query
scheduler/thread and start-overlap behavior still require passive observations
and static worker/constructor proof before any new query can be issued.

## Implemented passive observer (default off)

NativeInteractionAim::install appends observe_lean=false. When enabled it opens
native-lean-query-trace.csv alongside the existing interaction trace. Typed Lean
stage2 has its own thread-local snapshot, separate from context_ and ray().
The unchanged native stage executes once. Only queue1 caller0x3b9962/0x3b9a49
inside that scope can produce a Lean queue observation; fresh owner/resource,
manager, mask, flags and request shape must match. Native arguments and return
values remain unchanged on both acceptance and rejection. No new hooks, engine
queries, writes or translation consumers are introduced.

CSV phases0/1 mean stage-before/after and2/3 queue-before/after. Rejection1 means
stage ownership/resource change,2 queue precheck rejection and3 queue postcheck
rejection. The postcheck includes returned-ID pointer and unchanged request/axes
bytes. Rejections count observation failures, not native engine failures; one
call may contribute both pre/post rejections. Counters expose rows, dropped
changes, rejected observations, queue observations and trace I/O failure.
Rows cap at256. The v93 sampling revision buffers a whole stage transaction
(up to8 rows), including both stage endpoints and every queue before/after pair.
It commits at most every500ms under ordinary state, and immediately on resource,
owner, enabled or rejection-state changes. It never alters whether a native
query runs. Worker thread changes, pose/ID churn and repeated rejection state
do not bypass the interval. Clock is read under the trace lock. All rows in a
selected transaction are retained without phase deduplication; an incomplete,
overflowed or over-cap transaction emits no partial rows. CSV appends transaction
and sampling_reason. Reason bits:1 initial/reset,2 resource/owner,4 enabled,
8 rejection transition,16 interval,32 clock rollback. Skipped sampling differs
from dropped selected rows. Extra counters report transactions and skipped.
No event log establishes response semantics after truncation or worker completion.

native_lean_query_trace_check.cpp exercises actual default-off/opt-in stage
detours with mocked native originals, ownership/shape/mask/caller rejection,
read races, observation exceptions, identical argument bytes/return value,
original-once passthrough and bounded/deduplicated trace. MSVC/W4/WX passes.
Native active-lean observations remain parent-operated acceptance evidence.

## v92 native ABI evidence and remaining lifetime gap

Actual archive20261009T024459.821885Z-pid44016-arm-v92 reports no diagnostic,
fault or callback failure,1142 gameplay layers,256 rows/3589 dropped changes,
1064 observed queues and0 rejected observations. The old cap fills within2.641s
before the directional stages. Its38 complete queue pairs preserve all112
request bytes,48 axis bytes, pointers, mask0x802b and extra flags0x40. Shape
pointer matches the owned sensor, distance is0.8 native units and direction/axes
are unit vectors within float precision. There is one owner/resource tuple but
six engine callback thread IDs. This is an engine job path, not proof that queries
belong to a single fixed physics thread. Queue-before query_id reads the caller's
uninitialized output temporary; only after-return and final sensor IDs are issued
tokens. Private quantitative review: native-arm-assets/native-lean-v92-abi-review.json.

Static lifetime exports are direct-ghidra/hands-head-query-lifetime and
hands-head-query-cleanup:

- Query token indexes manager+0x8280+(id&0x1fff)*0x140, but every consumer first
  compares the FULL id to slot+0x88. Lower slot bits alone are unsafe.
- Completion0x092630 takes manager/ID-pointer/result-container in RCX/RDX/R8.
  It returns false while slot+0x8e mask0x02 is absent. When complete it copies or
  transfers native0x70-byte result records, calls cleanup0x0958c0, sets caller
  ID to-1 and returns true. Container allocation/free semantics are native.
- Cancel0x091a50 takes manager and ID pointer. A complete job is cleaned up;
  an incomplete job is marked slot+0x8e mask0x01 and caller ID becomes-1 immediately.
  Therefore an ID of-1 after cancellation does NOT prove worker completion.
- Cleanup0x0958c0 dispatches the result destructor and returns the slot through
 0x0921a0's atomic free list. Destructor0x090ac0 increments slot generation by
 0x2000 and frees result storage; do not read or consume a released token again.
- Shape release0xa9e180 atomically subtracts0x10000 from packed refcount at+0x10
  when its low16 bits permit references; when high16 reaches zero it calls the
  shape vtable destructor at+0x10. Direct refcount writes are inappropriate.
- Enqueue0x095550/0x08ffb0 copies the shape POINTER into the job and shows no
  shape retain. Result cleanup0x090ac0 shows no shape release either. Existing
  sensor-owned lifetime cannot be borrowed for an independent queued head cast.
  Query-manager teardown0x097670 drains queues/releases world resources; it is
  a lifecycle teardown function, not a safe head-query worker invocation.

An independent head cast needs its own native-created shape kept alive for all
pending jobs, verified worker completion/drain before release, full token and
owner/reference correlation, and engine lifecycle serialization. A cancellation
flag or pointer continuity alone does not meet that requirement. Keep all query,
cancel, shape creation/retention and resource-write integration disabled until
the runtime worker cancellation acknowledgement, scheduler phase and world lifetime are
proven. Observe completion and cancellation call-through before issuing requests.

Worker dispatch is now statically resolved in hands-head-query-worker and
hands-head-query-dispatch.0x0998c0 atomically dequeues the query-slot index,
tests cancellation mask0x01 BEFORE execution, and either calls0x0958c0 directly
or0x096610. Dispatcher0x096610 executes the variant then sets completed mask0x02.
For Lean, job type+0x8c is4 and variant+0x8d is1; it runs0x096ac0, whose native
world shape-cast call is0xc89bc0. This proves the asynchronous shape-bearing path.
It does not prove that cancellation concurrent with an executing cast fences
the shape. Native scheduler phase ordering remains essential.

Smallest defensible independent head-sweep proposal:

1. Add passive completion/cancel observations in the verified native query
   owner, restricted to FULL issued Lean tokens captured by the current trace.
   Preserve each original once. Record completed/pending return, token reset,
   before/after owner/world and actual engine thread; never dereference result
   storage after native cleanup. Observe setup/shutdown of the owned sensor
   rather than hooking the high-traffic generic shape destructor globally.
2. Prove where the query scheduler joins worker jobs and consumes results before
   resource teardown. No retained native Lean shape is a substitute for this.
3. Once explicitly authorized, create a separate native-owned head volume using
   the verified factory on an allowed engine callback, retain it through every
   pending query, and keep its full native token/result container separate from
   Lean IDs and state. Do not overwrite or borrow the native sensor's shape.
4. Queue bounded desired-head displacement with verified collision filters and
   self exclusion; associate a full tracking reference/owner/frame request.
   Consume results only in the appropriate owned engine scheduler phase. Stale
   or missing replies produce conservative fade/no positional approval, not
   an unguarded translation. Native unit conversion/volume clearance still
   require physical acceptance.
5. Publish one immutable common offset/fade decision for both eyes and all
   controller-derived poses. Cancel/drain and reset pending requests on loading,
   reference/owner changes and shutdown before releasing native resources.

Only stages1/2 of that proposal are passive research. No head query, shape
creation, refcount write, cancellation call or physical translation is enabled.

Before integration, prove shape construction/lifetime, query and result layouts,
collision masks, ignored player objects, world ownership and permitted engine
thread. Present/render only publishes requests and consumes immutable results.
Validate head-volume clearance, initial overlap, dynamic obstacles, stale replies,
loading and authored camera transitions. Preserve native locomotion/turning.
Native units-per-metre remains unverified; LOCAL supplies no floor datum.

## Primary guidance

[Khronos LOCAL reference space](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_REFERENCE_SPACE_TYPE_LOCAL.html)
defines a world-locked gravity-aligned origin and distinguishes valid from
tracked position. Recenter changes are time-qualified events.

[Godot room-scale guidance](https://docs.godotengine.org/en/stable/tutorials/xr/xr_room_scale.html)
separates physical and controller movement, processes collisions in physics,
and counters origin displacement when following the body. It describes fading
when physical motion exceeds permitted virtual motion and an explicit ready
action before recording the initial pose. These references supply no native ABI.

Choose coherent collision/fade behavior before activation. Full room walking
also needs native body movement/collision and turning around the current physical
position. Staged head-offset math does not establish complete room scale or
headset acceptance.
