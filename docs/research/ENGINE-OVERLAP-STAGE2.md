# Engine overlap stage 2: feasibility and blockers (static research, 2026-11-20)

Read-only. Nothing was run, injected or built. Method: `dumpbin /disasm /range` on the Steam exe (sha256 `b671f824...`), a `.pdata` function map, a direct-call/`rip`-xref index and an IAT map built in the scratchpad (not in the repo). RVAs are exe-relative. Limits: the call index sees only direct `call`/`jmp` (virtual calls are invisible), and `rip` xrefs are approximate (they miss `cmp [rip],imm` forms). Anything not shown by disassembly is marked UNVERIFIED.

## 0. Correction to the frame map (important for variant b)

`458c0` -> `5e100` -> renderer vcall `+0xB8` ([1422FCD70]) lands in a function at `3619d0` (its `.pdata` chunks show up as `361a59`, `361b2b`). Its order, read from the disassembly:
1. `[this+0xC8]=1` (frame-in-progress flag; `3619e0` jumps to the exit if it was already set), `[this+0xCC]++`.
2. `2eb9f0`, then `35dc60(world=&142BC58A0, this+0x63A590)` at `361a7e`: the per-frame gather (section 4).
3. Three loops over a listener array `[this+0xE0]` (vcalls `+0xE0`, `+0xE8`, `+0xF0`), `2ecd00`, `36b480`.
4. `361bc0` at `361b92`, which does, in this order: `3591b0` (**Present**, `361c63`), `35f9a0`, `2e9130`, `36ae40`, `368a60`, `3776c0`, then `361d60` (RenderSingleView, `361cda`), then `369420`, `2ecd00`, `2bcd70`.
5. `[this+0xC8]=0`; `458c0` then runs `2ec950` (if `[engine+0x1E9D]`) and tail-jumps to `998b0` (a bare `ret 0` stub).

So Present (`3591b0` -> `36b3a0`, `359264`) is the FIRST step of `361bc0`, before the render graph, and `35dc60`/`361a59` is a setup step before `361bc0`. Neither is "after the render graph". The engine already presents frame N at the head of frame N+1's render side, after the game side N+1 and the extract `45650`.

What really follows the render graph: `2c8440` ends with `jmp 2bbb30` (a 4-byte store), then `369420` (0x76 bytes, two vcalls), `2ecd00` (0xfb), `2bcd70` (0x29), a timestamp/stat block (`361cff`-`361d50`), and the tail of `458c0`. All are tiny (sizes from `.pdata`; their cost is UNVERIFIED but they are tens of instructions plus the vcalls).

## 1. Job graph: submit and wait are separate

