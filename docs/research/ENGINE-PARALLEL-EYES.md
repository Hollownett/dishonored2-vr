# Can the two eyes' render graphs run at the same time? (static research, 2026-11-20)

Read-only. Nothing was run, injected or built. Method: `dumpbin /disasm /range` on the Steam exe (RVAs, image base 0x140000000), a raw-byte scan for `call`/`rip` references (scratchpad scripts, not in the repo), `.pdata` for function bounds, plus the repo's own docs. "R" below is the object at renderer+0xD7F0 (the rcx of `351210`/`351780`/`351630`). Labels: [asm] read in the disassembly, [doc] taken from a repo doc, UNVERIFIED = not proven.

## 1. Who calls RenderSingleView twice (Q1)
- `361d60` loops over view records: array `[renderer+0x63A590]`, count `[renderer+0x63A598]&0xFFFFFF`, stride 0xF0 (`361fe7`-`362104`, index in ebp, byte offset in r14). A record is skipped when `[rec+0x20]` or `[rec+0x28]` is 0. [asm]
- Per record it calls `351210(R, [rec+0x20], [rec+0x28], count)` at `36201e`, `351780(R, &local, rec)` at `36208d`, `351630(R, [rec+0x30], [rec+0x38]&0xFFFFFF, ..)` at `3620e9`; after the loop `351210(R,0,0,1)` at `36213c`. [asm] So there is no second call site: the two eyes are two records of one loop. The only argument that differs is `rec` (rcx and the local out struct at rsp+0x60 are the same each time).
- Record fields used: `+0x1C` byte (counted into r9d), `+0x20` view object (fields `+0x2050`, `+0x2060` read at `3517da`-`351827`), `+0x28` camera view object (matrices `+0x3A1C..+0x3A60`, flags `+0x3A67..+0x3A69`, `+0x39C0`, `+0x4590`, `+0x7860`), `+0x30/+0x38` pointer list for `351630`, `+0xC0` float. [asm]
- A second caller exists: the secondary (Timepiece lens) view `366590`, via vcall `[renderer vtbl+0x1C8]` at `361fc2` (before the loop), which calls `351210` at `3668d1` and `351780` at `3668e5` with the same R. [asm; `src/secondary_view_guard.hpp` says the same]
- The render context comes from neither a global array nor a per-view field. In `351780` rdi = `R+0x5FABD0` (`35723a`, `354cf6`), passed to `2c8310`/`2c8440` at `358202`/`35820a`. So ctx = renderer+0x6023C0 = 0x143904940 in the image. [asm]

