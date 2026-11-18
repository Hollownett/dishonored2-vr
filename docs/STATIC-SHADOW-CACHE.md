# Static shadow cache across frames (plan 2A.1)

Date: 2026-11-19. Plan item 2A.1 of `docs/PERF-PLAN-90HZ-2026-11-19.md` (line 92; evidence E9/E10, lines 22-23).
Status: design plus an **observe-only** prototype behind `build/static-shadow-cache` (OFF by default). The prototype never
skips a job, never writes engine memory and never changes D3D state. Nothing in this document was run against the game:
the game was not launched, injected or read for this work. Facts come from `docs/ENGINE-PERF-PLAN.md` (static analysis of
the Steam build, sha256 `b671f824...6180`), the shipped shadow-share code and two public references.

## 1. Goal and size of the prize

In paired stereo the mod already renders SHADOWS_WORLD and SHADOWS_DYNAMIC once per frame (shadow share, v1005-v1012). At the
heavy spot on 2026-11-04 (RX 9070 XT, before sharing) SHADOWS_WORLD cost 1.61 ms (left) + 1.75 ms (right) and SHADOWS_DYNAMIC
0.87 + 0.86 ms (`ENGINE-PERF-PLAN.md` lines 1024-1029). With sharing, about 1.6-1.7 ms of SHADOWS_WORLD remains per frame.
SHADOWS_WORLD covers two kinds of jobs: sun-cascade slices (already time-sliced since v1109,
`r_shadowParallelPartialSliceUpdate 1`) and spot/omni atlas slices. The atlas slices redraw the same static world casters
into the local-light atlas every frame. This design caches that atlas content.

Resolution does not move the frame in the heavy areas (plan E1, line 14), so resolution-independent work is the suspect:
shadow caster drawing on the GPU and the job-thread CPU that records it. A cache hit skips both for the atlas slices.

## 2. What is established, and how

"Static" = from `docs/ENGINE-PERF-PLAN.md` (PE + capstone over the exe on disk) or from the mod's own code. "Live" = must be
read from the running game before an active path is built (AGENTS.md: verify engine assumptions against live memory).

