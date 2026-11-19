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

- **Thread and job settings:** the engine's threading switches are already all on (`r_schedulerThreadingMode 2`, deferred contexts, `mdnt_multithreaded_*`, `r_useAnimBlendJobs`). `jobs_numThreads` is an output.
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
- **The desktop mirror** costs ~0.7 ms per Present, but the user does not want the desktop window thinned by default ("no cut desktop"); thinning stays opt-in via `build/desktop-mirror-every`.
