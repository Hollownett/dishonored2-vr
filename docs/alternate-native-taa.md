# Native Game TAA in alternate-eye performance mode

2026-10-22: implementation candidate, with CPU checks and native ABI verification. Game/headset validation is pending. The user asked that Dishonored 2 remain closed while they play Helldivers 2; no game or GPU tests were run for this candidate.

Performance mode previously forced every temporal AA preference to SMAA. A single native view object alternates between eyes, so its ordinary previous camera state belongs to the other eye. Separating only the history textures does not fix that camera reprojection.

The candidate allows **Game TAA** (mode 1) when the pinned ABI, projection jitter and per-eye history hook are available. FSR modes still fall back to SMAA in performance mode. The launcher exposes `Game TAA in performance mode (experimental)`, enabled by default; turning it off writes `build/no-alternate-native-taa`. Select Game TAA in the VR settings to use it. A failed ownership check disables temporal AA for that session.

## Native ownership and implementation

The reference is EternalVR's [TAA hooks](https://github.com/ArcadaLabs-Jason/EternalVR/blob/12b31c836a84593f7e207ff2946d92062d7554e6/src/vkcore/taa_hooks.hpp) and [stereo temporal investigation](https://github.com/ArcadaLabs-Jason/EternalVR/blob/12b31c836a84593f7e207ff2946d92062d7554e6/docs/rig-findings/stereo-temporal.md). Its separation of temporal state by logical eye informed this implementation. Dishonored 2 offsets and calling conventions were verified independently against executable SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.

Native Setup at RVA `374e40` rolls three disjoint 64-byte current matrix blocks (`7390`, `7450`, `7510`) to previous blocks (`7410`, `74d0`, `7590`) when the native counter changes. Old render-copy position at `3a78` becomes previous position at `3a84`. Jitter at `7850` rolls to `7858` on every call, even with an unchanged counter. Setup then rebuilds current matrices. Native reset byte `bcfc` replaces previous matrices/position with newly generated current values.

Before original Setup, the wrapper seeds same-eye prior current matrices, position and jitter. Native code retains ownership of rollover and regeneration. Repeated same-counter calls retain the transaction's prior jitter and sample phase. Bootstrap and camera/history invalidations preserve native reset behavior and initialize previous jitter coherently. Seven exact instruction-region hashes gate this path, excluding the mod's own Setup detour prologue.

Logical eye and display frame come from the existing Setup/Begin pipeline. A qualified Begin admits an immutable receipt with the captured current/previous jitter. A worker must match that receipt, native counter and current snapshot before choosing its history pair. The worker uses that captured jitter instead of a mutable latest-eye array. Existing engine-owned history twins and per-eye ping-pong roles supply the image history.

Owner, allocation, dimensions, pose/reference epochs, mode changes and missed same-eye frames invalidate history. External reset requests revoke receipts immediately through an atomic generation; only the Setup owner mutates a transaction. Failed writes roll back before native Setup when possible. Failed transactions retain the singleton classifier so queued work takes a scoped temporal-skip path. Original post-processing still runs once, and cleanup preserves unrelated native flags.

## Validation and remaining evidence

CPU regression checks exercise alternating-eye continuity, duplicate Setup, first-eye bootstrap, native resets, allocation/owner/epoch changes, receipt admission, stale snapshots, counter changes during reads, cross-thread invalidation, partial-write rollback, cancellation and failure classification. Synthetic native job tests cover singleton eye selection, frozen jitter, history swapping, and temporal-skip cleanup including exceptions.

The source review and these checks establish implementation behavior against the verified instruction ordering. They do not establish actual worker timing, headset quality, comfort or complete gameplay.

Moving-object history is still unverified. Native TAA consumes the engine's motion texture, but the repository does not identify previous rigid-object transforms or skinning-palette ownership for the same eye's two-frame interval. No guessed object-state writes or motion-vector scaling were added. Follow-up game evidence must inspect a moving rigid object and a skinned character, plus actual TAA constants/history pairs, static edges, head motion, resets, settings transitions and performance.
