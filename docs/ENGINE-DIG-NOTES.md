# Engine dig notes: where to resume (2026-11-19)

Everything here comes from the Steam build of Dishonored 2 (exe sha256 `b671f824…6180`), measured with the simulated-headset rig on a Ryzen 5 5600X + RTX 5070. Addresses are exe RVAs.

The bigger picture is in `docs/PERF-NIGHT-2026-11-19.md` section 9. Public references on game/render overlap are in `docs/research/GAME-RENDER-OVERLAP-REFERENCES.md`.

## 1. Why this matters

After the DLSS fix the frame is CPU-bound on the game's main thread: 9.4–10.2 ms median at the user's Virtual Desktop setup, against an 11.1 ms budget at 90 Hz. The slowest 10–15 % of frames come from the game's own simulation in busy stretches. In the user's save one busy stretch repeats at 38–50 s into gameplay, with p90 around 14 ms.

The mod's own share of the main thread is about 0.8–1 ms: the Present hook (~0.5 ms), DLSS's CPU side (~0.27 ms) and the guarded game-memory reads. None of the cheap levers closes the gap (§6). What would close it is engine-structural: overlapping the game side with the render side.

## 2. The frame, mapped

Mapped from real stack walks (`tools/stack_tree.py`).

```
WinMain exe:e38a0 -> main loop / frame exe:450d0 (once per frame)
  pre-game steps: f2130, 11ac5bc, f30e0, 44ec0, ca490, 3a900, 43650 (builds the frame struct at [rsp+30])
  GAME SIDE  exe:459a0(engine obj rbx, &frame)                                ~25 % of the main thread
     -> 8acdf0 "PlayTime" -> 8ad260 (game frame; the mod's hand_body hooks it) -> 8af8a0 / 8af460
     -> 7ec5b0 -> 492540 -> 7a9e0 "TtarkSubmitAndWaitGraph" -> aab400 -> 9f7f0 (job graph run/wait)
     -> 492670 "TtarkComponentUpdateTask"
  in-between: 45650 (called right after; NOT every frame reaches it), 61a0a0, 9a9450, 48a50, vcalls, 45360 "screenshot", c9b40, 11ada44,
              45460 (native_console hooks it)
  RENDER SIDE exe:458c0(engine obj)  [skipped when byte [rbx+0x1e91] != 0]    ~73 %
     -> 5e100 -> 361b2b/361bc0 -> 361d60 -> RenderSingleView 351780 -> 2c8440 -> 7ad60 -> 9f7f0 -> 2c8490 (render graph executor)
                                                                                     -> 2c5d50 (pass job executor; RenderPassMap's hook)
                                -> 3591b0 -> 36b3a0 -> IDXGISwapChain::Present (the mod's XR path, then the desktop Present)
           -> 361a59 -> 35dc60 (other render steps; 374b60 -> a mod hook; 239ea0 -> d3d11)
job waits: 158570 / 158586 / 158468 (ZwWaitForSingleObject)
CPU topology: 3fbf0 calls 0xde2c0 (GetLogicalProcessorInformation via GetProcAddress) and WRITES the cvar jobs_numThreads (it is an output)
```

**Order correction (2026-11-20, verified in the disassembly; `docs/research/ENGINE-OVERLAP-STAGE2.md` section 0).** Inside `361bc0` the
Present path `3591b0` (call at `361c63`) runs BEFORE `361d60`/RenderSingleView (call at `361cda`). The engine presents frame N at the head of
frame N+1's render side, after game side N+1. Only small steps follow the render graph (`369420`, `2ecd00`, `2bcd70`). The mod's XR
submit and pacing wait therefore sit between the game side and the render graph of the next frame.

**Shares of the main thread** (stack walks; the sampling slows frames, so read shares, not ms):

| part | share |
|---|---|
| render side, all of it | 73 % |
| `RenderSingleView` (the render job graph: the main thread runs jobs or waits) | 46 % |
| the mod's Present/XR path | 13 % |
| other render steps | 9 % |
| game side | 25 % |
| the main thread blocked in a wait (across both sides) | 29 % |

