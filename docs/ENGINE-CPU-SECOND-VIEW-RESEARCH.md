# Second-view CPU work: list replay (plan 2B.2) and shared passes (plan 2B.1)

Research spike, 2026-11-19 (night). Read-only: no build, no game launch, no injection, no memory read of the game, no knob
change. Inputs: `docs/ENGINE-PERF-PLAN.md`, `docs/PERF-PLAN-90HZ-2026-11-19.md`, `docs/PERF-NIGHT-2026-11-19.md`, the sources
named below, Microsoft Learn, and the sim artifacts already on disk in `artifacts/perf-night/` (runs of 2026-11-19, RTX 5070,
Ryzen 5 5600X, the user's last save). Nothing here was run against the game tonight.

Evidence marks:

- **[code]** stated in this repo's source (file:line); **[doc]** in a repo doc (static disassembly or an earlier measurement);
- **[art]** read tonight from `artifacts/perf-night/<run>/` (status JSON, `object-motion.json`, `dxgi-gpu-trace.csv`,
  `during.txt`; summarised with `tools/perf_night_report.py`);
- **[pub]** public reference (Microsoft Learn); **[inf]** inferred, not verified.

## 0. Verdicts

| Item | Verdict | One-line reason |
|---|---|---|
| **2B.2** replay eye 1's lists for eye 2 with swapped view constants | **No-go** | Every entity draw carries its own view-projection-model matrix, written per draw inside the list; the visible set, light tiles and the mod's per-eye corrections are recorded per eye too. And removing the whole second view measured at most ~0.25 ms of engine time on this PC. |
| **2B.1** run per-view CPU passes once | **No-go for this iteration**, except where noted | The culling/gather passes produce per-eye data (visible set, light lists, tiles). The five shadow-setup writers are already shared. The GI/ocean/UI passes are low-risk "run once" candidates, but their CPU cost is unmeasured. Their combined ceiling is again bounded by the ~0.25 ms (at most ~0.7 ms of main-thread span) the whole second view costs. |
| Re-open condition | Live check L1 | Re-open 2B.1 only if a clean paired vs `build/perf-mono-view` A/B at the heavy spot shows the second view costs >= 1 ms of frame work. Do not re-open 2B.2 short of shader-level stereo (section 3.5). |

## 1. The measured ceiling: what the whole second view costs on this PC

The cheapest bound on any second-view CPU saving is the run that drops the second view entirely. R5 (performance mode: one
engine view per frame) did that against R1 (paired), same save, same settings, 2246x2419, FSR 3 Quality, 0.77 render fraction
**[art]** (`r1-baseline`, `r5-performance-mode`):

| | R1 paired | R5 one view | delta |
|---|---|---|---|
| work per frame p50 / p90 (ms) | 10.42 / 12.24 | 10.37 / 14.60 | -0.05 / +2.4 |
| engine gap p50 (Present exit to next enter) | 9.35 | 9.11 | -0.24 |
| main-thread span, first eye begin to Present enter | 2.98 + 5.98 = 8.96 | 8.25 | -0.71 |
| Present hook p50 (mod, on the same main thread) | 1.05 | 1.16 | +0.11 |
| ExecuteCommandList, all lists, main thread p50 | 0.16 ms (48 + 60 lists) | 0.08 ms (48 lists) | -0.08 |
| native GPU lists p50 | 7.30 ms | 6.16 ms | -1.1 |

Later paired runs on a quieter machine give the same main-thread span: 2.87 + 5.27 = 8.14 ms (`v1394-half`), 2.94 + 5.31 =
8.25 ms (`v1396-dlss-defaults`) **[art]**. That equals the one-view span.

What this means:

- **Executing the second view's lists costs ~0.08 ms of main-thread time on NVIDIA** **[art]** (`ecl_replay.total_ms_p50`
  0.16 for 108 lists). The RX 9070 XT of 2026-11-04 replayed for ~10 ms per frame because AMD reports `DriverCommandLists=0`
  and the runtime emulates the lists **[doc]** (ENGINE-PERF-PLAN.md:124-127, :508). Even there the replay time did not scale
  with the recorded call count (:785). 2B.2 could not remove that replay: eye 2's lists would still be executed.
