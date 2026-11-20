# SteamVR: safe mid-frame points for Spacewarp's generated frame (2026-11-21, v1450)

Static analysis of the Steam exe (`dumpbin` disassembly, RVAs, image base 0x140000000), the v1444 freeze stacks, the ctx_probe census
of six sims, and the user's v1445 SteamVR session (read-only: its status and frame-timing CSVs). Nothing was run in the game. Labels:
[asm] read in the disassembly, [data] measured, [model] computed from measurements plus a stated assumption.

## Bottom line

- **Points** (opt-in knob `build/spacewarp-safe-mid`, v1450): the generated frame goes out on the main thread at the first of these
  after its due time, with the engine's own immediate-context lock held:
  - `graph_end`: the return of each render-graph wait (exe `2c8440`);
  - `game_wait`: a job wait of the main thread inside the game side (exe `459a0`);
  - `game_end`: the return of the game side;
  - `render_start`: the entry of the render side (exe `458c0`).
  v1445's Present-time points stay.
- **Why nothing else touches the immediate context there.**
  - Inside the render side, workers use the immediate context only while they drain a render graph (section 3).
  - A graph's last drain finishes before its done flag is set, and the main thread's wait returns only after every job instance of that
    graph has left (section 4).
  - Outside the graphs, the only worker use is the engine's background flush. It takes the engine lock, which the send holds.
- **Where the due time lands** in the user's SteamVR frames: 9.2 ms after the Present hook ends (p10 8.4, p90 9.5). By the headset's
  own eye split, that is about 1.1 ms into eye 1's view for the median frame. So it lands mostly inside the second eye's render graph:
  45-73 % of frames in the second eye's graph, 14 % in the first eye's, and 7-35 % in the game side (section 6).
- **Expected on-time share** [model]: about 96-99 % of generated frames sent before SteamVR's late-slot threshold (about 7.6 ms after
  the due time). v1444's measured 4 % late at a 0.4 ms delay suggests a floor of a few percent. Expect roughly 90-97 % on time, against
  44 % in the user's v1445 session and 62 % in v1443.
- **Not proven by running.** Only the headset can show whether SteamVR presents these frames on time and whether the game stays stable.
  The status counts any other thread's immediate-context call during a send. On the first such call, the mid points switch off for the
  session.
- **The other route** (section 11): bind the XR session to the mod's own D3D11 device, so SteamVR never touches the game's device. It is
  the structural fix and takes SteamVR's per-frame cost off the main thread. Estimated at about 2-4 days for a first stage; recommended
  as the next step after v1450's headset test.

## 1. The problem, measured

The user's v1445 SteamVR session (Quest 2 over Steam Link, Spacewarp Always, 2026-11-20 23:5x to 2026-11-21 00:07):

| | count |
|---|---|
| generated | 4,842 |
| submitted | 2,136 |
| late, total | 2,706 (56 %) |
| `late_wait`: dropped, the next Present came within P/8 of the real frame's slot | 2,073 |
| `late_slot`: SteamVR's wait already gave the real frame's slot | 633 |

- Sent frames went out 5.5 ms after the due time on average.
- The game's work was 18-21 ms per real frame, at the 22.2 ms budget of half rate.
- v1445 sends only at the next Present. That Present comes about 22.6 ms (p50) after the previous one, so it usually lands within an
  eighth of a period of the real frame's slot, and the frame is dropped.

## 2. What froze v1444, re-read

`D:\d2vr-artifacts\hang-v1444\stacks-pid28104.txt`:

- **Main thread.** `GameXr::submit_spacewarp_` -> vrclient xrEndFrame -> d3d11+704fc -> nvwgf2umx -> wait.
- **Worker 18664**, a render-graph job (`7a1e1` -> `a0095` -> `2c85cb` -> `2c7879` -> `2c73a2` -> `2c6086`) -> d3d11+62274 -> d3d11+2024f
  -> `RtlEnterCriticalSection`.
