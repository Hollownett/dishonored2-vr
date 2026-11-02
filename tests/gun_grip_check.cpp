// Row A: GunGrip's drop/pickup (dual wield) and that the flag off is the old behaviour.
#include "../src/vr_controls.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
using A=GunGrip::Action;
static GunGrip::In base_in(std::uint64_t now) {GunGrip::In in{};in.gameplay=true;in.empty=true;in.loaded=true;in.draw_authorized=true;in.now=now;return in;}
struct Run {
    GunGrip g;std::uint64_t now=1000;
    GunGrip::Out step(GunGrip::In in,std::uint64_t dt=20) {now+=dt;in.now=now;return g.update(in);}
};
static GunGrip::In squeeze(const GunGrip::In& base,bool zone,bool drawn) {auto in=base;in.grip=1;in.zone=zone;in.drawn=drawn;in.empty=!drawn;return in;}
static GunGrip::In let_go(const GunGrip::In& base,bool zone,bool drawn) {auto in=base;in.grip=0;in.zone=zone;in.drawn=drawn;in.empty=!drawn;return in;}
// squeeze at the hip for >= 100 ms, return the action the draw fires with
static A draw_at_hip(Run& r,GunGrip::In base) {
    A got=A::none;
    for(int i=0;i<8;++i) {auto o=r.step(squeeze(base,true,false));if(o.action!=A::none) got=o.action;}
    return got;
}
int main() {
    for(bool dual:{false,true}) {
        auto base=base_in(0);base.drop_enabled=dual;
        {   // draw at the hip, hold it, let go AT the hip: holsters (both modes)
            Run r;assert(draw_at_hip(r,base)==A::draw);
            for(int i=0;i<3;++i) r.step(squeeze(base,true,true));       // the gun is in the hand
            const auto o=r.step(let_go(base,true,true));
            assert(o.action==A::holster);
        }
        {   // let go away from the hip: holster (flag off: today), drop (flag on)
            Run r;assert(draw_at_hip(r,base)==A::draw);
            for(int i=0;i<3;++i) r.step(squeeze(base,true,true));
            for(int i=0;i<3;++i) r.step(squeeze(base,false,true));
            const auto o=r.step(let_go(base,false,true));
            assert(o.action==(dual?A::drop:A::holster));
            if(dual) { // the engine still reports it drawn for a moment: a new squeeze does not hold it again, and no second action comes
                for(int i=0;i<3;++i) assert(r.step(squeeze(base,false,true)).action==A::none);
                assert(r.step(let_go(base,false,true)).action==A::none);
            }
        }
        {   // a draw that has not landed yet (pending) and is let go away from the hip is still a holster, never a drop
            Run r;assert(draw_at_hip(r,base)==A::draw);
            const auto o=r.step(let_go(base,false,false));
            assert(o.action==A::holster);
        }
        {   // a gun the wheel put in the hand: the first squeeze holds it, the release away from the hip drops it (flag on)
            Run r;
            for(int i=0;i<3;++i) r.step(squeeze(base,false,true));
            const auto o=r.step(let_go(base,false,true));
            assert(o.action==(dual?A::drop:A::holster));
        }
        {   // a release while busy (blocking, carrying) is never a drop
            Run r;assert(draw_at_hip(r,base)==A::draw);
            for(int i=0;i<3;++i) r.step(squeeze(base,true,true));
            auto busy=let_go(base,false,true);busy.busy=true;
            assert(r.step(busy).action==A::none);
        }
    }
    {   // pickup: a dropped native gun lies in the world (copy). A squeeze at the hip or within reach of it takes it back, with the draw's 100 ms and authorisation.
        auto base=base_in(0);base.drop_enabled=true;base.copy=true;
        {   Run r;A got=A::none;
            for(int i=0;i<8;++i) {auto in=squeeze(base,true,false);in.loaded=false;auto o=r.step(in);if(o.action!=A::none) got=o.action;} // the hip is empty (loaded false): still a pickup
            assert(got==A::pickup);
        }
        {   Run r;A got=A::none;
            for(int i=0;i<8;++i) {auto in=squeeze(base,false,false);in.copy_reach=true;auto o=r.step(in);if(o.action!=A::none) got=o.action;}
            assert(got==A::pickup);
        }
        {   Run r;A got=A::none; // not at the hip, not in reach: nothing
            for(int i=0;i<8;++i) {auto o=r.step(squeeze(base,false,false));if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   Run r;A got=A::none; // not authorised: the 100 ms alone does not take it
            for(int i=0;i<8;++i) {auto in=squeeze(base,true,false);in.draw_authorized=false;auto o=r.step(in);if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   Run r;A got=A::none; // a use target / object claim in reach keeps the squeeze
            for(int i=0;i<8;++i) {auto in=squeeze(base,false,false);in.copy_reach=true;in.object_claim=true;auto o=r.step(in);if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   Run r;A got=A::none; // flag off: a copy is ignored, the hip draws as before
            auto off=base;off.drop_enabled=false;
            for(int i=0;i<8;++i) {auto o=r.step(squeeze(off,true,false));if(o.action!=A::none) got=o.action;}
            assert(got==A::draw);
            Run r2;got=A::none;
            for(int i=0;i<8;++i) {auto in=squeeze(off,false,false);in.copy_reach=true;auto o=r2.step(in);if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   // pickup then hold then drop again
            Run r;A got=A::none;
            for(int i=0;i<8;++i) {auto o=r.step(squeeze(base,true,false));if(o.action!=A::none) got=o.action;}
            assert(got==A::pickup);
            auto in_hand=base;in_hand.copy=false;
            for(int i=0;i<3;++i) r.step(squeeze(in_hand,false,true));
            assert(r.step(let_go(in_hand,false,true)).action==A::drop);
        }
    }
    for(bool keep:{true,false}) { // a mantle takes the gun out of the squeezing hand (busy): drawn again after it, only while still squeezed
        auto base=base_in(0);Run r;assert(draw_at_hip(r,base)==A::draw);
        for(int i=0;i<3;++i) r.step(squeeze(base,false,true));
        auto scene=squeeze(base,false,false);scene.busy=true;scene.grip=keep?1.f:0.f;
        for(int i=0;i<20;++i) assert(r.step(scene).action==A::none);
        A got=A::none;
        for(int i=0;i<30;++i) {auto o=r.step(keep?squeeze(base,false,false):let_go(base,false,false));if(o.action!=A::none) got=o.action;}
        assert(got==(keep?A::draw:A::none));
    }
    {   // v962 (user, headset v951: a gun picked up with the free hand while the other holds the sword went to the hip): a gun the extras loop took natively
        // from the floor is expected in the hand; when it has not landed after expect_ms while the hand still squeezes, the hand draws it from the hip
        auto base=base_in(0);base.drop_enabled=true;
        {   Run r;r.g.expect(r.now);int actions=0;A got=A::none;bool flagged=false;std::uint64_t at=0;
            for(int i=0;i<30;++i) {auto o=r.step(squeeze(base,false,false));if(o.action!=A::none) {++actions;got=o.action;flagged=o.expected;at=r.now-1000;}}
            assert(got==A::draw && flagged && actions==1 && at>=350 && at<=400);   // once, after the wait, flagged as the pickup's fallback
            for(int i=0;i<3;++i) r.step(squeeze(base,false,true));                  // the draw lands: held like any drawn gun
            assert(r.step(let_go(base,false,true)).action==A::drop);
        }
        {   Run r;r.g.expect(r.now);A got=A::none;                                  // it landed on its own: no fallback
            for(int i=0;i<5;++i) r.step(squeeze(base,false,false));
            for(int i=0;i<40;++i) {auto o=r.step(squeeze(base,false,true));if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   Run r;r.g.expect(r.now);A got=A::none;                                  // the hand let go first: no fallback, a later squeeze away from the hip is a new one
            for(int i=0;i<5;++i) r.step(squeeze(base,false,false));
            for(int i=0;i<3;++i) r.step(let_go(base,false,false));
            for(int i=0;i<40;++i) {auto o=r.step(squeeze(base,false,false));if(o.action!=A::none) got=o.action;}
            assert(got==A::none);
        }
        {   Run r;r.g.expect(r.now);A got=A::none;                                  // busy (a mantle, the authored camera): waits, then draws while still within the 3 s
            for(int i=0;i<40;++i) {auto in=squeeze(base,false,false);in.busy=true;if(r.step(in).action!=A::none) got=A::draw;}
            assert(got==A::none);
            for(int i=0;i<10;++i) {auto o=r.step(squeeze(base,false,false));if(o.action!=A::none && got==A::none) got=o.action;}
            assert(got==A::draw);
        }
        {   Run r;r.g.expect(r.now);A got=A::none;                                  // nothing at the hip (not loaded) or an object claim: nothing to draw
            for(int i=0;i<40;++i) {auto in=squeeze(base,false,false);in.loaded=false;if(r.step(in).action!=A::none) got=A::draw;}
            for(int i=0;i<40;++i) {auto in=squeeze(base,false,false);in.object_claim=true;if(r.step(in).action!=A::none) got=A::draw;}
            assert(got==A::none);
            r.step(squeeze(base,false,false),2500);                                 // and after 3 s the wait is over
            for(int i=0;i<40;++i) {if(r.step(squeeze(base,false,false)).action!=A::none) got=A::draw;}
            assert(got==A::none);
        }
        {   Run r;A got=A::none;                                                    // no pickup, no expectation: a squeeze with an empty hand away from the hip does nothing
            for(int i=0;i<60;++i) {if(r.step(squeeze(base,false,false)).action!=A::none) got=A::draw;}
            assert(got==A::none);
        }
    }
    std::cout<<"gun_grip_check ok\n";
}
