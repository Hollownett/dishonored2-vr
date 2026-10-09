#include "../src/xr_continuity.hpp"
#include <cassert>
#include <iostream>

int main() {
    d2vr::XrContinuity state;
    assert(!state.advance(false,false,false,100) && state.epoch()==0);
    assert(state.advance(true,true,true,200) && state.usable());
    auto epoch=state.epoch();
    auto reference=state.reference_epoch();
    assert(!state.advance(true,true,true,300) && state.epoch()==epoch);
    // Visibility loss, including shouldRender=false while state stays visible.
    assert(state.advance(true,false,true,400) && !state.usable());
    assert(!state.advance(true,false,true,500));
    assert(state.advance(true,true,true,600) && state.epoch()==epoch+2);
    assert(state.reference_epoch()==reference); // Visibility is not a new room calibration.
    epoch=state.epoch();
    // OpenXR reference-space changes take effect at changeTime, not poll time.
    state.reference_change(1000);state.reference_change(900);
    assert(!state.advance(true,true,true,899) && state.epoch()==epoch);
    assert(state.advance(true,true,true,900) && state.epoch()==epoch+1);
    assert(state.reference_epoch()==reference+1);
    assert(!state.advance(true,true,true,999));
    assert(state.advance(true,true,true,1000) && state.epoch()==epoch+2);
    assert(state.reference_epoch()==reference+2);
    assert(!state.advance(true,true,true,1001));
    state.suspend();epoch=state.epoch();state.suspend();
    assert(!state.usable() && state.epoch()==epoch);
    assert(state.advance(true,true,true,1100));
    epoch=state.epoch();state.recenter();
    assert(state.usable() && state.epoch()==epoch+1);
    assert(state.reference_epoch()==reference+3);
    assert(state.advance(true,true,false,1200) && !state.usable());
    std::cout << "PASS: XR visibility return, suppressed frames, delayed recenter and stale-pose epochs\n";
}
