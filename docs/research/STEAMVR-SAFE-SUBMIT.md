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
- **Built in v1453** (section 14), opt-in `build/xr-own-device`, after v1450-v1452's headset sessions plateaued at about three quarters
  of generated frames on time. Lab-checked on the RTX 5070. In the simulator, all 2,049 generated frames went out from the waiting
  thread with 0 late; not yet run in the headset.

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

## 13. v1451 in the headset, and v1452 (2026-11-21 03:15)

**Session** (pid 31808): Spacewarp Auto. Steam Link set by the user to "Target Bandwidth" 145 Mbit/s and an encoded width of 960 px.

**The mod** [data]:
- **Late: 3,632 of 13,720 (26 %).** v1450: 31 %; v1445: 56 %.
  - Late slots 3,583, `late_wait` 49.
  - 3,556 grid handoffs.
- **The chains are gone.** The frame-timing CSV (the first 926 gameplay frames) has no two handed slots in a row; v1450 had runs of 86.
- **Real frames:** 93 % at two periods, 7 % at three.
- **Auto** locked 14 times and unlocked 13. 7 % of frames ran at full rate without generated frames, which this PC cannot hold on
  SteamVR.

| point | sent | late slot | delay after the due time | xrWaitFrame |
|---|---|---|---|---|
| `graph_end` | 2,683 | 1,687 (39 %) | 1.8 ms mean (v1450: 7.0) | 0.6 ms mean |
| `game_wait` + timer | 4,208 | 731 (15 %) | about 0.2 ms | 0.2-0.3 ms |
| `game_end` + `render_start` | 191 | 16 (8 %) | | |
| Present-time points | 3,001 | 201 (6 %) | | |
| `hook_end` (new) | 5 | 948 (99.5 %) | 5.4 ms | **6.6 ms mean** |

- **`hook_end` was a mistake.** Those frames were late before they were generated, and each send blocked the game for 6.6 ms inside
  the Present hook. v1452 removes the point; the grid rule stays.
- **SteamVR's display times** lie on the 90 Hz grid to within 0.1 ms. So a late slot is SteamVR moving the frame to a later vsync,
  not a jittery estimate.

**Steam Link** [data]:
- decode 9.6-9.8 ms median, over the 11.1 ms budget on 27-29 % of frames (v1450 session: 12-16 ms, 62-86 %);
- the headset still received only 65-68 fps;
- latency 30-35 ms.

**Where this leaves the inline route.** About three quarters of generated frames on time, against Virtual Desktop's 99.8 % with
spacewarp's waiting thread. Sends from the engine's phase points cannot follow SteamVR's slot rules more closely. Virtual Desktop's
behaviour on SteamVR needs section 11's route: the session on the mod's own device, with the waiting thread sending each generated
frame as SteamVR opens its slot.

## 14. v1453: the session on the mod's own device (opt-in `build/xr-own-device`, 2026-11-21)

**User** (2026-11-21): build the bigger fix, and make SteamVR's spacewarp work for most players, not only on this PC.

**Knob** `build/xr-own-device`, read once when the probe creates a GameXr:
- empty or `on`: every runtime except Virtual Desktop, whose runtime never touches the game's device;
- `all`: every runtime, Virtual Desktop and the simulator included;
- `off`, or no file: off, as before. The default stays off until a headset test.

