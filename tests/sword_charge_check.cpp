// v963: a charging sword strike on a held trigger, cancelled by a swing, a sheathe or a menu, fired on release past the
// threshold. Pure, no game access.
// v980: the sword hand's grip + trigger is that charge, and the same press is no longer the attack gate's explicit block
// (that block made the v963 charge unreachable).
#include "../src/attack_intent.hpp"
#include "../src/sword_charge.hpp"
#include <cassert>
#include <cstdio>
using d2vr::SwordCharge;

int main() {
    SwordCharge c;
    std::uint64_t now=1000;
    auto step=[&](float trigger,bool drawn,bool held,bool allowed,float speed,int ms=16) {now+=ms;return c.update(trigger,drawn,held,allowed,speed,now);};
    {   // a tap below the threshold fires nothing
        auto o=step(1,true,true,true,0,100);
        assert(o.charging && !o.ready);
        for(int i=0;i<5;++i) o=step(1,true,true,true,0,100);
        assert(o.ready && o.charge>=SwordCharge::fire_at);
    }
    c.reset();
    {   // hold 0.5 s and release: fired, with one haptic tick per third
        int ticks=0;auto o=step(1,true,true,true,0,16);assert(o.charging);
        for(int i=0;i<40;++i) {o=step(1,true,true,true,0,16);if(o.tick) ++ticks;}
        assert(o.ready && ticks>=1);
        o=step(0,true,true,true,0,16);
        assert(o.fired && !o.charging);
        o=step(0,true,true,true,0,16);assert(!o.fired);   // one shot per release
    }
    c.reset();
    {   // a swing (the hand moves) cancels the charge; a fresh press is needed
        auto o=step(1,true,true,true,0,16);assert(o.charging);
        for(int i=0;i<30;++i) o=step(1,true,true,true,0,16);
        o=step(1,true,true,true,3.0f,16);assert(!o.charging && !o.ready);
        o=step(1,true,true,true,0,16);assert(!o.charging);   // still held: no new charge without a new press
        o=step(0,true,true,true,0,16);assert(!o.fired);
        o=step(1,true,true,true,0,16);assert(o.charging && o.charge==0.f);   // a fresh press charges
    }
    {   // no sword, no charge; letting go of the sword cancels
        SwordCharge s;now=0;
        auto o=s.update(1,false,true,true,0,100);assert(!o.charging && !o.fired);
        o=s.update(1,true,false,true,0,100);assert(!o.charging);
        o=s.update(0,true,true,true,0,100);assert(!o.charging);
        o=s.update(1,true,true,true,0,100);assert(o.charging);
        o=s.update(1,true,true,false,0,100);assert(!o.charging && !o.ready);
    }
    {   // v980: the press that starts a charge is reported once (the caller counts it for the headset log)
        SwordCharge s;now=0;
        auto o=s.update(1,true,true,true,0,now);assert(o.started && o.charging);
        now+=16;o=s.update(1,true,true,true,0,now);assert(!o.started && o.charging);
    }
    {   // v980: the sword hand's grip + trigger owns the press; a consumed (Talk/Pickpocket) trigger does not
        assert(d2vr::sword_trigger_owns_press(true,true,false));
        assert(!d2vr::sword_trigger_owns_press(false,true,false));   // no sword in the hand: the trigger is not the charge
        assert(!d2vr::sword_trigger_owns_press(true,false,false));   // trigger up: nothing to own
        assert(!d2vr::sword_trigger_owns_press(true,true,true));     // the press is already a native Use
        // The v963 failure, pinned: while the sword hand's trigger was also its explicit block the attack gate was closed,
        // and the charge runs on that gate, so it could never charge. v980 clears that term for the charge's press.
        d2vr::AttackGate gate{};
        gate.owns=gate.allowed=gate.tracked=true;
        assert(d2vr::attack_allowed(gate));
        gate.explicit_block=true;
        assert(!d2vr::attack_allowed(gate));
        gate.explicit_block=false;   // the charge owns the trigger press
        assert(d2vr::attack_allowed(gate));
    }
    {   // v1298 pose charge: fist by the ear, blade forward, charge; a forward thrust releases; lowering the blade cancels
        using d2vr::PoseCharge;PoseCharge pc;std::uint64_t t=0;
        const PoseCharge::Pose ear{true,.95f,.1f,.1f,0.f},thrust{false,.95f,.1f,2.f,2.f},low{true,.2f,-.8f,.1f,0.f},away{false,.95f,.1f,.2f,.1f};
        auto run=[&](const PoseCharge::Pose& p,int frames){PoseCharge::Out o{};for(int i=0;i<frames;++i){t+=16;o=pc.update(p,true,true,true,t);}return o;};
        auto o=run(ear,10);assert(o.charging && !o.ready);
        o=run(thrust,1);assert(!o.fired);                          // too early: no fire, still charging (within leave_ms)
        pc.reset();run(ear,1);o=run(ear,40);assert(o.ready);
        o=run(thrust,1);assert(o.fired);
        o=run(thrust,1);assert(!o.fired && !o.charging);           // one shot
        run(ear,40);o=run(low,1);assert(!o.charging);              // lowered blade cancels
        o=run(thrust,1);assert(!o.fired);
        run(ear,40);o=run(away,30);assert(!o.charging);            // wandered off without a thrust: cancelled
        o=run(thrust,1);assert(!o.fired);
        run(ear,40);t+=16;o=pc.update(thrust,true,true,false,t);assert(!o.fired && !o.charging);  // a menu or the wheel: nothing
    }
    std::puts("sword_charge_check ok");
    return 0;
}