- **nvwgf2umx threads.** Thread 50988 is running inside nvwgf2umx+5c147a with 44.6 s of CPU time, a spin. Thread 36944 waits in
  nvwgf2umx+5c1bf3.

What the exe shows [asm]:

```
2c6047  lea rcx,[1432FC580]; call 36a6f0        ; engine lock (enable byte +0x63AC4A, lock int +0x63AC4C, cmpxchg 0->1)
2c6053  mov rcx,[1432D4EF8] ...                 ; the immediate context
2c6067  call [rax+1D0h]                         ; ExecuteCommandList(list, FALSE)
2c606d  lea rcx,[1432FC580]; call 36b4e0        ; unlock
2c6079  mov rcx,[rbx+118h]; call [rax+10h]      ; ID3D11CommandList::Release   <- return address 2c6086
```

- The worker had finished its ExecuteCommandList. It was releasing the command list inside the drain, and the drain still held the
  drain token.
- The main thread's SteamVR calls had entered the driver's immediate context while that drain was running, from a job wait inside the
  graph.
- Two producers wrote to the immediate context with nothing ordering them. A driver thread spun; the main thread waited on it while
  holding d3d11's lock, which the worker's Release needed. This is the v1421 lock cycle.
- **The rule that follows: while the runtime's calls run, no other thread may be inside a drain or any other immediate-context call.**

## 3. Who uses the immediate context, and when

The immediate context is `[1432D4EF8]` and its engine wrapper is `[1432D4EF0]`. There is one renderer (`0x1432FC580`), one view
renderer R (`+0xD7F0`) and one render context `ctx` (R+0x5FABD0 = `0x143904940`) [asm, `docs/research/ENGINE-PARALLEL-EYES.md`].

| user | thread | when | exclusion |
|---|---|---|---|
| Render-graph drain `2c72b0` -> `2c5f60` -> ExecuteCommandList (`2c6047`) | any graph job instance, the main thread too | only inside a render graph | drain token `[ctx+0x1F34]` + engine lock |
| Graph items flagged 0x20 on the immediate wrapper (`2c5fbb`-`2c6030`) | the drainer | the same | drain token only |
| Background flush `35ea10`: lock2 `+0x63AC50`, FinishCommandList of the global deferred context, ExecuteCommandList under the engine lock | job workers (stack: `a0095` -> `5eeb4` -> `2ea4aa`/`25b12d` -> `362531`) | any time; seen only outside both sides | engine lock |
| CreateUnorderedAccessView (`2bd5a0`, `2d37c0`) | engine code | not measured | engine lock |
| The main thread's own work: Present wrapper `36b3a0`; `351630` and the capture `358298` after each graph wait; draws between the sides | main | render side outside graphs; between the sides | phase order only |

- The engine lock has exactly four users in the exe: `2bd66f`, `2c604e`, `2d3825`, `35ea9b` [asm]. Nothing on the main thread's own
  path takes it.
- The engine therefore keeps the main thread's immediate-context use apart from the workers' by phase: the workers drain only inside a
  graph, and the main thread waits for the graph.

**Census** (ctx_probe, `build/ctx-probe`; sims E0, E0b, E1, E1b, E1c, E1d; about 42,000 game and render sides) [data]:
- Main thread inside the game side: 0 immediate-context calls of any hooked kind.
- Workers inside the game side: 0.
- Workers outside both sides: exactly 6,490 `targets` calls in every run.
  - Every sampled stack runs through the background flush (`35eab7`), so they are under the engine lock.
  - E0b's samples all fall in frames 3470-3477, about 8-14 per frame: one burst, consistent with streaming during a level load.
- Workers inside the render side: `execute_list` and `targets` only, the drains and the mod's own ExecuteCommandList hook chain.
- Blind spots: the census hooks viewports, targets, draws, Map/Unmap, UpdateSubresource, constant buffers, VSSetShader and
  ExecuteCommandList. It does not hook copies, clears, dispatches or queries. Section 5 says how the send guards against that.

