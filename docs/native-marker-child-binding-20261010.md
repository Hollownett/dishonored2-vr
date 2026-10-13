# Individual native marker binding

Pinned Steam PE SHA256 b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180. New read-only exports: `artifacts/re-toolchain/direct-ghidra/marker-child-owner-20261010`.

## Verified ownership path

- `53AA20` marker manager walks the array at manager+18, packed count at+20. Active marker+94==1 is passed to677B10 at call53AAE9 (return53AAEE).
- `677B10` resolves marker spawn at+C, gets manager+78 and slot+D4, updates native world/placement, and calls53A2F0(manager,slot,x,y).
- `53A2F0` resolves that same array[slot]. Pure677030(marker,0) returns marker+F0. This is the individual marker AS3 object receiver. Alternate offscreen receiver+398 is not selected by this native position setter.
- `02A8B0` consumes the existing receiver value-table at+18 and one-based low24 index at+20. The helper resolves exactly this value through the already byte-verified Iggy Get API from NativeInteractionPrompt::Api. Native display value tag24, node at value+27, and parent80/root iggy+26B0 are verified by existing NativeHudArtProbe research. No constructed property path or markersContainer capture is used.
- The actual projected native world anchor is marker+3C/+40/+44, as6763E0 proves. Entity centre+24 is a different input and must not substitute for the adjusted marker position.

`NativeMarkerChildBinding::observe(read,base,caller_rva,marker,current_hud_file,configured_api)` requires exact53AAEE caller, active entity-backed marker, manager slot equality, spawn registry identity, current double-validated ui/hud.iggy draw owner, matching marker receiver Iggy, native display value identity/tree and native x/y/width/height/visible getters. It revalidates identities and world anchor after getters. It returns a packet with marker/manager/entity/slot, world anchor and individual geometry. No engine state is written. `publish(packet,epoch,tick)` stores only a NativeHudChildDraw objective binding, owned by this marker. Caller retains packet.world for scene placement.

## Root hookup

Add an after-original observer to the existing NativeMarkerProbe Update hook. Capture _ReturnAddress BEFORE invoking original; then call the observer after that one original returns. Forward caller-base and marker. Do not install a duplicate677B10 hook or call Update again. The helper accepts only53AAEE; other Update calls fail closed.

Root supplies the verified current HUD file from existing native HUD ownership, and the configured Iggy Get API. Manager+AD8 is only a native nonzero gate in setters; its file type was not proven, so the helper intentionally does not guess it. Configure the existing NativeInteractionPrompt API before use; its exact Get ABI/prologue checks remain authoritative.

For each accepted packet, publish objective child binding immediately and retain marker->world/epoch/tick association for NativeHudChildDraw Begin/End capture and per-eye scene placement. Maintain original cadence, label/distance/visibility and clip semantics. Child draw's original always remains one call; hide/capture only that individual node. Clear bindings and association on epoch/world changes. NativeHudChildDraw currently has40 binding slots; if markers churn, stale entries require reclaim by its owning UI agent rather than this helper editing that header.

## Limits and checks

Detached/static marker96!=0 and alternate offscreen receiver+398 are deliberately excluded until ownership is separately proven. This implementation covers entity-backed NPC/objective/item marker instances; it does not prove all marker classes exist in that representation. Whole markersContainer capture and extra Update are unsupported.

Focused native_marker_child_binding_check passes MSVC C++17 /W4 /WX. It verifies individual slot/node/world identity, getter receiver, current HUD ownership, stale spawn, wrong tree, mid-get slot reuse and detached rejection, plus publication into the child-draw binding fixture. No live binding, actual draw ownership, scene-depth placement or headset quality has yet been proven for this new helper. Core/GameXR and the frozen hand selection sources were not edited.

## Getter-free refresh and offscreen guard

`refresh(read,base,caller_rva,marker,current_hud_file,cachedPacket)` revalidates manager slot, spawn/native entity registry, marker receiver/table/index and the exact40-byte existing value-record fingerprint, current HUD file/Iggy, and node ancestry before returning the new native world anchor. It issues no AS3 getter or engine call and does not extend cached qualification. Full `observe` must renew within250ms (root plans5Hz). Width/height/visible remain the last getter sample, so fresh actual native child draw is still mandatory for artwork publication; a cached packet alone does not establish current Flash visibility.

Both observe and refresh now explicitly reject marker+640!=0. Existing6763E0 placement sets this byte0 before projection and1 for clamped/offscreen placement;677030 consults it when selecting alternate receiver+398. The earlier helper simply never resolved alternate+398, rather than explicitly proving primary mode; the new byte guard makes that exclusion exact and conservative. Unknown/detached/offscreen variants keep original native rendering.

Focused check additionally verifies getter-free moving-anchor refresh, unchanged qualification time/getter count, expired cache, receiver-record change and explicit offscreen rejection.
