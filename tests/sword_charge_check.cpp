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
    std::puts("sword_charge_check ok");
    return 0;
}