## 4. Why the graph is complete when its wait returns [asm]

- **Submit** `2c8310`: clears `[ctx+0x2160]` and the done flag `[ctx+0x2161]`, then queues N+1 job instances of `2c8490`.
- **Drainer** `2c72b0`, while holding the drain token, for each execute-ring item:
  - runs `2c5f60` (ExecuteCommandList under the engine lock, then Release of the list; or a flag-0x20 item);
  - marks the item done (`[item+0x100]=4`);
  - decrements the outstanding count (`lock xadd [ctx+0x1F3C],-1`).
  - At zero it sets the done flag (`2c73c1`) and releases the ctx semaphore. So the last ExecuteCommandList, and its Release, finish
    before the flag. (`2c7278` and `2c7bca` set the flag on the same count for items that need no execute.)
- **Instance loop** `2c8490`:
  - picks an item (`2c7ac0`), runs it and retires it (`2c77b0`, which drains again).
  - With nothing runnable, it checks the done flag (`2c856e`) and leaves if it is set. Otherwise it drains (`2c857d`) and waits up to
    2 ms on the semaphore (`2c8589`).
- **Wait** `2c8440`: sets `[ctx+0x2160]`, then `7ad60` waits for the job group, helping, until every instance has returned. It then
  stores a time and resets a field of the immediate wrapper (`2bbb30`), with no D3D11 call.
- So after `2c8440` returns:
  - no thread is inside that graph's drain, and no graph item runs;
  - the next immediate-context use by a worker needs the next `2c8310`;
  - the engine's own main thread uses the immediate context right there, with no lock (`351630`, `358298`).

## 5. The points and their guards

`src/spacewarp_safe_mid.hpp`, `dxgi_probe.cpp spacewarp_mid_point`.

**Every point:**
- runs on the main thread (the Present thread), never inside `GameXr::present`, a runtime call or a D3D11 hook;
- runs only on the inline path (a runtime that refused the waiting thread: SteamVR, or `spacewarp-force-inline` in the sim);
- needs the frame to be due;
- holds the engine lock for the whole send, so background flushes and UAV creation wait:
  - the spin for the lock is bounded at 2 ms (skipped otherwise; `lock_skips`);
  - without the enable byte the point is refused (`refused_lock_off`);
- sends inside `XrGraphicsScope`, so the mod's hooks ignore SteamVR's D3D11 calls (the v1441 watch issue), and inside
  `ContextState::Scope`, which swaps the whole device context state and restores it;
- opens an exclusive window. Any hooked immediate-context call from another thread during the send counts a violation, with its
  thread and return address, and switches the mid points off for the session.

**Per point:**
- **`graph_end`**: section 4. The same moment the engine's main thread itself takes the immediate context.
- **`game_wait`**: only while the main thread is inside `459a0`, through job_wake's import patch of the exe's WaitForSingleObject. A
  wait whose object has not come by the due time ends at the due time (WaitForMultipleObjects with a high-resolution timer, lab-tested
  in v1444). The send runs, and then the wait goes on.
  - Render-side waits never send: the callback returns "nothing pending" outside the game side.
  - Inside the game side no thread uses the immediate context (census), except the background flush, which the engine lock keeps out.
- **`game_end`**, **`render_start`**: the same phase argument, at the ends of the game side.

**Not chosen:**
- **Mid-graph, with the drain token.** The main thread could hold the drain token and the engine lock during a graph wait, which is
  exactly the drainer's own protection.
  - It would cut about 1-4 ms of delay for frames whose due time falls inside a graph.
  - It could stall the graph up to 2 ms if the last drain was skipped. The instance loop drains again only before its 2 ms semaphore
    wait (`2c857d`).
  - Left as a follow-up for after `graph_end` is headset-tested.
