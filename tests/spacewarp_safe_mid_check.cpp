// v1450: spacewarp safe mid points (src/spacewarp_safe_mid.hpp): where the due time falls in the engine's loop, which points may send,
// the game-side wait gate, the exclusive window's violation count, and job_wake's timer flag. Build and run:
// build\night\lab.cmd spacewarp_safe_mid_check
#include "spacewarp_safe_mid.hpp"
#include "job_wake.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace sm=d2vr::safe_mid;
namespace jw=d2vr::job_wake;
static int failures=0;
static void check(bool ok,const char* what) {if(!ok) {++failures;std::printf("FAIL %s\n",what);} else std::printf("ok   %s\n",what);}
static std::uint64_t phase_count(sm::Phase p) {return sm::due_phase[p].load();}
static void reset_counts() {
    for(auto& c:sm::due_phase) c.store(0);
    for(auto& s:sm::stats) {s.sent=0;s.late_slot=0;s.dropped=0;s.after_us_sum=0;s.after_us_max=0;s.wait_us_sum=0;s.wait_us_max=0;s.call_us_sum=0;s.call_us_max=0;for(auto& h:s.after_hist) h=0;}
    sm::violations=0;sm::violated=false;sm::unarmed_records=0;
}
// One gameplay frame of the engine's loop as the hooks report it (ms offsets from t0, in ns)
static constexpr std::int64_t ms=1'000'000;

