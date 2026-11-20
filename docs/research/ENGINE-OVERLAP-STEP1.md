# Engine overlap step 1: probe, exe findings, first experiment (research, 2026-11-20)

Read-only research: nothing built, run or injected. RVAs are exe-relative (Steam build `b671f824…`); `dp:` is `src/dxgi_probe.cpp` at 01:00 (the other agent is editing it, so grep the symbol). Static work used `dumpbin -disasm -range` and a throwaway `.pdata`/xref script outside the repo.

**Bottom line.** Run E0 (section 3) before any stage-2 work. The v1408-v1410 failures were never separated from the engine detours, and both crash sites sit inside the render graph, not after it. The engine's workers do call the immediate context, under a lock that Present does not take.

## 1. What the artifacts and the exe say

1. **The AV pair is not tied to an asynchronous Present.** v1409-deadlock, v1409-hang-stack and v1410 each logged the same two AVs (`artifacts/perf-night/*/native-exception-context*.txt`): `2c6080` and `2e5b5c` (the latter `UNHANDLED`, fatal), on two render-graph workers in the same millisecond (stacks: `2c8490` ← `a0095` ← worker entry `7a236`). The last logged Present precedes them by 11.0 s (v1410), 11.7 s (hang-stack), 22.4 s (deadlock): the first-gameplay stall. "kicks 0" in v1410/hang-stack comes from a status written before that stall, so it does **not** show that no async Present ran; the first kick lands right after the stall. The control `none` had no detour; "detours, never async" was never run.
2. **Workers use the immediate context in the render side by design.** `2c6080` is the pass-job finish `2c5f60` (called from drain loop `2c72e5`+b8) → cold part `2c6047`: lock `36a6f0` on `[1432FC580]` (enable byte `+0x63AC4A`), `ExecuteCommandList` on the global immediate context `[1432D4EF8]` (vtbl `+0x1d0`), unlock `36b4e0`, `Release([job+0x118])`, which was NULL. Only `2bd5a0`, `2c6047`, `2d37c0`, `35ea43` take that lock. The engine's Present wrapper `36b3a0` (swapchain vtbl `+0x40`, else `Flush`) takes none, and the game switches D3D multithread protection off (comment in `targets_uav_hook`; the mod keeps it off: `d3d_protection_wanted=false`, dp:14433). An async Present is therefore unlocked against any worker `ExecuteCommandList`; only phase order protects it today.
3. **`2e5970` is not scheduled.** Its only reference is a `lea` at `2e09f0`+0x2eb (GDraw function-table fill); no call, jmp or data pointer. Crash stack: pass callback `31a97d` → `IggyPlayerDraw` `100c7c0` ("Attempted to re-enter Iggy from callback or second thread") → `1035f40` → `10387d5` → `swf_draw_triangulate.c` `103e230` → `2e5970` (write to `null+0x4c`). That is the IGGY pass job (`2c5d50`, `call [job+0xD8]`), part of the render graph. Iggy itself rejects a second thread inside it, so letting the next UI tick run beside this draw is a second suspect.
4. **`r_schedulerNoBackground`:** live 0; object `32d53d0` (ctor `1366590`; "background workers are disabled, all tasks are considered 'foreground' and required for the frame finish"); value `+0x28` = `32d53f8`. No code reads that slot or holds a pointer to the object, unlike `r_schedulerThreadingMode` (`32d52d0`) and `r_schedulerUseDeferredCtx` (`32d5350`), which have readers (`2c5a60`, `2c6290`, `2c7580`, `2c7ac0`…). `r_useAnimBlendJobs` and `r_oceanUseAsync` also have none, so absence proves nothing: whether it gates tasks is **undetermined**.
5. Main loop `450d0`: `459a0` (+f4), `45650` (+101), `45460`, then `458c0` only if `[engine+0x1e91]==0`.

## 2. Probe for notes step 4.1

**Ids.** Main thread = `gpu_render_thread` (dp:3016; stored every Present, dp:14200). Context = `gpu_game_context` (dp:3072, dp:14198; set while the GPU-trace path runs, as in sims/headset). `RenderPassMap` has `main_thread_id_` (`render_pass_map.hpp:349`, set at `:357`) and a thread-local `current_pass()` (`:661`, ≥0 inside a pass job).

**Hooks that see the immediate context** (all get `c`; count when `c==gpu_game_context`): the seven registry hooks, dp:3175-3575 (`async_present_guard` is in only four; `targets_uav_hook`, `draw_hook`, `draw_vertices_instanced_hook` lack it); ObjectMotion Map/Unmap/Update/VSSetCB/VSSetShader (`object_motion.hpp:404`, bodies `:1742-1797`; the template `I` does not tell the context kind); `TaaDispatchHook::execute_hook` = ExecuteCommandList (`taa_dispatch.hpp:100`, installed with `taa_history_installed`, dp:~13826). Blind spots: copy/clear/dispatch (ContextBarriers 110/68/37 could cover them).

**Inside the game side.** Nothing records it: `459a0` is hooked only by `async-present=game` (`install_game_side_hook`, dp:2588), wraps only the return, and one address per session. Install pinned detours on both `459a0` and `458c0` (prologue arrays dp:2590-2592) that write a phase atomic; `render_side_hook` (dp:2571) is the same detour, so share it.