- **A mod-side lock in every hook (candidate 4).** The census shows that the only worker immediate-context use is the drain, so this
  reduces to the drain token above.
- **D3D11 multithread protection.** v1421 hung in a lock cycle with the NVIDIA driver (plan section 16).
- **Render-side job waits.** That is v1444.

## 6. Where the due time lands

The due time is the real frame's xrWaitFrame return plus a period (`GameXr sw_inline_due_ns_`). User's v1445 session, 5,008 gameplay
frames, from the mod's frame-timing CSVs [data]:

| | p10 | p50 | p90 |
|---|---|---|---|
| Present hook start -> real xrWaitFrame return | 0.22 | 1.05 | 8.97 ms |
| Present hook duration | 2.02 | 3.74 | 11.12 ms |
| hook end -> next hook start (the engine's frame outside the hook) | 12.3 | 17.2 | 23.8 ms |
| hook end -> due time | 8.4 | 9.2 | 9.5 ms |
| due time as a share of that engine time | 35 % | 53 % | 73 % |

- The due time falls before the hook ends in 0.5 % of frames, and after the next Present has started in 2.1 %. In the remaining
  97.5 % it falls inside the engine's own frame.

**The headset's eye split** (`frame_split`, the session's last 900 frames, medians) [data]:

| | median |
|---|---|
| Present to Present | 22.40 ms |
| hook | 3.52 ms |
| eye 0's view begin -> eye 1's view begin | 5.43 ms |
| eye 1's view begin -> the next Present | 11.34 ms |

- Eye 1's view therefore begins about 11.0 ms after the hook starts. The due time, at about 12.2 ms, falls about 1.1 ms into eye 1's
  view. In the 8-worker sim (`pe-tl8` pass timeline), eye 1's view is its render graph, then the game side (3.1 ms), then about 1 ms of
  render side before the Present. So for the median frame the due time is inside eye 1's render graph.
- Independent evidence: both v1444 freezes caught the main thread sending from inside a render graph's wait within about 3 s. The due
  time lands inside the graphs often.

**Model** [model]: each live frame's engine time is split by the headset medians above. Eye 1's span is split into graph and game side
in three ways, because the headset's split inside eye 1 was not measured:

| eye 1's graph | due time in eye 1 graph / eye 0 graph / game side | sent at graph_end / game_wait | delay after due p50 / p90 / p99 | before the late threshold |
|---|---|---|---|---|
| 38 % of eye 1 (as the sim) | 58 / 14 / 22 % | 76 / 22 % | 1.4 / 4.0 / 6.2 ms | 99.4 % |
| 60 % (extra time in the render side) | 73 / 14 / 7 % | 91 / 7 % | 3.2 / 6.4 / 8.4 ms | 95.9 % |
| 2.6 ms (extra time in the game side) | 45 / 14 / 35 % | 62 / 35 % | 0.4 / 2.3 / 6.2 ms | 99.4 % |

The v1450 status measures the real distribution: `due_phase`, and per point `sent`, `late_slot` and the `after_ms` histogram.

## 7. Expected share of on-time generated frames

- **SteamVR's late-slot threshold** [model]: in v1445, 42.8 % of frames were dropped at the next Present. Of the rest, 23.1 % came back
  `late_slot`. Ranking those frames by their send delay puts the threshold at about 7.6 ms after the due time (7.2-7.9 ms under
  neighbouring assumptions). Frames whose xrWaitFrame is called earlier keep their slot.
- **The model** above sends 96-99 % of generated frames before that threshold. 0.2 % are dropped, where the due time falls after the
  next Present has started.
- **Floor.** v1444 sent 134 frames at 0.4 ms after the due time on average and still had 4 % late. SteamVR's own pacing (real frames
  that miss, slot phase drift) sets a floor. Expect roughly 90-97 % on time. Before: 44 % (v1445), 62 % (v1443), 99.8 % on VDXR with the
  waiting thread.
