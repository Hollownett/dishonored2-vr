# General physical hands and retained surface grips

The requested behavior is one general system for world contact and object grips. A hand can grip the contacted surface of a prop or an individual dynamic ragdoll body. The visible hand follows that body part while the raw controller supplies the desired motion. Palm, hand back and articulated fingers also collide with the surrounding world. Entity names, ragdoll limb lists, oversized activation spheres and special carry offsets must not determine the physical grip.

**Status, 2026-10-15:** the shared anchor and grip-session core is implemented and tested in isolation. It is not connected to native hand actors or grip constraints. Coordinated v553 work adds a separate experimental query-driven fitted hand response with an exact tested palette and admitted upload/attachment readers; see [FULL-HAND-COLLISION.md](FULL-HAND-COLLISION.md). This does not complete native physical hands or corpse-part attachment. The coordinating chat owns the exact candidate, game and production integration. No headset acceptance follows from offline work.

## Implemented core

`src/grip_anchor.hpp` captures an actual hand frame and surface contact in the contacted body's local frame. For initial body pose `B0` and hand pose `H0`, the retained hand frame is `L = inverse(B0) * H0`. At the current body pose `B`, the visible target is `B * L`. The raw controller `H` requests `H * inverse(L)` for the body. The original surface point and normal stay body-local throughout. The same equations apply to a standalone object, a ragdoll part or a static support.

The drive calculation uses actual contact-point velocities, including angular velocity crossed with the contact lever arm. It supplies force and free-couple limits, mass and acceleration limits, principal inertia with independently oriented principal axes, and an equal/opposite reaction wrench at the same actual surface point. Multiple hands' wrenches are summed before a single body velocity step, including different subshapes of one compound body while each surface lease remains independently owned. A combined body acceleration limit returns a common wrench scale that the adapter must also apply to all hand reactions. The example gains are test defaults; native units, engine mass/inertia extraction, solver parameters and headset comfort are unqualified. The velocity-step function is adapter math, not permission to call the existing velocity writer.

`GripBodyId` distinguishes the world, native body owner, full body handle and generation, shape owner, subshape key and geometry generation. A full handle alone cannot identify a retained surface: native `ca6910` can replace its shape without changing that handle. No timestamp or pointer reread is promoted to a native ownership lease.

`src/grip_session.hpp` retains that anchor and an adapter-supplied contact lease under one hand's increasing hold token. The lease must certify the exact captured hand/body frame, surface point and normal, as well as body/shape ownership; a receipt for the same body at another point cannot authorize a grip. Body snapshots preserve original source and sample clocks and reject identity/pose/reference/mapping changes. Callback admission and a serialized final commit reject stale revisions, including an old solved frame retained after a newer body sample. Release closes new commits immediately; bounded drain distinguishes still-running consumers from completed cleanup. Heap-owned callbacks and explicit MSVC SEH cleanup retain/release the lease through callback execution and unlock commit serialization on exceptions.

The lease interface is a contract for the future native adapter. The test lease is synthetic. Neither the interface nor an admitted test session establishes actual native shape ownership.

## Native ownership evidence

