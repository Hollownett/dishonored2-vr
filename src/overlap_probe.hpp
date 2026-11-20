#pragma once
// v1432, Phase 0 of docs/ENGINE-OVERLAP-PLAN.md (2026-11-20): how much of the main thread's wait for the render graph could overlap the
// next frame's game side, if that wait moved to the first point where the game side writes state the render graph reads. Timestamps only,
// no behaviour change; knob build/overlap-probe; main thread only.
//
// Per frame N (the last render-graph wait of its render side, exe 2c8440, is the one Phase 1 would defer):
//   W = time the main thread spends inside that wait (it helps run jobs, then blocks until the graph is done);
//   t = from the end of that wait to the end of the render side (the small tail);
//   p = from the end of the render side to the first write point of frame N+1: the first call of the component-update graph
//       (exe 7a9e0 TtarkSubmitAndWaitGraph), of the UI update (exe 31c80, which ticks Iggy), the extract (exe 45650) or the next render
//       side (exe 458c0), whichever comes first;
//   saved = min(W, t + p): with the wait deferred, the main thread would reach the sync point at wait start + t + p and only wait for
//       what is left of the graph. Optimistic (it ignores that the graph may run longer without the main thread's help).
//   g = game side duration (exe 459a0), for scale.
// Phase 0b (v1432): the two eyes' render graphs inside one render side (exe 2c8310 submits, 2c8440 waits, ~1.9 per gameplay frame). The
// game state does not change between them, so the first graph could go on running while the main thread prepares the second:
//   first_wait = the main thread inside the first graph's wait; gap = from the end of that wait to the second graph's submit (the main
//   thread's own work for the second view); intra = min(first_wait, gap); same_ctx = both graphs use the same render context object.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "pass_timeline.hpp"