- The second view's recording runs on the five job workers (and partly on the main thread, which also runs pass jobs: the
  main-thread sample `Dishonored2.exe+158209 <- +32a641 <- +2c5d86` in `v1394-half/during.txt` has the executor's
  return address after `call [rbx+0xd8]`, ENGINE-PERF-PLAN.md:893) **[art]**. Removing all of it moved the frame by
  0.05 ms (p50) and the engine gap by 0.24 ms.
- **Ceiling for 2B.1 + 2B.2 together at this spot: ~0.25 ms of frame time, at most ~0.7 ms of main-thread span.** The
  Present hook alone (~1.0 ms on the same thread) is larger.
- Caveats: R1 and R5 both ran before ~02:05, while helper agents were compiling (PERF-NIGHT section 4: those runs are
  0.5-1.5 ms pessimistic). R5's p90 is noisy. R4 (`build/perf-mono-view`, no AER warp work) was never run. The VD heavy area
  was never CPU-profiled. Live check L1 settles all of this.

## 2. How per-view data reaches a deferred command list in this engine

### 2.1 Job and list structure

| Fact | Evidence | Mark |
|---|---|---|
| Every `ARK_RENDERPASS_*` job runs through the executor `0x2c5d50`: it calls the callback `[item+0xd8]`, then finishes the job's deferred list with `FinishCommandList(restore=FALSE)` into `item+0x118`. One pass job = one command list. | ENGINE-PERF-PLAN.md:881-912; render_pass_map.hpp:1-13, :60-62 | [doc] static |
| The per-view job builder `0x351780` runs once per view right after the eye boundary `0x351210` (call sites `0x3668d1`/`0x3668e5`). | ENGINE-PERF-PLAN.md:1421-1430, :1948-1955 | [doc] static |
| About 45 lists per view (laptop sim); 48 + 60 lists per frame on the RTX 5070 sim. | VR-PERF-LAPTOP.md:244-253; `r1-baseline` `ecl_replay.lists_p50` | [doc] [art] |
| The main thread executes each list with `RestoreContextState=FALSE` (every list, 96,874 of 96,874 in R1), so each list starts from default state and is self-contained. | `r1-baseline` `ecl_replay.restore_false`; ENGINE-PERF-PLAN.md:508-509 | [art] [pub] |
| The scene state is bound only on the recording (deferred) contexts: 0 immediate PS binds (v994); scene draws in the trace are `context_type 1`. | ENGINE-PERF-PLAN.md:317-318; `r1-baseline/dxgi-gpu-trace.csv` | [doc] [art] |
| The job flag `item+0xf4 & 0x100` is a "cached command list" bit. Its semantics are unverified (whether the executor re-executes a stored list instead of calling the callback, and who sets it). Shadow share never saw it set (0 `cached_job` fallbacks). | ENGINE-PERF-PLAN.md:1048, :1251, :1262; shadow_share_model.hpp:89, :329 | [doc]; semantics unknown (U-3) |

### 2.2 Where the per-view values live