| # | Fact | Evidence | Status |
|---|---|---|---|
| F1 | SHADOWS_WORLD (`0x32f390`, dl=0) and SHADOWS_DYNAMIC (`0x32f380`, dl=1) are thin wrappers over `0x32d4b0`. Each job has a slice index at `item+0xf8`. Slices below the global parallel slice count at RVA `0x397a2f8` take the directional/cascade path `0x32ee50`; the rest take the spot/omni atlas path `0x32d6e0`. | ENGINE-PERF-PLAN 1043-1065 | static |
| F2 | One `_shadowMapAtlas` per render system (`render_system+0xc808`, created once by `0x371210`) and one `_parallelSplitShadowMap`, shared by both views. The atlas is a depth target. | 1120-1127, 1139-1141 | static; shared use confirmed live by v1004-v1012 sharing |
| F3 | **Clears.** `0x32d76e` in the atlas path clears **every** entry of the global atlas-info list `0x3308d88/0x3308d90` with `ClearDepthStencilView` (helper `0x2bafc0`, a whole-view clear) when the relative atlas slice index is 0 and `[r14+0x21] == 0`. `0x32f055` clears each parallel slice before drawing it. The phase-9 ownership table assigns both clears to SHADOWS_WORLD; SHADOWS_DYNAMIC draws "the dynamic caster lists, no atlas clear". | 1129-1137, 1756-1757 | static. **Open:** which value of `[r14+0x21]` DYNAMIC passes (check C6) |
| F4 | **Allocation is per frame.** SHADOWS_PREPARE's atlas-slice branch (`0x331cc0`) builds one `arkShadowMapInfo_t` entry (0x660 bytes) per shadowed light in the shared `default` pool (`manager+0x1c8ac0`) and makes "atlas tile allocations through the global 0x3308d88" every frame. It links the entry at `light+0x1a8`. Pointer lists: `default+0x264010` (atlas) and `+0x267020/+0x26a030` (parallel). | 1067-1118, 1690-1734, 1753 | static; the link `light+0x1a8` and the count `entry+0x18` are read in production since v1009/v1011 |
| F5 | **What holds a light's rect.** Its entry: an `idStaticList<arkShadowMapInfo_t,6>` of faces (`m_worldToShadow`, `m_shadowMapToWorld`, `m_shadowMapSidesInfo`). The data pointer is at `entry+0x10`, the face count is at `entry+0x18` (low 24 bits) and faces are 0x100 bytes apart. TLFLIGHTCULL copies 0x84 bytes per face into `cpu_shadowMapsInfos` and uploads them. The forward, fog, screen-shadow and water passes sample the atlas only through these records (matrices plus tile placement). | 1093-1095, 1914-1934, 1873-1908 | static. The 0x84-byte record content and the `[entry+0x10]` pointer reading are **live check C3** |
| F6 | **Rect stability across frames: unknown.** Nothing static says the allocator returns the same tile for an unchanged light. id Tech 6 (DOOM 2016) re-places regenerated maps: "re-generated depth maps don't necessarily stay inside the same tile within the atlas" ([Courrèges, DOOM 2016 graphics study](https://www.adriancourreges.com/blog/2016/09/09/doom-2016-graphics-study/)). | - | **live check C1** |
| F7 | **What SHADOWS_WORLD draws:** "the world/caster geometry" per light rect, from the shared lists that PREPARE built. SHADOWS_DYNAMIC draws the caster lists that COLLECT_DYNAMIC_CASTERS builds from the per-model flag array `[default+0x3c010]` with a camera-distance cull. GATHER_CASTERS (per view: camera position `view+0xbcd8`, frustum gates `view+0x3a20/0x3a34`) appends per-view caster records consumed by both WORLD and DYNAMIC. | 1173-1183, 1754-1757 | static. **Not known:** whether movable scenery (doors, chairs, the clock of v1007) is drawn by WORLD or DYNAMIC, and whether WORLD's per-light casters depend on the camera (v817/v818 measured 2.3 % of faces differing between the eyes, 1194-1199) — **checks C2/C3** |
| F8 | About 23 shadowed atlas lights per frame at the v1010 test spot (`light_captures` 600333 over 26215 frames). | 1861 | live counters from 2026-11-04 |
| F9 | The second view never writes the atlas in a shared frame (its five writers are suppressed). In a fallback frame it runs its own PREPARE/WORLD/DYNAMIC. A Timepiece lens pass borrows the second eye's view and runs its own shadow passes (v1257). | `src/shadow_share_model.hpp`, 1201-1219 | static + shipped |
| F10 | The engine has its own copy path (`ShadowMapCopy` `0x32db10`, `CopySubresourceRegion`) for manual multi-GPU (AFR) management; it is a partial-update cache for the parallel slices, not for the atlas. | 1143-1145 | static |
| F11 | The world pass applies depth bias from the view's shadow cvars (`env/shadow/hwBiasSpot16` and seven more read in `0x32d4b0`). Cached depth includes that bias. | 1062-1064 | static |

**What invalidates a light's cached world depth:**
- **Covered by the key:** the light moved, rotated, changed cone or radius, or its tile moved. All of these change the published
  records. A new light is a miss. A light that left the list is harmless: its tile is not sampled.
- **Covered by frame-level checks:** the atlas resource was recreated, the parallel slice count changed (that moves the
  world-job split, F1), an XR recovery or resolution change, a shadow-share fallback frame, a lens frame.
- **Not covered by the key (blind spots):** a world caster moved (door, movable prop, if WORLD draws it), camera-dependent world
  caster culling (F7), static-mesh LOD switching by camera distance, streaming or destruction, vertex animation in world
  casters (wind, cloth), and a bias cvar change (F11). These are bounded by the periodic refresh (section 3.4) and measured by
  the probe (section 5).
- **Sun cascades are never cached.** Only jobs on the atlas path (F1) are candidates. The engine already time-slices cascades.

## 3. Design

### 3.1 Key

Per shadowed light, from the first view's TLFLIGHTCULL light walk (the walk shadow share already makes, F4):
`{light node pointer, face count (entry+0x18), 64-bit hash of the 0x84-byte published record of each face}`. The record is
what the shaders use to find and project a light's tile. Equal records therefore mean the same placement and the same
projection, which covers F6 without decoding the record layout. The frame key adds the atlas resource identity (from the
probe) and the parallel slice count. The key is independent of light order. A frame hits if every current light is in the
cache with an equal key: a subset of the cache is enough.

The active path must decide **before** the first view's world atlas jobs record, which is earlier than TLFLIGHTCULL. It
computes the same key from PREPARE's atlas list (`default+0x264010` data, `+0x264018` count; `default` taken from the
PREPARE job's `+0xe0` table, hash `0xa4719e7a`, the same lookup PREPARE does at `0x332941`). It then cross-checks that
key against the TLFLIGHTCULL key later in the frame. A mismatch drops the cache and is counted.

### 3.2 Frame path (phase A: whole atlas, copies only)

| Frame | First view's world atlas jobs | Atlas after the world pass | SHADOWS_DYNAMIC |
|---|---|---|---|
| **hit** (`restore`) | relative atlas slice 0: callback replaced by one that records `CopyResource(atlas <- cache)` on the job's own deferred context, in place of the clear. The other atlas slices: no-op callback, like `skip_shadow_pass`. The executor still finishes a list (`render_pass_map.hpp`). Parallel slices run normally. | cached world-only depth | runs normally, draws on top |
| **miss** (`draw_capture`) | run normally (clear + world) | fresh world depth, then `CopyResource(cache <- atlas)` on the immediate context, just before the frame's first first-view SHADOWS_DYNAMIC list executes, and only if every first-view world atlas list has executed. Otherwise the capture is refused and the cache stays invalid. | normal |
| warm-up (`draw`) | normal | no capture while the key is unstable (saves a copy per frame) | normal |
| `bypass` | normal | untouched | normal |

Why these placements:
- The restore sits exactly where the clear was, in the same command list, so it inherits the engine's own clear-before-draws
  ordering. A skip without a restore cannot happen: both are in one callback.
- D3D11 copies whole depth resources but not parts of them. `CopySubresourceRegion` must copy the whole subresource for
  depth-stencil formats. That is why phase A copies the whole atlas, and why phase B uses draws.
- The capture must see world depth only. Depth-only drawing is order independent (depth min), so the engine could legally
  interleave world and dynamic lists. The capture is therefore gated by a runtime order check (C4), not assumed.
- The second view and shadow share are untouched. A mid-frame shadow-share fallback is harmless: the second view clears and
  redraws for itself after the first view has finished.

### 3.3 Per-light path (phase B: only if phase A's copy is too expensive or the hit rate is too low)

- The cache is an `R32_FLOAT` colour texture. Capture: per redrawn rect, a pixel shader reads the atlas SRV (the atlas is
  sampled by FORWARD, so it has an SRV) and writes the cache. Restore: per valid rect, a pixel shader writes `SV_Depth` from the
  cache with depth function ALWAYS, viewport = the rect. Both are draws, so partial rects are allowed.
- Changed lights are redrawn and the others restored. The world draws of valid lights are skipped in the deferred draw hook
  when their viewport is a restored rect. The engine still records them: this saves GPU time but not job-thread CPU. The
  restore draws are appended to relative atlas slice 0 after its clear.
- Refresh is staggered, one light in N per frame. A frame with a refresh redraw always captures. This keeps the staleness
  bound while another light swings every frame (lab case).
- It needs a mapping from a record to its viewport rect (C1 gives it) and the ownership proof that no other atlas slice draws
  into a restored rect.

### 3.4 Refresh, warm-up and bypass rules (as in `src/static_shadow_cache_model.hpp`)

- **Periodic refresh:** the whole atlas is redrawn and recaptured at least every N frames (knob value, default 8, range 2-240).
  At most (N-1)/N of frames hit. A blind-spot change (section 2) is visible for at most N-1 frames: 78 ms at 90 Hz for N=8,
  33 ms for N=4. The lab proves the bound for N = 4, 8 and 16 on both paths, and shows that without refresh it is unbounded.
- **Warm-up:** after a key change, capture only once the frame key has been identical for 2 frames (the per-light path: after
  2 cold frames).
- **Bypass, cache kept:** shadow-share fallback frame, lens frame.
- **Bypass, cache dropped:** unreadable key (a faulted read), more than 256 lights, overlapping current rects (an untrustworthy
  key), atlas identity change, XR recovery or resolution change (`invalidate()`).
- **To add in the active path:** drop the cache on a mod or engine shadow cvar write, on a frame gap above 100 ms (load
  screens, menus), and on a key mismatch between PREPARE and TLFLIGHTCULL.

### 3.5 Memory and copy cost

One extra copy of the atlas: width x height x slices x bytes per texel. The probe reports `probe.atlas[].bytes`. Examples:
4096² D32 = 64 MiB; 8192² D32 = 256 MiB; 8192² D16 = 128 MiB. A restore reads and writes it once per hit frame, and a capture
does the same once per miss. At about 640 GB/s (RX 9070 XT; RTX 5070 about 670 GB/s) that is roughly 0.2 ms (64 MiB) to 0.8 ms
(256 MiB), less if depth compression survives the copy (not measured). If the atlas is large and sparsely used, phase A can cost
nearly as much as it saves, and phase B (per-rect draws) is the answer. The probe's atlas description decides this before any
code.

### 3.6 Expected saving and its basis

- Ceiling: the first-view SHADOWS_WORLD atlas part. It is at most the 1.61 ms of SHADOWS_WORLD eye 0 (E10, measured before
  v1109's cascade time slicing), minus the cascade part (not separated yet: C7).
- Per hit: atlas-world GPU minus the restore copy (0.2-0.8 ms), plus the job-thread CPU of recording the atlas world jobs
  (unmeasured; the observe path reports `cpu_us_per_frame.world_atlas`).
- Times the hit rate: at most 7/8 at N=8. It falls with every moving or flickering-position light, with allocator churn (C1)
  and in the frame path with any change.
- **Estimate: 0.3-1.0 ms GPU per frame in shadow-heavy, static-light spots; 1.4 ms is the ceiling. The plan's 1-1.5 ms is the
  optimistic end.** Estimate only; C1, C5 and C7 replace it with numbers.

### 3.7 Hazards

| Hazard | Effect | Handling |
|---|---|---|
| H1 Allocator re-places tiles every frame | low hit rate, no error (the key changes) | measured by C1 (`rect_set_changed`, reasons) |
| H2 Movable casters drawn by WORLD (doors, props) | shadow lags up to N-1 frames | C2 (d); per-light caster signature from the draw probe; smaller N |
| H3 Camera-dependent world caster culling (F7, v817/v818's 2.3 %) | a caster coming into view casts late, for up to N-1 frames, while the head turns: the most likely VR problem | C2 (b); if real, add the per-rect draw signature to the key (its cost then counts against the saving) or keep N small |
| H4 Vertex-animated world casters (wind, cloth) | motion steps at 90/N Hz | visual check C8; exclude by N |
| H5 Static-mesh LOD by camera distance, streaming, destruction | late shadow change, up to N-1 frames | refresh; frame-gap invalidation; C2 (c) |
| H6 Capture or restore ordering | a wrong capture would bake dynamic casters into the cache (ghost shadows) | runtime order check refuses the capture; C4 |
| H7 Bias cvars applied at draw time (F11) | wrong bias until refresh | invalidate on cvar write |
| H8 Atlas recreated (quality preset, TEXTURE_STREAMER risk at 1264-1266) | stale or mismatched copy | atlas identity and description in the key; resource pointer reuse caught by the description |
| H9 Lens, fallback and alternate-eye frames | wrong owner of the atlas | first-view-only scope, bypass rules; alternate-eye frames are one-view frames, so H3 applies per eye |
| H10 VRAM | +64-256 MiB on a 12 GB card | report bytes; respect the existing VRAM budget code |
| H11 Engine AFR copy path (F10) | double copy if ever enabled | not enabled in single-GPU play; check at install |
| H12 PREPARE key read before WORLD | a wrong `default` pointer gives a garbage key | SEH-guarded reads; TLFLIGHTCULL cross-check; mismatch drops the cache |

## 4. What the prototype does today

Files: `src/static_shadow_cache_model.hpp` (pure model), `src/static_shadow_cache.hpp` (runtime and status),
`tests/static_shadow_cache_check.cpp` (lab, in `tools/build-msvc.cmd`). Hooks: `src/render_pass_map.hpp` (the existing
`0x2c5d50` executor hook; no new hook address), `src/shadow_share_model.hpp` and `src/shadow_share.hpp` (`FrameOutcome`,
`cache_frame_info`), and `src/dxgi_probe.cpp` (knobs, status, draw-hook call, execute-order call, recovery and teardown).

- **No knob: inert.** `RenderPassMap::static_cache_` stays null. Extra work: one null check per SHADOWS_WORLD/DYNAMIC job,
  one thread-local null check per deferred draw, and one atomic load per executed list (only with `build/native-gpu-timing`).
- **`build/static-shadow-cache` (observe).** Optional first token: the refresh period N. Live at the 1 Hz poll once the session
  started with it.
  - At the first view's TLFLIGHTCULL, the existing v1009 walk also hashes each linked light's records (`[entry+0x10]`,
    `entry+0x18` faces x 0x84 bytes, SEH-guarded) and submits the key.
  - The previous frame is then decided by the real model, as the active path would. The observe path assumes every capture
    succeeds.
  - Every first-view SHADOWS_WORLD/DYNAMIC job is classified (parallel or atlas, F1) and its callback timed.
- **`build/static-shadow-cache-probe` (observe + probe).** During first-view SHADOWS_WORLD jobs, each draw reads its viewport
  (`RSGetViewports`). When the rect changes, it also reads the depth target (`OMGetRenderTargets`, `GetDesc`; references
  released).
  - It hashes the draw stream per rect and compares frames: `unkeyed_rect_changes` and `unkeyed_draw_changes` count frames
    whose world pass changed while the key did not. These are the blind spots in numbers.
  - It records the atlas texture description and its bytes.
  - It checks on the immediate context that no SHADOWS_WORLD list executes after a SHADOWS_DYNAMIC list in one Present
    interval. This needs `build/native-gpu-timing`, because lists are only tagged with it.
  - Its own reads inflate the timed world CPU. Read `cpu_us_per_frame` with the probe off.

Status (`xr-live-status.json`, next to `shadow_share`):
`static_shadow_cache {enabled, mode:"observe", refresh_period, warmup, frames, hits, partials, misses, invalidations, refreshes,
bypasses, captures, hit_rate, lights_last, records_last, cache_lights, last_reason, reasons{...},
jobs_per_frame{world_parallel, world_atlas, dynamic_parallel, dynamic_atlas}, cpu_us_per_frame{...}, job_frames,
probe{enabled, frames, rects_per_frame, draws_per_frame, rect_overflow, key_changed, rect_set_changed, draw_sig_changed,
unkeyed_rect_changes, unkeyed_draw_changes, resource_overflow, atlas[{width,height,format,array,mips,paths,bytes}],
order{frames, world_lists, dynamic_lists, world_after_dynamic, frames_out_of_order}}}`.

**Not implemented:** the active path (skip, restore, capture copies, cache texture) and phase B. Both wait for the live
checks below.

## 5. Live checks before any active code (for the coordinator)

All read-only. Turning a knob on needs the user's go-ahead in the current conversation (AGENTS.md); no helper may do it.

| Check | How | Pass condition |
|---|---|---|
| C0 setup | Install the observe build. Knobs `build/static-shadow-cache`, `build/static-shadow-cache-probe`, `build/native-gpu-timing`. Heavy spot of the 2026-11-19 session. | `static_shadow_cache.enabled:true`, `frames` rising, `reasons.unreadable` about 0 |
| C1 rect stability | Stand still 30 s, then turn the head slowly 30 s, then walk 30 s. Read `hit_rate`, `reasons`, `probe.rect_set_changed`, `probe.unkeyed_rect_changes`. | Standing: `hit_rate` near 7/8 and `rect_set_changed` near 0 (otherwise the allocator churns: stop). `unkeyed_rect_changes` about 0 in every segment (the record hash covers placement; if not, the key is wrong) |
| C2 world casters | Same segments plus (d) open a door under a lamp and (e) a guard walking under a lamp. Read `probe.unkeyed_draw_changes` per segment. | (a) about 0. (b) head turn: > 0 means camera-dependent world culling (H3). (d) > 0 means doors are world casters (H2). (e) should stay about 0 (dynamic casters belong to DYNAMIC) |
| C3 memory layout (ReadProcessMemory, `tools/entities.py` style, game paused or still) | For three lit lights in the first view's TLFLIGHTCULL list (`[item+0x40]` list, shadow-share probe output): `entry=[light+0x1a8]`; `[entry+0x10] == entry+0x20`?; `[entry+0x18] & 0xffffff` = 6 (omni) or 1 (spot); the 0x84 bytes at `[entry+0x10]+i*0x100` equal the record words in `artifacts/shadow-share-probe.jsonl`. Read twice 1 s apart while still: are the same light pointers and the same bytes there? Also `[base+0x397a2f8]` = `r_shadowParallelSliceCnt`, and `default=[manager+0x1c8ac0]`'s atlas list (`+0x264010/+0x264018`) contains exactly the linked entries. | All equal. Unchanged bytes for a still scene |
| C4 order | `probe.order` over thousands of frames | `world_after_dynamic == 0`, `frames_out_of_order == 0` |
| C5 atlas | `probe.atlas[]` | One atlas resource with `paths` = 4 (atlas), separate from the parallel map. Bytes give the copy cost (section 3.5) |
| C6 DYNAMIC clears | Static: disassemble `0x32d6e0` around `0x32d76e` for the `[r14+0x21]` value DYNAMIC passes. Or one frame capture with a graphics debugger (an inject: coordinator's decision) | No atlas clear in SHADOWS_DYNAMIC |
| C7 GPU split | `gpu_passes` SHADOWS_WORLD at the heavy spot, plus `jobs_per_frame` / `cpu_us_per_frame`. A small follow-up can tag world-atlas lists with an unused pass id (52-62) to split the GPU time | The atlas share of SHADOWS_WORLD in ms. This is the real ceiling |
| C8 visual (after phase A) | N=4 first, then 8. Door, swinging lamp, foliage in wind, guard under lamp, head turn near a lamp, lens use, settings change. | No visible lag beyond N frames; no ghost of a dynamic caster |

Decision rule: build phase A only if C1 shows hit rates above about 0.6 standing and walking, C3/C4/C6 pass, and C5 + C7
predict a net saving of at least 0.3 ms. If C2(b) shows camera-dependent culling, the key needs the draw signature per rect,
or this item drops below the other 2A/2B items.

## 6. References

- Courrèges, *DOOM (2016) - Graphics Study*: the shadow atlas re-uses the previous frame's depth maps and regenerates only
  maps that need updating; regenerated maps can move tiles; the static part of a depth map is cached and only dynamic meshes
  are drawn on top. https://www.adriancourreges.com/blog/2016/09/09/doom-2016-graphics-study/
- Epic Games, *Movable Light Mobility* ("Shadow Map Caching"): cached shadow depths for unmoving point and spot lights from
  static or stationary primitives; lights that move are not cached; World Position Offset and animated materials are excluded
  (our H4); memory cap `r.Shadow.WholeSceneShadowCacheMb`; three movable point lights went from over 14 ms of shadow depths to
  about 1 ms in Epic's example.
  https://dev.epicgames.com/documentation/en-us/unreal-engine/movable-light-mobility-in-unreal-engine
- Microsoft, `ID3D11DeviceContext::CopySubresourceRegion` remarks: depth-stencil and multisampled resources must be copied as
  whole subresources (the reason for phase A's whole copy and phase B's draws).