## 2. State each view writes into shared objects (Q2)
All of it lands in R, in renderer-level singletons, or in game objects. [asm unless noted]
- `351210` (once per view): `[R+0x5FA340]=0`; 18 dwords `[R+0x5FA2D0..0x5FA314]` copied from `view+0x3A1C..0x3A60`; 9 cvar floats into `[R+0x5FA318..0x5FA33C]` (`351219`-`351412`).
- `351780` header: `[R+0x5FA358]=rec.view`, `[R+0x5FA340]=rec.camera` (`3517f5`-`3517f8`), `[R+0x5FA360]`, `[R+0x5FA348]/[+0x5FA350]` from globals `143309B28`/`143309B18`, `[R+0x5FA368..]`, `[R+0x5FA52C/560/568/570/584]` (`351f48`-`352002`).
- 47 list counts zeroed (`and dword [..],0FF000000h`, 33 direct on r12 plus 14 through pointers, R+0x435B08 to R+0x58E278), a 0xFF memset at `[R+0x4DDB10]` (`351acd`), and the "default" shadow pool `[R+0x1C8AC0]` (stride 0x660) reset at `351dec`-`351e66`. The pool is inside R, not global (`docs/ENGINE-OVERLAP-PLAN`/`shadow_share_model.hpp` call it shared because both views use the one R).
- The pass table `[R+0x5FA5A8]`: 65 lists (`34fe46` edi=0x41), items of 0x120 bytes, rebuilt in place every view; about 50 `AddPass` calls `2c7fb0` register them in ctx. Item parameter tables hold raw pointers into R (`352a37`-`352aaf`).
- Renderer-level singletons touched each view: flag bit 18 cleared in `1432F8A38/8AB8/8D48`, `14397A318/A798`; `1432FB510`; 12 floats `1432F9A50..7C` (`351a01`-`351a59`, conditional); `143308CE0`; the counter `1432FC650` (`352069`, ==0x32 once); a 0xC280-byte copy from `[1432F3538]+0x10` into `1432FC980` (`351e92`-`351f09`); `1432FB514`, `1432FC7E8` (`358265`-`358278`); `1432FC74A` (`357db2`); the current output-target entry `renderer+0xD5C8+idx*0x3D0` (accessor `35ec00`, index `[renderer+0xD5D8]`), whose 4-slot depth readback ring (+0x60, index +0x80) is read at `352b81`.
- Game objects: byte `[camera+0x3A69]=1` (`351a61`), and after the wait a rect copied from `[R+0x5FA340]+0x7860` into the caller's out struct (`3583e8`-`3583fc`).
- The second half of `351780`, after the graph wait, runs on the main thread: items reset by `34fbf0(R,..)` (`3583c9`), plus a conditional immediate-context capture when `[R+0x5FA2FB]&4` (`358298`-`3583bb`). `351630` then uses the immediate-context wrapper `[1432D4EF0]` (`351659`-`351746`) and reads `[R+0x5FA340]` again at `35172f`.
- Consequence: a second view built into the same R while the first graph runs overwrites the first graph's items, lists, camera floats and current-view pointer. A concurrent second graph needs its own copy of R, not just its own ctx.

## 3. Render targets (Q3)
- Both eyes use the same targets. Static: `361d60` reads the output-target index `[renderer+0xD5D8]` (`362033`, only read there; I found no write by direct reference) and the same table entry (`362026`-`36202c`) on every iteration; the readback ring above is in that entry. [asm]
- Traces: eye 0 and eye 1 bind the same depth and HDR/G-buffer objects ([doc] `ENGINE-CPU-SECOND-VIEW-RESEARCH.md` section 2.2, R2); the mod copies eye 0's finished image out at eye 1's `351210` (`src/dxgi_probe.cpp:2209`, [doc] `secondary_view_guard.hpp:34`). Nothing selects targets by view index: not proven absent in all of `351780`'s 28 KB, but no use of a view index was seen in the targets path.
- So parallel graphs would both write the same depth, G-buffer and lighting textures. The mod's copy-out only works because eye 1's first command executes after eye 0's last. Separate targets per eye are not available without replacing the output-target entry (UNVERIFIED how many passes cache its pointers).

## 4. Other multi-view paths and context count (Q4)
- `2c62d0` has one caller: `35b30a` inside the R constructor `35a4b0` (call `35b953`), itself called from the renderer constructor `35b410`, which the CRT initializer `1376f70` (call at `1376f7b`) runs on the static global `0x1432FC580`. [asm] So: one renderer, one R, one ctx, no array of contexts.
- Where the engine handles several views (the record loop, the secondary lens view, the 4-view builder `5bf00`), it uses the same ctx one after the other. The ocean fix in `native_ocean_eval.hpp` states the same: a second view "never existed for the engine's design". No split-screen or per-view-context path found.
- ctx teardown/setup entry points: R ctor `35a4b0`, passes init `34fe00` (call `36c619`), ctx init `3511a0` (call `36f867`, which runs `2c7580` at `3511ed`), R destructor `35c3d0`. A single outside user of R's address exists: `84813f` (`lea rcx,[143309D70]; call 34fd30`), role UNVERIFIED.