- **Main-thread cost:** one send per generated frame, as before, only at another point. But the 43 % of frames v1445 dropped are now
  sent too.
  - SteamVR's xrEndFrame costs the main thread about 1.1 ms p50 (2.3 ms p99; the real frame's `submit_cpu_ns`).
  - So v1450 adds roughly 0.4-0.5 ms per real frame on average, on a main thread already at 17-21 ms of its 22.2 ms.
  - The status reports the cost per point (`call_us_mean`/`_max`) and how long SteamVR's xrWaitFrame blocked there (`wait_us_mean`/`_max`;
    v1444: mean 0.25 ms, max 9.2 ms).
  - Section 11's route takes this cost off the main thread instead.

## 8. Implementation (v1450)

- **`src/spacewarp_safe_mid.hpp`**:
  - the main thread's phase (from the hooks): Present hook, after the hook, graph 0, after graph 0, graph 1, after graph 1, render ->
    game, game side, game -> render, before the hook;
  - the due-phase count;
  - the per-point stats;
  - the exclusive window;
  - the status JSON.
- **`src/dxgi_probe.cpp`**:
  - the four phase detours. They are the existing ctx-probe/overlap-probe detours, each installed once, prologues checked.
  - `spacewarp_mid_point`, `engine_context_try_lock`, and the job_wake idle callbacks;
  - the Present-hook phase scope;
  - the knob, the dev command `safemid off|graph|game|all`, and the status block `spacewarp_safe_mid`.
- **`src/game_xr.hpp`**: the inline frame records its outcome per point (`record_mid_`), including the Present-time points, for
  comparison.
- **`src/ctx_probe.hpp`**: `note()` checks the exclusive window on every hooked immediate-context call.
- **`src/job_wake.hpp`**: `idle_timer_work`, set while idle work runs because the due time came first.
- **Knob** `build/spacewarp-safe-mid`, off when absent. Text: empty or `all` = every mid point; `graph` = `graph_end` only; `game` =
  the game-side points only.
- **Lab**: `tests/spacewarp_safe_mid_check.cpp`, 38 checks covering:
  - due-phase attribution;
  - the game-wait gate, which never fires in render-side phases;
  - masks;
  - the violation window;
  - buckets and JSON;
  - job_wake's timer flag.
  `job_wake_idle_check` still passes.
- **The simulator cannot test the deadlock class**: simxr makes no D3D11 calls. A sim with
  `--knob spacewarp-safe-mid --knob spacewarp-force-inline` checks the hooks, the phases, the timing and the lock, not SteamVR.

## 9. Headset test: what to read

`artifacts/xr-live-status.json`:
- **`spacewarp_safe_mid`**:
  - `violations` must stay 0, `violated` false;
  - `hooks` and `waits` true;
  - `due_phase` gives where the due time fell;
  - `points.graph_end` / `game_wait` / `game_wait_timer` / `game_end` / `render_start`: `sent`, `late_slot`, `after_ms` buckets,
    `wait_us_max` and `call_us_mean`;
  - `lock_skips` and `refused_lock_off` should stay near 0.
- **`pacer.spacewarp`**: `late`, `late_wait` and `late_slot` against v1445 (56 % late), and `work_ms_p50`.
- **Feel**: moving things and turning should look like Virtual Desktop's 90.
- **If the game freezes**: take stacks with `tools/dump_stacks.py <pid> 0 <out> build/d2vr_render_probe_arm_v1450.dll`. The send's
  frames are under `spacewarp_mid_point`. Then remove the knob.

## 10. Secondary: SteamVR's "Use Legacy Reprojection Mode" (`disableAsync`)