namespace d2vr::overlap_probe {
inline std::atomic<bool> on{false};
inline std::atomic<DWORD> main_tid{};
inline std::int64_t now() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline bool here() noexcept {return on.load(std::memory_order_relaxed) && GetCurrentThreadId()==main_tid.load(std::memory_order_relaxed);}
enum Write {component_graph,ui_update,extract,render_side,writes};
struct State {
    std::mutex m;                                    // the vectors are read by the status writer
    // main thread only:
    std::int64_t wait_enter{},wait_exit{},render_exit{},game_enter{},game_ms_ns{},first_write{};int write_kind{-1},waits{},last_waits{};
    // Phase 0b: this render side's graph submits and waits (time, context), and the game side before it
    std::int64_t sub_t[4]{},wait_in[4]{},wait_out[4]{};const void* sub_c[4]{};const void* wait_c[4]{};int subs{},wcount{};std::int64_t frame_game_ns{};
    std::vector<float> first_wait,gap,intra;std::uint64_t eye_frames{},same_ctx{};
    bool in_render{},have_render{};
    std::int64_t render_t0{};const void* wait_ctx{};  // v1433 pass timeline markers
    // results (ms), the last 8192 frames:
    std::vector<float> w,t,p,saved,g;std::uint64_t frames{},kinds[writes]{},waits_total{};
};
inline State& state() {static State s;return s;}
inline void push(std::vector<float>& v,float x) {if(v.size()>=8192) v.erase(v.begin(),v.begin()+1024);v.push_back(x);}

inline void write_point(Write kind) noexcept {
    if(!here()) return;
    State& s=state();
    if(s.have_render && !s.in_render && !s.first_write) {s.first_write=now();s.write_kind=kind;}
}
inline void render_enter() noexcept {
    if(!here()) return;
    State& s=state();
    write_point(render_side);
    if(s.have_render && s.wait_exit && s.first_write) {
        const float W=static_cast<float>(s.wait_exit-s.wait_enter)/1e6f,T=static_cast<float>(s.render_exit-s.wait_exit)/1e6f;
        const float P=static_cast<float>(s.first_write-s.render_exit)/1e6f,G=static_cast<float>(s.game_ms_ns)/1e6f;
        if(W>=0 && T>=0 && P>=0 && W<200 && T<200 && P<200 && G>=1.f) {  // gameplay only: a real game side ran (menus and loads have ~0.15 ms)
            std::lock_guard<std::mutex> l(s.m);
            push(s.w,W);push(s.t,T);push(s.p,P);push(s.saved,std::min(W,T+P));push(s.g,G);
            ++s.frames;++s.kinds[s.write_kind];s.waits_total+=static_cast<std::uint64_t>(s.last_waits);
        }
    }
    s.frame_game_ns=s.game_ms_ns;s.subs=s.wcount=0;  // Phase 0b
    pass_timeline::frame(s.game_ms_ns>=1'000'000);s.render_t0=now();  // v1433
    s.in_render=true;s.have_render=false;s.wait_enter=s.wait_exit=0;s.waits=0;s.first_write=0;s.write_kind=-1;s.game_ms_ns=0;
}
inline void render_exit() noexcept {
    if(!here()) return;
    State& s=state();
    s.in_render=false;s.render_exit=now();s.have_render=s.wait_exit!=0;s.last_waits=s.waits;
    pass_timeline::add(pass_timeline::render_side,nullptr,s.render_t0,s.render_exit,0);  // v1433
    if(s.subs>=2 && s.wcount>=1 && s.frame_game_ns>=1'000'000 && s.sub_t[1]>s.wait_out[0] && s.wait_out[0]>=s.wait_in[0]) {  // Phase 0b, gameplay
        const float W1=static_cast<float>(s.wait_out[0]-s.wait_in[0])/1e6f,Gap=static_cast<float>(s.sub_t[1]-s.wait_out[0])/1e6f;
        if(W1<200 && Gap<200) {
            std::lock_guard<std::mutex> l(s.m);
            push(s.first_wait,W1);push(s.gap,Gap);push(s.intra,std::min(W1,Gap));++s.eye_frames;if(s.wait_c[0]==s.sub_c[1]) ++s.same_ctx;
        }
    }
}
inline void graph_submit(const void* ctx) noexcept {
    if(!here()) return;State& s=state();
    if(s.in_render && s.subs<4) {s.sub_t[s.subs]=now();s.sub_c[s.subs]=ctx;++s.subs;}
    if(s.in_render) {const auto t=now();pass_timeline::add(pass_timeline::graph_submit,ctx,t,t,0);}  // v1433
}
inline void wait_enter(const void* ctx=nullptr) noexcept {
    if(!here()) return;State& s=state();
    if(s.in_render) {s.wait_enter=now();s.wait_ctx=ctx;if(s.wcount<4) {s.wait_in[s.wcount]=s.wait_enter;s.wait_c[s.wcount]=ctx;}}
}
inline void wait_exit() noexcept {
    if(!here()) return;State& s=state();
    if(s.in_render && s.wait_enter) {s.wait_exit=now();++s.waits;if(s.wcount<4) s.wait_out[s.wcount++]=s.wait_exit;
        pass_timeline::add(pass_timeline::graph_wait,s.wait_ctx,s.wait_enter,s.wait_exit,0);}  // v1433
}
inline void game_enter() noexcept {if(!here()) return;state().game_enter=now();}
inline void game_exit() noexcept {
    if(!here()) return;State& s=state();
    if(s.game_enter) {const auto t=now();s.game_ms_ns=t-s.game_enter;pass_timeline::add(pass_timeline::game_side,nullptr,s.game_enter,t,0);}  // v1433
}

inline std::string json() {
    State& s=state();std::lock_guard<std::mutex> l(s.m);std::ostringstream o;o.setf(std::ios::fixed);o.precision(3);
    const auto stats=[&](const char* name,std::vector<float> v) {
        std::sort(v.begin(),v.end());
        const auto at=[&](double q) {return v.empty()?0.f:v[std::min(v.size()-1,static_cast<std::size_t>(q*v.size()))];};
        o<<",\""<<name<<"\":{\"p10\":"<<at(.1)<<",\"p50\":"<<at(.5)<<",\"p90\":"<<at(.9)<<"}";
    };
    o<<"{\"on\":"<<(on.load()?"true":"false")<<",\"frames\":"<<s.frames<<",\"waits_per_frame\":"<<(s.frames?static_cast<double>(s.waits_total)/s.frames:0.);
    stats("wait_ms",s.w);stats("tail_ms",s.t);stats("to_first_write_ms",s.p);stats("saved_ms",s.saved);stats("game_ms",s.g);
    o<<",\"first_write\":{\"component_graph\":"<<s.kinds[component_graph]<<",\"ui_update\":"<<s.kinds[ui_update]<<",\"extract\":"<<s.kinds[extract]
     <<",\"render_side\":"<<s.kinds[render_side]<<"}";
    o<<",\"eyes\":{\"frames\":"<<s.eye_frames<<",\"same_ctx\":"<<s.same_ctx;  // Phase 0b
    stats("first_wait_ms",s.first_wait);stats("gap_ms",s.gap);stats("intra_ms",s.intra);
    o<<"}}";
    return o.str();
}
} // namespace d2vr::overlap_probe
