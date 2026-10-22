#include "../src/discovery_cadence.hpp"
#include <cstdio>
#include <cstdlib>
using d2vr::DiscoveryCadence;
static int failures=0;
#define REQUIRE(c,m) do{if(!(c)){std::printf("FAIL: %s (line %d)\n",m,__LINE__);++failures;}}while(0)
int main(){
    {DiscoveryCadence c; // every_frame_until_stable
     bool all=true;for(std::uint64_t f=1;f<=120;++f) all=all && c.due(f,true);
     REQUIRE(all,"the first 120 stable frames are all due");REQUIRE(c.throttling(),"120 stable frames arm the throttle");}
    {DiscoveryCadence c; // throttles_after_120_stable
     for(std::uint64_t f=1;f<=120;++f) c.due(f,true);
     unsigned due=0;for(std::uint64_t f=121;f<=121+79;++f) {const bool d=c.due(f,true);due+=d;REQUIRE(d==(f%8==0),"only every 8th frame is due once throttled");}
     REQUIRE(due==10,"80 throttled frames hold 10 reads");}
    {DiscoveryCadence c; // invalidate_returns_to_every_frame
     for(std::uint64_t f=1;f<=200;++f) c.due(f,true);
     REQUIRE(c.throttling(),"throttled before the invalidation");
     c.invalidate();
     bool all=true;for(std::uint64_t f=201;f<=320;++f) all=all && c.due(f,true);
     REQUIRE(all,"every frame is due again after invalidate()");REQUIRE(c.throttling(),"and 120 stable frames re-arm the throttle");
     REQUIRE(c.invalidations()==1,"invalidations are counted");}
    {DiscoveryCadence c; // invalidate_in_the_same_frame_forces_a_read
     for(std::uint64_t f=1;f<=130;++f) c.due(f,true);
     REQUIRE(!c.due(131,true),"throttled frame 131 is not due");
     c.invalidate();REQUIRE(c.due(131,true),"an invalidation inside a frame re-evaluates it as due");}
    {DiscoveryCadence c; // unstable_frame_resets_count
     for(std::uint64_t f=1;f<=119;++f) c.due(f,true);
     REQUIRE(c.due(120,false),"an unstable frame is due");REQUIRE(c.stable_run()==0,"and resets the run");
     bool all=true;for(std::uint64_t f=121;f<=240;++f) all=all && c.due(f,true);
     REQUIRE(all,"119 stable frames earlier do not count after a reset");
     REQUIRE(!c.due(241,true) && c.due(248,true),"the throttle arms 120 stable frames after the reset");}
    {DiscoveryCadence c; // an_unstable_read_while_throttled_ends_the_throttle
     for(std::uint64_t f=1;f<=200;++f) c.due(f,true);
     REQUIRE(c.due(208,true),"frame 208 is a scheduled read");
     REQUIRE(c.due(209,false),"the read found a change: the next frame is due");REQUIRE(!c.throttling(),"throttle off");
     REQUIRE(c.due(210,true),"and stays every frame");}
    {DiscoveryCadence c; // several_callers_in_one_frame_agree
     for(std::uint64_t f=1;f<=130;++f) c.due(f,true);
     const bool first=c.due(131,true);
     REQUIRE(c.due(131,true)==first && c.due(131,false)==first,"a repeat call in the same frame returns the first answer");
     REQUIRE(c.stable_run()==120,"and counts nothing");}
    if(!failures) std::puts("PASS: discovery cadence is every frame until 120 stable frames, then every 8th, and invalidate() restores every frame");
    return failures?1:0;
}
