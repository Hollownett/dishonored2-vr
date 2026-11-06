#include "../src/vr_controls.hpp"
#include <cassert>
#include <cstdio>

using d2vr::HeartGrip;
using A=HeartGrip::Action;

int main() {
    {   // v1045 journal: the right-hand wheel picks the Heart while the LEFT hand (the old slot-1 arm) squeezes a pistol; the arm flips to the right with an open grip: no drop
        HeartGrip g;
        assert(g.update(true,0,1.f,false,true)==A::none && g.held());      // the old arm squeezing: held for it
        assert(g.update(true,1,0.f,false,true)==A::none && !g.held());     // arm flip: state starts over, the right grip is open
        for(int i=0;i<10;++i) assert(g.update(true,1,0.f,false,true)==A::none);
    }
    {   // a wheel arrival with the grip open stays until the hand has squeezed it once
        HeartGrip g;
        for(int i=0;i<20;++i) assert(g.update(true,1,.1f,false,true)==A::none);
        assert(g.update(true,1,.5f,false,true)==A::none && !g.held());     // below the press threshold
        assert(g.update(true,1,.7f,false,true)==A::none && g.held());
        assert(g.update(true,1,.5f,false,true)==A::none && g.held());      // release hysteresis .4
        assert(g.update(true,1,.3f,false,true)==A::drop);
    }
    {   // hold then let go at the hip puts it away; once
        HeartGrip g;g.update(true,1,0.f,true,true);g.update(true,1,.9f,true,true);
        assert(g.update(true,1,.0f,true,true)==A::put_away);
        assert(g.update(true,1,.9f,true,true)==A::none && g.update(true,1,0.f,true,true)==A::none);   // the unequip is in flight: no second action
        assert(g.update(false,1,0.f,true,true)==A::none);                  // gone ...
        g.update(true,1,0.f,false,true);assert(!g.held());                 // ... and a new arrival starts open
        g.update(true,1,.9f,false,true);assert(g.update(true,1,0.f,false,true)==A::drop);
    }
    {   // a hand already squeezing at the arrival counts as held (a pickup squeeze), as GunGrip
        HeartGrip g;assert(g.update(true,0,.9f,false,true)==A::none && g.held());
        assert(g.update(true,0,0.f,false,true)==A::drop);
    }
    {   // nothing outside gameplay; a grip released during a scene does not drop
        HeartGrip g;g.update(true,1,0.f,false,true);g.update(true,1,.9f,false,true);
        assert(g.update(true,1,0.f,false,false)==A::none && !g.held());
        assert(g.update(true,1,0.f,false,true)==A::none);
        assert(g.update(true,1,.9f,false,true)==A::none && g.update(true,1,0.f,false,true)==A::drop);
    }
    std::puts("heart_grip_check: ok");
    return 0;
}