- **Indurhra's finding** (github.com/Indurhra/dishonored-2-vr `docs/VERIFICATION-2026-10-26.md`, "Real PSVR2 half-rate diagnosis";
  PSVR2 on SteamVR, 90 Hz, native stereo, no frame generation):
  - with the async compositor path, the game held 45-46 fps, the desktop Present took about 6.4 ms, and the compositor showed two
    presents per frame ("async flag 4");
  - with per-application `disableAsync=true`, about 90 fps and a 0.11 ms Present, in an A/B/A/B test;
  - turning motion smoothing off alone did not change it;
  - they note that disabling async "can change judder behaviour" when a heavy scene misses refresh.
- **The user's v1445 SteamVR session** (Quest 2 over Steam Link) shows none of that: the desktop Present took 0.16 ms (p50, p99 0.41).
  Spacewarp holds the game at 45 real fps by design anyway.
- **Recommendation:** do not set it from the launcher now.
  - It is a per-app SteamVR behaviour change with a stated judder trade-off, and it was found without frame generation.
  - Worth one headset A/B with Spacewarp off (SteamVR > Settings > Video > Per-application video settings > Dishonored 2 > Use Legacy
    Reprojection Mode), and on PSVR2.
  - If it helps there, `FrameGen.SyncSteamVr` could write `disableAsync` beside `motionSmoothingOverride`, with the same backup and
    restore.

## 11. The other route: the XR session on the mod's own D3D11 device (evaluated, not built)

The coordinating session asked for this evaluation on 2026-11-21. The idea: bind the OpenXR session to a second D3D11 device ("D2") on the
same adapter. SteamVR then never touches the game's device ("D1"), and the generated frame can go out from spacewarp's existing waiting
thread exactly in its slot, as on Virtual Desktop. The v1421/v1444 deadlock class disappears by construction, because SteamVR's
immediate-context calls go to D2, which no engine thread uses.

**What the code does today** [code]:
- The session is bound to the game's device (`game_xr.hpp`, `XrGraphicsBindingD3D11KHR binding.device=device`, about line 2762).
- GameXr owns more than a dozen swapchain kinds: two eye chains, two wrist chains, world UI, menu band, HUD window, pointer, hint, dot,
  blank, vignette, settings, and overlay chains made on demand.
- It renders into their images with the game's immediate context: 66 `context_->` uses in `game_xr.hpp`. `spacewarp_pass`,
  `stale_eye_warp`, `vr_menu_imgui` and the eye anti-aliasing, sharpening and upscaling passes write into them too.
- All 66 acquire/wait/release calls go through GameXr's own lease wrappers (v1393, `game_xr.hpp` 582-600, same names as the OpenXR
  functions). That is one place to interpose. Swapchain creation and enumeration are member function pointers as well.
- All of GameXr's OpenXR calls run on the Present thread inside `present()`: events, views (3704), input sync and hand spaces
  (`sample_input`, 3718), and haptics (queued, flushed there). The exceptions are VDXR's waiting thread (`xrWaitFrame` only) and v1450's
  mid points. Moving only the generated frame's calls to the thread adds no concurrency the VDXR path does not already have. v1421's
  "collect the thread's frame at the start of `present()`" rule still holds.

**Stage 1 design (opt-in, for example `build/xr-own-device`):**
- **The device.** Create D2 on the adapter whose LUID `xrGetD3D11GraphicsRequirementsKHR` names, with BGRA support, not single-threaded,
  and with D3D11 multithread protection on. Only mod threads use D2, so the v1421 cycle with the game's own Present and the Steam
  overlay cannot form there. Bind the session to D2.
- **Mirror images.** For every runtime swapchain image (on D2), create a texture on D1 with `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` and
  open it on D2 (`OpenSharedResource1`). Enumeration hands GameXr the D1 mirrors, so every render path stays as it is.
- **Synchronisation, inside the lease wrappers.** One shared `ID3D11Fence` (D3D11.4, `D3D11_FENCE_FLAG_SHARED`, opened on D2 with
  `OpenSharedFence`). At release, the Present thread signals it on D1 after its rendering. D2's context waits on it GPU-side, with no CPU
  stall, copies the mirror into the runtime image, and the runtime's `xrReleaseSwapchainImage` follows.