| Data | How it reaches the list | Evidence | Mark |
|---|---|---|---|
| **Per-view globals** (VS/CS/PS `b1`: view-projection with jitter, previous VP, `cb_jittervectors`, frame number; 800-byte buffers; 9 such buffers learned) | Written with `Map(WRITE_DISCARD)` on the recording context. Object motion hooks Map/Unmap/UpdateSubresource(1) on both context kinds and classifies the writes of learned b1 buffers (92,828 of 92,858 matched an eye VP in R1). A draw reads what its own list last DISCARD-wrote into its b1, or else the latest version executed before its list. | buffer_views.hpp:50-56, :126-129; object_motion.hpp:1281-1301, :1752-1797; image-quality-research.md:136-140; `r1-baseline/object-motion.json` | [code] [art] |
| Count of those writes | ~10.4 per Present in paired and ~9.7 in one-view mode. That is far fewer than the ~45 lists a view binds them in, and they do not double with the second view. So most lists probably read a b1 that an earlier list wrote. Which lists write it is unknown. | `object-motion.json` of R1 / R5 | [art]; the reading is [inf] (U-2) |
| **Per-draw constants** (`PerInstanceCB` at VS `b2`: `cb_mvpmatrix` = VP·M, `cb_modelviewmatrix`, `cb_previousmvpmatrix` for z-prepass/velocity) | Composed on the CPU per draw per view and written into b2 (Map DISCARD / UpdateSubresource) on the same recording context, right before the draw. R1: 918.1 b2 writes per Present for 918.1 layout-reflected draws (one write per draw); 142 of 8.2 M draws had no same-context write. The engine fills the previous rows from its cached MVP of the last rendered view. | WEAPON-DRAW-PLACE-TEMPORAL.md:272-282, :328-349; object_motion.hpp:1-15, :45-48, :1291-1292; `r1-baseline/object-motion.json` (`layout_draws`, `shadow_writes`, `no_shadow`) | [code] [doc] [art] |
| **Instanced world records** (`ro_directinstancebuffer`: dynamic structured SRV, 96-byte records = current + previous world rows) | View-independent rows. The view-projection comes from the per-view constants. Mapped each frame with DISCARD or NO_OVERWRITE. Whether the record set is filled per view (visible instances) or once per frame is unknown. | object_motion.hpp:1799-1815, :1944-1949 | [code]; fill scope unknown (U-4) |
| **Per-view light/shadow buffers** (`shadowMapsInfos`, `cpu_lightToPutInTiles(_cb)`, per-tile heads) | TLFLIGHTCULL builds them from the view's light list and projection and uploads them inside its own list (`0x2d3ef0`: Map when the wrapper type is 3, else UpdateSubresource), into GPU buffers shared by both views at different list positions. | ENGINE-PERF-PLAN.md:1873-1934, :2073-2077 | [doc] static |
| **Render-graph per-view parameters** (job item `+0x58/+0x60`, `+0x80/+0x88` resource tables, `+0xe0` named objects, `+0x48` `parallelShadows`, `+0x70` view) | Resolved at record time into binds (baked into the list). The mod's per-eye TAA history twins are applied here, at record time, in the POSTPROC job's `+0x80` table. | ENGINE-PERF-PLAN.md:1460-1468; native_taa_history.hpp:17-30 | [doc] [code] |
| **Scene render targets** | Shared by both eyes: in tonight's R1 trace the eye-0 and eye-1 phases bind the same depth (`...254752`, 2246x2419) and HDR/G-buffer texture objects. The mod copies eye 0's image out at the eye-1 begin. | `r1-baseline/dxgi-gpu-trace.csv` (phases 2 and 3); image-quality-research.md:34-37; DEVELOPMENT.md:694; VR-PERF-LAPTOP.md:283 | [art] [doc] |
| Immediate-context updates between lists | None observed for scene state. The engine has an immediate-context fallback for jobs flagged `0x400` (`0x2c5f60`), which creates no list. | render_pass_map.hpp:10-13; ENGINE-PERF-PLAN.md:906-910 | [doc]; absence of immediate CB updates is [inf] |

**Answer to the plan's gate** (PERF-PLAN-90HZ 2B.2: "Go only if the view block can be swapped between ExecuteCommandList
calls instead of being baked in by Map(DISCARD)"):

- For the b1 per-view block the answer is "partly, probably" [inf]. Lists that do not write b1 themselves read whatever
  version was executed last, so an immediate-context write between lists would reach them.
- For the per-draw b2 block the answer is **no** [code][art]. Its view-projection-model matrix is written per draw inside the
  list, and a finished `ID3D11CommandList` is immutable [pub].
- D3D11 allows only `WRITE_DISCARD` / `WRITE_NO_OVERWRITE` maps on a deferred context [pub].

## 3. 2B.2: replaying eye 1's lists for eye 2

### 3.1 What would have to be true

