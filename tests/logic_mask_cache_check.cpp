#include "../src/logic_mask_cache.hpp"
#include <cstdio>
using d2vr::LogicMaskCache;
static int failures=0;
#define REQUIRE(c,m) do{if(!(c)){std::printf("FAIL: %s (line %d)\n",m,__LINE__);++failures;}}while(0)
// A fake engine: the live mask, whether the game object validates, and how many live table reads happened.
struct Engine {
    unsigned mask{2};bool valid{true};bool readable{true};unsigned live_reads{};
    unsigned ask(LogicMaskCache& c,bool enabled,std::uint64_t frame,std::uint64_t now) {
        return c.get(enabled,frame,now,[&]{return valid;},[&]()->std::optional<unsigned>{++live_reads;if(!readable) return std::nullopt;return mask;});
    }
};
int main(){
    {LogicMaskCache c;Engine e; // rereads_every_frame (kills a cache that never refreshes)
     const unsigned masks[]{2,2,256,2,1u<<15,2,2,4,2,2};
     bool all=true;std::uint64_t now=1000;
     for(std::uint64_t f=1;f<=10;++f) {
         e.mask=masks[f-1];
         for(int call=0;call<90;++call) all=all && e.ask(c,true,f,now)==masks[f-1];
         now+=11;
     }
     REQUIRE(all,"every call of every frame returns that frame's live value");
     REQUIRE(e.live_reads==10,"ten frames of 90 calls cost ten live reads");
     REQUIRE(c.reads==10 && c.served==890,"counters: 10 reads, 890 served");}
    {LogicMaskCache c;Engine e;std::uint64_t now=1000; // one_frame_reload_state_is_seen
     e.mask=2;e.ask(c,true,1,now);e.mask=1u<<15;now+=11;
     REQUIRE(e.ask(c,true,2,now)==(1u<<15),"a reload mask lasting a single frame is returned in that frame");
     e.mask=2;now+=11;REQUIRE(e.ask(c,true,3,now)==2,"and gone in the next");}
    {LogicMaskCache c;Engine e;std::uint64_t now=1000; // at_most_one_frame_old
     e.mask=2;REQUIRE(e.ask(c,true,5,now)==2,"first call reads live");
     e.mask=256;REQUIRE(e.ask(c,true,5,now)==2,"a change inside the frame waits for the next frame");
     REQUIRE(e.ask(c,true,6,now+1)==256,"the next frame's first call (any thread) reads live: at most one frame old");}
    {LogicMaskCache c;Engine e;std::uint64_t now=1000; // load_is_never_served_from_the_cache
     e.ask(c,true,7,now);e.valid=false;
     REQUIRE(e.ask(c,true,7,now)==0,"validation is live on every call: a load returns 0 inside the same frame");
     e.valid=true;e.mask=1u<<15;const auto before=e.live_reads;
     REQUIRE(e.ask(c,true,7,now)==(1u<<15) && e.live_reads==before+1,"the cache was dropped by the failed validation");
     REQUIRE(c.resets>=1,"the drop is counted");}
    {LogicMaskCache c;Engine e;std::uint64_t now=1000; // failed_read_returns_zero_and_is_not_cached
     e.readable=false;REQUIRE(e.ask(c,true,1,now)==0,"a failed table read answers 0");
     e.readable=true;e.mask=2;REQUIRE(e.ask(c,true,1,now)==2,"and is retried in the same frame");}
    {LogicMaskCache c;Engine e;std::uint64_t now=1000; // knob_off_reads_every_call
     for(int i=0;i<5;++i) e.ask(c,false,1,now);
     REQUIRE(e.live_reads==5 && c.served==0,"no-discovery-throttle: every call reads");}
    {LogicMaskCache c;Engine e; // stalled_frame_counter_expires
     e.mask=2;e.ask(c,true,9,1000);e.mask=256;
     REQUIRE(e.ask(c,true,9,1000+LogicMaskCache::max_age_ms-1)==2,"inside the age limit the frame's value stands");
     REQUIRE(e.ask(c,true,9,1000+LogicMaskCache::max_age_ms)==256,"a frame counter that stops advancing cannot freeze the mask");
     e.mask=4;REQUIRE(e.ask(c,true,9,500)==4,"a clock that went backwards re-reads");}
    if(!failures) std::puts("PASS: the logic mask is read live once per frame, validated live on every call, never served across a load, and the knob reads every call");
    return failures?1:0;
}