## 5. What a second render context needs (Q5)
- ctx itself is small and self-contained: `2c62d0` writes ctx fields, three TLS slots (`TlsAlloc` into +0x18E0/+0x1AF0/+0x1D08), and two process globals `1432D52B4/52B8` (same values each time). `2c7580` allocates the job descriptor `[ctx+0x1F48]` (vtable `141b16160`, `[+8]=ctx`, `[+0x10]=N`), initialises N+1 deferred contexts of 0x12C10 bytes (`2c7630`-`2c7653`) through the global device `[1432D4F00]`/immediate context `[1432D4EF8]`, and creates the semaphore `[ctx]`. [asm]
- A graph's job instances do stay with their ctx: `2c8490` uses `[this+8]` for every call (`2c8559`-`2c85d3`), and `2c7ac0` indexes the deferred context from that ctx's own array by slot (`2c7cc6`). Each ctx has its own queues (`+8`, `+0x850`), execute ring (`+0x1098`), counters (`+0x1F3C`) and done flag (`+0x2161`).
- Process-wide pieces: the id stack `14229BDC0/BDC8` (ids 1..N; `2c6530` pushes ids at init; `2c8490` pops at `2c8512` and pushes back at `2c8661`) and the lock `1432D5450`. A second `2c7580` pushes 1..N again, which is harmless while at most N instances are alive (they are threads, N = thread count). An EMPTY pool is not handled: `2c852d` uses an uninitialised stack value. [asm]
- BLOCKER for real parallelism: an instance holds its thread until its graph is done. With nothing runnable it waits on the ctx semaphore (`158450` with a 2 ms timeout, `2c8585`) and loops; it leaves only when `[ctx+0x2161]` is set or `2c74c0` says so (`2c85d3`). `2c74c0` asks `a1bc0` -> `9f5a0` at most every 0xC8 ticks (`2c7510`); that looks like a deadline-based yield (UNVERIFIED). `2c8310` submits `[ctx+0x1F48]+0x10 + 1` instances (`2c8343`, `2c834f`), where N = `7a910()` = scheduler thread count + 1 (`7a929`). So graph A's instances can take all workers and park, which matches "1-3 of 5 busy" while the main thread waits (UNVERIFIED: parked, not sleeping in the scheduler). Graph B's instances then queue until A ends.
- Fix: before each submit, write a smaller instance count into the descriptor (`[ctx+0x1F48]+0x10`, plain data written once at `2c6561`), for example 2 for each graph. Pass slot masks default to all slots (`181a40` returns 0xFFFFFFFF), so fewer instances do not strand a pinned pass (other registration paths: UNVERIFIED).
- To keep the graphs apart the second graph also needs its own R (6 MB: `35a4b0`, then `34fe00`, then `3511a0`). R's extent is at most 0x5FCD78 bytes (the next renderer member sits at renderer+0x60A568, R+0x5FCD78; exact size UNVERIFIED). Whether R holds D3D objects created elsewhere at startup is UNVERIFIED (only `34fe00` and `3511a0` were seen for R).

## 6. GPU order (Q6)
- Execution on the immediate context is done by a single drainer per ctx: `2c72b0` takes `[ctx+0x1F34]`, pops the execute ring `[ctx+0x1098]` and calls `2c5f60`, which takes the engine lock `36a6f0`, calls ExecuteCommandList (vtable `+0x1D0` on `[1432D4EF8]`, `2c6067`) and unlocks (`36b4e0`). Items flagged 0x20 run their callback on the immediate wrapper `[1432D4EF0]` there without that lock (`2c5fbb`-`2c6030`). Order within a ctx follows the ring; across two ctxs it is the order the two drainers get there. The retire step `2c77b0` calls `2c72b0` after every item. [asm]
- Mod hazards: shadow share captures per-light links in eye 0's TLFLIGHTCULL and restores them in eye 1's, and eye 1 reads eye 0's atlas on the GPU ([doc] `shadow_share.hpp`). That is a CPU dependency (eye 0's PREPARE and TLFLIGHTCULL jobs must finish before eye 1's TLFLIGHTCULL starts) and a GPU one (all of eye 0's shadow lists execute before eye 1's lighting).
- Enforcement: hold graph B's drainer until graph A is complete and the main thread has finished `351630` for A. A hook on `2c72b0` (or `2c5f60`) can test A's `[ctx+0x2161]` plus a main-thread flag and return early; B's jobs still record in parallel, only B's executes wait. The retire path calls `2c72b0` again, but A's completion must also kick B once (nothing else would). Immediate-context use by the main thread (`351630`, markers `2bc340` etc.) must not overlap B's drain either, so this gate also serialises that. Whether B's later items need their predecessors executed (not only recorded) before they run: run-dependencies release at retire (`2c7866`/`2c7d70`), not at drain; the execute chain is separate (`2c73f2`-`2c742e`). [asm; not exercised]
- The mod's hooks (RenderPassMap thread-local pass id, eye order by `351210` begin order, shadow-share model, per-eye TAA twins) assume serial views and need a per-graph view index.

