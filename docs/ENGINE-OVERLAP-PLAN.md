# Engine overlap ("the big engine change"): plan, 2026-11-20

User decision (2026-11-20 06:20): start the big engine change, i.e. run the game half and the render half of each frame in parallel.
This plan comes first. Every phase ends with a measurement gate, and each later phase needs the user's go-ahead.

Background: `docs/ENGINE-DIG-NOTES.md` sections 2, 7 and 8, and `docs/research/ENGINE-OVERLAP-STEP1.md` / `ENGINE-OVERLAP-STAGE2.md`.

## 1. What is verified (static disassembly and the v1427 probe)

- **Frame order on the main thread:**
  1. game side `459a0` (N+1);
  2. `45650`, which rebuilds the world/view object `142BC58A0` in place;
  3. render side `458c0`:
     - gather `35dc60`: pointers to live game objects;
     - `361bc0`: first the Present of frame N (`3591b0`, call at `361c63`: the mod's XR submit and pacing wait run here), then setup, then
       RenderSingleView `361d60`;
     - a small tail.
- **The render graph** is submitted by `2c8310` (handle `[ctx+0x2158]`) and waited on by `2c8440` (sets `[ctx+0x2160]=1`, then `7ad60`).
  The two calls are back to back at `358202`/`35820a`.
- **The graph finishes without the main thread.** Its body `2c8490` runs on any thread. Workers leave when `2c74c0` reports done. The done
  flag `[ctx+0x2161]` is set by whichever thread retires the last pass job (`2c73c1`, after the pass finish `2c5f60`). Nothing in
  `2bb000`-`2d0000` reads `[ctx+0x2160]` (the rest of the exe is not scanned yet). Deferring the main thread's wait is therefore
  mechanically possible.
- **Immediate context:**
  - pass finishes run `ExecuteCommandList` on the immediate context under the engine spinlock (`36a6f0`/`36b4e0`);
  - the game side never touches the immediate context (probe E0);
  - the render side's own main-thread calls (Present, the mod's XR path) take no lock, so the deferred graph must be complete before the
    next render side starts.
- **Data:** the hand-off is single-buffered. Graph passes read live game objects (transforms, bones, materials) through pointers. Game side
  N+1 writes those same objects.
- **Iggy UI:** tick in the game side (`31c80` -> `2cc90` -> `100ccd0`), draw in the graph's IGGY passes (`31a710`/`31a8e0`). The guard is
  a busy flag `[player+0x3584]`: an overlap is reported as an error and races inside Iggy.

## 2. The honest size of the prize

- The game side is ~17-20 % of the main thread (~2.4 ms). Most of it is the component-update and physics job graphs. Those graphs write
  exactly what the render graph reads.
- Without a copy of that state, game side N+1 may only overlap graph N up to the first write. That point is before the component graph
  (`7a9e0` TtarkSubmitAndWaitGraph) and before the UI update `31c80`.
- Expected gain without a snapshot: about 0.3-0.6 ms per frame. Phase 0 measures it exactly.
- The full ~2.4 ms needs a snapshot of every piece of game state the passes read: transforms, bone palettes, materials, particles, lights,
  decals, UI. That is a rewrite of the engine's render front end from inside a mod: weeks, with high risk of crashes and of wrong motion
  vectors (DLSS ghosting).
- For scale: DLSS Quality instead of DLAA saves ~1.7 ms today (sim, ENGINE-DIG-NOTES section 8).

## 3. Phases

**Phase 0: measure the window. No behaviour change; about 2 hours including sims.**
- Pinned detours (existing `install_pinned_hook`) on `2c8310`, `2c8440`, `459a0`, `7a9e0`, `31c80`, `45650` and `458c0`. Each one stamps
  the steady clock into a per-frame record.
- A read of the done flag, polled where it is cheap, gives the time the graph finished on the workers.
- Output per frame:
  - how long the graph still runs after the main thread starts waiting (the part the main thread could leave);
  - how long the main thread spends from the return of `458c0` to the first write point in the next game side.
- Gate: the overlap that this pair allows (median and p10) must be at least 0.4 ms, or Phase 1 is not worth its risk.

**Phase 1: deferred graph wait with sync before the first write. 1-2 days if Phase 0 passes. Opt-in knob plus dev command
`overlap on|off|abab`.**
- On the main thread, `2c8440` records the graph and returns without waiting.
- The original wait runs at the first sync point reached:
  - entry of `7a9e0` (component graph);
  - `31c80` (UI and Iggy tick);
  - `45650`;
  - `458c0`;
  - level load, ResizeBuffers, session end.
- The work `2c8440` does after its wait (a timing store, then `2bbb30`) moves to that sync point.
- Lock-step mode: sync at the game side's entry. This tests the machinery with zero overlap first.
- Watchdog: a wait over 50 ms, an exception, or a missing done flag turns the feature off for the session.
- Gate: an interleaved A/B (the clock-driven `abab` method) over at least 25 pairs at the user's settings shows a gain. The sims must also
  show no new exception, no Iggy re-entry message, and no visual difference in the captures. Then a headset test.

**Phase 2: snapshot and full overlap. Weeks; only if the user wants it after Phase 1.**
- First profile what the pass jobs read from game memory: page-guard sampling of the objects the gather collects, in a sim.
- Then copy that state at the end of the gather and point the passes at the copy.
- Not planned in detail until Phase 1's numbers are in.

