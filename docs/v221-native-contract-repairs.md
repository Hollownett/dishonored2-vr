# Further native grab, attachment and timing repairs

This continues `v221-independent-fit-repairs.md` under the user's instruction to work without the headset. The full product goal remains open. No game was launched, no DLL was loaded into it, and no headset appearance, comfort or native FPS result was obtained.

## Implemented behavior

One valid released grip sample now arms the physical latch at a new epoch, while that reset sample still returns false. A held grip at epoch/focus return stays disarmed. The production publication/filter/dispatcher fixture verifies that an immediate subsequent squeeze can acquire without a second release callback.

Physical acquisition now requires native obstruction clearance. The existing original wrapper at RVA `0x3b7b90` builds its own native collision filter and collector. It accepts a dynamic entity without requiring native Use registration. It is invoked only after the original stage-0 callback in a qualified player sensor scope. The wrapper's low-byte return is the obstruction verdict; its output byte is scan continuation, not proof of clearance. No raw Havok collector or new physics ABI was constructed.

Two bounded hand requests retain the source pose, epoch, camera/player, candidate body and token. Sensor, source, camera, body lifetime and contact are revalidated before and after the query, outside mailbox locks. A successful result commits only pure controller math and internal hold ownership at that exact source pose. A later eye publication then updates an existing hold. Native motor/velocity writes remain in the existing dispatcher (`0x89fb0`, velocity wrapper `0xc85dc0`). This avoids another real ordering failure where every new publication could invalidate clearance before acquisition.

Equipped joint substitution now accepts only the published pose-state-3 contract and the requested joint's exact current slot. Publication verifies the full contiguous remap with one bulk read. A changed remap under an unchanged allocation/generation revokes the palette. Other pose states remain native and cannot alias a tracked wrist into another joint.

Full-rate XR and Present timing rows are now submitted together to a fixed 256-record queue. Producers use try-lock and copy fixed numeric arrays; the worker performs formatting, file writes, flushes and 20,000-row rotation. Overflow drops whole pairs and reports `frame_timing_dropped`; write failure reports `frame_timing_write_failed` without forcing XR recovery. Exact-frame analysis still breaks continuous windows at missing Presents. This moves these timing file operations out of Present; other optional captures and diagnostics retain their own original paths. No v221 stall attribution or FPS gain is claimed. [Windows synchronous write semantics](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile) support keeping file I/O outside the rendering callback.

## Evidence

The isolated output is `build/d2vr_render_probe_repairs_20261011b.dll`, SHA-256 `c199c2be19fc78cbce0f3c46494adc653823ef421d439136085d31c7ec7387f7`. The complete required MSVC `/W4 /WX /O2` build/check script and final DLL link passed. Input bytes matched before/after the build. The compiler read the workspace; it did not compile from the frozen archive. Provenance is in `artifacts/build-source/20261011T100156.310474Z-f35b0d71/`.

The preceding diagnostic DLL and rejected v221 DLL retain their recorded hashes. The normal demo selection, user saves and preferences were not changed.

`artifacts/repair-v221-native-contracts/repair-report.json` indexes the focused seven-check run, complete build log, failed warning-only attempt, native ABI exports and research artifacts. The obstruction fixture invokes production stage/filter/service/dispatcher paths with owned sparse memory and a stub original wrapper. It covers blocked/unreturned results, callback order, source replacement, contact movement, epoch/focus/authored/sensor/body invalidation and release during the query. It proves the call contract and state transitions, not real collision results or native object behavior.

## Body fit and native Settings remain incomplete

The garment investigation used actual body and sleeve meshes across both characters, independent dimensions and raised/bent poses. Coincident bind positions include overlapping patches rather than a complete shared topological edge. Their separation cannot be reported as visible-hole width.

Palette-only corrections did not maintain the connection across bends. Two targeted positional band experiments joined selected points but produced severe stretching and folded triangles; both were rejected. No corrective vertex path entered production. The input palettes, corrected plot caption, 720 cases and native surface/buffer evidence are retained in `artifacts/research/garment-continuity/`. Same-draw palettes, Corvo/body draw receipt and packed normal/tangent semantics still require proof before structural geometry wiring.

Native Settings research now proves typed provider construction/setters and the raw callback ABI `callback(context, UiFile*, arguments)`. Unregister removes the registration but leaves copied queued callbacks executable. Whole-file teardown supplies broader task/queue handling; a custom adapter lifetime barrier remains unqualified. AS3 `currentSection` is separate owned data, and stock navigation/back/restore uses four native section objects. Adding a fifth row cannot safely reuse those callbacks. Current GDI settings therefore remain until a complete native data, navigation and lifecycle adapter is demonstrated.

Visible gait and body/garment fit, sword placement, physical holds/throws during real stalls, sustained native performance and all other outstanding UI/gameplay entries remain unaccepted. Preserve the user's accepted perspective, locomotion, turning and entry/exit when obtaining that evidence.