## 7. Verdict (Q7)
Feasible with named fixes. Hard: several days and real crash risk; nothing here is proven by running. Order of difficulty:
1. A second R (`35a4b0` + `34fe00` + `3511a0`) and redirecting `351210`/`351780`/`351630` of view 1 to it (rcx only).
2. Instance counts per graph, so both graphs get workers (section 5).
3. The execute gate, or B's executes run late (section 6) and the cross-graph CPU order for shadow share.
4. Serialising renderer-level singletons and main-thread immediate-context code (section 2): builder of B must not run beside A's tail; simplest is a mutex around "builder+tail" sections, keeping only the submitted graphs concurrent.
5. Per-graph view index for the mod's hooks.
Expected gain: UNVERIFIED. Two graphs of 3.7 and 2.6 ms critical path each get about half the workers; the ceiling is ~2.6 ms, a realistic guess 1 to 1.5 ms.

Minimal opt-in experiment (knob `build/parallel-eyes`, off by default, sim only, user not playing):
- P0, read-only: extend `job_wake` to log the caller RVA of each wait and count threads inside `2c8490` while the main thread waits. If most workers sit in `2c8585`, the parked-instance finding holds.
- P1: allocate R2 (0x5FD000 bytes, zeroed, 16-aligned), run `35a4b0`, `34fe00`, `3511a0` on it at the first gameplay frame, leave it idle. Expect no crash and no change.
- P2, serial: for the second record only, hook `351210`/`351780`/`351630` to pass R2. Gate: capture hashes equal to the baseline, shadow share counters unchanged.
- P3, overlap: at the first `2c8440` of a frame, do not wait; wake a helper that runs the second view's three calls on R2 under the section 6 gate and a builder/tail mutex, then wait both. Gate: 25 interleaved A/B pairs show at least 1 ms less main-thread wait, equal capture hashes, no new exception, 50 ms watchdog falls back to serial.

## 8. P0 result (2026-11-20 08:47, sim, read-only stack sampling)

Setup: the sim at the user's settings (2496x2688, DLSS Quality, spacewarp off). 200 rounds of stack walks over every thread, 55 of them
while the main thread was inside a render graph's wait (`2c8461` on its stack). Average threads per sampled wait:

| state | threads |
|---|---|
| parked in the graph's job body (`2c858e`, after the 2 ms wait) | 1.04 |
| running a pass job | 0.80 |
| idle in the scheduler (no graph instance held) | 3.16 |

- The parked-instance blocker of section 5 is mostly refuted: workers are not held by the graph, they wait in the scheduler.
- The graph's low parallelism is its own dependency chain: ~1.8 threads active, the main thread included.
- A second graph submitted beside it would find ~3 idle workers at once, so the headroom for running both eyes' graphs together is
  real.
- Fixes 1 and 3-5 of section 7 still stand: a second R, the execute gate, singletons and the mod's per-eye hooks. P1-P3 are days of
  work and gain nothing until P3.

## 9. P1 groundwork (2026-11-20 09:15, static; not built)

The second renderer's setup chain, read at the call sites:
- `35b410` (the renderer constructor, CRT init at process start): `lea rcx,[rdi+0xD7F0]`, then `call 35a4b0` at `35b953`. `35a4b0(R)` is a
  plain constructor: rcx only, initialises inline vectors (`[R]=R+0x10`, counts at `+8`, ...).
