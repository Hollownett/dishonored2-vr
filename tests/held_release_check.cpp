#include "../src/held_release.hpp"
#include <cmath>
#include <cstdio>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using d2vr::HeldRelease;const HeldRelease::V still{},fast{0,1,-3};
    {   // held grip: slow release drops
        HeldRelease h;std::uint64_t t=0;
        for(;t<1000;t+=11) expect(!h.update(true,.9f,still,0,t).drop,"held keeps");
        const auto o=h.update(true,.1f,still,0,t);expect(o.drop && !o.toss,"slow release drops");
        expect(!h.update(true,.1f,still,0,t+11).drop,"drops once");
    }
    {   // fast release throws with the recent peak velocity, even as the hand slows at the release
        HeldRelease h;std::uint64_t t=0;
        for(;t<600;t+=11) h.update(true,.9f,still,0,t);
        h.update(true,.9f,fast,3.2,t);t+=11;
        const auto o=h.update(true,.1f,{0,0,-.5},.5,t+30);
        expect(o.toss && !o.drop && o.velocity[2]==-3,"fast release throws along the hand");
    }
    {   // click pickup stays in hand; the next squeeze and release drops
        HeldRelease h;
        expect(!h.update(true,.9f,still,0,0).drop,"pickup");
        expect(!h.update(true,.1f,still,0,150).drop,"click release keeps it");
        expect(!h.update(true,.1f,still,0,2000).drop,"stays without grip");
        h.update(true,.9f,still,0,2100);
        expect(h.update(true,.1f,still,0,2200).drop,"next release drops");
    }
    {   // pickup without the grip held (e.g. Use from the other hand): first squeeze then release lets go
        HeldRelease h;
        expect(!h.update(true,0,still,0,0).drop,"no grip at start");
        h.update(true,.9f,still,0,500);
        expect(h.update(true,.1f,still,0,800).drop,"release drops");
        expect(!h.update(false,0,still,0,900).drop && !h.update(true,0,still,0,1000).drop,"new carry rearms cleanly");
    }
    {   // the throw keeps the engine's speed and takes only the hand's direction; legacy keeps hand x1.5 capped at 20
        const std::array<double,3> hand{0,-6,-8},engine{0,2,-10.198039027185569};   // hand speed 10, engine speed 10.39
        const auto t=d2vr::held_throw_velocity(hand,engine,false);
        const double speed=std::sqrt(t.v[0]*t.v[0]+t.v[1]*t.v[1]+t.v[2]*t.v[2]);
        expect(t.ok && std::fabs(speed-10.4)<.01,"throw speed is the engine's");
        expect(std::fabs(t.v[1]/speed+.6)<1e-9 && std::fabs(t.v[2]/speed+.8)<1e-9,"throw direction is the hand's");
        const std::array<double,3> swing{0,-30,0};   // a 30 m/s swing at the floor still leaves at the engine's speed
        const auto f=d2vr::held_throw_velocity(swing,engine,false);
        expect(f.ok && std::fabs(f.v[1]+10.4)<.02 && f.v[0]==0 && f.v[2]==0,"a fast hand swing does not raise the speed");
        const auto old=d2vr::held_throw_velocity(hand,engine,true);
        expect(old.ok && std::fabs(old.v[1]+9)<1e-9 && std::fabs(old.v[2]+12)<1e-9,"legacy-throw: hand x1.5");
        const auto oldfast=d2vr::held_throw_velocity(swing,engine,true);
        expect(oldfast.ok && std::fabs(oldfast.v[1]+20)<1e-9,"legacy-throw: capped at 20 m/s");
        expect(!d2vr::held_throw_velocity({0,0,0},engine,false).ok,"no hand direction leaves the engine velocity");
        expect(!d2vr::held_throw_velocity(hand,{0,0,0},false).ok,"no engine velocity leaves it alone");
        expect(!d2vr::held_throw_velocity({0,std::nan(""),1},engine,false).ok && !d2vr::held_throw_velocity(hand,{1,std::nan(""),1},false).ok,"non-finite input leaves the event alone");
    }
    if(!failures) std::printf("PASS: held object drop/throw on grip release, click pickup sticky, throw uses recent peak\n");
    return failures?1:0;
}