| # | Requirement | Status | Evidence |
|---|---|---|---|
| R1 | A command list may be executed more than once | **True** | [pub] "A command list is immutable"; it is meant to be "played back" when "you need to render something often" (MS Learn, Command List). ExecuteCommandList validates only queries begun outside the list. |
| R2 | Eye 2 renders into the same targets eye 1's lists bind | **True** (current build, sim) | [art] R1 trace; eye 0 is copied out at the eye-1 begin. A canted/asymmetric eye still renders at the same size. |
| R3a | The per-view b1 block can be replaced between executes | **Unknown, possibly true for lists that do not write b1** | section 2.2; needs U-2. The mod would also have to update the b1 buffer itself (or rebind a mod-owned copy at record time) on every stage that reads it. |
| R3b | Per-draw constants hold no view-dependent values | **False** | `cb_mvpmatrix` = VP·M, `cb_modelviewmatrix` and `cb_previousmvpmatrix` are per draw and per view, written in-list; one b2 write per layout draw in R1 |
| R4 | Both eyes draw the same set | **False** | Per-view culling: GATHER_UMBRA, GATHER_DYN_MODELS, light lists. Shadow casters differ for 2.3 % of faces (v817/v818); ~4 of ~23 shadowed lights per frame differ per view (v1010 counters, ENGINE-PERF-PLAN.md:1860-1871) [doc]. A replay misses what only eye 2 sees at its outer edge. |
| R5 | The lists write nothing that must differ per eye or must not repeat | **False** | TLFLIGHTCULL tile lists and `shadowMapsInfos` upload from eye 1's projection; the coarse-depth readback the view build copies from a 4-slot staging ring (dxgi_probe.cpp:7460-7463); OCCLUSION_QUERIES; the ocean point-evaluation Dispatch's queue semantics (native_ocean_eval.hpp:3-10); CLEAR [doc][code] |
| R6 | The mod's per-eye fixes do not live in the recorded lists | **False** | Recorded per draw or per job: object motion's per-eye previous rows (pool b2 bound per draw, object_motion.hpp:10-15, :1326-1331); weapon/held placement by the pass's eye view (render_pass_map.hpp:351-353, :572-585); per-eye TAA history twins (record-time `+0x80` table); visibility stamp; shadow-share redirects [code]. A replay carries eye 1's versions into eye 2. |
| R7 | The CPU side effects of eye 2's recording are not needed | **False / unknown** | The engine's per-draw MVP cache ("last rendered view") would stop holding eye 2's matrices, which breaks the `PrevMvpTable` chain (object_motion.hpp:45-48). Shadow-share capture/restore expects both views' TLFLIGHTCULL. Other consumers are unknown. |
| R8 | The saving is worth it | **False here** | Section 1: the whole second view is ~0.25 ms of engine time. The replay would remove only the recording part; the ECL part (~0.08 ms on NVIDIA, the dominant part on AMD) remains. |

### 3.2 Hazards, named

- **Per-view render targets.** These are not a hazard here: the targets are shared (R2). The hazard is the opposite one: the
  eye-1 image must be captured before eye 2's lists run, which the mod already does.
- **Resources written in-list.** A replay re-applies eye 1's DISCARD writes (b1, b2, tile buffers, instance records), so
  everything eye 2 reads after it sees eye 1's data unless each write is intercepted at record time.
- **DISCARD versioning.** Replaying a list that DISCARDs is legal; the runtime or driver renames the buffer each time [pub, inf].
  A NO_OVERWRITE map in a list that did not DISCARD the buffer first (ring-buffer style) would write into regions later lists
  may own when replayed out of order [inf]. Object motion hooks NO_OVERWRITE maps of the instance buffers, so the engine
  uses them (object_motion.hpp:1781) [code]. Exact use is unknown.
- **Queries.** A replayed OCCLUSION_QUERIES list re-issues eye 1's Begin/End on the same query objects. The CPU then reads
  eye-1-geometry results in eye 2's slot. The mod's own timestamp queries wrap each ECL on the immediate context and are not
  affected [code] (native_list_gpu_timer.hpp).
- **TAA history.** The engine has one ping-pong pair chosen by global frame parity (image-quality-research.md:24-31). The mod
  swaps per-eye twins at record time (native_taa_history.hpp:17-30). A replayed eye-1 POSTPROC list would read and write eye
  1's history in eye 2.
- **The mod's own interception.** Shadow share, weapon placement, rig/held masks and the visibility stamp all key on the job's
  view `item+0x70`. A replay has no job, so none of them can run for eye 2.

### 3.3 How the reference mods do it, and why it does not transfer

- **BetterVR 1.0 (Cemu, Vulkan)** [brief + inf]: the emulator owns the guest's uniform memory and translates the guest command
  stream itself. It can re-submit a pass with the right-eye camera values. Here the matrices are composed by game code on
  the CPU and frozen into D3D11 lists we do not translate.
- **Dying Light VR** [brief]: uses the Chrome Engine's own record-once / play-per-eye path. The Void engine exposes no such
  path. The only related bit is the unverified `item+0xf4 & 0x100` cached-list flag (U-3).