## 4. Rules for every phase

- Off by default; one runtime switch; any anomaly falls back to the unmodified frame.
- Present, `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` and every immediate-context call stay on the main thread, in their current order.
- Sims only when the user is not playing. No compiles during the user's headset tests (they cost 0.5-1.5 ms of frame time).
- Measure with interleaved pairs, never a handful. A 5-pair VRS result (-1.2 ms) was noise at 25 pairs (-0.17 ms).

## 5. Phase 0 result (2026-11-20 08:18): the gate fails, Phase 1 is not worth building

Run: v1432 with `build/overlap-probe`, user-like sim (2496x2688, DLSS Quality: the user's setting since the v1431 test, spacewarp off,
`run_hands`), 1,256 gameplay frames. Frames whose game side ran at least 1 ms; the menus are excluded. All six detours installed, 0
exception records.

| per frame | p10 | p50 | p90 |
|---|---|---|---|
| main thread inside the render graph's wait (`2c8440`, the last of ~1.9 per frame) | 1.18 ms | 2.66 ms | 3.40 ms |
| tail after that wait to the end of the render side | 0.009 ms | 0.013 ms | 0.020 ms |
| end of the render side to the next game side's first write | 0.021 ms | 0.026 ms | 0.033 ms |
| what Phase 1 could save: min(wait, tail + to first write) | 0.031 ms | 0.040 ms | 0.053 ms |
| the game side | 2.47 ms | 3.26 ms | 4.09 ms |

- The first write is the component-update graph (`7a9e0`) in 1,223 of 1,256 frames. The game side starts writing the objects the render
  graph reads 26 us after the render side returns.
- Deferring the render graph's wait to that point gains 0.04 ms. The gate (0.4 ms) fails by a factor of ten, so Phase 1 is not built.
- The overlap that exists is the render graph's wait against the game side: min(2.66, 3.26) ms, about 2.7 ms per frame at p50. It is
  reachable only with Phase 2: a snapshot of every piece of game state the passes read, so the game side can write while the graph reads
  the copy. That is weeks of work and needs the user's explicit go-ahead.
- The probe stays in the build, opt-in (`build/overlap-probe`, status `overlap_probe`, gameplay frames only).

## 6. Phase 0b (2026-11-20 08:25): the two eyes' graphs, and the one larger lever left

Same sim; the probe also hooks the render graph's submit (`2c8310`). 1,137 gameplay frames had two graphs, one per eye.

| per frame | p10 | p50 | p90 |
|---|---|---|---|
| main thread inside the first graph's wait | 1.88 ms | 3.70 ms | 4.81 ms |
| main thread inside the second graph's wait (section 5) | 1.04 ms | 2.58 ms | 3.39 ms |
| the main thread's own work between the first wait and the second submit | 0.08 ms | 0.11 ms | 0.15 ms |

- Both graphs use the same render context object in every frame (1,137 of 1,137). It holds the graph handle `[ctx+0x2158]` and the flags
  `+0x2160`/`+0x2161`, so a second graph cannot be submitted while the first runs.
- With the main thread's own work between the graphs only ~0.1 ms, deferring the first wait gains ~0.1 ms.
- The larger lever: the main thread spends ~6.3 ms per frame waiting on two graphs run one after the other, while only 1-3 of 5 workers
  are busy (section 2 of ENGINE-DIG-NOTES). The graphs are dependency chains, so running the two eyes' graphs at the same time could save
  ~2 ms or more.
- That needs a second render context and probably separate intermediate targets per eye. The eyes may share the engine's depth, G-buffer
  and lighting targets. The mod's shadow share also makes the second eye read the first eye's shadow atlas: GPU order through the
  immediate context.
- A deep engine change like Phase 2, unmeasured beyond these timings, so a user decision.

## 7. Parallel eyes, statically assessed (2026-11-20 09:00; `docs/research/ENGINE-PARALLEL-EYES.md`)

**What the code shows** (key claims re-checked in the disassembly: the view loop calls `351780` at `36208d` with
`this = renderer + 0xD7F0`, and the render context sits at that + `0x5FABD0` = `0x143904940`; the idle-job wait is at `2c8589`):
- There is one renderer and one render context. Each eye rewrites the renderer in place: camera, view pointers, 65 pass-item lists,
  shadow pool.
- Both eyes draw into the same depth, G-buffer and output targets.

**What concurrent eye graphs would need:**
- a second copy of the renderer (about 6 MB, built by the engine's own constructors);
- separate targets, or GPU order held: the second eye's command lists after the first's, and the mod's shadow share after the first
  eye's shadow jobs;
- fewer job instances per graph: an idle graph job seems to park on its worker until its graph is done, so a second graph would queue
  behind it (UNVERIFIED).

**Verdict:**
- Feasible with named fixes. Several days, real crash risk.
- Gain: ceiling ~2.6 ms, realistic guess 1-1.5 ms, unmeasured.
- A staged opt-in experiment (`build/parallel-eyes`, P0 read-only to P3 overlap) is in the research document.
- Like Phase 2's snapshot, it waits for the user's decision.
- P0 run (section 8 of the research document): while the main thread waits on a graph, ~3.2 workers idle in the scheduler, 1.0 parked,
  0.8 working. The headroom for a concurrent second graph is real; the cost is the second renderer and the mod's per-eye hooks.