- `9f7f0(sched, job)` is a help-run-until-done wait, not a submit. It takes a per-thread slot from a 128-entry lock-free pool at `sched+0x260` (pop `9f84a`-`9f8d1`, push `a00ed`-`a0159`), then loops: lock the queue critical section (`[sched+0x20]+8`, `EnterCriticalSection` `9fe29`), pick a runnable job from the heap at `sched+0x50` (skip entries whose byte `+0xB` bit 0 is set unless it is the target, `9fd64`-`9fd77`), run it with vcall `[job+0x40]->+0x10` (`9fffc`/`a0092`), and stop when `word [job+4]==0xFFFF` (`9fd15`). With nothing runnable it blocks.
- Blocking site: wrapper `158570` (`WaitForSingleObject` on `[rcx]`, `158580`) at `9fe04` (timed, from the deadline at `[sched+0x38]`) and `9fe17` (INFINITE, `edx=-1`). The object is the slot's event at `slot+0x38`. Waiters are woken by `SetEvent` (`158560`) at `9fb7f` for every slot bit in the finished job's mask `[job+0x18]`. Nothing here is tied to a particular thread: any thread can wait. `TlsGetValue([143E8DE04])` (`9f82d`) only fetches a profiler scope buffer and is null-tolerant (`9f8f7`, `7aa09`).
- Wait entry points: `a2e70` (reached through the scheduler's secondary vtable `1ad5e08`, slot `+0x58`, pointer at `1ad5e60`) tail-jumps to `9f7f0`, or to the sibling loop `a01b0` when `[sched+0x36c]==1`. `a2d40` is the wait-for-all thunk (also `9f7f0`, or `a01b0` when `[+0x384]==1`). UNVERIFIED: which mode is live (`r_schedulerThreadingMode 2` is the known value).
- `7ad60(graphObj, job)`: global scheduler `[1422819C0]` vcall `+0x28`, `B=sched+0x18`, `B->+0x58(job)` = wait (`7ad7f`-`7ad8f`), then `aab3a0` (releases the 24-byte entries of the builder), then tail-jump to the builder's vtable `+0x18` = `aa9ca0` (clears counts). `7ad10(job)` is the same wait + release for a bare handle.
- Submit: `aaacc0(graph, sched+0x18, ...)` builds the dependency set and queues the jobs, returning the join job. `7aab0` is submit only (returns the handle in `rax`, `7ab37`). Game side `aab400` = `aaacc0` + `B->+0x58` wait + `aab3a0` + reset (`aab418`-`aab445`); `7a9e0` TtarkSubmitAndWaitGraph passes flag 1 where `7aab0` passes 0.
- Render side: `351780` calls `2c8310` (SUBMIT: clears `[ctx+0x2160]`/`[+0x2161]`, `aa96f0` builds entries, `7aab0` stores the handle at `[ctx+0x2158]`) and `2c8440` (WAIT: sets `[ctx+0x2160]=1`, `7ad60` on that handle) back to back at `358202` and `35820a`. They are separate functions, but nothing runs between them in the current code. The job body is `2c8490`: a loop of `2c7ac0` (next pass job), `2c5d50` (run pass), `2c77b0` (retire); with nothing ready it waits `158450` (`WaitForSingleObject`, 2 ms timeout, handle at `[ctx+0]`, `2c8585`; pair `158440` = `ReleaseSemaphore`; type of the object UNVERIFIED) and exits through `[ctx+0x2161]` (`2c856e`) or `2c74c0`.
- Role split by thread: `2c8490` calls `IsMainThread` (`166670`, `2c8540`). The main thread keeps looping until `[ctx+0x2161]`; other threads leave when `2c74c0` says done (`2c85cb`-`2c85dc`).
- The render side also launches and joins its own job inside `351780`: `8af1c0` (`3520c3`) submits through `7ab80` (handle `[1439A52A0]`), `8afd80` (`3581fa`) waits for it via `7ad10`. No job crosses a frame boundary that I could find (virtual calls excepted).

Answer to Q1: submit and wait are separable (`2c8310`/`2c8440`, `7aab0`/`7ad10`); the calling thread helps run jobs while it waits (`9f7f0`), and blocks on a per-slot event only when nothing is runnable.

## 2. Iggy re-entry guard

- Guard: `lock cmpxchg [player+0x3584], 1` with expected 0 (helper `10701a0`: `eax=r8d; cmpxchg [rcx],edx`). It is a plain busy flag. No thread id is compared. A nonzero old value reports "Attempted to re-enter Iggy from callback or second thread in %s" (string `1f207f0`) through `102a1d0` -> `102a800` (what that does, fatal or not: UNVERIFIED). Exit does `cmpxchg 1->0` and reports "Internal reentrancy-checking error" (`1f20830`) if it was not 1. The flag is per player, not global.
- Functions with the guard (all in `100c250`-`100ce90`): TickRS `100ccd0`, InitializeAndTickRS `100c970`, DispatchEventRS `100c6e0`, GotoFrameRS `100c8a0`, Draw `100c7c0`, CallMethodRS `100c50f`, SetAntialiasing `100ca10`, SetBitmapFontCaching `100caf0`, ConfigureGCBehavior `100c5c0`, SetTextureFromResource `100c250`, plus `100ce90` and a nested check in `1035f40` (called by Draw via `10363a0`).
- Game side calls the tick: `459a0` -> `45b20` (`45a20`) -> `4b190` (`45b56`) -> `31c80` (`4b284`; UI update, runs queued UI events then loops over the player list `[ui+0xAF40]`, `31d66`/`31dd6`) -> `2cc90` -> `100ccd0` (`2ccfd`). `2cc90` accumulates the frame time into `[ui+0x1778]` and ticks when it reaches 1.0 (`comiss` against the float at `1f697b0` = 1.0, `2cce5`). Established by call-site disassembly of each hop.
- Render side draws: `2dd580` (sets the draw state, tail-jumps to `100c7c0`, `2dd649`) is called from `31a710` (IGGY_3D pass callback, call site `31a7d4`), `31a8e0` (IGGY_FULLSCREEN callback, call site `31a97d`; the callbacks are registered at `350f80`-`351040` through `2c2960`) and a small function `363386` (no caller or pointer found; UNVERIFIED).
- DispatchEvent/GotoFrame/CallMethod wrappers (`2cb20`, `2bf60`, `2c620`...) have direct callers (`33450`, `336a2`, `337ff`, `734f0`) but I found no direct-call path to `459a0` (UI script calls are virtual). Whether other Iggy calls run on the game side: UNVERIFIED.
- Consequence: the guard would fire if the next frame's `2cc90` tick (or any guarded call) runs on the same player while the IGGY pass is inside `100c7c0`. Because it is only a flag, it detects the overlap rather than preventing it (the second caller gets an error and does nothing, while the SWF state race inside the first call is not protected). Draw already runs on arbitrary worker threads (graph job), so the guard has no thread affinity. Fix: a mutex around the tick and around the two IGGY pass callbacks, or defer the tick until the IGGY passes finish.

## 3. Thread-specific state in the render side

Direct-call closure from `361bc0`, `361d60`, `351780`, `3591b0`, `36b3a0`, `35dc60`, `361a59`, `374b60`, `239ea0` (depth 9-12; virtual calls not followed):
- `IsMainThread` = `166670`: `GetCurrentThreadId` (`158150`) compared with `[1432C7ED8]`; true when that is 0 or equal. The id is stored by `1665b0` from WinMain (`e38a0`, call at `e39d1`); `1665b0` goes to `166830` (error) if called from another thread. Callers: `2c8490` (role split above), `36a740` (swap-chain/video mode set; off-thread it calls `166830`, `36a935`), and `2f7e5`, `48ad0` (game region, `48b02`), `2cc740`, `7da0b0`, `8106f0`, `810820`, `811040`, `811170`. None is reachable by direct calls from `361bc0`, `3591b0`, `36b3a0` or `35dc60`.
- `GetCurrentThreadId`: only `11c49c8` (CRT per-thread data, `11c4a1e`), `158150`/`158480` (thread naming, `RaiseException 0x406D1388`), `348574` (window foreground code, `348808`), `e3e00`, `a436e0`, `aa1eb0`. None carries a main-thread assumption except `166670`.
- `TlsGetValue`: 3282 call sites, almost all the profiler pattern (`TlsGetValue([143E8DE04])`, `rdtsc`, store into `[tls+8]`), null-tolerant. `2c62d0` (render context constructor) calls `TlsAlloc` three times (`[ctx+0x18E0]`, `+0x1AF0`, `+0x1D08`); their readers were not traced (UNVERIFIED), but workers already use them.
- `gs:[58h]`: eleven functions (`5feb0` at `5ff8a`, `607f0`, `60c00`, `61510`, `7a100`, `22fd0`, `fa9b0`...), all the compiler's thread-safe-static init guard on TLS index `[143EBE10C]` (`[tls+0x10]` compared with a global epoch). `11ad7c0` uses `gs:[10h]` (stack probe). Not thread-bound.
- Conclusion: the code under the render side is already run from worker threads; the only explicit "this must be the main thread" checks are `IsMainThread` users (`2c8490`, `36a740`) and the game-side ones.

## 4. Per-frame data: single buffered

- `45650` (between game and render sides) calls `5bf00(&142BC58A0, rdx, [engine+0x190]->..., renderer vcall +0xF0, 0)` or `5cfa0(&142BC58A0, [engine+0x1F00])` (`456ce`, `456e8`). `5bf00` rebuilds, in place, the global world/view object at `142BC58A0` from the game's view table (`[rsi+0x48]`, records of 0x39D0 bytes, up to 4 views, `5bfb6`) and writes its iterator at `[+0x21070]` (`5c01c`).
- `35dc60` then walks `[142BC58A0+0x19AE0]` (view list) and collects pointers to live objects from each view record (`[view+0x39F0]`, count `[+0x39F8]`) into per-frame arrays with a dedupe-add `8918f0` (`35e0a0`-`35e11f`).
- `351780` stores the view pointers `[v+0x20]`, `[v+0x28]` into the renderer (`[this+0x5FA358]`, `[+0x5FA340]`, `3517da`-`3517f8`) and reads camera floats straight from the game's view object (`[r13+0x3A9C]` and neighbours, `351881`), so the graph runs on pointers into live game memory.
- I found no `frame & 1` or two-slot copy of this data in `5bf00`, `361bc0`, `3591b0`, `35dc60` (instruction scan for `and/xor/test reg,1` and the frame counters `[engine+0x1ED8/0x1EDC]`, `[this+0xCC]`; `361d60` and `351780` have only flag-style `and x,1` and the pass-slot field `[ctx+0x12AD0]`). The rings I did see are not frame buffers: `[renderer+0xD7C8+4*i]` indexed by `[+0xD7D8]` (frame-time smoothing, `361c84`) and the free-id stack at `[14229BDC0]` (deferred-context ids, `2c84f9`). Absence is not proven for the 400+ functions below `351780`.
- So the engine is not built for game N+1 to write while render N reads. Anything the game side changes (transforms, bones, materials, UI state, entity lifetime) is visible to the render graph; `142BC58A0` itself is rewritten by `45650` each frame.

## 5. Bottom line

### (a) Whole render side `458c0` on a helper thread, next game side on main: NOT FEASIBLE as a drop-in; feasible only with a snapshot/cut and named fixes
Blockers, most serious first:
1. Single-buffered, pointer-based hand-off (section 4): the game side mutates what `35dc60`/`351780`/the pass jobs read. Needs either a cut after the gather (the passes that copy dynamic data) with a snapshot of everything later passes read, or a lock-step barrier before any game-state write. `45650` must additionally wait for the previous render to finish (it rewrites `142BC58A0`), so only `459a0` itself can overlap.
2. Iggy: tick (`2cc90`) versus draw (`31a710`/`31a8e0`): same-player overlap trips the guard and races inside Iggy. Needs a mutex or deferred tick (section 2).
3. Shared job scheduler: the game's own graph waits (`aab400` -> `9f7f0`) may pick a queued render job (`2c8490` instance, unflagged) and then loop until the whole render graph finishes, serializing the overlap. Whether render jobs set the exclusive flag (`a2ed0`/`a2ee0` write `[job+0xB]`) is UNVERIFIED.
4. Main-thread identity: `IsMainThread` is false on the helper (`2c8490` takes the worker role, `36a740` would assert, five game-side callers must keep seeing true on main). Do not change `[1432C7ED8]`.
5. Immediate context: the game side makes none (probe E0), workers use it under the engine spinlock `36a6f0`/`36b4e0` (`1432FC580`), but the render-side main-thread part (`Present`, `OMSet...`, `2bbb30`) takes no lock; on a helper it needs the lock if anything else may touch the context.
6. `458c0` and the main loop share `engine` fields (`[rbx+0x59]`, `0x1ED8`, `0x1EDC`, `0x1E91`, `0x1E9D`) and the busy flag `[renderer+0xC8]`; their game-side readers were not traced (UNVERIFIED).
7. The mod's own Present/XR path (13 % of the main thread per the notes) must stay where OpenXR expects it (Microsoft guidance), so the helper cannot own it unmodified.

### (b) Render tail only on a helper: NOT FEASIBLE as framed, and there is almost nothing to overlap
- Present is not in the tail; it is the first step of `361bc0` (`361c63`). `35dc60`/`361a59` is gather/setup before `361bc0`. The real post-graph tail is `2bbb30`, `369420`, `2ecd00`, `2bcd70` and some timestamps (all small).
- Moving Present(N) to a helper therefore overlaps the rest of render N+1 (`35dc60`, listeners, graph), not the game side, which matches the E1 result (no gain). If tried, the helper must hold the engine spinlock, and the next `361c63` must wait for it.
- A narrower variant that could pay is handing off the render side's blocking work after the graph wait inside the same render side, which stage 1 already approximates; no new structural opening was found.

### Cheapest path if stage 2 is attempted
Cut inside `3619d0` after `35dc60` (and the listener loops), snapshot the view records and pointed-to object state the graph needs, run `361bc0`'s graph part on the helper, mutex the Iggy tick/draw, delay `45650` until the helper signals, and keep a lock-step switch. Not recommended before profiling what the pass jobs read from game objects.
