#pragma once
// ctx_probe (docs/research/ENGINE-OVERLAP-STEP1.md section 2; opt-in build/ctx-probe): which thread calls the game's immediate context, and in
// which phase of the engine's main loop (exe:459a0 game side, exe:458c0 render side). Counts only; one relaxed load per hooked call when off.
// Expected in the unmodified game: job x render large; job|other x outside|game all 0 (otherwise a worker uses the context outside the
// render side, which an asynchronous Present would race against). The first 64 such calls are handed to `sink` (dxgi_probe writes them to
// ctx-probe.jsonl as they happen, because the status writer stops in a stall or a crash).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "spacewarp_safe_mid.hpp"

namespace d2vr::ctx_probe {
enum Phase:unsigned {outside,game,render,phases};     // main thread, set by the two phase hooks
enum Who:unsigned {main_thread,job,other,async,whos}; // job = a worker inside a render pass job
enum Kind:unsigned {viewports,targets,targets_uav,draw_indexed,draw_indexed_inst,draw,draw_inst,map,unmap,update,set_cb,set_vs,execute_list,kinds};
inline constexpr const char* phase_names[phases]={"outside","game","render"};
inline constexpr const char* who_names[whos]={"main","job","other","async"};
inline constexpr const char* kind_names[kinds]={"viewports","targets","targets_uav","draw_indexed","draw_indexed_inst","draw","draw_inst","map","unmap","update","set_cb","set_vs","execute_list"};
struct Sample {std::uint64_t tick{},frame{};const void* ret{};DWORD tid{};std::uint8_t who{},phase{},kind{};void* stack[14]{};unsigned short depth{};};  // stack: v1427b
inline constexpr unsigned max_samples=64;
inline std::atomic<bool> on{false};
inline std::atomic<unsigned> phase{outside};
inline std::atomic<DWORD> main_tid{0},async_tid{0};
inline std::atomic<const void*> context{nullptr};
inline std::atomic<std::uint64_t> frame{0};                    // the Present counter, stored each Present by dxgi_probe
inline std::atomic<int (*)() noexcept> pass_of{nullptr};       // &RenderPassMap::current_pass; unset = every worker counts as "other"
inline std::atomic<void (*)(const Sample&) noexcept> sink{nullptr};
inline std::atomic<std::uint64_t> calls[whos][phases][kinds]{};
inline std::atomic<std::uint64_t> leaks{0},game_enters{0},render_enters{0};
inline Sample samples[max_samples];
inline void note(const void* c,Kind k,const void* ret) noexcept {
    safe_mid::note_call(c,ret);  // v1450: another thread's call inside a spacewarp mid-point send (one acquire load when no send runs)
    if(!on.load(std::memory_order_relaxed) || !c || c!=context.load(std::memory_order_relaxed)) return;
    const DWORD t=GetCurrentThreadId();
    const unsigned ph=phase.load(std::memory_order_relaxed);
    Who w=other;
    if(t==main_tid.load(std::memory_order_relaxed)) w=main_thread;
    else if(t==async_tid.load(std::memory_order_relaxed)) w=async;
    else if(const auto p=pass_of.load(std::memory_order_relaxed); p && p()>=0) w=job;
    calls[w][ph<phases?ph:outside][k].fetch_add(1,std::memory_order_relaxed);
    if((w==job || w==other) && ph!=render) {
        const auto n=leaks.fetch_add(1,std::memory_order_relaxed);
        if(n<max_samples) {
            Sample& s=samples[n];
            s.tick=GetTickCount64();s.frame=frame.load(std::memory_order_relaxed);s.ret=ret;s.tid=t;
            s.who=static_cast<std::uint8_t>(w);s.phase=static_cast<std::uint8_t>(ph);s.kind=static_cast<std::uint8_t>(k);
            s.depth=RtlCaptureStackBackTrace(1,14,s.stack,nullptr);  // who calls: the immediate return address can be a mod hook in a chain
            if(const auto f=sink.load(std::memory_order_relaxed)) f(s);
        }
    }
}
} // namespace d2vr::ctx_probe
