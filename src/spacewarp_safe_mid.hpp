#pragma once
// v1450 (SteamVR spacewarp; docs/research/STEAMVR-SAFE-SUBMIT.md; knob build/spacewarp-safe-mid, OFF by default): the main thread's
// mid-frame points where the generated frame may go out to a runtime whose frame calls use the game's D3D11 immediate context (SteamVR's
// xrWaitFrame / xrBeginFrame / xrEndFrame). v1444 sent it from ANY main-thread job wait and froze twice: inside a render graph the
// engine's workers drain command lists onto the immediate context (exe 2c72b0 -> 2c5f60). v1445 sends it only at the next Present, which
// comes after most generated slots on SteamVR (the user's v1445 session: 2,706 of 4,842 generated frames late).
//
// The points (all on the main thread, outside GameXr::present, with the engine's immediate-context lock held, see the doc for the proof):
//   graph_end    the return of a render-graph wait (exe 2c8440). The graph's last drain (ExecuteCommandList + Release of its list) ran
//                before the drainer set the done flag [ctx+0x2161] (2c73c1); the graph's job instances leave only after that flag, and
//                the wait (7ad60) returns only after every instance left. The engine's own main thread uses the immediate context right
//                here, without any lock (351630 through the immediate wrapper [1432D4EF0]).
//   game_wait    a main-thread job wait inside the game side (exe 459a0), through the job_wake idle mechanism (the wait ends at the due
//                time if its object has not come first). No worker uses the immediate context in the game side (ctx_probe census: 0
//                calls of any hooked kind over ~40,000 sim frames); the only use outside both sides is the engine's background flush
//                (35ea10), which takes the engine lock.
//   game_end     the return of the game side.
//   render_start the entry of the render side (exe 458c0), before its first engine call.
//   hook_end     v1451: the exit of the Present hook (the main thread inside the engine's Present call, no graph in flight), for a frame
//                whose due time passed while the hook generated it; otherwise its first point would be the first graph's end.
// Present-time points (v1445) stay as they were: present_start (the Present hook's start), present_fence (present()'s GPU fence wait),
// present (present()'s own late path).
//
// The engine lock (exe 36a6f0 / 36b4e0 on 0x1432FC580: enable byte +0x63AC4A, lock int +0x63AC4C, 0 -> 1) is the engine's own protocol
// for the workers' immediate-context calls: the render graph's ExecuteCommandList (2c6047), the background flush (35ea10) and two
// CreateUnorderedAccessView calls (2bd5a0, 2d37c0) take it; nothing else in the exe does. Holding it keeps those out while the runtime runs.
// Without the enable byte the mid points are refused (the engine would not serialise its own background flush either).
//
// While a mid point's runtime calls run, an immediate-context call from any other thread through the mod's hooks (ctx_probe::note's call
// kinds) is a violation: it is counted with its thread and return address, and the mid points switch off for the session.
//
// Measurement (always, with the knob): where the due time fell in the engine's loop (`due_phase`, one count per generated frame), and per
// point the frames sent, the late slots, how long after the due time the frame went out, how long the runtime's xrWaitFrame took and how
// long the whole send took (the main thread's cost).
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace d2vr::safe_mid {
// Where the main thread is in the engine's loop. Set on the main thread by the probe's hooks, in this order every gameplay frame:
// render side entry -> before_hook -> Present hook -> after_hook -> graph submit -> graph0 -> wait return -> after_graph0 -> graph1 ->
// after_graph1 -> render side exit -> render_to_game -> game side entry -> game -> game side exit -> game_to_render -> (render side entry).
enum Phase : unsigned {hook,after_hook,graph0,after_graph0,graph1,after_graph1,graph_more,after_graph_more,render_to_game,game,game_to_render,before_hook,phases};
inline constexpr const char* phase_names[phases]={"hook","after_hook","graph0","after_graph0","graph1","after_graph1","graph_more","after_graph_more",
    "render_to_game","game","game_to_render","before_hook"};
// v1451: hook_end = the Present hook's exit, for a frame that came due inside the hook (the earliest point after the frame exists).
// v1456: render_wait / render_wait_timer = a main-thread wait outside the game side and outside the Present hook (render graphs included),
// only while the XR session is on the mod's own device (own_device below).
enum Point : unsigned {graph_end,game_wait,game_wait_timer,render_wait,render_wait_timer,game_end,render_start,hook_end,present_start,present_fence,present_late,points};
inline constexpr const char* point_names[points]={"graph_end","game_wait","game_wait_timer","render_wait","render_wait_timer","game_end","render_start","hook_end",
    "present_start","present_fence","present"};
// Knob text (build/spacewarp-safe-mid): empty or "all" = every mid point, "graph" = graph_end only, "game" = the game-side points only
// (game_wait, game_end, render_start). Dev command `safemid off|graph|game|all`.
enum Mask : unsigned {mask_off=0u,mask_graph=1u,mask_game=2u,mask_all=3u};
inline unsigned mask_of(const std::string& text) noexcept {
    if(text=="graph") return mask_graph;
    if(text=="game") return mask_game;
    if(text=="off") return mask_off;
    return mask_all;
}
inline const char* mask_name(unsigned m) noexcept {return m==mask_all?"all":m==mask_graph?"graph":m==mask_game?"game":"off";}
inline constexpr bool mid_point(Point p) noexcept {return p<=hook_end;}
inline constexpr unsigned mask_bit(Point p) noexcept {return p==graph_end?mask_graph:p==hook_end?mask_all:mid_point(p)?mask_game:0u;}

inline std::atomic<unsigned> mask{mask_off};          // the knob (or the dev command)
inline std::atomic<bool> hooks_ok{false};             // the game side, render side, graph submit and graph wait detours are all in
inline std::atomic<bool> waits_ok{false};             // job_wake's import patch is in (game_wait)
inline std::atomic<bool> violated{false};             // an immediate-context call from another thread inside a send: off for the session
// v1456: the XR session is on the mod's own D3D11 device (src/xr_own_device.hpp; set by GameXr with its session, cleared at its close).
// The runtime's calls then never touch the game's device, so a send needs neither the engine's immediate-context lock nor the exclusive
// window (a worker's immediate-context call cannot meet the runtime's), and every main-thread wait outside the Present hook is a point,
// render graphs included (render_wait): the generated frame goes out at its due time from wherever the main thread waits. All of the
// runtime's frame calls stay on the main thread (v1453's waiting thread ending frames broke SteamVR's frame timing).
inline std::atomic<bool> own_device{false};
inline std::atomic<DWORD> main_tid{0};                // the Present thread (the engine's main thread)

// May a frame go out at point p? (Present-time points always may: they are v1445's.)
inline bool allowed(Point p) noexcept {
    if(!mid_point(p)) return true;
    const bool own=own_device.load(std::memory_order_relaxed);
    if(violated.load(std::memory_order_relaxed) && !own) return false;
    if((p==render_wait || p==render_wait_timer) && !own) return false;  // v1456: only on the own device
    const unsigned m=mask.load(std::memory_order_relaxed);
    if(!(m&mask_bit(p))) return false;
    const bool wait=p==game_wait || p==game_wait_timer || p==render_wait || p==render_wait_timer;
    return wait?hooks_ok.load(std::memory_order_relaxed) && waits_ok.load(std::memory_order_relaxed):hooks_ok.load(std::memory_order_relaxed);
}

// ---- main thread only (the hooks run on it; the job_wake idle callbacks too) ----
struct MainState {
    Phase phase{before_hook};
    unsigned graphs{};               // graph submits in this render side
    std::int64_t due{};bool armed{},due_seen{};
};
inline MainState& state() noexcept {static MainState s;return s;}

// ---- counters (written on the main thread, read by the status writer) ----
inline constexpr unsigned after_buckets=7;  // < 0.25, < 0.5, < 1, < 2, < 4, < 8, >= 8 ms after the due time
inline constexpr const char* after_bucket_names[after_buckets]={"lt0_25","lt0_5","lt1","lt2","lt4","lt8","ge8"};
struct PointStats {
    std::atomic<std::uint64_t> sent{},late_slot{},dropped{},after_us_sum{},after_us_max{},wait_us_sum{},wait_us_max{},call_us_sum{},call_us_max{};
    std::atomic<std::uint64_t> after_hist[after_buckets]{};
};
inline PointStats stats[points];
inline std::atomic<std::uint64_t> due_phase[phases]{};
inline std::atomic<std::uint64_t> armed_frames{},unarmed_records{},lock_skips{},lock_spin_us_max{},refused_lock_off{},transitions{};
inline std::atomic<std::uint64_t> violations{};inline std::atomic<DWORD> violation_tid{0};inline std::atomic<const void*> violation_ret{nullptr};
inline std::atomic<std::uint64_t> window_opens{};

inline unsigned after_bucket(std::int64_t after_ns) noexcept {
    const std::int64_t us=after_ns/1000;
    return us<250?0u:us<500?1u:us<1000?2u:us<2000?3u:us<4000?4u:us<8000?5u:6u;
}
inline void raise_max(std::atomic<std::uint64_t>& m,std::uint64_t v) noexcept {
    std::uint64_t cur=m.load(std::memory_order_relaxed);
    while(v>cur && !m.compare_exchange_weak(cur,v,std::memory_order_relaxed)) {}
}

// A generated frame became pending on the inline path with its due time (GameXr, main thread inside present()).
inline void arm(std::int64_t due_ns) noexcept {
    MainState& s=state();s.due=due_ns;s.armed=true;s.due_seen=false;armed_frames.fetch_add(1,std::memory_order_relaxed);
}
// The due time has passed in the phase that is ending (or still running): count it once per frame.
inline void see_due(std::int64_t now) noexcept {
    MainState& s=state();
    if(s.armed && !s.due_seen && s.due && now>=s.due) {s.due_seen=true;due_phase[s.phase].fetch_add(1,std::memory_order_relaxed);}
}
inline void enter(Phase next,std::int64_t now) noexcept {
    see_due(now);
    MainState& s=state();s.phase=next;transitions.fetch_add(1,std::memory_order_relaxed);
}
// The hooks' transitions (main thread)
inline void render_enter(std::int64_t now) noexcept {state().graphs=0;enter(before_hook,now);}
inline void render_exit(std::int64_t now) noexcept {enter(render_to_game,now);}
inline void game_enter(std::int64_t now) noexcept {enter(game,now);}
inline void game_exit(std::int64_t now) noexcept {enter(game_to_render,now);}
inline void hook_enter(std::int64_t now) noexcept {enter(hook,now);}
inline void hook_exit(std::int64_t now) noexcept {enter(after_hook,now);}
inline void graph_submit(std::int64_t now) noexcept {
    MainState& s=state();const unsigned i=s.graphs++;
    enter(i==0?graph0:i==1?graph1:graph_more,now);
}
inline void graph_return(std::int64_t now) noexcept {
    const Phase p=state().phase;
    enter(p==graph0?after_graph0:p==graph1?after_graph1:after_graph_more,now);
}
inline bool in_game() noexcept {return state().phase==game;}
// May a main-thread wait send now? The game side always (v1450); on the own device also the render side, except inside the Present hook
// (present()'s own points send there).
inline bool wait_phase_ok() noexcept {const Phase p=state().phase;return p==game || (own_device.load(std::memory_order_relaxed) && p!=hook);}

// The frame went out (or was dropped) at point p. after_ns: from the due time to the start of the runtime's xrWaitFrame (or to the drop);
// wait_ns: the xrWaitFrame; call_ns: the whole send on this thread. outcome: 0 sent, 1 late slot (the wait was the real frame's), 2 dropped
// before any runtime call (too near the real frame's slot).
enum Outcome : unsigned {sent_ok,late_slot,dropped};
inline void record(Point p,Outcome outcome,std::int64_t now,std::int64_t after_ns,std::int64_t wait_ns,std::int64_t call_ns) noexcept {
    MainState& s=state();
    if(!s.armed) unarmed_records.fetch_add(1,std::memory_order_relaxed);
    see_due(now);s.armed=false;
    PointStats& st=stats[p<points?p:present_late];
    if(outcome==dropped) {st.dropped.fetch_add(1,std::memory_order_relaxed);return;}
    (outcome==late_slot?st.late_slot:st.sent).fetch_add(1,std::memory_order_relaxed);
    const auto after_us=static_cast<std::uint64_t>(std::max<std::int64_t>(after_ns,0)/1000),wait_us=static_cast<std::uint64_t>(std::max<std::int64_t>(wait_ns,0)/1000);
    const auto call_us=static_cast<std::uint64_t>(std::max<std::int64_t>(call_ns,0)/1000);
    st.after_us_sum.fetch_add(after_us,std::memory_order_relaxed);raise_max(st.after_us_max,after_us);
    st.wait_us_sum.fetch_add(wait_us,std::memory_order_relaxed);raise_max(st.wait_us_max,wait_us);
    st.call_us_sum.fetch_add(call_us,std::memory_order_relaxed);raise_max(st.call_us_max,call_us);
    st.after_hist[after_bucket(after_ns)].fetch_add(1,std::memory_order_relaxed);
}
// A pending frame was abandoned without a send attempt (a resize, a fault, the session ending)
inline void abandon() noexcept {state().armed=false;}

// v1451: after a late slot at an inline point, the wait handed to the next real frame for its due time: SteamVR's slot grid, not the
// clock time of the late wait. The late frame was due at due_ns for its target slot; the handed slot opens step_ns (its display time minus
// that target) later. Outside half a period to four periods (a skipped run or a clock jump) the clock time stays.
inline std::int64_t grid_handoff(std::int64_t due_ns,std::int64_t step_ns,std::int64_t base_ns,std::int64_t clock_ns) noexcept {
    if(due_ns<=0 || base_ns<=0 || step_ns<base_ns/2 || step_ns>4*base_ns) return clock_ns;
    return due_ns+step_ns;
}
// job_wake idle callbacks: inside the game side (v1450), or on the own device in any phase but the Present hook (v1456); only with a frame
// armed and the wait points allowed
inline std::int64_t game_wait_left(std::int64_t now,std::int64_t none) noexcept {
    const MainState& s=state();
    if(!s.armed || !wait_phase_ok() || !allowed(s.phase==game?game_wait:render_wait)) return none;
    return s.due-now;
}

// ---- the exclusive window (ctx_probe::note checks it on every hooked immediate-context call) ----
inline std::atomic<DWORD> window_tid{0};
inline std::atomic<const void*> window_context{nullptr};
struct Window {
    Window(const void* context) noexcept {window_context.store(context,std::memory_order_relaxed);window_tid.store(GetCurrentThreadId(),std::memory_order_release);window_opens.fetch_add(1,std::memory_order_relaxed);}
    ~Window() {window_tid.store(0,std::memory_order_release);}
    Window(const Window&)=delete;Window& operator=(const Window&)=delete;
};
inline void note_call(const void* c,const void* ret) noexcept {
    const DWORD owner=window_tid.load(std::memory_order_acquire);
    if(!owner || !c || c!=window_context.load(std::memory_order_relaxed)) return;
    const DWORD t=GetCurrentThreadId();
    if(t==owner) return;
    if(violations.fetch_add(1,std::memory_order_relaxed)==0) {violation_tid.store(t,std::memory_order_relaxed);violation_ret.store(ret,std::memory_order_relaxed);}
    violated.store(true,std::memory_order_relaxed);
}

inline std::string json(std::uintptr_t image_base=0) {
    std::ostringstream o;
    o<<"{\"mask\":\""<<mask_name(mask.load())<<"\",\"hooks\":"<<(hooks_ok.load()?"true":"false")<<",\"waits\":"<<(waits_ok.load()?"true":"false")
     <<",\"violated\":"<<(violated.load()?"true":"false")<<",\"violations\":"<<violations.load()<<",\"violation_tid\":"<<violation_tid.load();
    const auto ret=reinterpret_cast<std::uintptr_t>(violation_ret.load());
    o<<",\"violation_ret\":\"";
    if(ret && image_base && ret>=image_base && ret<image_base+0x5000000) o<<"exe+0x"<<std::hex<<(ret-image_base)<<std::dec;else o<<"0x"<<std::hex<<ret<<std::dec;
    o<<"\",\"armed\":"<<armed_frames.load()<<",\"unarmed_records\":"<<unarmed_records.load()<<",\"windows\":"<<window_opens.load()
     <<",\"lock_skips\":"<<lock_skips.load()<<",\"lock_spin_us_max\":"<<lock_spin_us_max.load()<<",\"refused_lock_off\":"<<refused_lock_off.load()<<",\"due_phase\":{";
    for(unsigned i=0;i<phases;++i) o<<(i?",":"")<<'"'<<phase_names[i]<<"\":"<<due_phase[i].load();
    o<<"},\"points\":{";
    for(unsigned i=0;i<points;++i) {
        const PointStats& st=stats[i];const auto n=st.sent.load()+st.late_slot.load();
        o<<(i?",":"")<<'"'<<point_names[i]<<"\":{\"sent\":"<<st.sent.load()<<",\"late_slot\":"<<st.late_slot.load()<<",\"dropped\":"<<st.dropped.load()
         <<",\"after_us_mean\":"<<(n?st.after_us_sum.load()/n:0)<<",\"after_us_max\":"<<st.after_us_max.load()
         <<",\"wait_us_mean\":"<<(n?st.wait_us_sum.load()/n:0)<<",\"wait_us_max\":"<<st.wait_us_max.load()
         <<",\"call_us_mean\":"<<(n?st.call_us_sum.load()/n:0)<<",\"call_us_max\":"<<st.call_us_max.load()<<",\"after_ms\":{";
        for(unsigned b=0;b<after_buckets;++b) o<<(b?",":"")<<'"'<<after_bucket_names[b]<<"\":"<<st.after_hist[b].load();
        o<<"}}";
    }
    o<<"}}";
    return o.str();
}
} // namespace d2vr::safe_mid