Busy stretches grow the game side (24.8 → 26.3 %) and its job waits (7.9 → 9.4 %).

**Threads:** the main thread, 5 job workers (each ~50 % busy) and the NVIDIA driver's threads (`nvwgf2umx`; `nvgpucomp64` compiles shaders). There is no separate game or render thread: the game and render sides run in sequence on the main thread.

**Joint states** (`tools/thread_profile.py`, the `rounds` field): while the main thread waits on engine jobs, usually only 1–3 of the 5 workers run, and all 5 only ~4 % of the time. The waits are dependency chains, not a worker shortage, so more workers would not help.

**GPU API use** (`tools/worker_d3d.py`):
- Game-side stacks never pass through d3d11/dxgi/the driver: 0 of 60 main-thread walks and 0 of 177 worker walks.
- Render jobs do pass through d3d11 (90 of 834 worker walks).
- Between the halves only the Steam overlay's input hooks appear (`USER32`), not D3D.

## 3. The overlap experiment (v1408–v1410, opt-in `build/async-present`, off by default, crashes)

Stage 1 moved only the desktop window's Present (Steam overlay + DXGI + driver, ~0.7 ms) onto a worker thread, to overlap the next frame's pre-game steps and game side. `src/async_present.hpp` implements the hand-off and its lab (`tests/async_present_check.cpp`) passes. The knob text selects the sync point:

| text | sync point | sim result |
|---|---|---|
| `none` | no hook, never asynchronous | normal (the control) |
| `game` | wraps the game side `459a0` | game stops presenting ~20 s into gameplay |
| `next` | entry of `45650` | **deadlock**: some frames reach the render side without passing `45650`. Inside D3D11 `Map` (device lock held) the mod's safety net waited for the worker, and the worker's Present (Steam overlay → DXGI) waited for that lock (`tools/dump_stacks.py`) |
| `render` (default, v1410) | entry of the render side `458c0`; the safety net only counts | **crash**: first-chance access violations on two worker threads at gameplay start, then the process died |

The two crash sites:
- `exe:2c6080` in function `2c6047`, a neighbour of the pass executor `2c5d50`; a virtual call through a null object.
- `exe:2e5b5c` in function `2e5970`, which is **Iggy's GDraw D3D layer** (strings " creating vertex buffer", "CreateBuffer", "GDraw D3D out of memory"). Iggy is the game's Scaleform-like UI; its pass is `IGGY_FULLSCREEN`.

**Leading hypothesis.** The scheduler keeps **background tasks** (`r_schedulerNoBackground 0`: "background workers … tasks not required for the frame finish") running on the workers after the render side returns. Some of them, Iggy/GDraw UI work among them, use the **D3D11 immediate context**. That is safe in the unmodified game because the main thread leaves the immediate context idle during its game side. An asynchronous Present in that same window breaks the assumption.

