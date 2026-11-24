// Host check of src/power_cancel.hpp (GitHub issue #6: the grip cancels a held power aim instead of casting it).
// cl /nologo /std:c++17 /EHsc /W4 /I. tests\power_cancel_check.cpp && power_cancel_check.exe
#include "src/power_cancel.hpp"
#include <cstdio>

using d2vr::PowerCancelLatch;
static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

namespace {
struct Run {
    PowerCancelLatch l;int edges=0;PowerCancelLatch::Out last{};
    PowerCancelLatch::Out step(float trigger,float grip,bool power=true,int frames=1) {
        for(int i=0;i<frames;++i) {last=l.update(trigger,grip,power);if(last.cancel_edge) ++edges;}
        return last;
    }
};
}

int main() {
    {   // aim_then_grip_cancels: the trigger aims, the grip cancels once, LT stays released while the trigger is held
        Run r;auto o=r.step(1.f,0.f,true,20);
        EXPECT(o.exempt_fist && !o.suppress_trigger && r.l.aim_held());
        o=r.step(1.f,1.f);
        EXPECT(o.cancel_edge && o.suppress_trigger && !o.exempt_fist);
        o=r.step(1.f,1.f,true,30);
        EXPECT(r.edges==1 && o.suppress_trigger && !o.exempt_fist);
    }
    {   // no_aim_grip_keeps_fist_rule: grip first, trigger after = a fist: no aim, no exemption, no cancel
        Run r;r.step(0.f,1.f,true,5);auto o=r.step(1.f,1.f,true,20);
        EXPECT(!o.exempt_fist && !o.suppress_trigger && r.edges==0 && !r.l.aim_held());
        o=r.step(1.f,0.f,true,3);   // opening the fist with the trigger still held does not start an aim
        EXPECT(!o.exempt_fist && r.edges==0);
    }
    {   // same_frame_grip_and_trigger_is_a_fist
        Run r;auto o=r.step(1.f,1.f,true,10);
        EXPECT(!o.exempt_fist && r.edges==0);
    }
    {   // release_after_cancel_never_casts: cancel, then let go of the trigger with the grip still closed: no new aim, no second
        // cancel; the next aim needs a fresh trigger pull with the grip open
        Run r;r.step(1.f,0.f,true,10);r.step(1.f,1.f,true,5);
        auto o=r.step(0.f,1.f,true,5);
        EXPECT(!o.suppress_trigger && !o.exempt_fist && r.edges==1);
        o=r.step(0.f,0.f,true,5);o=r.step(1.f,0.f,true,3);
        EXPECT(o.exempt_fist && r.edges==1);
    }
    {   // cancel_then_open_grip_keeps_trigger_suppressed: the grip opens again while the trigger is still held: still cancelled
        Run r;r.step(1.f,0.f,true,10);r.step(1.f,1.f,true,3);auto o=r.step(1.f,0.f,true,10);
        EXPECT(o.suppress_trigger && !o.exempt_fist && r.edges==1);
    }
    {   // grip_flutter_single_cancel: a grip wavering around the threshold after the cancel gives one cancel only
        Run r;r.step(1.f,0.f,true,10);
        for(int i=0;i<10;++i) {r.step(1.f,.6f);r.step(1.f,.4f);r.step(1.f,.3f);}
        EXPECT(r.edges==1);
    }
    {   // grip_hysteresis_during_aim: a partly closed grip (between open and close) never cancels
        Run r;r.step(1.f,.3f,true,10);auto o=r.step(1.f,.5f,true,10);
        EXPECT(o.exempt_fist && r.edges==0);
    }
    {   // ordinary_release_casts: aim and release without the grip: no cancel, no suppression (the game casts)
        Run r;r.step(1.f,0.f,true,10);auto o=r.step(.1f,0.f,true,3);
        EXPECT(!o.suppress_trigger && !o.exempt_fist && r.edges==0 && !r.l.aim_held());
    }
    {   // no_power_no_aim: no power in the hand: the latch never exempts, suppresses or cancels
        Run r;auto o=r.step(1.f,0.f,false,10);o=r.step(1.f,1.f,false,5);
        EXPECT(!o.exempt_fist && !o.suppress_trigger && r.edges==0);
    }
    {   // power_leaves_hand_clears: the power is switched away during a cancel: the latch lets go of LT
        Run r;r.step(1.f,0.f,true,10);r.step(1.f,1.f,true,2);auto o=r.step(1.f,1.f,false,2);
        EXPECT(!o.suppress_trigger && !o.exempt_fist);
        EXPECT(r.l.cancels()==1);
    }
    if(failures) {std::printf("power_cancel_check: %d failure(s)\n",failures);return 1;}
    std::printf("power_cancel_check: ok\n");
    return 0;
}
