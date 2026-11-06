#include "../src/vram_budget.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace d2vr;
    const VramState sixteen{9ull<<30,15ull<<30,true};
    assert(resolution_fits_vram(sixteen,1.0) && resolution_fits_vram(sixteen,.5) && resolution_fits_vram({},9.0)); // shrink / unknown: allowed
    assert(resolution_fits_vram(sixteen,1.1025)); // 105 %
    assert(resolution_fits_vram(sixteen,1.5625)); // 125 %
    assert(!resolution_fits_vram(sixteen,4.0)); // 200 %: 9 GB * 2.5 = 22 GB of 15
    assert(std::fabs(fit_resolution_multiplier(sixteen,1.0,1.25)-1.25)<1e-9);
    const auto capped=fit_resolution_multiplier(sixteen,1.0,2.0);
    assert(capped>1.0 && capped<2.0 && resolution_fits_vram(sixteen,capped*capped) && !resolution_fits_vram(sixteen,(capped+.05)*(capped+.05)));
    const VramState full{14ull<<30,15ull<<30,true};
    assert(std::fabs(fit_resolution_multiplier(full,1.0,1.5)-1.0)<1e-9); // nothing fits: stay where we are
    assert(std::fabs(fit_resolution_multiplier(full,1.5,1.0)-1.0)<1e-9 && std::fabs(fit_resolution_multiplier(sixteen,1.0,0.8)-0.8)<1e-9);
    std::cout<<"PASS: vram budget guard (shrink/unknown allowed, clear OOM refused, stepwise cap, stays put when nothing fits)\n";
}
