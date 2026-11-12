// v1251: TimepieceGrip (src/timepiece_grip.hpp): grip squeeze = unfold, open = fold, via short native Use pulses keyed on the game state.
#include "timepiece_grip.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
using d2vr::TimepieceGrip;

int main() {
    CHECK(d2vr::is_timepiece_equip_name("Timeshift"));
    CHECK(!d2vr::is_timepiece_equip_name("Empty") && !d2vr::is_timepiece_equip_name("Crossbow") && !d2vr::is_timepiece_equip_name("Heart"));
    {   // not equipped (powers, the Heart, no-powers playthrough): never presses Use
        TimepieceGrip g;
        for(std::uint64_t t=0;t<3000;t+=11) CHECK(!g.update(false,t<1500?1.f:0.f,0,false,t));
    }
    {   // squeeze: one pulse, the game unfolds; hold: nothing; release: one pulse, the game folds
        TimepieceGrip g;std::uint64_t t=1000;
        CHECK(!g.update(true,0,0,false,t));
        t+=11;CHECK(g.update(true,1,0,false,t));
        t+=60;CHECK(g.update(true,1,0,false,t));               // the pulse lasts pulse_ms
        t+=100;CHECK(!g.update(true,1,69,false,t));            // unfolding
        for(int i=0;i<200;++i) {t+=11;CHECK(!g.update(true,.8f,70,false,t));} // held up, light grip dips above release
        t+=11;CHECK(g.update(true,.1f,70,false,t));            // release -> fold press
        t+=200;CHECK(!g.update(true,0,0,false,t));
        for(int i=0;i<200;++i) {t+=11;CHECK(!g.update(true,0,0,false,t));}
    }
    {   // the device came up without the grip (scene or carried over): an open hand folds it, at most `attempts` presses
        TimepieceGrip g;std::uint64_t t=0;unsigned presses=0;bool was=false;
        for(int i=0;i<1000;++i) {t+=11;const bool p=g.update(true,0,70,false,t);if(p && !was) ++presses;was=p;}
        CHECK(presses==TimepieceGrip::attempts);
    }
    {   // a refused Use (a door took it): retried after settle_ms, then given up; a new squeeze starts over
        TimepieceGrip g;std::uint64_t t=0;unsigned presses=0;bool was=false;
        for(int i=0;i<1000;++i) {t+=11;const bool p=g.update(true,1,0,false,t);if(p && !was) ++presses;was=p;}
        CHECK(presses==TimepieceGrip::attempts);
        for(int i=0;i<10;++i) {t+=11;g.update(true,0,0,false,t);}
        presses=0;was=false;
        for(int i=0;i<30;++i) {t+=11;const bool p=g.update(true,1,0,false,t);if(p && !was) ++presses;was=p;}
        CHECK(presses==1);
    }
    {   // a time shift (68) is never interrupted; busy hands (a Use target, a held object, the wheel) never press
        TimepieceGrip g;std::uint64_t t=0;
        for(int i=0;i<200;++i) {t+=11;CHECK(!g.update(true,0,68,false,t));}
        TimepieceGrip b;
        for(int i=0;i<200;++i) {t+=11;CHECK(!b.update(true,1,0,true,t));}
        t+=11;CHECK(b.update(true,1,0,false,t));               // free again with the grip still squeezed: raise it
    }
    std::printf("timepiece_grip_check ok\n");
    return 0;
}