- **reference VR mod** [brief]: D3D12 view instancing. D3D11 has no view instancing (ENGINE-PERF-PLAN.md:71). NVIDIA's NVAPI
  multi-view would still need every vertex shader rewritten.

### 3.4 Verdict

**No-go.** R3b, R4, R5 and R6 are false, and R8 caps the gain at a fraction of a millisecond on the user's PC.

### 3.5 The only technically open route (not recommended)

Shader-level stereo in the style of 3Dmigoto / Helix: patch every vertex shader at `CreateVertexShader` (the dinput8 proxy
already sees them; 6,184 VS, 2,023 with `cb_mvpmatrix`, WEAPON-DRAW-PLACE-TEMPORAL.md:263-268) to multiply the clip position
by a per-eye `X = VP_R · VP_L^-1` from a mod-owned constant buffer. On top of that:

- fix every other view-dependent input (view-space position, camera position, inverse projections in screen-space passes,
  jitter, previous MVP);
- widen eye 1's culling to the union frustum;
- re-derive light tiles per eye;
- rework all of the mod's per-eye record-time fixes.

Weeks of work, high image risk, and still capped by section 1.

## 4. 2B.1: per-view CPU passes that could run once

Pass ids are `src/render_pass_table.hpp`. Per-pass GPU ms are R1 [art] (`gpu_passes`, L = eye-0 phase, R = eye-1 phase).
**No per-pass CPU timing exists.** Every CPU statement below is static, from a pass name, or "unknown".

| id | Pass (callback RVA) | Writes per view | Risk if run once | Verdict |
|---|---|---|---|---|
| 3 | UPDATE_NPC_DECALS (0x340F10) | - | - | **N/A**: already once per frame (GPU L 0.94 / R 0.00) [art] |
| 4 | CULL_OCEAN (0x31A320) | Per-view ocean patch set [inf, name] | Missing ocean patches at eye 2's edge | **No-go**: low value, ocean spots only |
| 5 | GATHER_UMBRA (0x31C710) | Per-view occlusion-culling result (Umbra is a CPU occlusion middleware; the game has umbra gates, door-push-research.md:97) [inf] | High: objects visible only to eye 2 culled (edges, disocclusion). Shared culling measured slower in id Tech (ENGINE-PERF-PLAN.md:12, :71) [doc]. | **Needs-live-check** (L1, then L2, then static U-5). Default no-go. Possibly the largest per-view CPU pass, but unmeasured. |
| 6 | GATHER_LIGHTS (0x31BCC0) | The view's light list (`item+0x40`); clears `light+0x1a8` for every light (0x31c074) [doc] | High: ~4 of ~23 shadowed lights differ per view [doc]; it is the first link of the shadow-share chain (v1009-v1012) | **No-go** |
| 7 | INDIRECT_LIGHTING_GATHER_LIGHTS (0x3101D0) | Unknown (GI light set) | Medium: unknown view dependence | **Needs-live-check**, low priority (L2; skip-callback A/B) |
| 8, 9 | GATHER_DYN_MODELS (0x31AD30) + _FINALIZE (0x31A150) | Per-view visible dynamic models; probably the per-draw MVP composition or its cache [inf: object_motion.hpp:3-5 "MVP in the last rendered view"] | High: eye 2 would get eye 1's visible set and possibly eye 1's MVPs (no stereo on entities) | **No-go** unless static U-5 shows MVPs are composed at record time; then needs-live-check |
| 10 | SORT (0x33D320) | Per-view draw order/keys from the eye position [inf] | Only meaningful after 8/9 are shared | **No-go** standalone |
| 11, 12, 13 | SHADOWS_PREPARE / COLLECT_DYNAMIC_CASTERS / GATHER_CASTERS | - | - | **Done**: shadow share skips them for view 2 (setup mode; R1: 36 second-view jobs skipped per frame, 0 fallbacks) [art][doc] |
| 14, 15 | INDIRECT_LIGHTING_UPDATE_DYNAMIC_MODELS (0x310230), _UPDATE_WORLD_STATE_AND_OCCLUDERS (0x310200) | GI state for models and occluders [inf] | Low-medium. The flat game runs them once per frame, so running them once restores the flat rate; the risk is a hidden per-view dependence. | **Needs-live-check** (L2 CPU; image A/B). Part of 2A.3. |
| 16 | GPUCULLING (0x2FC030) | Per-view GPU visibility | Per view by nature; not active at these spots (`r_useGPUCulling` was a test-spot artefact, ENGINE-PERF-PLAN.md:10) | **No-go** |
| 18 | DEPTHMINMAX (0x2FC130) | Reduction of the eye's own depth | Per view by nature | **No-go** |
| 22, 23, 24 | INDIRECT_LIGHTING_ACCUMULATE_FINAL_SH / _UPDATE_IBL / _UPDATE_PROBES | Probe/SH/IBL state [inf]; GPU <= 0.06 ms (UPDATE_PROBES L 0.007 / R 0.064) [art] | Low-medium (same flat-rate argument) | **Needs-live-check**, low value. 2A.3 territory. |
| 26 | TLFLIGHTCULL (0x2F7260) | Per-view tile light lists and `shadowMapsInfos` upload from the eye's projection [doc] | Per view by nature; the shadow-share capture/restore lives here | **No-go** |
| 30 | UPDATE_OCEAN_FFT (0x3053B0) | Ocean FFT [inf: view-independent]; GPU 0.003 ms [art] | Low | **No-go on value** (< 0.01 ms) |
| 31 | UPDATE_OCEAN (0x305370) | Point-evaluation Dispatch, already once per frame slot (v731) [code]; GPU L 0.04 / R 0.02 | Low | **No-go on value** |
| 45 | TEXTURE_STREAMER (0x34FD00) | Unknown whether it runs per view | Low | **Needs-live-check** (L4: does view 2 get this job?) |
| 46-50 | IGGY_* (UI: 0x31A690, 0x31A710, 0x31A8E0, 0x31AC40, 0x31AB10) | UI tessellation and draws [inf]; IGGY_FULLSCREEN has lists in both phases (GPU L 0.22 / R 0.03) [art] | Medium: the mod captures HUD art from the game's HUD deferred set (`begin_native_hud_art`); skipping view 2's UI may change what the overlays capture | **Needs-live-check** (L2 CPU; static which view the HUD capture uses) |
| 51 | OCCLUSION_QUERIES (0x31D7E0) | Per-view hardware queries read back later by the CPU | Per view by nature | **No-go** |
| 0, 2 | CLEAR, ASYNC_CLEAR | Clears the shared targets for this view | Eye 2 must clear | **No-go** |
| 17, 19-21, 25, 27-29, 32-43 | Raster passes (DEPTHPASS, FORWARD_*, FOG, POST...) | Recording of eye 2's image | That is 2B.2 (section 3) | **No-go** |