- `36c5f0`: `364670(ebx)`, `34f6c0([1432F3538])`, then `34fe00(R)` at `36c619`, which builds the pass tables. Its two global predecessors
  already ran for the first renderer.
- `36f800(renderer, X)`: `rbx = X` (its second argument), then `3511a0(R, X)` at `36f867`.
  - `3511a0`, unless byte `[1432C7EE9]` is set, calls `2c7580(ctx = R+0x5FABD0, device [1432D4F00], immediate context [1432D4EF8], X)`.
  - That creates the job descriptor `[ctx+0x1F48]` and N+1 deferred contexts.
  - `X` is an engine object handed over while the renderer starts, before the mod loads, so a hook cannot catch it. It would have to be
    recovered from where `2c7580` keeps it in the first context: not traced yet.

**Remaining work:**
- P1: find `X`, allocate R2, run the three setup calls, leave it idle.
- P2: route the second view record to R2.
- P3: the concurrency gates, and the mod's per-eye hooks made safe for two views at once.

Several days, as estimated; none of it gains frame time before P3. Stopped here pending the user's decision.

Later static findings (09:20), for whoever resumes this:
- `X` does not matter: `2c7580` never reads r9 (it is overwritten at `2c75b0`), so `3511a0(R2, 0)` is enough.
- `2c6530` inserts the new instance ids 1..N at the FRONT of the id array (`14229BDC0`), and `2c8490` pops from the BACK. A second
  context's duplicate ids are only reached when more than N instances are alive at once, which N job threads cannot do.
- Hazard for P3: several passes take the render context from the GLOBAL renderer (`373680(&renderer)` = renderer+0x6083C0), not from
  R. Most only read the deferred-mode flag `+0x1F40`, but GATHER_CASTERS (`330c10`) runs its parallel-for "gatherCaster" (`3295c0`)
  on that context's queues. A second graph's GATHER_CASTERS would push jobs into the first graph's context.

## 10. Pass timeline (2026-11-20 09:25, v1433 `build/pass-timeline`): the graphs are wide, parallel eyes does not pay

**Method.**
- Every job item the executor `2c5d50` runs is timestamped on its thread: render passes, gatherCaster sub-jobs and game jobs.
- The main thread's graph submits and waits are recorded too.
- Recording: 524,288 records, 796 gameplay frames.
- Settings: the user-like sim (2496x2688, DLSS Quality, spacewarp off).

| per gameplay frame, p50 | eye 0 graph | eye 1 graph |
|---|---|---|
| span, submit to end of wait | 2.66 ms | 1.72 ms |
| item time, summed over threads | 12.0 ms | 6.4 ms |
| average threads inside an item | 4.3 of 6 | 3.5 of 6 |
| share of the span with 4+ threads busy | 68 % | 50 % |

**What this changes.**
- The stack-sampling numbers of sections 8 and P0c (~1.7 threads busy, 3 workers idle) were an artifact.
  `p0_parked.py`/`p0c_collect.py` suspend and walk ~128 threads one at a time, so one "round" spans far longer than a graph. Each
  worker's sample comes from a different moment, mostly outside the graph.
- The timeline uses exact per-item timestamps and replaces those numbers.
- Eye 0's graph is a wide fan-out. Shadow-caster collection and gathering (parallel-for over all six job threads), 16 shadow items,
  six GATHER_DYN_MODELS, three DEPTHPASS, and FORWARD_WORLD/DYNAMIC overlap almost throughout.
- The main thread runs graph items during its "wait" (`7ad60` helps).
- Running both eye graphs at once: 18.4 thread-ms of items on 6 job threads needs at least 3.06 ms, against 4.38 ms serial.
  - Ceiling with perfect packing: ~1.3 ms.
  - Realistic: below 1 ms.
  - Cost: several days and the P3 hazards above.

**Verdict: parallel eyes is shelved.** The graphs are limited by thread count, so the cheaper lever is more job threads. The engine
makes clamp(physical cores - 1, 2, 15) workers, 5 on the 6-core/12-thread 5600X, and leaves the six SMT threads unused. See
`docs/ENGINE-DIG-NOTES.md` section 9.

