#include "../src/resolution_debounce.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main() {
    d2vr::ResolutionDebounce d;
    CHECK(!d.take(0));
    // A drag through many values applies once, the last one, after the slider is quiet.
    for(int i=0;i<10;++i) d.note(.5+i*.05,1000+i*100);
    CHECK(d.pending());
    CHECK(!d.take(1900+699));
    const auto v=d.take(1900+700);
    CHECK(v && *v==.5+9*.05);
    CHECK(!d.pending() && !d.take(5000));
    // A new value while a retry is queued replaces it and restarts the quiet period.
    CHECK(d.defer(1.0,6000));
    d.note(1.5,6100);
    CHECK(!d.take(6700) && *d.take(6800)==1.5);
    // A failed apply retries a bounded number of times, then gives up (no endless loop, no throw).
    unsigned n=0;std::uint64_t t=10000;
    d.note(2.0,t);
    for(;n<100;++n) {
        t+=1000;const auto r=d.take(t);if(!r) break;
        if(!d.defer(*r,t)) break;
    }
    CHECK(n==d2vr::ResolutionDebounce::max_attempts);
    CHECK(!d.pending());
    std::puts("resolution_debounce_check OK");
}
