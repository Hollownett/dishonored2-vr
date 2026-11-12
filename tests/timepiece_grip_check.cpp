// v1254: TimepieceGrip (src/timepiece_grip.hpp) is a PRESS toggle: each fresh squeeze flips the device once, letting go changes nothing, and a
// held grip never repeats (user: "press grip = unfolded, press it again = folded, from hold grip to press only"). The game's state (69 unfolding,
// 70 up, 68 shifting, 0 folded) decides the direction of each press; a short native Use pulse is sent while the press and the device disagree, at
// most `attempts` times per press. The request counters are cumulative, so every case works from its own baseline.
#include "timepiece_grip.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
using d2vr::TimepieceGrip;
// Runs `frames` frames of one grip/state and returns how many Use presses it requested (the counters are cumulative).
static unsigned run(TimepieceGrip& g,float grip,int state,bool busy,std::uint64_t& t,unsigned frames) {
    const auto before=TimepieceGrip::requests[TimepieceGrip::unfold_grip].load()+TimepieceGrip::requests[TimepieceGrip::fold_release].load();
    for(unsigned i=0;i<frames;++i) {t+=11;g.update(true,grip,state,busy,t);}
    return static_cast<unsigned>(TimepieceGrip::requests[TimepieceGrip::unfold_grip].load()+TimepieceGrip::requests[TimepieceGrip::fold_release].load()-before);
}
static unsigned long long unfold() {return TimepieceGrip::requests[TimepieceGrip::unfold_grip].load();}
static unsigned long long fold() {return TimepieceGrip::requests[TimepieceGrip::fold_release].load();}
int main() {
    CHECK(d2vr::is_timepiece_equip_name("Timeshift"));
    CHECK(!d2vr::is_timepiece_equip_name("Empty") && !d2vr::is_timepiece_equip_name("Crossbow") && !d2vr::is_timepiece_equip_name("Heart"));
    {   // not equipped (powers, the Heart, no-powers playthrough): never presses Use, whatever the grip does
        TimepieceGrip g;std::uint64_t t=0;
        for(unsigned i=0;i<600;++i) {t+=11;CHECK(!g.update(false,i%120<60?1.f:0.f,0,false,t));}
    }
    {   // first squeeze unfolds exactly once; holding it up for ~4 s does not repeat; letting go does nothing
        TimepieceGrip g;std::uint64_t t=0;const auto u0=unfold(),f0=fold();
        CHECK(run(g,0,0,false,t,20)==0);                       // open hand, folded device: nothing
        CHECK(run(g,1,0,false,t,1)==1);                        // the squeeze: one Use
        CHECK(unfold()==u0+1);
        CHECK(run(g,1,69,false,t,10)==0);                      // the game unfolds (69): no more presses
        CHECK(run(g,1,70,false,t,380)==0);                     // held up 4.2 s: still one press in total
        CHECK(unfold()==u0+1 && fold()==f0);
        CHECK(run(g,0,70,false,t,40)==0);                      // release: nothing, the device stays up
        CHECK(unfold()==u0+1 && fold()==f0);
    }
    {   // the second squeeze folds it once, a third unfolds it again: toggled from the game's state, not from the grip
        TimepieceGrip g;std::uint64_t t=0;const auto u0=unfold(),f0=fold();
        CHECK(run(g,1,0,false,t,1)==1);                        // press 1: unfold
        run(g,1,69,false,t,10);run(g,1,70,false,t,60);run(g,0,70,false,t,20);
        CHECK(fold()==f0);
        CHECK(run(g,1,70,false,t,1)==1);                       // press 2: fold
        CHECK(fold()==f0+1);
        run(g,1,0,false,t,10);run(g,1,0,false,t,60);run(g,0,0,false,t,20);
        CHECK(run(g,1,0,false,t,1)==1);                        // press 3: unfold again
        CHECK(unfold()==u0+2);
    }
    {   // a device already up with an open hand is NOT folded (v1251's open-hand rule is gone: only a press acts)
        TimepieceGrip g;std::uint64_t t=0;const auto f0=fold();
        CHECK(run(g,0,70,false,t,400)==0);
        CHECK(fold()==f0);
    }
    {   // hysteresis: the same squeeze eased to .45 (still above release) is not a second press; it must fall under release first
        TimepieceGrip g;std::uint64_t t=0;const auto f0=fold();
        CHECK(run(g,1,70,false,t,1)==1);                       // press: fold
        CHECK(fold()==f0+1);
        CHECK(run(g,.45f,70,false,t,20)==0);                   // above release: down_ stays set, no new press
        CHECK(run(g,.2f,70,false,t,20)==0);                    // under release, no press yet
        CHECK(run(g,1,70,false,t,1)==1);                       // a fresh crossing of `press`: one more request
        CHECK(fold()==f0+2);
    }
    {   // a running shift (68) is never pressed into; the pending press applies once the shift is over
        TimepieceGrip g;std::uint64_t t=0;const auto f0=fold();
        CHECK(run(g,1,68,false,t,1)==0);                       // squeezed while shifting: no Use
        CHECK(run(g,1,68,false,t,200)==0);                     // ~2.2 s of shift: still nothing
        CHECK(fold()==f0);
        CHECK(run(g,1,70,false,t,80)==1);                      // the shift ends up (70), after its settle: the held squeeze folds it once
        CHECK(fold()==f0+1);
    }
    {   // busy (a Use target under the hand, a held object, a menu): the squeeze is not the device's; a fresh squeeze after busy works
        TimepieceGrip g;std::uint64_t t=0;const auto u0=unfold();
        CHECK(run(g,1,0,true,t,200)==0);
        CHECK(TimepieceGrip::busy_frames.load()>0);
        CHECK(run(g,0,0,false,t,20)==0);                       // released after busy: nothing
        CHECK(run(g,1,0,false,t,1)==1);                        // a fresh squeeze unfolds
        CHECK(unfold()==u0+1);
    }
    {   // a refused Use (a door took it): at most `attempts` presses per squeeze, then given up; the next squeeze starts over
        TimepieceGrip g;std::uint64_t t=0;const auto u0=unfold();
        unsigned pulses=run(g,1,0,false,t,1);                  // press 1
        pulses+=run(g,1,0,false,t,1000);                       // the device never comes up: the retries only
        CHECK(pulses==TimepieceGrip::attempts);
        CHECK(unfold()==u0+TimepieceGrip::attempts);
        CHECK(TimepieceGrip::gave_up.load()>=1);
        run(g,0,0,false,t,20);
        CHECK(run(g,1,0,false,t,1)==1);                        // a new squeeze tries again
        CHECK(unfold()==u0+TimepieceGrip::attempts+1);
    }
    std::printf("timepiece_grip_check ok\n");
    return 0;
}