- **The generated frame.** `spacewarp_thread_ready_()` accepts SteamVR when the session is on D2. The waiting thread then does
  xrWaitFrame, xrBeginFrame and xrEndFrame in the slot, the v1420 design. v1422's rule ("nothing touches the game's device off the render
  thread") still holds, because D2 is not the game's device.
- Everything else stays on the Present thread.

**Effort** [estimate]: about 2-4 days to a sim-checked opt-in candidate:
- D2 creation, binding, and the teardown and recovery paths (resize, session loss, Steam Link fault ride-through): about half a day.
- The mirror layer in the lease wrappers, plus create, enumerate and destroy: about 1 day.
- Fence ordering across the two devices, and the thread path on SteamVR: half a day to a day.
- Sim checks: overlays, the wrist watch, menus, eye captures, a resolution change; the simulated runtime must accept the second device.
  Half a day to a day.
- Headset iterations come on top.

**Stage 2** (later, optional): move the real frame's frame calls and its pacing to the thread as well.
- This takes SteamVR's real-frame xrEndFrame (p50 1.1 ms, p99 2.3 ms) off the main thread.
- It needs a new pacing handshake: the main thread would wait on the thread's xrWaitFrame result instead of calling it. That touches
  the pacing and locomotion feel the user has accepted.
- About 3-5 more days.

**Risks:**
- **Synchronisation.** Shared fences need Windows 10 1703 or later and driver support (fine on the RTX 5070). A missing Signal/Wait
  pair shows torn or stale images, not a hang. Keyed mutexes are the fallback; they block on the CPU.