Mechanism, if a "run once" candidate passes L2: the existing callback substitution on the single executor hook
(`skip_shadow_pass`, render_pass_map.hpp:217-220, :459-466). It needs no new hook address, and the executor still finishes an
empty list. Precedent and warning: the shadow-setup sharing needed six builds (v1000-v1012) because the per-view outputs of
skipped passes were not where the static analysis first placed them (light links, record counts). Budget each pass for that.

## 5. Live checks to run next (coordinator-owned; the user must not be playing)

| # | Check | How | Settles | Effort |
|---|---|---|---|---|
| **L1** | Second-view cost on a quiet machine, at the sim spot **and** the VD heavy-area spot | `tools/win_perf_sim.py`: paired vs `--knob perf-mono-view` (R4: one engine view, no AER warp work), interleaved, 3 rounds each, same save/script, nothing compiling. Read `xr-frame-timing.csv` work p50/p90, `frame_split` eye spans and `hook_ms`, `ecl_replay`, `gpu_passes` (`tools/perf_night_report.py`), plus one `tools/thread_profile.py` per arm (main-thread run/wait split). | Whether 2B is worth anything. If the work p50 delta is < 1 ms at both spots, close 2B.1/2B.2 for this iteration. Repeats R5, which overlapped compiles. | ~2 h of runs |
| **L2** | Per-pass CPU time per view and per thread kind | Small knob-gated build: QPC around `original_(item)` in `RenderPassMap::execute_hook` for every tagged pass (the shadow jobs already do this in `begin/end_cache_job`). Accumulate by pass x view order (shadow share's per-frame view index) x main/worker thread; status `cpu_passes`. No new hook address. Read-only alternative: stack sampling attributed to the executor return `0x2c5d86` plus the callback frame (SuspendThread, so also coordinator-owned; less exact). | Which per-view passes are heavy, and whether they land on the main thread. Only worth doing if L1 >= 1 ms. | S: build + lab ~0.5 day, runs ~1 h |
| **L3** | Static checks on the exe on disk (no game process) | PE + capstone, as in phases 7-10: (a) the executor's `item+0xf4 & 0x100` branch and who sets it (0x2c5d6c, 0x2c77b0 area); (b) where `cb_mvpmatrix` is composed: FINALIZE `0x31a150` vs the recording passes; (c) which jobs Map the b1 per-view buffers; (d) GATHER_UMBRA `0x31c710` inputs and output storage. | U-2, U-3, U-5; turns several "inferred" rows above into facts | ~1 day |
| **L4** | Read-only memory read (game paused or still) | ReadProcessMemory (`tools/entities.py` style) of the pass vectors at `render_system+0xd7f0 + id*0x18` (0x120-byte items): `+0xf4` flags (any `0x100`?), `+0xfc` counts, `+0x70` view for each pass, sampled at both eye boundaries if a hook-free method allows. Otherwise from L2's status. | Which passes get a job per view vs per frame (TEXTURE_STREAMER, IGGY), and whether cached jobs exist at all | ~1-2 h |

Effort if anything goes ahead:

- 2B.1 per accepted pass: 3-6 days (skip and per-view output carry, image A/B, headset).
- 2B.2: not recommended. The section 3.5 route would take several weeks.
- The main-thread time that does exist is elsewhere:
  - the mod's Present hook (~1.0 ms p50 on the main thread, plan 2B.4);
  - game code (46 % of the main thread, plan 2C);
  - the untested job-count cvars (`jobs_numThreads`, `r_schedulerThreadingMode`, PERF-NIGHT section 7).

## 6. Top unknowns

- **U-1**: The real cost of the second view at the heavy spot on a quiet machine. R5 overlapped compiles, R4 never ran, and
  the VD heavy area was never CPU-profiled (L1).
- **U-2**: Which lists write the b1 per-view buffers. About 10 writes per Present in both paired and one-view mode, against
  ~45 lists per view (L3c, L2).
- **U-3**: The meaning of the `item+0xf4 & 0x100` cached-list bit. It is the only hint of an engine-side list-reuse path
  (L3a, L4).
- **U-4**: Whether the instanced world records (`ro_directinstancebuffer`) are filled per view or per frame, and what share
  of the draws is instanced (b1-driven) vs per-draw b2 MVP (L3).
- **U-5**: Where per-draw MVPs are composed, and what GATHER_UMBRA / GATHER_DYN_MODELS write. This decides whether any
  culling pass could ever be shared (L3b, L3d).

## Sources

- Repo: `docs/ENGINE-PERF-PLAN.md` (lines cited above), `docs/PERF-PLAN-90HZ-2026-11-19.md`, `docs/PERF-NIGHT-2026-11-19.md`,
  `docs/VR-PERF-LAPTOP.md`, `docs/WEAPON-DRAW-PLACE-TEMPORAL.md`, `docs/image-quality-research.md`, `docs/DEVELOPMENT.md`,
  `src/render_pass_map.hpp`, `src/render_pass_table.hpp`, `src/shadow_share.hpp`, `src/shadow_share_model.hpp`,
  `src/object_motion.hpp`, `src/buffer_views.hpp`, `src/native_list_gpu_timer.hpp`, `src/native_taa_history.hpp`,
  `src/native_ocean_eval.hpp`, `src/dxgi_probe.cpp`.
- Artifacts (2026-11-19 sim, read-only): `artifacts/perf-night/r1-baseline`, `r5-performance-mode`, `v1394-half`
  (`during.txt`), `v1396-dlss-defaults`, and the other run folders' `xr-live-status.json`.
- Microsoft Learn: `ID3D11DeviceContext::Map` (deferred contexts: DISCARD / NO_OVERWRITE only),
  `ID3D11DeviceContext::ExecuteCommandList` (RestoreContextState FALSE returns the context to default state; query validation),
  "Command List" overview (immutable, recorded once and played back).
- Reference mods: as summarised in the coordinator's brief and PERF-PLAN-90HZ section 2. Their source was not re-read for this
  spike.
