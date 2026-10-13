# Native objective/interface marker audit

Read-only audit, 2026-10-10. No game/PC control, builds, source edits or visual acceptance. Objective/interface marker placement remains open. The native projection/cache path is identified; a runtime frame-cache/per-eye failure is a hypothesis, not a confirmed trace result.

## Confirmed source behavior

- `src/native_marker_probe.hpp` observes `arkEntityMarker::Update` at RVA `0x677B10` and WorldToScreen at `0x6DDA50`. Originals run unchanged; input/output/cache are read only. The observer does not correct marker placement.
- `src/native_hud_rect.hpp` forwards the complete HUD rectangle unchanged and `markers_corrected()` returns false. Whole-movie HUD scaling and compensating coordinate hooks are disabled. `hud.iggy` also contains the damage/vignette surface, so shrinking it would move world markers and expose vignette edges rather than solve their projection.
- `prepare_head_view()` in `src/dxgi_probe.cpp` temporarily changes the prepared render-view pose/FOV/projection at `+0x9C..+0x140`, then restores it. The objective marker projection cache at idView `+0x3EB0`/`+0x3EF0` has no correction code anywhere in `src`. The native crosshair's left-eye publication is a separate hand-aim path and does not repair objective markers.

## Verified native path and cache limitation

| Evidence | What it establishes |
|---|---|
| `artifacts/marker-re/w2s.txt`, `0x6DDA50` | `void(view,const float3*,float3*)` wrapper converts projected NDC to pixel coordinates using gameplay dimensions at game-system `+0x340EA8/+0x340EAC`; x=(NDCx+1)*width/2, y=(1-NDCy)*height/2. |
| `artifacts/marker-re/w2s2.txt`, `0x88D970` | Projection reads the 4×4 matrix at idView `+0x3EB0`, divides by clip w, and first calls the lazy cache updater. |
| `artifacts/marker-re/w2s3.txt`, `0x890DD0` | Cache is rebuilt only when idView `+0x3EF0` differs from the game-system virtual `+0xC0` token. There is no eye identity in this comparison. Rebuild uses `0x374680` projection and `0x3746D0` view matrices. |
| `artifacts/marker-re/w2s2.txt`, `0x7E7110` | Marker placement obtains the top view from the native game-system view stack at `+0x1F52B8`, not directly from the mod's completed rendered-eye snapshot. |
| `artifacts/marker-re/w2s4.txt`, `0x677B10` | Marker Update resolves the entity, updates native marker world data, calls placement, then updates native position, distance/scale and other state. Its one identified caller is marker-manager `0x53AA20`. Re-running full Update for each eye could repeat unrelated marker side effects. |
| `artifacts/marker-re/w2s2.txt`, `0x6763E0` | Placement caches the projected result at marker `+0xE4/+0xE8/+0xEC` and returns early when unchanged. Native clamping/rotation logic therefore needs its own per-eye state consideration. |
| `artifacts/marker-re/w2s5.txt`, `0x53A2F0` | Native position setter maps gameplay pixels relative to screen center into HUD extents, dividing by gameplay size minus safe-zone margins (`0x3309CB8/BC`), and resolves the marker's existing Iggy object. Raw per-eye pixel coordinates must go through this mapping exactly once. |

These findings permit two concrete failure mechanisms: a marker update may project using an untracked native view, or a shared frame token may retain the first eye's projection for the second eye. A once-per-frame marker/UI update can then reuse the same native artwork position in both eyes. Both mechanisms fit the source; neither is established as the active runtime cause without a marker trace. A stale gameplay/HUD dimension or applying safe-zone mapping twice is another possible offset source. Do not claim that invalidating the matrix token alone fixes placement.

## Required native-art world rendering

The user clarified that markers must appear inside the game scene near their world targets, not on the default HUD or a separate composition layer. Per-eye HUD x/y correction alone is therefore insufficient. Reuse native marker artwork, label, distance and visibility on a world-positioned surface rendered into each game eye. Keep native full Update at its original cadence and leave full HUD/vignette bounds intact.

Record a bounded association from each marker's verified native Update/project/position calls: marker identity, world target, manager at marker `+0x78`, slot at `+0xD4`, owner thread and epoch. Establish individual marker draw ownership before isolating its artwork and suppressing only that marker's duplicate HUD draw. The existing whole-movie capture for wheel/notes/journal does not establish individual HUD-child ownership and must not be reused as proof. World rendering needs correct perspective, stereo parallax and a verified scene-depth policy. Keep objective visibility semantics for distant or obstructed goals while anchoring the marker to the native target.

The read-only observer ABIs/prologues are ready. A world-space implementation is **not ready** until individual native artwork ownership and the scene render/depth seam are verified. Native position setter `0x53A2F0` remains useful diagnostic evidence; changing it alone would still draw on the HUD and does not fulfill the requirement. Existing marker-manager Update cannot safely be invoked from the render thread based only on its address. A generic WorldToScreen detour also cannot correct two eye draws when that projection occurs only once before both draws.

## Bounded runtime evidence still needed

No `native-marker-probe.csv` or summary was found in the inspected archived runs; the probe is opt-in and `build/marker-probe` was absent during this audit. The parent has corrected archive preservation for future probe outputs. Existing counters/prologue tests do not prove marker alignment or update cadence.

Next authorized marker diagnostic should retain Update/W2S phase and thread, world target, output pixels, cache token before/after and matrix (already present in probe rows), plus Present/pose epoch, native setter dimensions/safe-zone and the owned HUD eye-draw phase. Phase counts alone are insufficient: phase is a globally published render stage and other threads can project concurrently. Correlate marker identities and frame tokens. Validate a stationary objective, close item marker, head rotation, Quality/Balanced changes and Virtual Desktop focus return while preserving accepted camera/locomotion. Until then marker placement and headset acceptance remain unverified.