**The device** (`src/xr_own_device.hpp`, `d2vr::XrOwnDevice`):
- D2 is created on the game device's adapter, so `xrGetD3D11GraphicsRequirementsKHR`'s LUID check holds. It has the game's feature
  level, BGRA support and `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` (its work is a few copies and the runtime's
  calls; the v1421 hang ran through the driver's threaded optimisation). D3D11 multithread protection is on.
- **Two** shared `ID3D11Fence`s, where section 11 had one:
  - A, signalled on D1: the mirror's content is complete;
  - B, signalled on D2: the copy has read the mirror. Without B, D1 could redraw a mirror while its last copy was still queued on D2.
- Creation ends with a round trip: a 16 x 16 mirror, copied between the fences. Any failure keeps nothing, and the status says why.

**Mirrors:**
- For each runtime image, a texture on D1 with that image's description (typeless formats included) and `D3D11_RESOURCE_MISC_SHARED`,
  opened on D2.
- It uses a legacy shared handle (`GetSharedHandle`), not the NT handle of section 11: one call, no security attributes, and any
  single-mip 2D format on the lab's hardware. Mipmapped, multisampled and array images are refused; every GameXr swapchain is
  single-mip, single-sample and one layer.
- `enumerate_images_()` replaces all nine `xrEnumerateSwapchainImages` sites and hands GameXr the mirrors. So every render path (eyes,
  spacewarp, overlays, wrist watch, menus) keeps drawing on D1, on the render thread, unchanged.

**Order, inside the v1393 lease wrappers** (Present thread):
- **acquire:** the index joins the chain's queue. OpenXR acquires in order, waits for the oldest acquired image and releases the oldest
  waited one.
- **wait:** D1 waits on the GPU for B, up to that image's last copy. With three images per chain, that copy finished two or three
  frames ago.
- **release:**
  1. D1 signals A and flushes;
  2. D2 waits for A on the GPU, copies the mirror into the runtime's image, signals B and flushes;
  3. the runtime's `xrReleaseSwapchainImage` follows.
  - The CPU never waits.
- `settle_lease_` (abandoned frames) and the destroy paths keep the mirror queue in step. `close()` drops the device after
  `xrDestroySession`, never before it.

**The generated frame:**
- With the own device, `spacewarp_thread_ready_()` accepts any runtime. The waiting thread waits for the generated frame's slot and
  begins and ends that frame itself, at once: v1420's Virtual Desktop design, now on SteamVR (status `thread_ended`).
- A wait that returns the real frame's slot is handed to the real frame, as before.
- The thread's calls never overlap `present()`'s OpenXR calls: `present()` collects the thread's frame before its first call. D2 is
  used by one thread at a time; multithread protection covers the runtime's own threads.
- The real frame, pacing, locomotion and turning are unchanged.

**Fallback, for PCs where this fails:**
- If `xrCreateSession` refuses D2, the same construction binds D1 (`why`: "runtime refused it (code)").
- If D2 cannot be made, for example without shared fences (Windows 10 1703 or later and driver support), D1 is bound (`why`: "failed:
  ...").
- If an image cannot be mirrored, that session fails and recovers, and every later session in that game run binds D1 (`blocked`
  names the image and the failing call).
- If two own-device sessions in a row close before 900 real frames (10 s at 90 Hz), the own device stays off for the rest of the game's
  run (`blocked`). A session that ends 900 frames clears the strikes.

**Status** `spacewarp.own_device` (both status files): `on`, `why`, `copies`, `failures`, `chains`, `thread_ended`, `frames`,
`strikes`, `blocked`.

**Checks:**
- Build: 16 warnings, the baseline.
- Lab `xr_own_device_check` (21 checks, hardware, the RTX 5070):
  - creation and multithread protection;
  - typeless mirrors on D1, with the runtime image's description;
  - the game device's colours reaching each runtime image after the fences;
  - re-enumeration reusing the mirrors;
  - two images held at once;
  - a release without a wait copying nothing;
  - unknown chains ignored;
  - mipmapped and array images refused, with the refusal named;
  - 200 frames beside another thread on D2;
  - reset.
- `spacewarp_safe_mid_check` and `job_wake_idle_check` pass.
- **Simulator** (2026-11-21 13:00, run `v1453-own-device`, scenario `head_turn_walk`, knobs `xr-own-device` empty and `spacewarp`
  `always`, the user's diagnostics on) [data]. `simulated_xr.hpp` now accepts a second device on the same adapter.
  - Own device: `on`, `why` "on", 10 chains mirrored, 16,059 copies, 0 failures, 4,016 real frames ended on it, 0 strikes.
  - Spacewarp: 2,050 generated, 2,049 submitted, every one begun and ended by the waiting thread (`thread_ended` 2,050). 0 late,
    0 late slots, 0 failures, 0 inline frames.
  - Real frames: 2,049 two periods apart, each with one generated frame between; 77 one period apart (no generated frame: start and
    loads).
  - 0 session recoveries. The 3 `recoverable_faults` are the usual start and load notes (empty frames before the eyes had sources);
    earlier sims had 1-3.
  - Eye captures from the mirrors: picture content (mean RGB about 56/46/37, no black pixels). The simulator does not read back what
    it receives, so pixel delivery into the runtime's images rests on the lab check above.
  - The simulator's timing is not SteamVR's. Only the headset shows whether SteamVR presents the generated frames on time.
- **Not run yet:** the headset.

**v1454** (merge `428df96`) = v1453 + v1460, the body and legs rules from the body/legs session (plan section 30), so the user's
headset test has both. All 10 lab tests of the two branches pass. Simulator (`v1454-combined`, the same scenario and knobs) [data]:
- own device on, 20,814 copies, 0 failures;
- 2,984 of 2,985 generated frames sent by the waiting thread, 0 late, 0 late slots, 0 recoveries;
- the body rules active: `body2` on, the torso and leg rules in about 5,970 eye images, the uploaded legs the shown image's own in all
  3,060 frames.
- Installed in the main checkout at the user's go-ahead (2026-11-21 13:30): launcher `47990fa3...`; v1453's is kept as
  `DishonoredVRLauncher.before-v1454.exe`. `build\xr-own-device` is on.
- Spacewarp itself must be on in the launcher (Auto or Always): `build\spacewarp` was absent, which is Off.

**v1455** (merge `e1427b8`) = v1454 + v1461 (`0d71f18`). v1461 is the body/legs session's fix of three gaps its v1460 tint sim found:
the boots' toe caps, the upper chest, and scenery beside a leaning player. All 10 lab tests pass. That session's class-tint sim
(`sw1455-tint`) [data]:
- own device on, 20,783 copies, 0 failures;
- 2,938 of 2,939 generated frames sent by the waiting thread, 0 late, 0 recoveries;
- the boots, chest and upper sleeves in their classes.
- Installed in the main checkout (2026-11-21 13:45) in place of v1454, as the user had asked for a last body/legs fix to be included:
  launcher `bae8bdc8...`; v1454's is kept as `DishonoredVRLauncher.before-v1455.exe`.

**What to read after a SteamVR session with the knob on:**
- `own_device.on` true, `why` "on", `blocked` empty, `failures` 0.
- `copies`: two per real frame (the eyes), one per overlay redraw, and two per generated frame.
- spacewarp `thread_ended` close to `generated`, against section 13's 26 % late:
  - `late` and `late_slot` small;
  - `inline_frames` near 0, since the inline path is not used.
- Frame-timing CSV: one generated frame per slot between real frames.
- The GPU timers for the copies' cost: expected under 0.3 ms per frame on the RTX 5070.

**Risks left:**
- SteamVR with a session device that never presents a window is normal for VR applications, but untested here, over Steam Link included.
- Older drivers: shared fences and typeless sharing. Both fail at creation or at the first mirror, and the fallback binds D1.
- Memory: the mirrors double the swapchain memory (about 130 MB for the eye chains at the user's size).

## 15. v1455 in the headset, and v1456 (2026-11-21 14:10)

**User** (SteamVR over Steam Link, Quest 2, Spacewarp Auto): "Steam VR fix is not working at all": gameplay did not show in the headset.

**Session** (pid 41948) [data]. Sources: the mod's status and frame-timing CSV, SteamVR's `xrclient_Dishonored2.txt`, and
`vrcompositor.txt`.
- **The own device itself worked.** The session was on D2, with 19,346 copies and 0 failures. The menus showed in the headset:
  about 4,600 presents not timed out, about the menu phase at 90 Hz.
- **Gameplay broke**, 3.4 s after its first projection layer:
  1. 14:09:21.9: a new swapchain (the HUD window). The waiting thread's frame calls then took about 300 ms: present 2074 waited
     318 ms for the thread.
  2. Present 2074's xrWaitFrame returned at once, with a display time already in the past.
  3. A 1.2 s gap in the engine followed (present 2076), probably the level-start autosave.
  4. During it, at 14:09:23.228, SteamVR logged "Application submitted two left scene textures for a single frame!", then
     `ComposeLayerProjection: failed to submit view N: VRCompositorError_AlreadySubmitted`, 5,186 times.
  5. From present 2077 on, every xrWaitFrame returned the last display time plus 2 ns: 1,295 real frames until the game closed.
     xrWaitFrame did not block at first and later blocked 2-10 ms.
- **SteamVR's verdict:** of the app's 9,129 presents, 4,525 were in "Timed out" (1,764 time-outs). That is SteamVR's own scene instead
  of the game, as in the user's report.
- **Earlier sessions:** v1445, v1450 and v1451 had no "two scene textures" warning and real frames always 2 vsyncs apart. Their
  time-out share was 11-17 %.
- **The difference: v1453's waiting thread began and ended the generated frame itself.** SteamVR got frame calls from two threads;
  after a stall its frame bookkeeping never recovered.
- **Side finding: SteamVR Motion Smoothing was on.** SteamVR was started at 14:07:52 and Spacewarp switched on in the launcher at
  14:08:10. The launcher changes the per-app override only while SteamVR is closed: `steam.app.403640` had no
  `motionSmoothingOverride`. SteamVR reported 22.2 ms periods in the menus.

**v1456 (commit `37dbb2b`):**
- **One thread for SteamVR.** The waiting thread never ends frames again; Virtual Desktop keeps v1420's design. SteamVR uses the
  inline path, so all frame calls are on the render thread, as in v1445-v1452.
- **Sends from any wait, on the own device.** `safe_mid::own_device` is set with the session. Then the mid points need neither the
  engine lock nor the exclusive window. Every main-thread wait outside the Present hook sends at the due time; render graphs are
  included (new points `render_wait` and `render_wait_timer`).
  - So a send never touches the game's device: the generated images are released in `present()`, and the send skips the context-state
    swap, which used `SwapDeviceContextState` on the game's immediate context.
  - `build/xr-own-device` switches the mid points on.
- **Watchdog**, on any runtime:
  - a real frame whose display time is under half a period after the last one stops generated frames;
  - 30 such frames over 1.5 s recreate the session, at most 3 times per game run;
  - status `own_device.timing_stalls`, `timing_stall_frames` and `timing_recoveries`.
- **Lab:** all 10 pass; `spacewarp_safe_mid_check` covers the own-device gates.
- **Simulator** (`v1456-inline-own`: `spacewarp-force-inline`, `spacewarp always` from 1 s into gameplay, `xr-own-device`) [data]:
  - own device on, 19,918 copies, 0 failures;
  - 1,833 of 1,855 generated frames sent, 0 late slots, 0 failures. The 22 drops came too near the real frame's slot (the v1440
    margin), all at `render_wait`;
  - 76 sends from render-side waits (52 `render_wait`, 24 `render_wait_timer`), most within 0.25 ms of the due time; 0 watchdog stalls;
  - StartProbe returned 1 at shutdown ("Native interaction ray/door hooks did not restore or drain"). That is the same intermittent
    teardown failure as the v1423 sim (`sw-v1423-run`), unrelated to spacewarp; the earlier runs returned 0 or 4.
- **Installed** in the main checkout at the user's go-ahead (14:40): launcher `67ed89ed...`; v1455's is kept as
  `DishonoredVRLauncher.before-v1456.exe`.

**For the next headset session:**
- Close SteamVR before Launch VR, so the launcher switches Motion Smoothing off for the game; or set it Off in SteamVR's per-application
  video settings.
- Read:
  - `spacewarp_safe_mid.points` (`render_wait_timer` and `game_wait_timer` share, `late_slot` per point), and the late share against
    section 13's 26 %;
  - `own_device.timing_stalls` (should be 0);
  - SteamVR's `xrclient_Dishonored2.txt`: no "two scene textures" warning;
  - `vrcompositor.txt`: the time-out share.
