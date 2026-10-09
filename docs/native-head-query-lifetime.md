# Independent head query: passive lifetime proof and integration plan

This is an implementation plan against Steam build17576867, SHA256
`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
No head query, AddReference, Release, cancellation or positional translation is
enabled. `native_head_query_lifetime.hpp` is a pure evidence policy; it performs
no memory access and does not grant permission to release native Lean resources.

## Exact ABI and retirement boundary

| RVA | Windows x64 arguments and return | Verified behavior |
|---|---|---|
|096610|void(manager RCX, slot RDX)|Dispatches kind+8c; kind4/variant+8d=1 calls096ac0; sets completed bit2 at+8e after cast returns. No token increment.|
|094970|void(pool RCX, slot RDX)|Pool is manager+200; atomically publishes the initialized slot index. Caller095600 is the shape queue's return address.|
|0998c0|RCX worker wrapper; manager at wrapper+8; other incoming arguments not semantically established|Atomically dequeues index, reads slot cancel bit1 before execution; calls0958c0 at0999a1 or096610 at0999a8. Original return addresses0999a6/0999ad distinguish branches.|
|0958c0|void(manager RCX, slot RDX)|Kind4 calls090ac0 at095926, return address09592b; then tail calls0921a0(manager+200, slot) to return pool slot.|
|090ac0|slot* (slot RCX, uint64 flags RDX)|Adds2000 to token+88; frees result records; returns original slot in RAX. With flags0 it does not free the slot itself.|
|091a50|void(manager RCX, uint32* ID RDX)|Full-token guard; complete jobs cleaned immediately, otherwise sets cancel bit1; both set caller ID toffffffff.|
|092630|bool(manager RCX, uint32* ID RDX, native result container R8)|Full-token guard; false while incomplete, otherwise copies/transfers result storage then cleans slot and sets ID toffffffff.|
|a9e120|void(shape RCX)|If low16 of packed+10 is nonzero, lock-CAS adds10000; no allocation and no return-value ownership token.|
|a9e180|void(shape RCX)|Paired packed refcount release; destructor when upper16 reaches zero.|
|097670|void(manager RCX)|Drains queued slots and releases world+190/resources, but does not locally show an executing-worker join.|

The slot address is `manager+8280+(full_token&1fff)*140`. Every association must
match the full token at+88, kind4, variant1, request shape at+B8, and the captured
manager/world/owner identity. Before-queue output-ID contents are uninitialized
and must never be registered as tokens.

Observe token increment inside090ac0, called under a typed0958c0 TLS scope. At
090ac0 return the pool has not yet received the slot; exactly `old+2000` modulo
uint32 is attributable to this cleanup. At0958c0 return, another thread could
already reuse the slot. Never infer retirement from a changed generation read
later, a caller ID of-1, or a teardown callback alone.

## Smallest passive hook set

Keep existing typed Lean stage/queue observer as the only eligibility producer.
For diagnostics, track at most eight native issued tokens. Overflow means the
audit is incomplete; keep original behavior and report it. Do not sample token
lifecycle transitions at500ms: every transition of a registered token matters.
Use bounded256 event rows plus dropped/rejected/overflow counters.

0. Hook094970 as `void(__fastcall*)(uintptr_t,uintptr_t)` under the exact existing
   typed Lean queue TLS scope. Require pool=manager+200 and caller095600; verify
   initialized slot, full token, kind4/variant1, shape, request bytes and world.
   Register the key BEFORE original enqueue publishes it. 095550 calls094970
   BEFORE writing the returned ID; registering at queue return can miss worker
   execution entirely. Reconcile the native returned ID against this record.
   Never register the uninitialized ID pointer contents.
1. Hook096610 as `void(__fastcall*)(uintptr_t,uintptr_t)`. Filter before memory
   reads against a registered manager and derived slot, then snapshot full token,
   kind, variant, flags, shape and world. Record worker_begin, call original once,
   record worker_end from the immutable entry key after NORMAL return. Capture
   caller RVA0999ad and thread. Do NOT reread slot bytes after return: the engine
   sets complete before returning, so another thread may already consume, clean
   and reuse it. Static096610 control flow establishes cast-return completion;
   observe actual complete bit2 at a guarded cleanup/consumer entry instead.
   Never inspect result storage here.
2. Hook0958c0 with the same two-argument void signature. Exact registered key
   establishes a scoped cleanup context containing manager, slot, old full token,
   preflags and caller. Preserve any nested scope. Worker cancellation is proven
   by caller0999a6 plus cancel bit1 and no prior worker_begin; normal completion
   requires worker_end. A teardown caller097763 is diagnostic only.
3. Hook090ac0 as `uintptr_t(__fastcall*)(uintptr_t,uint64_t)`. Accept only within
   that cleanup scope, same slot, flags0, caller09592b. Record before and after
   full token, return pointer, empty result count/pointer, and original once.
   This is the retirement proof BEFORE slot pool return. Do not reread the slot
   after the outer cleanup returns. Cleanup can race ahead of the observer's
   worker_end row because complete is set inside the original. Buffer its
   immutable retirement evidence and resolve after the same registered worker
   invocation returns; never retry by reading the reused slot. A cleanup seen
   before worker_end is not proof that the native cast was still executing.
4. Hook091a50 and092630 only for exact registered manager/ID pointer FULL tokens.
   Cancellation observations mark cancelled but never retire. Consumer success
   records a bounded copied result summary; native container ownership stays
   native. Capture IDs before the original call because cleanup changes them.
5. Hook097670 only for the registered manager. Before original: invalidate
   result publication, record current world and number of pending/active keys.
   After original: record world pointer cleared and unresolved-key count. Do
   not dereference old world/slot memory after return. Any active token during
   teardown disproves an assumed join and blocks independent query activation.

Every hook preserves original arguments, result and original-call count. Generic
high-frequency callbacks reject unregistered pointers without reading them.
Hook installation needs pinned instruction signatures, existing callback
drain/restore behavior, and original-once tests before any live observation.
The isolated policy does not install hooks. The default-off lifecycle observer
described below installs a bounded subset without any query/resource calls.

Suggested event columns: sequence/tick/thread/event/caller, player/owner-generation,
reference-epoch, manager/world/shape, slot/full-token/kind/variant,
before-flags/after-flags, before-generation/after-generation, native-return,
active-count/pending-count, rejection. Keep the request identity from the issued
record immutable, so an old owner can finish retiring after a new owner appears.

## Scheduler and world-load evidence

New exact exports are in direct-ghidra/hands-head-retirement,
door-keyhole-consumers-20261009, head-query-scheduler-final and head-query-task-entry.
Manager shutdown8533e0 passes callback pair `(manager,853b60)` through811040 with
command1c0, unregisters world callbacks, then tail calls097670. Bounded raw
decode proves853b60 unpacks a component/flags payload then tail-jumps853220.
That target dispatches component vtable+1a0 or component+b4 activation flags.
The pair is consistent with an activation-command handler registration bridge,
NOT a worker-drain request. It does not establish a join.

Manager update097bc0 creates a scheduler object with table1ad5748, stores manager
at+8, starts a native task via07a9b0, runs world update, invokes07aca0, and later
submits task descriptors throughaa96f0/aab400 before094700. Raw decode shows its
098a70 method calls0908c0, which builds descriptors over manager+148348.
This is a different world-task path, not the0998c0 collision worker. Its07aca0
wait cannot be used to claim the collision worker joined. aa99c0/aa96f0 are
descriptor/reference builders, not a proven worker join.

Exact PE contains a single absolute0998c0 pointer at RVA1ad5260. Surrounding
callback table1ad5250 contains a953d0/c8900/0998c0/aa0aa0. Its actual producer is
now verified as096240(manager RCX): it builds a stack descriptor `{table, manager}`,
adds that single descriptor throughaa96f0, invokes07a9e0→aab400, then destroys
the task list. The completion graph builderaaacc0 returns its root task handle
from descriptor+10; with one worker descriptor, that worker is the root.

Bounded read-only PID44016 inspection verifies factory global22819c0,
vtable1ad11f0, getter7b840, and its direct fields factory+8→owner+498. The
returned context+18 has scheduler vtable1ad5e08. Method+58=a2e70 enters9f7f0 or
a01b0; both execute/wait for tasks until the root task's word+4 becomesffff.
The completion mark follows the task's pending/running counts reaching zero.
Afterward, method+10=a2a10→a2780 RECYCLES completed handles; it is not the wait.
No getter or scheduler method was called by the inspection.

Game callback8af8a0 invokes query producer1c02e0,096240(game+60280), then native
consumer1c08e0. These are general world/entity query lists; they are not proof
that every Lean producer belongs to this exact phase. They establish a concrete
engine-owned collision-worker join candidate. The other096240 callers781d00
and7b7be0 also use native query batches. A passive096240 entry/normal-return
observation restricted to the exact owned manager should next correlate issued
tokens, worker callbacks, queue cursors and unchanged world. It must not invoke
the scheduler, create queries, or read recycled root-task handles after return.

This establishes ordinary-frame join control flow, not world-load serialization.
Loading still needs an observed owner/world boundary and static caller ordering;
another producer could publish after a worker found the queue empty. The
independent request must be issued in a verified native query phase, followed by
the matching join and owned consume phase, rather than relying on elapsed time.

The proposed passive hooks can establish whether every monitored native Lean
token retires before teardown and on which callbacks/threads. They cannot prove
the full engine's join solely from a bounded token sample. Static scheduler
dispatch plus runtime owner/world boundaries remain required.

## Independent shape and immutable result policy after proof

Create a separate shape on the verified engine-owned setup phase using the native
factory. A box/rounded volume may use the verified min/max factory0ef880;
sphere/capsule primitive ABI remains unverified. Never borrow sensor+40. Confirm
the factory's returned ownership, exact shape type and valid native dimensions.
Use a9e120/a9e180 only as native paired calls once authorized; never manually
modify the refcount. Keep the owner reference until all its tracked tokens retire
and the engine scheduler has joined. A timeout chooses conservative fade and
retains pending ownership; it must not force Release.

Maintain at most one outstanding independent cast per owner/reference epoch.
An epoch change invalidates publication immediately while old records/resources
continue retiring. Separate request includes origin, desired displacement,
volume/filter identity, request timestamp and full token. Before publication,
copy the native result into immutable mod storage, validate finite bounds and
hit-distance range, and match current owner/world/epoch/request. Reject stale,
cancelled, reordered or old-world results. Apply one common approved offset to
eyes and controller-derived poses with a fade decision for stale/blocked results.
The pure policy's `current_completed()` is completion evidence only; it is not
a collision approval. `tracked_shape_retired()` covers registered records only.

Tests cover full-token/slot/type mismatch, pending cancellation, cancellation
during a cast, generation transition and wrap, owner/reference reset without
forgetting pending ownership, teardown without fence, and bounded capacity.
The isolated C++17 test passes MSVC /W4 /WX. No live lifetime hook or head sweep
acceptance is claimed.

## Implemented default-off passive subset

`NativeInteractionAim::install` appends `observe_lean_lifetime=false`; true
requires typed `observe_lean=true`. `NativeLeanLifecycle` pins32 bytes of the
six hook entries094970/096610/0958c0/090ac0/091a50/097670 against the exact PE.
It adds no hand routing, query calls or shape reference operations. Consumer
092630 is intentionally deferred; retirement is observed through its existing
native cleanup call.

Exact owned Lean queue scopes admit up to eight pending full tokens before
publication. Worker-end uses immutable entry keys. Scoped destruction captures
generation while the slot is still out of its pool; if native cleanup beats the
wrapper-end event, immutable evidence waits for that wrapper to finish. Teardown
without a completed/cancelled-before-cast proof logs retirement_unproven.
Overflow/rejections/dropped rows make the audit incomplete rather than silently
establishing lifetime safety.

Callbacks capture at most256 compact immutable event rows under the observer
mutex. They never stream or flush files. After hooks disable and callback depth
reaches zero, `disable()` verifies restored bytes, writes and closes
native-lean-lifecycle.csv. Shutdown return now uses an immutable entry copy only; it never reads manager,
owner, old world or slot memory.

Isolated tests execute installed callbacks with unregistered arguments to check
original-once/unchanged-return behavior, exercise accepted registration and
cleanup races, forbid slot reads after worker return and old-resource reads after
shutdown, verify no event file output before disable, cap/reset/overflow and
repeated cleanup. Existing actual NativeInteractionAim default-off tests also
pass MSVC /W4 /WX.

## v95 bounded native retirement evidence

Actual archive20261009T033719.458444Z-pid44016-arm-v95: diagnostic/fault zero,
callback false; observer rejects/overflow/file failures zero. Lifecycle prefix
contains256 rows over422ms, then5074 dropped rows. It registers52 full tokens
before their worker_begin observations. Fifty complete chains are exactly
issued→worker_begin→worker_end→cleanup_begin→retired, preserving one immutable
player/sensor-generation/machine-generation/manager/world/shape tuple.
Producer caller95600 and worker caller999ad match exact native instructions.
All50 cleanup callers are92b06, immediately following092630's cleanup call at
92b01. Every retirement has complete bit2 and generation+2000 modulo32. Eight
native callback threads occur. Serial51/52 end at worker_end due the row cap.

The independent query trace contains146 rows in37 complete stage transactions
(18 active,19 inactive), with36 byte-preserving queue before/after pairs.
Machine proof and hashes: native-arm-assets/native-lean-v95-lifetime-proof.json;
factory/instance research: native-query-scheduler-{factory,instance}-v95-44016.json.
This proves ordinary completed-query retirement in the captured prefix. It does
not prove cancellation, load drain, retirement after the cap, or a head sweep.
Admission sampling plus reserved owner/shutdown rows is needed for a later
long-lived lifecycle capture; do not silently treat dropped rows as quiescence.


## v99 passive join and world shutdown scope

Pinned PE proof `native-query-world-shutdown-static-v98.json` establishes
PhysicsWorldLocal constructor8516e0 and vtable1c80090, with shutdown slot+10
pointing to8533e0. GameLocal shutdown8ab0f0 calls that slot at8ab13c;
8533e0 tail-calls097670 at8534ce. The current-game save/restore pair8a2dc0/
8a2de0 is a global-pointer scope, not a lock. The ordinary096240 normal-return
waits for its synchronous native root worker task. No static proof yet establishes
loader exclusion of an outstanding collision batch.

The existing default-off `observe_lean_lifetime` flag now additionally installs
exact32-byte guarded void(RCX) hooks096240 and8ab0f0. Both preserve original
arguments and invoke the original exactly once. Entry matches a registered token's
GameLocal/PhysicsWorldLocal, player, typed FSM and Lean sensor, generations,
declaration and shape pointers against live ownership. Return logs only an owned
entry copy. Nested097670 follows the same rule, so no post-shutdown native reads
are needed. CSV appends game/machine/sensor pointers without changing old columns.
Serial/token remain a registered-query watermark; active/pending counts cover
only the observer's registered set and are not a global engine fence.

Hard cap remains256 rows: ordinary events stop at192, join pairs at224,
and32 rows remain for shutdown pairs. Each admitted boundary reserves its return
row before entry is recorded, including nested shutdown. Joins sample at most
once per500ms for the same full resource identity; clock rollback or identity
change permits a new pair. Exhaustion increments dropped; an incomplete trace
cannot authorize resource release or head translation. CSV is still serialized
only after hooks disable and callback depth reaches zero.

Isolated /W4 /WX fixtures validate all eight original call-through signatures,
strict stale-owner rejection, sparse join admission, nested pair reservation,
ordinary-cap preservation and returns after simulated owner/manager/world/slot
storage destruction. These hooks provide passive ordering evidence only. No new
queries, shape references, resources or head translation are enabled. A future
actual load capture must correlate join_return, workers' complete retirement,
game_shutdown_begin/world_shutdown_begin and exact owner replacement, and must
account for every relevant issued token before treating shutdown as a safe fence.


## v100 nested teardown ownership correction

GameLocal shutdown calls owner cleanup at8ab11a and8ab12d before the virtual
world shutdown dispatch8ab13c. Consequently the standalone strict ownership
reader must not re-read player/FSM/sensor inside that nested teardown.
The accepted GameLocal entry now provides a thread-local RAII immutable Boundary
only around its original call. Nested097670 requires that same observer's scoped
accepted transaction, exact manager and return address8ab13f, a still-live typed
PhysicsWorldLocal vtable and an unchanged manager world pointer. It then copies
its parent's owner/token keys without reading player/FSM/sensor, shape or slots.
8534ce is a tail JMP, so8ab13f (not8534ce) is the native return address.
A rejected parent masks the nested path; it never falls through into old-owner
reads. Independent shutdown retains the standalone live typed ownership guard.

Scopes restore previous TLS on normal return and exception unwind. Unwind rows
release their reserved capacity and explicitly do not prove normal native return.
The installed-hook fixture deletes all player/FSM/sensor storage during the
GameLocal original, rejects any subsequent read into those ranges, and validates
nested acceptance, wrong caller/manager/world rejection, nested scope restoration,
original-once execution, exception restoration and later standalone admission.
This corrects observer lifetime handling, not the unresolved engine load fence.

In v99 ordinary lean capture, the normal row cap did not stop registry mutations.
Join-return active0/pending2 means two registered entries remain unretired;
completed workers waiting for later consumer cleanup count as pending. A normal
root-task join does not itself imply destructor/generation retirement. Dropped
rows prevent auditing every intervening chain and cannot establish global quiescence.


## v101 historical world scope and rejection diagnosis

The v100 load capture has one rejection and no shutdown rows. Its CSV cannot
identify that rejection's exact guard; it does not prove which actor/global
field changed. GameLocal entry has now been narrowed to the native argument's
exact GameLocal vtable, its embedded PhysicsWorldLocal vtable atgame+60280 and
world pointer matching a historically registered query tuple. It does not read
the current-game global or player/FSM/sensor. GameLocal may enter after those
owners retired. CSV `owner_scope=1` explicitly means historical actor fields;
it gives no live-actor, shape validity, query drain or release permission.
Nested world shutdown propagates that same historical marker. Join and independent
manager shutdown still require the original full live ownership guard, including
current-game comparison before any old-owner read.

At most8 `boundary_rejected` rows use the teardown reserve. `guard_reason`
identifies the first failed guard; `guard_readable`, `guard_expected` and
`guard_observed` contain its single captured read. Diagnostics do not re-read
the failed field or follow it. Reasons:1 GameLocal vtable,2 PhysicsWorldLocal
vtable,3 world pointer,4 current-game global,5 game player pointer,6 player
vtable,7 FSM vtable,8 FSM player,9 sensor vtable,10 sensor player,11 sensor FSM,
12 sensor declaration,13 sensor shape,14 sensor identity word,15 FSM identity
word. `after_value` carries boundary kind(0 join,1 GameLocal,2 manager).
A zero observed value with readable0 means read failure. Remaining old columns
retain their previous meaning. Other rejection counters (e.g. nested caller
mismatch) do not fabricate field-read evidence.

Fixture extends installed shutdown-hook coverage to actor storage destruction
and current-game replacement BEFORE GameLocal entry. World-only entry plus
nested teardown accept without any retired owner read. Wrong manager vtable and
changed world fail closed, and serialized observed/expected values are checked.
Original-once, unwind, existing cap/reservation and standalone live guard checks
continue to pass. Actual teardown capture is still needed; own head queries and
translation remain disabled.


## v102 observed checkpoint destructor scope

Actual v101 manager-shutdown rejection has callerRVA8a4bb2 and reason5:
game+3130b0 is0 rather than the registered old player. Read-only module inventory
confirms executable base0x7ff60ce30000. Pinned raw code places that return after
8a4baf's manager virtual shutdown in GameLocal destructor8a4830. Its ABI is
void(gameRCX); Game VT+0 points to scalar deleting wrapper8a5f60, which calls
that destructor at8a5f6f and only afterward frees storage whenflags&1. Before
world teardown,8a4af5 calls7b5f50(game+3130b0). Exact helper code invokes each
nonnull actor's VT+8 with1 and zeros the pointer; this explains the observed
missing actor. Outer virtual load caller remains unknown; destructor entry logs
its raw caller rather than inventing that chain.

The existing passive flag now guards a ninth exact32-byte hook at8a4830.
Kind3 entry verifies typed GameLocal/PhysicsWorldLocal and matching registered
world plus the complete old player/FSM/sensor slot, vtable and generation proof.
It intentionally omits the current-game global requirement: an incoming game may
already replace that global while the old actor remains live. `owner_scope=0`
on destructor_begin means the actor was verified at entry only. Nested and
return/unwind rows switch toowner_scope1 (historical); no actor remains proved live
after original cleanup.
Missing old actor rejects with explicit reason5 and masks nested admission.

An admitted destructor original runs inside its own immutable TLS scope.
Nested097670 accepts only matching parentkind3, manager/world and exact return
8a4bb2. Parentkind1 continues requiring8ab13f. Nested observation reads only
still-live manager vtable/world; destructor_return and destructor_unwind read
nothing native. All originals run once. Standalone live guard and GameShutdown
world policy are retained; caps and return reservations are unchanged.

Installed-hook fixtures verify global-root replacement before entry with old
actor alive, actor destruction inside original, nested caller pin, missing actor
negative case, original-once normal/throw behavior, TLS restoration/masking and
a different thread unable to borrow parent admission. Pinned disk PE checks cover
new hook bytes and scalar-deleting Game VT slot. No independent query or resource
operations are enabled. This observer can establish scoped teardown order; it
still cannot establish a scheduler fence or permit independent head-query release.
Physical tracked head rendering should stay1:1; future collision results should
support fading and inhibit virtual body movement rather than clamp the tracked camera.