```cpp
// src/ctx_probe.hpp (new). knob build/ctx-probe
namespace d2vr::ctx_probe {
enum Phase:unsigned{outside,game,render,phases};     // main thread, set by the two phase hooks
enum Who:unsigned{main_thread,job,other,async,whos}; // job = worker inside a render pass job
enum Kind:unsigned{viewports,targets,targets_uav,draw_indexed,draw_indexed_inst,draw,draw_inst,map,unmap,update,set_cb,set_vs,execute_list,kinds};
inline std::atomic<bool> on{false};inline std::atomic<unsigned> phase{outside};
inline std::atomic<DWORD> main_tid{},async_tid{};inline std::atomic<const void*> context{};
inline std::atomic<int(*)() noexcept> pass_of{};     // &RenderPassMap::current_pass
inline std::atomic<std::uint64_t> calls[whos][phases][kinds],leaks{};
struct Sample{std::uint64_t tick,frame;const void* ret;DWORD tid;std::uint8_t who,phase,kind;};inline Sample samples[64];
inline void note(const void* c,Kind k,const void* ret) noexcept { // note(ctx,Kind::map,_ReturnAddress())
    if(!on.load(std::memory_order_relaxed)||c!=context.load(std::memory_order_relaxed)) return;
    const DWORD t=GetCurrentThreadId();const unsigned ph=phase.load(std::memory_order_relaxed);
    const Who w=t==main_tid.load(std::memory_order_relaxed)?main_thread:t==async_tid.load(std::memory_order_relaxed)?async:
        pass_of.load(std::memory_order_relaxed)()>=0?job:other;
    calls[w][ph][k].fetch_add(1,std::memory_order_relaxed);
    if((w==job||w==other)&&ph!=render){const auto n=leaks.fetch_add(1);if(n<64) samples[n]={GetTickCount64(),frame_order::frame.load(),ret,t,(uint8_t)w,(uint8_t)ph,(uint8_t)k};}
}}
```
Set `context`/`main_tid` beside dp:14198-14200 and `async_tid` from a new `AsyncPresent::worker_thread()` getter (`worker_id_` is private, `async_present.hpp:120`). Add one `note(...)` at the top of each listed hook (the ObjectMotion and TaaDispatch ones through a function pointer, like their `wide_*` pointers). Append each of the first 64 `leaks` to `ctx-probe.jsonl` as it happens: the status writer stops in the 11 s stall and in a crash.

**Status** (both writers, beside `"async_present"`, dp:15458 and dp:19341; only with the knob): `"ctx_probe":{"main_tid","async_tid","leaks","game_enters","render_enters","kinds":[13 names],"calls":{"main|job|other|async":{"outside|game|render":[13 counts]}},"samples":[{"tick","frame","tid","who","phase","kind","ret":"exe+0x2e5b5c"}]}`. Expected in the unmodified game: `main.game` all 0 (stack walks), `job.render` large, `job|other` × `outside|game` all 0.

## 3. First experiments

Check `build/` first: it holds `diagnostics` and `spacewarp`, no `alternate-eye`. `AsyncPresent` calls `original_present` directly, bypassing `gated_present`'s `dxgi_gate` (dp:17425), so keep spacewarp off here. Fresh `--name` each; the coordinator owns the launches.

```
E0 python tools/win_perf_sim.py --name ov-e0 --disable alternate-eye --disable spacewarp --knob ctx-probe
E1 python tools/win_perf_sim.py --name ov-e1 --disable alternate-eye --disable spacewarp --knob ctx-probe --knob async-present=render --during "tools/dump_stacks.py {pid} 20 artifacts/perf-night/ov-e1.txt build/d2vr_render_probe_arm_v<N>.dll"
E2 E1 plus --game-cvar r_schedulerNoBackground=1 --command "20 cvarget r_schedulerNoBackground"
```
E0 (detours and counters, no async) separates the hooks from the Present. E2's cvar goes in the config so it is applied at startup (a live `cvar` may come too late for init-time reads). Read each folder's `native-exception-context*.txt` for `0x2c6080` / `0x2e5b5c`.

| result | meaning |
|---|---|
| E0 `leaks`>0, `ret` in `2c6047` / `2e5970` / `103e230` | background immediate-context use confirmed; the `ret` list names the tasks whose entry/exit the async Present must wait for |
| E0 `leaks`=0 | hypothesis refuted: nothing touches the context outside the render side |
| E0 dies with the AV pair | the detours or timing, not the Present: v1408-v1410 conclusions are void |
| E1 dies, E0 clean | the async Present; read the `async` bucket and the stack dump |
| E1 clean, `kicks` ≫ 2 | alternate-eye was the confound; time it with `--disable diagnostics --knob frame-timing` |
| E2 differs from E1 | the cvar gates something |

**Not determined:** why `[job+0x118]` and the GDraw object were null; whether the first kick coincides with the AVs (log kick/done into the `frame_order` ring and flush it from the crash observer); what mod frames `+0x2af6af` and `+0x23fbff` (inside the Iggy stack) are; whether anything reads `r_schedulerNoBackground`.