- **GPU cost and latency.**
  - One extra copy per eye per frame, plus the generated frame's: two images of about 21 MB at 2244 x 2352 RGBA8, roughly 0.1-0.2 ms on
    the RTX 5070.
  - Two device contexts share one GPU (Steam Link's encoder is a third); hardware scheduling usually interleaves them well.
  - The copy runs after the game's GPU work, as SteamVR's own copy does today. The expected added latency is under 0.5 ms; measure it
    with the existing GPU timers.
- **Memory.** The mirrors double the swapchain memory: about 127 MB for the eye chains (3 images x 2 eyes, RGBA8), little for the
  overlays. Fine on 12 GB.
- **The mod's D3D11 hooks.** D2's contexts run the same d3d11.dll code, so D2's copies and SteamVR's calls on D2 pass through the
  probe's hooks.
  - The hooks compare against `gpu_game_context` and skip other contexts, but each must be rechecked.
  - Multithread protection switches D2's methods to d3d11's thread-safe layer. That was v1420's registry-poisoning lesson: create D2 and
    turn protection on before the probe installs its hooks, or prove the hooks never bind D2's methods.
- **Swapchain image ownership.**
  - The runtime image must be acquired and waited before D2's copy writes it, and released only after the copy is queued.
  - The wrist chains keep an image acquired across frames (`self_managed_chain_`), so their copy must run at their release.
  - Mirror formats and bind flags must match what GameXr creates views with: sRGB or UNORM, RTV, SRV, and UAV for the upscalers.
- **OpenXR threading.** Unchanged in stage 1, as above. A crash in VD's runtime under concurrent haptics (v1421) is why haptics stay
  queued.
- **SteamVR with a device that never presents a window.** Normal for VR applications, but untested here, over Steam Link included.

**Recommendation:**
1. Test v1450's mid-frame points now. They are ready, small and opt-in, and fix most of the lateness.
2. Build stage 1 of this route next, behind its own off-by-default knob, as the structural fix. It removes the deadlock class by
   construction, sends the generated frame in its slot as on Virtual Desktop, and takes the generated frame's SteamVR call off the main
   thread, about 1 ms per real frame, which v1450 puts back on it.
3. Do stage 2 only if the real frame's 1.1 ms SteamVR submit still holds the CPU-bound main thread back after stage 1.

## 12. v1450 in the headset, and v1451 (2026-11-21)

**User** (SteamVR over Steam Link, Quest 2, Spacewarp Always and Auto, `build/spacewarp-safe-mid` on): "better but not very stable";
they want the stability Virtual Desktop has.

**The mod** (session pid 47384, 7,267 generated frames) [data]:
- **Late: 2,269 (31 %), against 56 % in v1445.**
  - Drops at the next Present (`late_wait`) fell from 2,073 to 37.
  - Late slots rose from 633 to 2,232.
  - Violations 0, `lock_skips` 0. No freeze.
- **By point:**

| point | sent | late slot | send delay after the due time |
|---|---|---|---|
| game side's job waits (`game_wait` + timer) | 2,736 | 62 (2.2 %) | about 0.25 ms mean |
| `game_end` + `render_start` + Present-time points | 371 | 0 | |
| `graph_end` | 1,891 | 2,170 (53 %) | 7.0 ms mean; 1,682 at 8 ms or more |

- **Due times:** 2,042 fell inside a Present hook. In v1445 that was 23 of 5,008.
- **The cause: a feedback loop.**
  1. A late slot hands its wait to the next real frame. The next due time was taken from the clock time of that late wait, which came
     well after SteamVR's slot opened.
  2. That due time then fell before the next generated frame even existed.
  3. The frame's first point was the first graph's end, about 7 ms later. So it was late too, and handed on again.
  - In the frame-timing CSV (the session's first 1,066 gameplay frames) the handed slots come in runs of up to 86 frames, about 2 s at
    45 fps. In v1445 the longest run was 5.
- **Real frames:** steadier. Three-period slips fell from 14.6 % to 5.8 %. The pacer's sleep now runs, because the generated frame no
  longer goes out at the hook's start.
- **Cost of a send on the main thread** (`call_us_mean`): about 1.0-1.4 ms at the game-side points, 2.0 ms at `graph_end`. xrWaitFrame
  blocks for 0.55 ms on average there, at most 9.8 ms.

**Steam Link** (`D:\Steam\logs\vrlink_aftm_*.csv` and `driver_vrlink.txt`, headset clock) [data]:
- **Decode on the Quest 2:**
  - 12-16 ms per frame (median) during gameplay, over the 11.1 ms budget on 62-86 % of frames;
  - 4.9 ms in the v1445 session at the same stream settings;
  - receive -> decode start is 0.1 ms, so this is decoding, not queueing.
- **What the headset got:** 62-75 fps.
- **End-to-end latency:** about 31-33 ms, against 20 ms.
- **Stream settings:**
  - 2748 x 2880 per eye, 10-bit;
  - automatic bandwidth up to 350 Mbit/s (the driver's default target is 200);
  - 21-42 "bad link" stalls per minute, up to 0.4 s each (10-15 per minute in the v1445 session).
- **SteamVR's global resolution** is set by hand to 150 % (`supersampleScale` 1.5).
- **Conclusion.** Steam Link's stream, not the mod's timing, caps this session below 90. The mod sent more unique frames than v1445, the
  stream got heavier, and the decoder and the Wi-Fi fell behind. Steam Link's own settings offer:
  - "Target Bandwidth" (automatic or a fixed Mbps);
  - "Use 10 bit depth encoding";
  - "Encoded Video Size".

**v1451 (mod):**
- **The grid rule.** After a late slot at an inline point, the handed wait for the next due time is this frame's due time plus the
  step to the handed slot (its display time minus this frame's target): SteamVR's slot grid (`safe_mid::grid_handoff`; status
  `grid_handoffs`). Outside half a period to four periods, the clock time stays.
- **`hook_end` point.** A frame that comes due inside the Present hook goes out at the hook's exit, not at the first graph's end.
- **Lab:** `spacewarp_safe_mid_check`, 43 checks. Not headset-tested.
