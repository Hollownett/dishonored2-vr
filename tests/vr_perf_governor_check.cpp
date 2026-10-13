#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "vr_perf_governor.hpp"
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr std::uint64_t P=11'111'111;
static void run(PerfGovernor& g,unsigned n,double busy,double missrate) {
    for(unsigned i=0;i<n;++i) g.feed({static_cast<std::uint64_t>(P*busy),P,missrate>0 && (i%100)<missrate*100});
}
int main() {
    CHECK(missed_slot(3*P,P,P) && !missed_slot(2*P,P,P) && !missed_slot(P,0,P) && !missed_slot(5,9,P) && !missed_slot(5*P,P,0));
    { PerfGovernor g; CHECK(std::fabs(g.scale()-.77f)<1e-4f);
      run(g,45,.7,0); CHECK(std::fabs(g.scale()-.77f)<1e-4f); } // dead band / not enough calm windows
    { PerfGovernor g; run(g,45,1.0,.10); CHECK(g.scale()<.77f); const float a=g.scale(); // misses -> down
      run(g,45*40,1.0,.10); CHECK(g.scale()==.6f && a>.6f); } // floor
    { PerfGovernor g; run(g,45*100,.5,0); CHECK(std::fabs(g.scale()-.95f)<1e-4f); } // calm -> up to cap
    { PerfGovernor g; run(g,45,1.0,.10); const float d=g.scale(); run(g,45*2,.5,0); CHECK(g.scale()==d); // cooldown
      run(g,45*6,.5,0); CHECK(g.scale()>d); }
    { PerfGovernor g; run(g,45,1.0,.10); const float d=g.scale(); // one down step
      for(int i=0;i<30;++i) g.feed({P,P,true}); // the change's own stall/miss burst lands in the settle window
      run(g,45,.9,0); CHECK(g.scale()==d && g.changes==1); }
    { PerfGovernor g; run(g,45,1.0,.5); CHECK(g.scale()<=.77f-.11f); } // severe: 2 steps
    { PerfGovernor g; run(g,45*20,.9,0); CHECK(std::fabs(g.scale()-.77f)<1e-4f && g.changes==0); } // between thresholds holds
    { PerfGovernor g; CHECK(!g.feed({P*10,P,true}) && !g.feed({0,P,false}) && !g.feed({P,0,false})); run(g,44,.5,0); CHECK(g.changes==0); } // stalls ignored
    { PerfGovernorConfig c;c.min_scale=.9f;c.max_scale=.8f;c.start_scale=2;PerfGovernor g(c);CHECK(g.scale()==.9f); }
    { PerfGovernor g; for(int i=0;i<2000;++i){run(g,45,(i&1)?1.0:.5,(i&1)?.1:0);} CHECK(g.changes<1500); CHECK(g.scale()>=.6f && g.scale()<=.95f); }
    { PerfGovernorConfig c;c.busy_high=1.02;PerfGovernor g(c);run(g,450,.9,0);CHECK(g.changes==0); // work 0.9 of the period, no misses: hold
      PerfGovernor h(c);run(h,450,.9,.10);CHECK(h.scale()<.77f); } // 10% missed slots: step down
    { PerfGovernor g; run(g,450,1.1,0); CHECK(g.changes==0); } // display-locked "busy" without missed slots never steps down
    std::puts("vr_perf_governor_check ok");
}
