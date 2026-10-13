# Matched native artwork bounds

`NativeArtBounds` is a standalone three-slot queue for the exact verified immediate context and its owning thread. It installs no hooks and never invokes native UI code. Configure once on that thread:

```cpp
NativeArtBounds bounds;
if (!bounds.configure(immediate, verified_present_thread)) return;
NativeArtBounds::Metadata metadata{child.owner, child.file, child.iggy,
    child.epoch, child.tick, capture_serial};
if (!bounds.queue(completed_capture_texture, metadata)) { /* retain original */ }
if (auto result = bounds.poll()) {
    // Reject mismatched owner/pose epoch or stale capture before publishing.
    if (result->empty) { /* explicitly clear replica for this capture owner */ }
    else if (result->fresh(current_owner, current_pose_epoch, GetTickCount64())) {
        // Publish result->texture with result->crop and original metadata.
        // RETAIN result while GameXr or any other consumer uses that texture.
    }
}
```

Use a queue per bounded capture stream. Only queue completed native command-list captures. Queuing copies the source to a retained ring texture before reducing alpha; this ties the crop to those pixels even if the native source changes on the next frame. The compute shader uses 16x16 groups, reduces nonzero alpha in group-shared memory, then atomically combines exclusive pixel bounds into a 16-byte raw UAV. A 16-byte initial tuple is uploaded explicitly because a raw-buffer UAV clear would repeat its first component.

The helper copies only the 16-byte bounds to a staging buffer and ends an EVENT query. `poll()` uses `GetData(DONOTFLUSH)` followed by `Map(DO_NOT_WAIT)` and returns immediately when unavailable. It performs no Flush, sleep, readiness loop or full-surface CPU map. It preserves the previous compute shader, dynamic class instances, SRV0 and UAV0. Deferred contexts, another thread/device, incompatible textures, zero metadata and busy/leased ring entries are rejected. Runtime failures return no new replica and leave the native source untouched.

Results contain exact owner/file/movie/epoch/tick/serial metadata and an explicit empty flag. A result lease prevents reuse of its ring texture; retaining only its COM pointer while discarding the lease violates this contract. Release the old lease when replacing/clearing the corresponding published GameXr source. Poll all available results, apply serial ordering in the core, and do not pair an earlier crop with a newer texture. A one-frame-old accepted capture may be projected at the current owned world anchor, while preserving its original capture timestamp/epoch for freshness checks.

The WARP `/W4 /WX` fixture passed moving pixels across 16x16 boundaries and texture edges, exact retained texture/crop matching, transparent-empty results, pending/leased three-slot backpressure, original-source preservation and owner/epoch/250ms freshness checks. The fixture alone submits its test commands with Flush and uses bounded sleeps while awaiting results; application submission is normal Present. No runtime implementation waits are introduced. Live GPU timing and actual native child completeness remain integration checks.