The pinned executable and function exports are recorded in `artifacts/general-grip-20261015/research/native-constraint-discovery.md` and its evidence hash manifest. The pinned executable SHA256 is `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.

- `712da0` resolves bone-aware native body endpoints, builds a constraint using `713850`, adds it through `c90de0` and registers endpoint/lifecycle callbacks.
- The actual motor callback `712bd0`, reached through `712d00`, copies force caps from native motor fields into solver output. `713850` constructs motors and a decl-dependent atom stream. This proves a force-limited native path exists; it does not establish a callable custom grip configuration.
- `7149b0`, `7164c0` and `716560` show constraint removal, body-removal handling, callback unregistration and endpoint detachment. `7128c0` releases motors and atom storage.
- Dispatcher `89fb0` contains remove and disable operations. No queued-add path has been proved. A temporary command-shaped object in `c90de0` does not mean the operation is deferred safely.
- The body creation chain is identified, but complete mod-owned hand shape/body/motion setup, collision layers, mutation phase and matching solver drain/destruction remain unqualified. Borrowing another game's Havok layouts would not resolve these contracts.

The focused follow-ups are `native-mutation-lifecycle-addendum.md` and `native-world-step-addendum.md` in that research directory, with separate evidence manifests. They trace native component Start/Stop through the engine lifecycle, and the actual world graph completion in `097bc0 -> aab400 -> queue virtual+58 -> 09f7f0/0a01b0`. The queue's virtual+10 recycles completed handles; it is not the wait. This proves selected graph-endpoint completion semantics, while full solver-backend dependency coverage and exclusion across load/shutdown remain unproved. A component queue lock or unrelated preparation-task wait cannot authorize foreign body/constraint mutation.

## Required production chain

1. Obtain complete native surface contacts for the final fitted palm/finger/back geometry. Broad-phase bounds discover candidates only. Admission needs an actual supported surface and its native body/subshape lease. General mesh/compound traversal and startup clearance need qualification; the current root-sphere/capsule diagnostics are insufficient.
2. Create owned physical hand bodies/colliders in a verified native mutation phase. Preserve raw tracking independently. Drive the hand toward tracking with bounded strength, and let engine contacts resist it. A render-space positional clamp does not replace these bodies.
3. Attach the physical hand to the contacted body with a fully owned, force-limited native constraint using the captured local frame. Keep ordinary physics and ragdoll joints active. Apply native teardown on release, owner loss, topology change, reload and body removal. Never substitute pelvis towing, fabricated body positions or acquisition-time snapshots.
4. Read the actual constrained hand/body frame after physics. Feed this target and current target geometry to arm and finger IK. Account for the driver's root-motion conversion so locomotion cannot carry a world-fixed grip away from its contact. Keep the raw palette separate for desired-motion queries.
5. Carry the grip token and ownership admission through final skinning and attachment publication, including cached-frame paths. Validate the final articulated 24-proxy hand against the world; rigid reprojection of a previous finger palette cannot certify it. If a retained contact cannot be honored, reject/release it through its owner rather than applying an unrelated palm correction afterward.
6. Qualify native execution in isolation, then build an exact coordinated candidate. Check both hands, one hand holding while the other pushes, two hands on one object or different ragdoll parts, resistance from walls and heavy bodies, tracking loss/reload, and worst contiguous frame costs. Real-headset acceptance must establish visible contact, stable fingers/arms, usable grip/release and preserved locomotion/turning.

## Verification and boundaries

`tests/grip_anchor_check.cpp` checks 1,000 arbitrary rigid frames, blocked and moving bodies, preserved local contact, force/mass limits, a genuine angular-acceleration clamp, world-frame covariance, non-aligned principal inertia, reaction moment conservation, two-hand wrench accumulation, identity replacement and malformed input.

`tests/grip_session_check.cpp` checks real consumer/revoke threads, bounded pending drain, release before final commit, frozen revision rejection, immutable anchor, mismatched same-body contact/normal/hand frame, exact IDs/epochs/clocks, retained lease ownership, callback destruction before drain, recursive/self revocation, C++ exceptions and actual MSVC `RaiseException` inside commit.

`tests/grip_ik_pipeline_check.cpp` composes the core with the actual `fullbody::Driver` and the native rig fixture. It reconstructs the palm from the output model-space palette and actual model frame, rather than trusting a solver diagnostic. A resisting body keeps the visible target while the separate raw palette follows tracking; translated/rotated bodies retain the same local contact. In the fixture, player-root translation and yaw produce about 4.8cm of error without inverse-root conversion and less than 1 micrometre with it. Frozen, expired and released admissions cannot publish the resulting owned palette. The final skin/attachment copies are fixture consumers, not evidence of the real native uploads or terminal draw.

All three fixtures pass MSVC C++17 `/EHsc /W4 /WX /O2 /MT`; the reproducible command and complete output are `artifacts/general-grip-20261015/focused-check.cmd` and `focused-check.log`. These are offline checks of shared code. They do not demonstrate in-game physics, generic native geometry ownership, terminal draw ownership, headset appearance or Boneworks/Bonelab parity. Production response enablement and headset acceptance remain false.

## Primary public references

- [HIGGS source](https://github.com/adamhynek/higgs/blob/master/src/hand.cpp): an independent design reference for local attachment frames, native grab constraints and owned creation/removal. Its GPL source was studied for contracts; no implementation was copied, and its Skyrim Havok ABI is not Dishonored's ABI.
- [Jolt architecture](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md): a primary reference for physics-body/constraint ownership and mutation synchronization. It is not evidence of a compatible native layout or safe call phase here.