int main() {
    sm::main_tid=GetCurrentThreadId();sm::mask=sm::mask_all;sm::hooks_ok=true;sm::waits_ok=true;  // as with the knob and every detour in
    {   // the due time inside the second eye graph: counted there; sent at that graph's end
        reset_counts();const std::int64_t t0=1000*ms;
        sm::render_enter(t0);sm::hook_enter(t0+ms/2);
        sm::arm(t0+12*ms);                                      // armed inside the Present hook: due a period after the real frame's wait
        sm::hook_exit(t0+3*ms);sm::graph_submit(t0+3*ms+ms/5);
        sm::graph_return(t0+8*ms);                              // eye 0: 3.2 .. 8 ms
        check(phase_count(sm::graph0)==0 && phase_count(sm::after_graph0)==0,"not due yet after the first graph");
        sm::graph_submit(t0+8*ms+ms/10);
        sm::graph_return(t0+13*ms);                             // eye 1: 8.1 .. 13 ms; due at 12 ms
        check(phase_count(sm::graph1)==1,"the due time fell in the second graph (counted at its return)");
        sm::record(sm::graph_end,sm::sent_ok,t0+13*ms+ms/10,ms+ms/10,ms/5,ms/2);
        check(sm::stats[sm::graph_end].sent==1 && sm::stats[sm::graph_end].after_hist[3]==1,"sent at graph_end, 1.1 ms after the due time (bucket lt2)");
        check(sm::stats[sm::graph_end].after_us_max==1100 && sm::stats[sm::graph_end].wait_us_max==200 && sm::stats[sm::graph_end].call_us_max==500,"after, wait and call times kept in us");
        sm::render_exit(t0+13*ms+ms/5);sm::game_enter(t0+13*ms+ms/4);
        check(sm::game_wait_left(t0+14*ms,jw::idle_none)==jw::idle_none,"a sent frame leaves the game waits alone");
        sm::game_exit(t0+17*ms);sm::render_enter(t0+17*ms+ms/10);
        std::uint64_t total=0;for(unsigned i=0;i<sm::phases;++i) total+=phase_count(static_cast<sm::Phase>(i));
        check(total==1,"one due-phase count per generated frame");
    }
    {   // the due time inside the game side: the wait gate counts down only there
        reset_counts();const std::int64_t t0=2000*ms;
        sm::render_enter(t0);sm::hook_enter(t0+ms/2);sm::arm(t0+12*ms);sm::hook_exit(t0+2*ms);
        sm::graph_submit(t0+2*ms);
        check(sm::game_wait_left(t0+3*ms,jw::idle_none)==jw::idle_none,"no game wait inside a render graph (v1444's freeze)");
        sm::graph_return(t0+5*ms);sm::graph_submit(t0+5*ms);
        check(sm::game_wait_left(t0+6*ms,jw::idle_none)==jw::idle_none,"... nor in the second graph");
        sm::graph_return(t0+7*ms);
        check(sm::game_wait_left(t0+8*ms,jw::idle_none)==jw::idle_none,"... nor after it in the render side");
        sm::render_exit(t0+7*ms);sm::game_enter(t0+7*ms);
        sm::mask=sm::mask_all;sm::hooks_ok=true;sm::waits_ok=true;
        check(sm::game_wait_left(t0+8*ms,jw::idle_none)==4*ms,"in the game side: the time until the due time");
        check(sm::game_wait_left(t0+12*ms+ms/4,jw::idle_none)==-ms/4,"... and due (<= 0) once it has passed");
        sm::mask=sm::mask_graph;
        check(sm::game_wait_left(t0+8*ms,jw::idle_none)==jw::idle_none,"the graph-only mask keeps the game waits off");
        sm::mask=sm::mask_all;sm::waits_ok=false;
        check(sm::game_wait_left(t0+8*ms,jw::idle_none)==jw::idle_none,"no game waits without job_wake's patch");
        sm::waits_ok=true;
        sm::record(sm::game_wait_timer,sm::sent_ok,t0+12*ms+ms/20,ms/20,ms/10,ms/3);
        check(phase_count(sm::game)==1,"a send inside the game side counts the due time there");
        check(sm::stats[sm::game_wait_timer].after_hist[0]==1,"sent within 0.25 ms of the due time");
        sm::game_exit(t0+15*ms);
        std::uint64_t total=0;for(unsigned i=0;i<sm::phases;++i) total+=phase_count(static_cast<sm::Phase>(i));
        check(total==1,"still one count after the later transitions");
    }
    {   // late slot and drop outcomes, and a frame abandoned on a resize
        reset_counts();const std::int64_t t0=3000*ms;
        sm::render_enter(t0);sm::hook_enter(t0);sm::arm(t0+11*ms);sm::hook_exit(t0+3*ms);
        sm::record(sm::present_start,sm::dropped,t0+22*ms,11*ms,0,ms/100);
        check(sm::stats[sm::present_start].dropped==1 && sm::stats[sm::present_start].sent==0,"a drop is counted apart from sends");
        check(phase_count(sm::after_hook)==1,"... and its due time where the main thread was");
        sm::arm(t0+30*ms);sm::record(sm::graph_end,sm::late_slot,t0+37*ms,7*ms,ms/10,ms);
        check(sm::stats[sm::graph_end].late_slot==1 && sm::stats[sm::graph_end].after_hist[5]==1,"a late slot 7 ms after (bucket lt8)");
        sm::arm(t0+40*ms);sm::abandon();sm::graph_submit(t0+45*ms);
        check(phase_count(sm::after_hook)==2,"an abandoned frame is not counted at later transitions");
        sm::record(sm::graph_end,sm::sent_ok,t0+46*ms,0,0,0);
        check(sm::unarmed_records==1,"a record without an armed frame is flagged");
    }
    {   // which points may send
        reset_counts();sm::hooks_ok=true;sm::waits_ok=true;
        sm::mask=sm::mask_off;
        check(!sm::allowed(sm::graph_end) && !sm::allowed(sm::game_wait) && sm::allowed(sm::present_start) && sm::allowed(sm::present_fence),"off: only the Present-time points (v1445)");
        sm::mask=sm::mask_graph;
        check(sm::allowed(sm::graph_end) && !sm::allowed(sm::game_wait) && !sm::allowed(sm::game_end) && !sm::allowed(sm::render_start),"graph: graph_end only");
        sm::mask=sm::mask_game;
        check(!sm::allowed(sm::graph_end) && sm::allowed(sm::game_wait) && sm::allowed(sm::game_wait_timer) && sm::allowed(sm::game_end) && sm::allowed(sm::render_start),"game: the game-side points");
        sm::mask=sm::mask_all;sm::hooks_ok=false;
        check(!sm::allowed(sm::graph_end) && !sm::allowed(sm::game_end),"no mid point without the phase detours");
        sm::hooks_ok=true;sm::violated=true;
        check(!sm::allowed(sm::graph_end) && !sm::allowed(sm::game_wait) && sm::allowed(sm::present_start),"a violation switches the mid points off, not the Present-time ones");
        sm::violated=false;
        check(sm::mask_of("")==sm::mask_all && sm::mask_of("all")==sm::mask_all && sm::mask_of("graph")==sm::mask_graph && sm::mask_of("game")==sm::mask_game && sm::mask_of("off")==sm::mask_off,"knob text");
    }
    {   // the exclusive window: another thread's call on the immediate context inside a send
        reset_counts();int immediate=0,deferred=0;
        sm::note_call(&immediate,nullptr);
        check(sm::violations==0,"no window: nothing counted");
        {
            sm::Window window(&immediate);
            sm::note_call(&immediate,nullptr);
            check(sm::violations==0,"the sending thread's own (runtime) calls are not violations");
            std::thread([&] {sm::note_call(&deferred,nullptr);}).join();
            check(sm::violations==0,"another thread on a deferred context is not a violation");
            std::thread([&] {sm::note_call(&immediate,reinterpret_cast<const void*>(0x1234));}).join();
            check(sm::violations==1 && sm::violated && sm::violation_ret==reinterpret_cast<const void*>(0x1234),"another thread on the immediate context is one, with its return address");
        }
        std::thread([&] {sm::note_call(&immediate,nullptr);}).join();
        check(sm::violations==1,"after the window closes nothing more is counted");
        sm::violated=false;
    }
    {   // after-time buckets
        check(sm::after_bucket(0)==0 && sm::after_bucket(249'999)==0 && sm::after_bucket(250'000)==1 && sm::after_bucket(999'999)==2 && sm::after_bucket(1'000'000)==3,"buckets below 1 ms");
        check(sm::after_bucket(3'999'999)==4 && sm::after_bucket(4'000'000)==5 && sm::after_bucket(8'000'000)==6 && sm::after_bucket(-5)==0,"buckets above 1 ms (negative = 0)");
    }
    {   // the status object
        const std::string j=sm::json(0x140000000);int depth=0;bool balanced=true;
        for(char c:j) {if(c=='{') ++depth;if(c=='}' && --depth<0) balanced=false;}
        check(balanced && depth==0,"status JSON braces balance");
        check(j.find("\"due_phase\":{\"hook\":")!=std::string::npos && j.find("\"graph_end\":{\"sent\":")!=std::string::npos && j.find("\"game_wait_timer\":")!=std::string::npos,"status JSON names the phases and points");
    }
    {   // job_wake's timer flag: true only while the work runs because the due time came first
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);jw::ticks_per_second=f.QuadPart;
        static std::int64_t due=jw::idle_none;static int runs=0;static bool saw_timer=false,saw_plain=false;
        const auto steady=[] {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
        static std::int64_t (*now)()=+[]() -> std::int64_t {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
        jw::idle_left=+[]() noexcept -> std::int64_t {return due==jw::idle_none?jw::idle_none:due-now();};
        jw::idle_work=+[]() noexcept {++runs;(jw::idle_timer_work?saw_timer:saw_plain)=true;due=jw::idle_none;};
        const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        due=steady()-ms;DWORD timeout=INFINITE,result=0;
        jw::idle_wait(event,timeout,result);
        check(runs==1 && saw_plain && !saw_timer,"work due at the wait's start runs without the timer flag");
        due=steady()+15*ms;timeout=200;runs=0;saw_plain=saw_timer=false;
        jw::idle_wait(event,timeout,result);
        check(runs==1 && saw_timer && !saw_plain && !jw::idle_timer_work,"work at the timer runs with the flag, cleared after");
        CloseHandle(event);jw::idle_left=nullptr;jw::idle_work=nullptr;
    }
    std::printf(failures?"FAILED %d\n":"ALL PASSED\n",failures);
    return failures?1:0;
}
