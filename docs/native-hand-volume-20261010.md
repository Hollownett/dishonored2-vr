# Native hand-centred Use acquisition

Pinned Steam PE SHA256: `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.

## Verified path

`3b8d60` queues five owned InteractionSensor jobs through `095280`. Only slots 0–2 use the object mask `20cc38`; character slots use `408`. The geometry pointer lives at request +28; ray at +30. `08ffb0` eagerly copies request112 and frame48 but borrows the geometry pointer. `095370` has identical six-argument queue/pool ABI and calls `08ffb0(...,2,1)` instead of `(...,2,0)`. `096ac0` consequently installs the native initial-overlap collector alongside its sweep collector into the same job result stream. There is exactly one queued engine call per original call.

`ca6550` is the pure 113-byte hknpSphereShape constructor. RTTI vtable RVA `1ec1e78` confirms the type. It allocates nothing, writes external-storage marker `1ffff` at +10, radius at +28, zero XYZ centre at +50 (W=.5). The native allocating caller `3b82c0` separately replaces the low16 storage-size marker after allocation. Our aligned96-byte shape keeps the constructor's external marker. Full constructor and232-byte overlap-queue code are validated before construction. Storage and its identity snapshot are static and immutable for DLL/process lifetime; session disable never reinitializes, frees, or decrements it. `0958c0`/`090ac0` clean up results and job slots without reading/releasing the borrowed geometry. `c89bc0` forwards it to the physics world virtual +98 without taking shape ownership. DLL unload remains unsupported while workers may borrow it.

The routed object query uses a zero-centred .45m sphere, tracked grip translation in the ray ONLY, and a1mm sweep. An initial-overlap collector finds objects beside/behind the hand rather than requiring a narrow forward hit. Native request masks, flags, other exclusion/filter fields and query IDs remain intact. Validation additionally matches the original geometry to sensor slots0–2 and the query manager/world to the current game. The direct stage0 ray uses unit direction and max distance .45m, so it cannot override nearby acquisition with a remote ray target.

`3b8430` still consumes and sorts native results, resolves interaction components via `877720`, runs native availability and `3b80a0` permissions/range/item-semantic facing/height checks, and uses `3b7b90` native obstruction checks. No shared target/global eligibility writes were added. The obstruction origin stays native camera/player context; no controller-facing cone gate was found there. Static food/money/armor can participate in this native path, unlike the separate movable-physics-body grab selector. Exact in-game success has not yet been observed for this change.

## Root integration

`publish_hands(camera,hands,epoch,tick,grip_centres)` accepts two optional world-space grip centres. Missing/nonfinite centres or centres more than .5m from their associated aim pose safely fall back to that hand's aim position. Epoch/player/camera identity and250ms publication freshness remain mandatory. Report `volume_requests()` and `volume_rejected()` alongside existing interaction telemetry. Existing opposite grip lock and qualified selected-hand preference remain in force; native eligibility loss still removes preference.

## Evidence files and checks

New pinned exports: `artifacts/re-toolchain/direct-ghidra/hand-use-volume-20261010/function-140ca6550.txt`, `function-140095370.txt`, `function-140096ac0.txt`, `function-1403b7a40.txt`. Existing exports: `demo-interaction-aim/function-1403b8d60.txt`, `hands-head-query-cleanup/function-140090ac0.txt`, `head-sync-cast-proof/function-140c89bc0.txt` plus stage0 permission/obstruction research.

Checks compiled with MSVC C++17 /W4 /WX and passed: `native_hand_volume_check.cpp` success and changed-ABI-rejection processes, `native_interaction_two_hand_check.cpp`, `native_lean_query_trace_check.cpp`. These prove immutable lifecycle/code-pin rejection, zero-centre plus per-hand translation, bounded ray fields, routing/freshness policy, and existing hook pass-through. They do not prove native overlap delivery, selection quality, static-item Use, obstruction behaviour, or headset feel. Root must verify those live.

Dishonored1 primary reference: [interact_aim.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/interact_aim.cpp). Its design routes owned query arguments and preserves the engine's selector/prompt eligibility. Its documented prompt fix uses a stable gameplay/capsule aim origin rather than varying per-eye render origins. That source does not establish a native hand-volume implementation; D2 sphere/overlap ABI evidence above establishes this implementation independently.
