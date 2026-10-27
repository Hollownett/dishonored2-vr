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
    {   // a body on the shoulder stays whatever the grips do; a squeeze that starts at the shoulder and leaves it takes the body off
        using A=std::array<bool,2>;using G=std::array<float,2>;using S=std::array<double,2>;
        const A none{false,false},right_in{false,true};const S rest{0,0};
        const auto off=[](const d2vr::ShoulderCarry::Out& o) {return o.drop || o.toss;};
        d2vr::ShoulderCarry c;
        // the grip that lifted it there is held at the carry's start, then let go, the hand lowered: the body stays
        c.update(false,right_in,G{0,.9f},rest,0);
        expect(!off(c.update(true,right_in,G{0,.9f},rest,100)) && !off(c.update(true,none,G{0,.9f},rest,400)),"the docking grip carried away does not take it off");
        expect(!off(c.update(true,none,G{0,0},rest,600)) && !off(c.update(true,none,G{0,0},rest,5000)),"released grips leave it on the shoulder");
        // a squeeze started away from the shoulder and brought there does not take hold
        c.update(true,none,G{0,.9f},rest,5100);
        expect(!off(c.update(true,right_in,G{0,.9f},rest,5200)) && !off(c.update(true,none,G{0,.9f},rest,5300)),"a squeeze from elsewhere is not a take");
        c.update(true,none,G{0,0},rest,5400);
        // squeeze at the shoulder, let go there: nothing
        c.update(true,right_in,G{0,.9f},rest,6000);
        expect(!off(c.update(true,right_in,G{0,0},rest,6100)) && !off(c.update(true,none,G{0,0},rest,6200)),"let go at the shoulder: it stays");
        // squeeze at the shoulder and pull away slowly: put down, by that hand, once
        expect(!c.consumes(1),"no squeeze, nothing consumed");
        c.update(true,right_in,G{0,.9f},S{0,.3},7000);
        expect(c.consumes(1) && !c.consumes(0),"the take squeeze is the carry's (no native Use)");
        {const auto o=c.update(true,none,G{0,.9f},S{0,.4},7300);expect(o.drop && !o.toss && o.hand==1,"a slow pull puts it down");}
        expect(!off(c.update(true,none,G{0,.9f},rest,7400)),"one take-off, not a Use every frame");
        expect(c.consumes(1),"still consumed after the take-off until it is let go (no Use picking the body up again)");
        c.update(false,none,G{0,.9f},rest,7450);expect(c.consumes(1),"consumed while held, even once the carry ended");
        // the game kept it: a fresh take after retry_ms works again
        c.update(true,none,G{0,0},rest,8000);c.update(true,right_in,G{0,.9f},rest,8700);
        expect(c.update(true,none,G{0,.9f},rest,8750).drop,"the game kept the body: taken off again");
        c.update(false,none,G{0,0},rest,9000);
        // squeeze at the left shoulder and swing away: thrown
        c.update(true,A{true,false},G{.9f,0},S{2.5,0},10000);
        {const auto o=c.update(true,none,G{.9f,0},S{2.8,0},10050);expect(o.toss && !o.drop && o.hand==0,"a swing off the shoulder throws it");}
        // no carry: nothing
        c.update(false,right_in,G{0,0},rest,11000);c.update(false,right_in,G{0,.9f},rest,11100);
        expect(!off(c.update(false,none,G{0,.9f},rest,11200)),"no carry, no take-off");
    }
    if(!failures) std::printf("PASS: held object drop/throw on grip release, click pickup sticky, throw uses recent peak; shoulder carry take-off\n");
    return failures?1:0;
}
