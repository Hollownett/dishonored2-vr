// v1249 pure check of the held_walk_burst trigger (src/held_walk_burst.hpp Machine): arm after a 1 s hold above 1 m/s, 16 frames, cooldown,
// one burst per kind (walking), a second per kind only above 3 m/s, never a third; a gap in the placements restarts the hold.
#include <cstdio>
#include "../src/held_walk_burst.hpp"
using namespace d2vr::held_walk_burst;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static void run(Machine& m,unsigned h,unsigned k,std::uint64_t t0,std::uint64_t t1,double speed,bool& started,std::uint64_t& at) {   // 90 Hz
    started=false;
    for(std::uint64_t t=t0;t<t1;t+=11) {m.note(h,k,t);if(!started && m.try_start(t,speed)) {started=true;at=t;}}
}
int main() {
    Machine m;bool s;std::uint64_t at{};
    run(m,0,prop,0,900,2.0,s,at);CHECK(!s);                      // held < 1 s
    run(m,0,prop,900,1200,0.5,s,at);CHECK(!s);                   // slow
    run(m,0,prop,1200,1400,2.0,s,at);CHECK(s && m.kind==prop && m.hand==0 && m.index==1);CHECK(at>=1000);
    CHECK(!m.try_start(1500,2.0));                               // active
    bool done=false;for(unsigned i=0;i<16;++i) done=m.frame_done(2000);CHECK(done && !m.active && m.captured==16);
    run(m,0,prop,2000,3500,2.0,s,at);CHECK(!s);                  // cooldown (2 s) then walking tier spent
    run(m,0,prop,3500,5000,2.0,s,at);CHECK(!s);                  // 2 m/s: second burst needs > 3 m/s
    run(m,0,prop,5000,5500,4.0,s,at);CHECK(s && m.index==2);
    for(unsigned i=0;i<16;++i) m.frame_done(6000);
    run(m,0,prop,8100,9000,6.0,s,at);CHECK(!s);                  // never a third
    run(m,1,severed,10000,12000,1.5,s,at);CHECK(s && m.kind==severed && m.hand==1 && m.index==1);    // another kind has its own count
    for(unsigned i=0;i<16;++i) m.frame_done(12500);
    // a gap restarts the hold clock; a kind change too
    Machine g;g.note(0,use,0);g.note(0,use,900);g.note(0,use,2000);CHECK(!g.try_start(2000,2.0));for(std::uint64_t t=2100;t<=3100;t+=100) g.note(0,use,t);CHECK(g.try_start(3100,2.0) && g.kind==use);
    Machine c;c.note(0,prop,0);c.note(0,severed,900);CHECK(!c.try_start(1100,2.0));
    CHECK(!Machine{}.exhausted());
    std::printf(failures?"held_walk_burst_check FAILED\n":"held_walk_burst_check ok\n");
    return failures?1:0;
}