**Caveat found later (12:40):** `build/alternate-eye` (the launcher's performance mode) was switched on at 10:28, during the user's
play session, so every async-Present run above (10:43-11:40) ran in **performance mode** (one eye per frame, the AFW2 rebuild), not
paired stereo. Retest in paired stereo (`win_perf_sim.py --disable alternate-eye`) before drawing conclusions; the crash may be specific
to the alternate-eye path. Always check `build/` for knobs the launcher may have added before comparing runs across a session.

Lessons:
- Never wait inside a D3D11 callback; it may hold the device lock.
- Not every frame passes every main-loop call; the render-side entry `458c0` is the only reliable pre-GPU point.
- The one-time ~11 s stalls at the start of every sim's gameplay are the two `StaleEyeWarp` shader compiles (normal).

## 4. Next steps, in order

1. **Prove or refute the hypothesis.** In the mod's immediate-context hooks (draws, viewports, targets, ObjectMotion's Map/Unmap/cbuffer hooks), count calls with `context == gpu_game_context` made from a thread other than the Present thread, and separately those made while the main thread is in the game side. A probe knob is enough.
2. **Test async Present with `r_schedulerNoBackground 1`**, set live with `cvar` before gameplay or with `--game-cvar`. Measure that cvar's own cost first.
3. **If the probe confirms it:** find the background task that uses the immediate context (start with Iggy's GDraw `2e5970` and its callers). Either make the async Present wait for it (a hook on that task's entry/exit) or move it into the render side.
4. **Stage 2, the real overlap** (render jobs beside the next game side, up to ~2.4 ms). Following `docs/research/GAME-RENDER-OVERLAP-REFERENCES.md`:
   - cut after the gather/extract passes (`GATHER_DYN_MODELS`, `GATHER_UMBRA`, `COLLECT_DYNAMIC_CASTERS`, …);
   - find out what the later passes still read from live game state (bone matrices, material parameters, UI draw lists) and snapshot it;
   - keep Present and the XR calls on the main thread (Microsoft's guidance);
   - re-sample the head pose at render time;
   - ship behind a switch that can be toggled at runtime, plus a lock-step debug mode.
5. **Half-rate simulation** is the cheaper alternative (skip `459a0` every other frame, ~1.2 ms average). It needs the user's go-ahead: player movement would update at 45 Hz, and the project rules protect the locomotion the user has accepted.

## 5. Tools and how to use them

- `tools/win_perf_sim.py`: proxy-driven sim runs.
  - Options: `--knob K[=V]`, `--disable K`, `--game-cvar N=V`, `--command 'T line'` (live `cvar`/`cvarf`/`cvarget`), `--during "tools/x.py {pid} …"` started at `--sample-delay`.
  - It refuses to run while `Dishonored2.exe` runs. `--seconds` ≤ 120 gives about 60 s of gameplay.
  - Use a fresh `--name` each time: a run folder that already has `during.txt` skips the `--during` tool.
- `tools/stack_tree.py <pid> [s] [hz] [out] [tid]`: real call tree of one thread (dbghelp `StackWalk64`). Mod frames keep offsets; resolve them with `tools/attribute_mod_hits.py`'s `resolver`.
- `tools/dump_stacks.py <pid> [delay] [out] [mod_dll]`: every thread's stack once, with mod frames resolved. This is how the deadlock was found.
- `tools/worker_d3d.py <pid> [s] [out]`: does each thread class (main or worker × game-side or render job) pass through D3D?
- `tools/thread_profile.py`: leaf sampling with per-round joint states.
- Static helpers: the scratch scripts that built the function map (`.pdata` function ranges, the strings each function references, call lists, import calls) are easy to rebuild from `tools/stack_tree.py`'s `exe_index`.
- Builds: `build\compile-night.cmd <ver>` writes to `build\night\`; install with `sh build/night/use.sh <ver>`. Check the game is not running first (the user may be playing). Labs: `build\night\lab.cmd <test> [libs]`.
- **Pitfalls:**
  - Git Bash rewrites `C:/…` arguments, so pass repo-relative paths.
  - Sampling suspends threads and slows frames, so the pacer's wait share in a profile is an artifact.
  - Medians are comparable across runs; p90 only within a run (alternating A/B, two runs in opposite order: `build/night/abab.py`).

## 6. Measured dead ends (do not retest without a new idea)

- **Thread and job settings:** the engine's threading switches are already all on (`r_schedulerThreadingMode 2`, deferred contexts, `mdnt_multithreaded_*`, `r_useAnimBlendJobs`). `jobs_numThreads` is an output. The worker COUNT is a lever after all: section 9 (proxy knob `build/job-workers`).
- **Shadow cvars:** no effect, set live or in the config.
- **LOD and frame-target cvars:** no effect set live (`lod_dist_*`, `mdnt_minDistanceForLod`, `com_FPSTargeted`).
- **Game ticking:** no physics-step rhythm in the frame times; `com_fixedTic` is 0.
- **Engine timing output:** `com_speeds` and `com_logFile` print nothing; the retail build has no console log.
- **Rendering tricks already measured:**
  - the static shadow cache as designed;
  - replaying eye 1's lists (per-draw matrices are baked into the lists);
  - the second view's CPU cost (~0.24 ms) is not the lever.
- **Small mod-side items:**
  - the mod's body/hand features (~0.1 ms);
  - per-frame GPU allocations (~0.1 ms).
- **Thread wake-ups, core sharing, the "VSSetShader wait"** (section 8):
  - wake-up latency is 3.6 us mean;
  - giving the main thread its own core changed nothing (12 interleaved pairs);
  - the VSSetShader wait was a raw-stack-scan artifact;
  - GlyphSwap's shared lock on every texture bind: a lock-free per-thread memory of non-glyph views (68 % of lookups skipped the lock)
    changed nothing (21 interleaved pairs, median +0.004 ms, faster in 10 of 21). Reverted.
- **The desktop mirror** costs ~0.7 ms per Present, but the user does not want the desktop window thinned by default ("no cut desktop"); thinning stays opt-in via `build/desktop-mirror-every`.

## 7. Night of 2026-11-20: the probe, the engine lock, a stable asynchronous Present (v1427-v1428)

Design: `docs/research/ENGINE-OVERLAP-STEP1.md`. All runs: the user's save, `run_hands`, paired stereo, spacewarp off.

**E0, ctx-probe (knob `build/ctx-probe`, `src/ctx_probe.hpp`; status `ctx_probe`, `ctx-probe.jsonl` with stacks).** Phase detours on
`459a0` (game side) and `458c0` (render side), counters per thread kind x phase x call kind on the game's immediate context.
- The main thread makes no immediate-context call inside the game side (0).
- Engine render-graph workers call OMSetRenderTargets and ExecuteCommandList on it inside the render side (266k / 239k calls).
- Outside both sides only one kind of call happens: an engine worker (entry `7a1e1`/`a0095`) running a job through `2ea4aa` (next to
  Iggy's GDraw) -> `362531` -> `35ea43`, which takes the engine's own immediate-context spinlock (`36a6f0`/`36b4e0` on `0x1432FC580`:
  enable byte `+0x63AC4A`, lock int `+0x63AC4C`, `lock cmpxchg` 0->1, unlock stores 0) and calls ExecuteCommandList (vtable `+0x1D0`) on
  `[0x1432D4EF8]`. The OMSetRenderTargets the probe counted there is the mod's own ExecuteCommandList hook chain inside that call.
- The background-task hypothesis is confirmed with a twist: background immediate-context use exists, but always under that lock.

**E1 (`build/async-present=render` + probe).** No access violation in paired stereo (the v1408-v1410 AVs were not this). The safety net
still switched the feature off after one Present: (1) the worker's own Present (Steam overlay + DXGI) sets viewports through the mod's
hooks, which counted it as a violation (fixed: the worker's calls are its own); (2) the render side goes on using the immediate context
after its Present (`5e16e` -> `361b97` -> ... -> RSSetViewports), so a Present handed off inside the render side overlaps it. v1428: the
Present hook only records the call (`AsyncPresent::defer`); `render_side_hook` hands it to the worker when the render side returns
(`kick_deferred`); the worker holds the engine spinlock around its Present; the next render-side entry waits. Result (E1d): 1256 of 1256
gameplay Presents asynchronous, 0 violations, 0 lock spins, no crash.

**But no gain.** Frame work p50 11.48 ms with and without it (p90 17.42 vs 16.33). The worker's Present takes ~1.35 ms (it competes for
the 6 cores) and on 25 % of frames the next render side waits for it (~2 ms): that eats the ~0.6 ms it saves. Kept opt-in
(`build/async-present`, dev command `asyncpresent on|off`). The remaining overlap that could pay is stage 2 (render jobs beside the next
game side), not attempted.

**Also measured:** diagnostics on vs off, frame work p50 12.77 vs 12.84 ms (no cost on the main thread).

**Fixed on the way (v1428):**
- Every session's first gameplay frame spent ~11-15 s in D3DCompile on the main thread (StaleEyeWarp's shaders for the
  canted-to-straight resample; the spacewarp pass ~1 s more when it first engaged). Compiled bytecode is now cached under the mod
  folder (`shader-cache/`, `src/shader_cache.hpp`) and a background thread fills it during the menus: sim, first gameplay Present 0.3 ms,
  no Present over 0.5 s in the whole run.
- With diagnostics off the stereo-capture block never completes (its frame capture needs diagnostics), so it ran every Present and
  retried a failing `TaaDispatchHook::install` each time (~2 % of the main thread in MinHook's address checks). The install is now retried
  once a second. Why that install fails without diagnostics (MH_CreateHook on Dispatch) is not known; whether FSR/DLSS replace the TAA in
  that configuration should be checked (the sims here ran without the upscaler).

**Main-thread profile, v1429, the user's config (diagnostics on, spacewarp off, 8 s of `run_hands`):** 37 % of the samples in mod frames:
`ObjectMotion::set_vs_hook` 10.9 %, `GameXr::present` 2.1 % (the one-frame-in-flight fence wait), then many items under 2 % each
(`read_self_raw`, `native_grab`, `native_readable_range_cached`, RenderPassMap, glyph swap, object motion's Map/cbuffer hooks).
**Correction (section 8):** that profile came from `tools/thread_profile.py`, which attributes a waiting thread to the first non-system
address it finds by scanning raw stack memory. The `set_vs_hook` share (and section 6's "wait inside VSSetShader") was a stale return
address left in the stack, not a wait in that hook. Real stack walks never pass through it.

## 8. Later on 2026-11-20 (03:10-04:05): three thread levers measured, none pays (v1430, all opt-in, off by default)

All runs: the user's save, `run_hands`, spacewarp off unless stated. "User-like" means the user's headset eye size `--eye-size "2496 2688"`.

**Job wake-up latency (`src/job_wake.hpp`, knobs `build/job-wake` = measure, `build/job-spin` = "<main us> <other us>", dev command
`jobspin <main> <other>`).** The engine's threads sleep in KERNEL32 `WaitForSingleObject` (wrappers `158450`/`158570`/`158590`) and are woken
by `SetEvent`/`ReleaseSemaphore`. The probe patches only the exe's import table and times every wait and the signal that ended it.
- Main thread: ~49 waits per gameplay frame, ~8 of which sleep; wake-up latency mean 3.6 us (87 % under 5 us, 13 % 5-20 us).
- Workers: same picture, mean 3.8 us.
- So wake-ups cost the main thread ~0.03 ms per frame. Spinning before the wait (50 us main, 20 us workers) changed nothing measurable.
  The Balanced power plan is not slowing the job chain.

**Core isolation (`src/core_isolation.hpp`, knob `build/core-isolation` = `soft`|`hard`, dev command `cores off|soft|hard` and
`cores abab soft|hard <ms>` for measurement).**
- A read-only probe of every thread's priority, affinity and ideal processor found that nothing is pinned. All threads use affinity
  `0xfff`. The 5 job workers (start `7a220`) have ideal processors 0/5/7/9/11 and the main thread has 1. CPUs 0 and 1 are one physical
  core, so the main thread's preferred core is shared with a worker.
- `hard` gives the main thread core 1 (CPUs 2 and 3) alone and keeps every other thread of the process off it.
- The mod switched hard isolation on and off every second and logged each switch (status `core_isolation.switches`). This gave 12
  interleaved pairs at user-like DLSS Quality: median on-off +0.02 ms, faster in 6 of 12.
- No effect. SMT sharing does not slow the critical path. Analysis script pattern: pair each 1 s segment with the next one.

**Real call tree of the main thread (`tools/stack_tree.py`, StackWalk64; v1430, user-like DLSS Quality).**
- Render side 82 %, game side 17 %. The 16 % "wait in `GameXr::present`" is `xrWaitFrame`: the sampling slows frames until the pacer
  locks to half rate (a known artifact).
- Without it, the mod's exclusive share is ~13 %. The largest items:
  - guarded game-memory reads, 3.5 %: `VirtualQuery` on cache misses 1.9 %, the reads 1.6 %, mostly `NativeGrab::focus_body`'s discovery;
  - the sim-only scripted eye dumps (`capture_xr_transfer`, 21 MB every 2 s), 0.8 %;
  - `GlyphSwap::lookup`, 0.6 %;
  - per-frame file opens and `fs::exists` polls, ~0.5 %.
- At most ~0.3-0.5 ms is left to trim, in many small cuts.

**The user's own lever is the upscaler, not the engine threads (same sim, only `--aa-mode` changed, spacewarp off):**

| setting | frame work p50 / p80 | engine gap p50 | frames under 11.1 ms |
|---|---|---|---|
| DLAA (the user's choice, mode 7) | 13.97 / 17.64 ms | 13.33 ms | 12 % |
| DLSS Quality (mode 8) | 12.24 / 13.89 ms | 11.61 ms | 36 % |

- With DLAA the pacer locks to half rate even without spacewarp: the GPU load blocks the engine's thread for ~1.7 ms more per frame.
- The user's v1423 headset session matches the DLAA row: engine gap p50 13.84 ms; 8,800 of 9,125 gameplay presents generated (45 real +
  45 spacewarp), only 8 late.
- At these frame times no CPU trim below ~3 ms changes what the headset shows. Auto unlocks only when the 80th percentile of the work
  drops under 8.9 ms.

**NVIDIA VRS at DLAA (v1431, dev command `vrs abab 1000`: coarse shading on in even seconds of the frame clock, off in odd ones).**
Tested because DLAA blocks the engine's thread ~1.7 ms more than DLSS Quality.
- Setup: user-like DLAA, spacewarp off, default rings (full rate within 35 % of the render height, 2x2 beyond; 59 % of the tiles
  coarse); 4 runs, 25 one-second pairs.
- Frame work: median on-off -0.17 ms (mean -0.28 +- 0.47 s.e.), faster in 15 of 25 (sign test p = 0.42).
- Engine gap: faster in 9 of 25.
- No measurable gain. The first run's -1.2 ms (5 pairs) was noise. The DLAA cost is not the scene passes' pixel shading that VRS reduces.

**What is left for the engine: stage 2, statically assessed (`docs/research/ENGINE-OVERLAP-STAGE2.md`).**
- Submit and wait of the render graph are separate functions (`2c8310` / `2c8440`, back to back at `358202`/`35820a`).
- The hand-off from the game to the renderer is single-buffered and pointer-based:
  - `45650` rebuilds the world/view object `142BC58A0` in place;
  - the gather `35dc60` and RenderSingleView read live game objects.
- Game side N+1 beside render side N therefore needs, beyond a helper thread:
  - a snapshot cut after the gather;
  - `45650` delayed until the render finishes;
  - a mutex between Iggy's tick (`2cc90` -> `100ccd0`, game side) and its draw (IGGY pass callbacks `31a710`/`31a8e0`): the guard is a
    busy flag `[player+0x3584]`, not a thread check;
  - care with `IsMainThread` (`166670`).
- The "tail only" variant has nothing to overlap, because Present comes first.
- Verdict: a multi-day project with crash risk, and up to ~2.4 ms at best. Profile what the pass jobs read from game objects before
  starting.
- Phase 0 measured it (`docs/ENGINE-OVERLAP-PLAN.md` section 5, v1432 `build/overlap-probe`):
  - the next game side writes 26 us after the render side returns, so deferring the render graph's wait gains 0.04 ms;
  - the ~2.7 ms overlap (render graph wait 2.66 ms vs game side 3.26 ms p50) needs the snapshot rewrite.
  - Phase 0b: the two eyes' graphs run one after the other, both on one render context (first wait 3.70 ms, second 2.58 ms p50), with
    only 0.11 ms of main-thread work between them. Running them at the same time (a second context, separate targets) is the other deep
    lever.
- Half-rate simulation needs the user's go-ahead on 45 Hz movement.
- Neither brings this PC to 90 real fps at DLAA. Spacewarp's 90 is the frame rate the headset shows.


## 9. 2026-11-20 09:10-10:05: the eye graphs are wide; more job workers pay (v1433, proxy knob `build/job-workers`)

**Pass timeline (v1433 `build/pass-timeline` with `build/overlap-probe`, measurement only).**
- Every job item of the executor `2c5d50` is timestamped on its thread, along with the main thread's graph submits and waits and the
  render and game sides. Details: `docs/research/ENGINE-PARALLEL-EYES.md` section 10.
- Eye 0's graph: span 2.66 ms, 4.3 of the 6 job threads inside an item on average, 12 thread-ms of items.
- Eye 1's graph: span 1.72 ms, 3.5 threads busy, 6.4 thread-ms.
- The earlier "1-3 of 5 workers busy" (sections 2 and 6, the P0 sampling) was an artifact of suspending ~128 threads one at a time.
  Use timestamps, not stack sampling, for concurrency.
- Consequences:
  - running both eyes' graphs at once can save at most ~1.3 ms, so parallel eyes is shelved;
  - the graphs are limited by thread count, which makes more workers the cheaper lever.

**The worker count.**
- `79c60` returns clamp(physical cores - 1, 2, 15): 5 on the 6-core/12-thread 5600X.
  - The count comes from `de2c0` -> `de070` (GetLogicalProcessorInformation through GetProcAddress).
  - It is cached in exe .bss `0x2bf8414`.
  - First call: the render context constructor `2c62d0` (CRT static init), then the scheduler init `7b600`.
- `jobs_numThreads` only publishes the result (`3fd7d`). The six SMT threads stay unused.
- Workers are not pinned. Their affinity is `0xfff`, and their ideal processors are spread by Windows.
- The mod DLL loads seconds too late. Only a statically imported DLL's DllMain runs before the CRT init. That is the dinput8 proxy,
  which now:
  - reads `<modhome>\build\job-workers` = N (2..15);
  - checks the exe (the 14 bytes at `79c60` and SizeOfImage);
  - writes N into the cache.
  No knob means no change. The proxy log shows `job-workers knob: N`.
- The launcher reinstalls `<modhome>\build\dinput8.dll` beside the game at every VR launch, so a proxy change must be copied there.

**A/B** (separate alternating runs, user-like sim: 2496x2688, DLSS Quality, spacewarp off; means of the per-run p50):

| workers | runs | engine gap | first eye graph wait | second | game side |
|---|---|---|---|---|---|
| 5 (engine) | 4 | 12.54 ms | 4.15 ms | 2.86 ms | 3.32 ms |
| 8 | 4 | 11.76 ms | 3.55 ms | 2.62 ms | 3.29 ms |
| 10 | 2 | 11.70 ms | 3.51 ms | 2.66 ms | 3.32 ms |

- Every 8-worker run beat every 5-worker run on engine gap and first-graph wait; the ranges do not overlap.
- About -0.8 ms of engine CPU per frame. Most of it is the shadow-heavy first eye graph. The game side does not change.
- 10 workers adds nothing over 8.
- Set to 8 in the user's `build\job-workers` for the next headset test (manifest `artifacts/repair-v1433-test-candidate.json`).
- Not checked in the headset: Virtual Desktop's streamer and the runtime share the same 12 threads. Remove the file if it stutters.

## 10. 2026-11-20 10:05-10:40: what the eye graphs' critical path is made of, and the mod's share of it (v1434)

**The chain** (8-worker pass timeline, `docs/research/ENGINE-PARALLEL-EYES.md` section 10; p50 offsets from the graph's submit):
- GATHER_UMBRA (visibility) ends at 0.86 ms.
- GATHER_DYN_MODELS (x6) ends at 1.06 ms.
- SORT runs 1.08-1.43 ms.
- FORWARD_WORLD runs 1.46-2.39 ms.
- Graph end at 2.58 ms (eye 1).
- FORWARD_WORLD finishes last in 72 % of eye 1's graphs. In eye 0's graphs the shadow items finish last in 61 %.
- Each link is a single job. UMBRA and SORT already fan out internally (sub-jobs, waited on in `32a5ec`). FORWARD_WORLD and
  FORWARD_DYNAMIC are one recording each (`311aa0`, flag 0/1).

**Mod hooks inside the render jobs.** Read-only sampling of the engine's job threads, attribution only:
- "Own" mod time means the first non-system frame above the leaf is mod code. Time in the original D3D11 call that a hook forwards
  belongs to the engine.
- Own mod time is 7.3 % of all job-item samples:
  - FORWARD_*: 24 %, of which `GlyphSwap::lookup` 12.5 % (a shared lock and hash lookup per bound texture);
  - shadow items: 12.5 %, spread over ObjectMotion's classify/written/prepare and the draw hooks (each about 1-2 %; not worth the
    weapon and rig-mask risk);
  - IGGY_FULLSCREEN: 36 % (the mod's own world-UI drawing; off the critical path);
  - every FinishCommandList: ~8 % (observers).

**v1434: GlyphSwap only in UI passes.**
- Glyph views are bound only by UI work. Every swap in three sims happened in IGGY_FULLSCREEN (status `glyph_pass.swaps_by_pass`).
- The hook is now a passthrough in render passes 0-45 and 51. It still runs in the Iggy passes 46-50 and outside any pass.
- Knob `build/glyph-all-passes` = v1433; dev command `glyphpass on|off|abab <ms>`.
- In-run A/B, 22 pairs over 3 sims (8 workers, DLSS Quality): frame work p50 12.23 vs 12.45 ms, median pair -0.18 ms, faster in 12 of
  22. A small gain, not statistically proven.

**What is left.**
- Redundant-state elision was measured with no gain before (v997/v998).
- Splitting FORWARD_WORLD's recording over two deferred contexts would cut ~0.4-0.5 ms per eye from the chain. It needs a reverse of
  `311aa0` (6.4 KB) and of the engine's per-context wrapper (`2bba80` creates one), plus an execute-order hook. That is 1-2 days.
- Running the two eyes' graphs at once is worth more with 8 workers. The 8-worker timeline has 28.6 thread-ms of items over 9 threads,
  so at least ~3.2 ms against ~6 ms serial: about 2 ms possible.
  - The cost is still a second renderer plus the hazards in the research doc: GATHER_CASTERS's sub-jobs go to the global context,
    shadow share makes eye 1 wait on eye 0's light jobs, and shared targets need an execute gate.
  - Several days. A user decision.

**Main-thread trim in progress (not built yet, 11:00): the grab discovery's registry scan (`src/native_grab.hpp`).**
- Each pose tick, every hand scans 1,024 of the engine's 16,383 registry slots, each through its own guarded read. It then runs four
  class tests per entity, each re-reading the class pointer.
- The v1430 call tree put guarded reads at 3.5 % of the main thread, mostly this scan.
- The change:
  - the slice is read in one or two bulk reads;
  - empty slots are skipped;
  - the class pointer is read once per entity;
  - a failed bulk read falls back to the old path.
- Switches: `build/grab-scan-legacy` and dev command `grabscan on|off|abab <ms>`. Status `grab_scan` reports `us_per_call` per mode.
- Lab: `native_grab_compile` checks the bulk path over 40 slices including two registry wraps (same bodies, cursor and lookup count as
  the per-slot path) and the fallback on a failed read. Everything up to an older assertion passed.
- That older assertion (`grab_focus_candidate({old_target},...)`, line 79 at HEAD) also fails at HEAD. `build\night\lab.cmd` printed
  LAB_PASS anyway, so check its output for "Assertion failed".
- Measured at 11:05 (v1436, one sim, `grabscan abab 1000`, DLSS Quality, 8 workers): `focus_body` costs 146.5 us per call with the
  old scan and 134.3 us with the bulk scan (530 and 786 calls; 730 bulk slices, 0 failures). That is -8 %, about 0.025 ms per frame.
  The 6 frame pairs cannot resolve that.
- What the measurement shows: grab focus as a whole is ~0.28 ms of main-thread time per frame (two hands x ~140 us). The registry reads
  are not the bulk of it. The likely bulk is the per-tick revalidation of up to 64 cached bodies within ~3.3 m (`native_rigid_body` per
  body). Revalidating far bodies less often would change grab focus, so it needs a headset feel-check first. Not done.
- The bulk scan stays in (default on, `build/grab-scan-legacy` = old); the test candidate stays v1434.
