#include "../src/vr_controls.hpp"
#include "../src/vr_combat.hpp"
#include <cassert>
#include <cstdio>

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
int main() {
    {
        for(int previous=0;previous<2;++previous) {
            const auto next=static_cast<unsigned>(previous)^1u;
            std::array<float,2> grips{},uses{};std::array<bool,2> fresh{},claims{true,true};
            grips[next]=uses[next]=1;fresh[next]=true;
            assert(d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            grips[next]=std::nextafter(.45f,0.f);
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            grips[next]=.45f;grips[static_cast<unsigned>(previous)]=.35f;
            assert(d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            grips[static_cast<unsigned>(previous)]=std::nextafter(.35f,1.f);
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            grips[static_cast<unsigned>(previous)]=0;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,true));
            assert(!d2vr::released_gaze_yields(previous,previous,false,grips,uses,fresh,claims,false));
            assert(!d2vr::released_gaze_yields(previous,static_cast<int>(next),true,grips,uses,fresh,claims,false));
            grips[static_cast<unsigned>(previous)]=1;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            grips[static_cast<unsigned>(previous)]=0;uses[static_cast<unsigned>(previous)]=1;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            uses[static_cast<unsigned>(previous)]=0;fresh[next]=false;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            fresh[next]=true;claims[next]=false;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
            claims[next]=true;grips[next]=0;
            assert(!d2vr::released_gaze_yields(previous,previous,true,grips,uses,fresh,claims,false));
        }
        for(unsigned side=0;side<2;++side) {
            d2vr::InteractOwner grip;
            std::array<float,2> values{};
            grip.update(values,static_cast<int>(side),false,false,true,1000,false,true);
            values[side]=1;
            auto result=grip.update(values,static_cast<int>(side),false,false,true,1010,false,true);
            assert(result.consume_trigger[side] && !result.interact);
            result=grip.update(values,static_cast<int>(side),false,false,true,1700,false,true);
            assert(result.consume_trigger[side] && !result.interact);
            result=grip.update(values,static_cast<int>(side),false,false,true,1710,true,true);
            assert(result.consume_trigger[side] && !result.interact);
            result=grip.update(values,static_cast<int>(side),true,false,true,1800,true,true);
            assert(result.consume_trigger[side] && result.interact);
            grip.update(values,static_cast<int>(side),true,false,false,1810,true,true);
            assert(!grip.update(values,static_cast<int>(side),true,false,true,1820,true,true).interact);
            values[side]=0;
            assert(!grip.update(values,static_cast<int>(side),true,false,true,1830,true,true).interact);
            values[side]=1;
            assert(grip.update(values,static_cast<int>(side),true,false,true,1840,true,true).interact);
        }
        d2vr::InteractOwner trigger;
        trigger.update({0,0},0,false,false,true,1000,false);
        trigger.update({1,0},0,false,false,true,1010,false);
        const auto fallback=trigger.update({1,0},0,false,false,true,1260,false);
        assert(!fallback.consume_trigger[0] && !fallback.interact);
    }
    {
        d2vr::OfferedChokeGrip owner;d2vr::VrCombat combat;
        owner.update(false,false,false,true,1,0,0,false);
        auto route=owner.update(true,true,true,true,1,123,0,false);
        d2vr::CombatFrame frame{};frame.tracked=frame.equipped=true;frame.kill=frame.drop=frame.choke=true;
        frame.head_fwd={0,0,-1};frame.head_right={1,0,0};frame.aim={0,0,-1};frame.trigger=frame.grip=1;
        for(unsigned step=0;step<20;++step) {
            frame.allowed=!route.consume_use;frame.now_ms=1000+step*16;frame.pos={step*.1f,1,-.4f};frame.vel={6,0,0};
            const auto decision=combat.update(frame);
            assert(!decision.rt && !decision.choke && decision.swing==d2vr::SwingKind::none);
            route=owner.update(true,false,true,true,1,0,123,false,true);
        }
        route=owner.update(false,false,false,true,1,0,123,false,true);
        frame.allowed=!route.consume_use;frame.now_ms+=16;
        assert(!combat.update(frame).rt);
        owner.update(false,false,false,true,1,0,0,false);
        frame.allowed=true;frame.now_ms+=16;frame.trigger=frame.grip=0;frame.kill=frame.drop=frame.choke=false;frame.vel={};
        const auto released=combat.update(frame);
        assert(!released.rt && !released.choke && released.swing==d2vr::SwingKind::none);
    }
    {
        d2vr::TalkYOwner owner;
        const d2vr::TalkYOwner::Target talk{0x10000,12,123},other{0x20000,12,456};
        const auto update=[&](bool pressed,std::uint64_t now,d2vr::TalkYOwner::Target target,bool allowed=true,std::uint64_t epoch=1) {
            return owner.update(pressed,allowed,epoch,now,target);
        };
        assert(!update(true,1000,talk).interact);
        update(false,1001,{});
        auto result=update(true,1010,talk);assert(result.consume_y && result.interact);
        assert(owner.owns(talk) && !owner.owns(other));
        auto changed_owner=talk;changed_owner.owner=0x30000;
        assert(!owner.owns(changed_owner));
        result=update(false,1030,talk);assert(result.consume_y && result.interact);
        result=update(false,1170,talk);assert(!result.consume_y && !result.interact);
        result=update(true,1200,{});assert(!result.consume_y && !result.interact);
        result=update(true,1210,talk);assert(!result.consume_y && !result.interact);
        update(false,1220,{});
        assert(update(true,1230,talk).interact);
        result=update(true,1240,other);assert(result.consume_y && !result.interact);
        result=update(true,1250,talk);assert(result.consume_y && !result.interact);
        update(false,1260,{});
        assert(update(true,1270,talk).interact);
        result=update(true,1280,talk,false);assert(result.consume_y && !result.interact && result.owned);
        result=update(true,1290,talk);assert(result.consume_y && !result.interact);
        update(false,1300,{});
        assert(update(true,1310,talk).interact);
        result=update(true,1320,talk,true,2);assert(result.consume_y && !result.interact);
        update(false,1330,{},true,2);
        assert(update(true,1340,talk,true,2).interact);
        owner.cancel();assert(!owner.owns(talk));
        result=update(true,1350,talk,true,2);assert(result.consume_y && !result.interact && result.owned);
    }
    {
        d2vr::OfferedChokeGrip owner;
        const auto update=[&](bool use,bool started,bool held,std::uint32_t offered,std::uint32_t active=0,bool occupied=false,
            bool allowed=true,std::uint64_t epoch=1) {return owner.update(use,started,held,allowed,epoch,offered,active,occupied);};
        assert(!update(true,true,true,123).rb);
        update(false,false,false,0);
        auto result=update(true,true,true,123);
        assert(result.rb && result.consume_use);
        assert(update(true,false,true,0,123).rb);
        result=update(true,false,true,0);
        assert(!result.rb && result.consume_use);
        assert(!update(true,false,true,123).rb);
        update(false,false,false,0);
        assert(!update(true,true,true,0).rb);
        result=update(true,false,true,123);
        assert(!result.rb && !result.consume_use);
        update(false,false,false,0);
        assert(!update(true,true,true,123,0,true).rb);
        update(false,false,false,0);
        assert(update(true,true,true,123).rb);
        assert(!update(true,false,true,456,456).rb);
        update(false,false,false,0);
        assert(!update(true,true,false,123).rb);
        update(false,false,false,0);
        assert(update(true,true,true,123).rb);
        assert(!update(true,false,true,123,0,false,false).rb);
        assert(!update(true,false,true,123).rb);
        update(false,false,false,0);
        assert(update(true,true,true,123).rb);
        assert(!update(true,false,true,123,0,false,true,2).rb);
        update(false,false,false,0,0,false,true,2);
        assert(update(true,true,true,123,0,false,true,2).rb);
        assert(owner.victim()==123);owner.cancel();assert(owner.victim()==0);
        assert(!update(true,false,true,123,0,false,true,2).rb);
    }
    {
        d2vr::GripIntent intent;
        d2vr::OfferedChokeGrip owner;
        owner.update(false,false,false,true,1,0,0,false);
        auto grip=intent.update(1,0,1000,true,100,false);
        assert(!owner.update(grip.use,grip.use_started,true,true,1,123,0,false).rb);
        grip=intent.update(1,0,1100,true,100,false);
        auto route=owner.update(grip.use,grip.use_started,true,true,1,123,0,false);
        assert(route.rb && route.consume_use && !grip.pass_trigger);
        grip=intent.update(1,1,1200,true,100,false);
        route=owner.update(grip.use,grip.use_started,true,true,1,0,123,false,true);
        assert(route.rb && route.consume_use && !grip.fist && !grip.pass_trigger);
        grip=intent.update(0,1,1300,true,100,false);
        route=owner.update(grip.use,grip.use_started,false,true,1,0,123,false,true);
        assert(!route.rb && route.consume_use && !grip.pass_trigger);
        intent.update(0,0,1400,true);
        owner.update(false,false,false,true,1,0,0,false);
        intent.update(1,1,1500,true);
        grip=intent.update(1,1,1600,true);
        route=owner.update(grip.use,grip.use_started,true,true,1,123,0,false);
        assert(grip.fist && !grip.use && !route.rb && !route.consume_use);
    }
    {
        std::array<d2vr::GripIntent,2> intents;
        for(auto& intent:intents) {
            assert(!intent.update(std::nextafter(.45f,0.f),0,1000,true).use);
            assert(intent.kind()==d2vr::GripIntent::Kind::none);
            auto output=intent.update(.45f,0,1010,true);assert(!output.use && !output.fist);
            output=intent.update(.45f,0,1049,true);assert(!output.use && output.pending_use);
            output=intent.update(.45f,0,1050,true);assert(output.use && output.use_started && !output.fist && !output.pending_use);
            output=intent.update(.35f,0,1120,true);assert(output.use && !output.use_started);
            output=intent.update(std::nextafter(.35f,0.f),0,1130,true);assert(!output.use);
            output=intent.update(.45f,std::nextafter(.55f,0.f),1140,true);assert(!output.fist);
            output=intent.update(.45f,.55f,1150,true);assert(output.fist && output.fist_started && !output.pass_trigger);
            output=intent.update(.35f,.35f,1160,true);assert(output.fist && !output.fist_started);
            output=intent.update(std::nextafter(.35f,0.f),std::nextafter(.35f,0.f),1170,true);assert(!output.fist && !output.use);
        }
        d2vr::GripIntent reserved;
        assert(!reserved.update(.45f,0,1000,false).use);
        assert(!reserved.update(.8f,0,1200,true).use);
        reserved.update(0,0,1210,true);
        reserved.update(.45f,0,1220,true);
        assert(reserved.update(.45f,0,1320,true).use_started);
    }
    {   // v613 item 4: a physical grab hands off to the carry (Use) when told; the Use is held only until the carry shows (or 500 ms); the release of a confirmed carry (either hand) drops it
        using K=d2vr::GripIntent::Kind;using Choice=d2vr::GripAloneChoice;
        auto upd=[&](d2vr::GripIntent& i,std::uint64_t now,float grip,bool handoff,bool active){return i.update(grip,0.f,now,true,100,false,400,40,Choice::grab,true,150,handoff,active);};
        d2vr::GripIntent i;std::uint64_t now=1000;
        auto o=upd(i,now,1.f,false,false);assert(o.grab_started && i.kind()==K::grab);
        o=upd(i,now+=20,1.f,false,false);assert(o.grab && !o.use);
        o=upd(i,now+=20,1.f,true,false);assert(i.kind()==K::carry && o.use_started && o.use && !o.grab);
        o=upd(i,now+=20,1.f,true,false);assert(o.use && !o.use_started);
        o=upd(i,now+=20,1.f,false,true);assert(!o.use && !o.drop_started);                  // the carry started: the Use press ends
        o=upd(i,now+=20,0.f,false,true);assert(o.drop_started && !o.use);                    // release of the confirmed carry: drop
        // the game never starts the carry: the Use is held 1.5 s, then dropped; a later release does nothing
        d2vr::GripIntent j;now=5000;upd(j,now,1.f,false,false);upd(j,now+=20,1.f,true,false);assert(j.kind()==K::carry);
        o=upd(j,now+=1400,1.f,false,false);assert(o.use);o=upd(j,now+=200,1.f,false,false);assert(!o.use && !o.drop_started);
        o=upd(j,now+=20,0.f,false,false);assert(!o.drop_started);
        // released inside the state lag: the carry shows up 200 ms later and is dropped then, once
        d2vr::GripIntent k;now=9000;upd(k,now,1.f,false,false);upd(k,now+=20,1.f,true,false);
        o=upd(k,now+=100,0.f,false,false);assert(!o.drop_started);o=upd(k,now+=100,0.f,false,true);assert(o.drop_started);o=upd(k,now+=20,0.f,false,true);assert(!o.drop_started);
        // released with no carry and the wait over: nothing; never a carry while one is running
        d2vr::GripIntent m;now=12000;upd(m,now,1.f,false,false);upd(m,now+=20,1.f,true,false);o=upd(m,now+=1600,0.f,false,false);o=upd(m,now+=20,0.f,false,true);assert(!o.drop_started);
        d2vr::GripIntent z;now=20000;o=z.update(1.f,0.f,now,true,100,false,400,40,Choice::none,true,150);assert(z.kind()==K::drain && !o.use && !o.grab);   // V1: a corpse with nothing in reach: no Use, no grab
        d2vr::GripIntent n;now=15000;upd(n,now,1.f,false,false);upd(n,now+=20,1.f,true,true);assert(n.kind()==K::grab);
    }
    {   // GripIntent: grip alone = Use after 40 ms (trigger finger off the trigger; else 100 ms), grip + trigger = fist, trigger first is held back, taps survive
        using K=d2vr::GripIntent::Kind;
        d2vr::GripIntent a;auto o=a.update(1,0,1000,true);assert(!o.use && !o.fist && a.kind()==K::deciding);
        o=a.update(1,0,1030,true);assert(!o.use && o.pending_use);o=a.update(1,0,1040,true);assert(o.use && o.use_started && o.pass_trigger==false);
        o=a.update(1,1,1200,true);assert(o.use && !o.fist && !o.pass_trigger); // a trigger during a Use is not a fist or a cast
        o=a.update(0,1,1300,true);assert(!o.use && !o.pass_trigger && a.kind()==K::drain);o=a.update(0,0,1400,true);assert(a.kind()==K::none && o.pass_trigger);
        d2vr::GripIntent b;o=b.update(1,0,0,true);o=b.update(1,1,40,true);assert(o.fist && o.fist_started && !o.use && !o.pass_trigger);
        o=b.update(1,0,500,true);assert(o.fist);o=b.update(0,0,600,true);assert(!o.fist && b.kind()==K::none); // relaxing the trigger keeps the fist
        d2vr::GripIntent c;o=c.update(0,1,0,true);assert(!o.pass_trigger);o=c.update(1,1,60,true);assert(o.fist && !o.pass_trigger); // trigger first, grip joins
        d2vr::GripIntent d;o=d.update(0,1,0,true);o=d.update(0,1,100,true);assert(o.pass_trigger && !o.use && d.kind()==K::trigger); // a cast after 100 ms
        o=d.update(1,1,150,true);assert(!o.fist && o.pass_trigger); // a grip during a cast is nothing
        d2vr::GripIntent e;o=e.update(0,1,0,true);o=e.update(0,0,30,true);assert(o.tap_trigger && o.pass_trigger);o=e.update(0,0,100,true);assert(!o.tap_trigger); // trigger tap
        d2vr::GripIntent t;o=t.update(1,0,0,true);o=t.update(0,0,40,true);assert(o.use && o.use_started);o=t.update(0,0,200,true);assert(o.use);o=t.update(0,0,300,true);assert(!o.use); // grip tap = short Use
        // A late trigger while the Use has nothing to act on becomes the fist; with a target, or too late, the Use stays.
        d2vr::GripIntent l;o=l.update(1,0,0,true,100,true);o=l.update(1,0,100,true,100,true);assert(o.use);
        o=l.update(1,1,300,true,100,true);assert(o.fist && o.fist_started && !o.use);
        d2vr::GripIntent m;o=m.update(1,0,0,true);o=m.update(1,0,100,true);o=m.update(1,1,300,true,100,false);assert(o.use && !o.fist);
        d2vr::GripIntent n;o=n.update(1,0,0,true,100,true);o=n.update(1,0,100,true,100,true);o=n.update(1,1,600,true,100,true);assert(o.use && !o.fist);
        // A finger already on the trigger (>= .05) keeps the full 100 ms window; the quick commit never beats the window for a fist.
        d2vr::GripIntent q;o=q.update(1,.1f,0,true);o=q.update(1,.1f,60,true);assert(!o.use && q.kind()==K::deciding);o=q.update(1,.1f,100,true);assert(o.use_started);
        d2vr::GripIntent r;o=r.update(1,0,0,true);o=r.update(1,.04f,39,true);assert(!o.use);o=r.update(1,0,40,true);assert(o.use_started);
        d2vr::GripIntent f;o=f.update(1,0,0,true);o=f.update(1,.6f,30,true);assert(o.fist && !o.use);
        d2vr::GripIntent w;o=w.update(1,1,0,false);assert(!o.fist && !o.use && o.pass_trigger);o=w.update(1,1,10,true);assert(!o.fist && w.kind()==K::drain); // weapon hand
    }
    {   // G3 (plan Task 6): the one-list decision replaces the 40 ms quick Use. grab -> Kind::grab (no trigger passed, ends on release); use -> Use
        // at once; pending keeps deciding, then Use after the window; a trigger joining while deciding is still a fist; one_list=false is today.
        using K=d2vr::GripIntent::Kind;using C=d2vr::GripAloneChoice;d2vr::GripIntent::Out o;
        d2vr::GripIntent g;assert(g.decision_since(500)==500);
        o=g.update(1,0,1000,true,100,false,400,40,C::grab,true);
        assert(g.kind()==K::grab && o.grab && o.grab_started && !o.use && !o.fist && !o.pass_trigger && g.decision_since(1010)==1010);
        o=g.update(1,0,1010,true,100,false,400,40,C::use,true);assert(o.grab && !o.grab_started && !o.use);   // decided once
        o=g.update(1,1,1050,true,100,false,400,40,C::grab,true);assert(o.grab && !o.fist && !o.pass_trigger);   // a trigger during a grab is not a cast
        o=g.update(0,1,1100,true,100,false,400,40,C::grab,true);assert(!o.grab && g.kind()==K::drain && !o.pass_trigger);
        o=g.update(0,0,1150,true,100,false,400,40,C::grab,true);assert(g.kind()==K::none && o.pass_trigger);
        d2vr::GripIntent u;o=u.update(1,0,0,true,100,false,400,40,C::use,true);assert(o.use && o.use_started && u.kind()==K::use && !o.grab);
        d2vr::GripIntent p;o=p.update(1,0,0,true,100,false,400,40,C::pending,true);assert(p.kind()==K::deciding && o.pending_use && !o.use && !o.grab);
        assert(p.decision_since(50)==0);
        o=p.update(1,0,60,true,100,false,400,40,C::pending,true);assert(!o.use && p.kind()==K::deciding);   // no 40 ms quick Use with one_list
        o=p.update(1,0,99,true,100,false,400,40,C::pending,true);assert(!o.use);
        o=p.update(1,0,100,true,100,false,400,40,C::pending,true);assert(o.use && o.use_started && p.kind()==K::use);   // 100 ms: pending -> use
        d2vr::GripIntent b;o=b.update(1,0,0,true,100,false,400,40,C::pending,true,250);   // the H1 buffer: the caller allows pending to 250 ms
        o=b.update(1,0,120,true,100,false,400,40,C::pending,true,250);assert(b.kind()==K::deciding && !o.use);
        o=b.update(1,0,130,true,100,false,400,40,C::grab,true,250);assert(o.grab_started && b.kind()==K::grab);
        d2vr::GripIntent c;o=c.update(1,0,250,true,100,false,400,40,C::pending,true,250);o=c.update(1,0,500,true,100,false,400,40,C::pending,true,250);
        assert(o.use_started);
        d2vr::GripIntent f;o=f.update(1,0,0,true,100,false,400,40,C::pending,true);o=f.update(1,1,60,true,100,false,400,40,C::pending,true);
        assert(o.fist && o.fist_started && !o.use && !o.grab);   // a trigger at 60 ms makes the fist
        d2vr::GripIntent l;o=l.update(1,0,0,true,100,true,400,40,C::use,true);assert(o.use);   // the late fist is unchanged
        o=l.update(1,1,300,true,100,true,400,40,C::use,true);assert(o.fist && o.fist_started && !o.use);
        d2vr::GripIntent t;o=t.update(0,1,0,true,100,false,400,40,C::grab,true);assert(!o.pass_trigger && t.kind()==K::deciding);   // trigger first: today
        o=t.update(0,1,100,true,100,false,400,40,C::grab,true);assert(t.kind()==K::trigger && !o.grab);
        d2vr::GripIntent legacy;o=legacy.update(1,0,0,true,100,false,400,40,C::grab,false);o=legacy.update(1,0,40,true,100,false,400,40,C::grab,false);
        assert(o.use_started && !o.grab && legacy.kind()==K::use);   // one_list=false (build/no-grip-alone-grab): the 40 ms quick Use, no grab
    }
    {
        d2vr::XWheelOwner x;
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"idle X stays closed");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"a fresh allowed X opens the wheel");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"held X keeps the wheel open");
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"release closes the wheel for the native equip");
        x.update(true,true);
        require(x.wheel(),"a released X can open the wheel again");
        x.update(true,false);
        require(!x.wheel() && x.x_consumed(),"lost allowed cancels at once and keeps the press claimed");
        x.update(true,true);
        require(!x.wheel() && x.x_consumed(),"allowed returning mid-press never reopens");
        x.update(false,true);
        require(!x.x_consumed(),"the cancelled claim ends on release");
        x.update(true,false);
        require(!x.wheel() && !x.x_consumed(),"a press that starts disallowed is ignored");
        x.update(true,true);
        require(!x.wheel() && !x.x_consumed(),"an ignored press never late-opens");
        x.update(false,true);x.update(true,true);
        require(x.wheel(),"release re-arms the fresh press edge");
        x.reset();
        require(!x.wheel() && !x.x_consumed(),"reset clears the owner");
        x.update(true,true);
        require(x.wheel(),"reset does not latch X");
    }
    {   // Two-handed wheel: left X at once, right A after 350 ms; first hand owns until release.
        d2vr::WheelHands w;
        auto u=[&](bool x,bool a,unsigned long long t,bool allowed=true) {w.update(x,a,allowed,t);};
        u(0,0,0);
        require(!w.wheel() && w.hand()==-1,"idle");
        u(1,0,100);
        require(w.wheel() && w.hand()==0 && !w.a_consumed(),"X opens the wheel for the left hand");
        u(1,1,200);u(1,1,900);
        require(w.hand()==0 && !w.a_consumed(),"A cannot steal or double-open while X owns it");
        u(0,1,950);u(0,1,1400);
        require(!w.wheel() && !w.a_consumed(),"X release closes; the A held through it never late-opens");
        u(0,0,1500);u(0,1,2000);u(0,0,2100);
        require(!w.wheel() && !w.a_consumed(),"A tap stays a native jump");
        u(0,1,3000);u(0,1,3300);
        require(!w.wheel() && !w.a_consumed(),"A before the hold stays native");
        u(0,1,3360);
        require(w.wheel() && w.hand()==1 && w.a_consumed(),"A hold opens for the right hand");
        u(1,1,3400);
        require(w.hand()==1,"X cannot take over while A owns it");
        u(1,0,3500);u(1,0,3600);
        require(!w.wheel() && !w.a_consumed(),"A release closes; X pressed during A stays ignored");
        u(0,0,3800);u(1,0,4000);u(1,0,4100,false);u(1,0,4200);
        require(!w.wheel(),"lost allowed cancels, no reopen");
        u(0,0,4300);u(0,1,5000);u(0,1,5100,false);u(0,1,5500);
        require(!w.wheel() && !w.a_consumed(),"A disallowed during the hold never opens");
        float tx=.1f,ty=0,mx=0,my=-1;
        d2vr::wheel_select(1,tx,ty,mx,my,true,1,0);
        require(tx==0 && ty==-1 && mx==0 && my==0,"right-owned: left stick selects, move dropped");
        tx=.9f;ty=0;mx=0;my=1;
        d2vr::wheel_select(0,tx,ty,mx,my,true,0,1);
        require(tx==.9f && ty==0 && my==0,"left-owned: right stick wins over dial and left stick is dropped");
        tx=ty=0;d2vr::wheel_select(1,tx,ty,mx,my,true,0,1);
        require(tx==0 && ty==1,"hand dial fills in when the stick is idle");
    }
    {   // Pointing cursor: either hand selects regardless of which button opened the wheel.
        using V=std::array<double,3>;
        d2vr::WheelCursor c;d2vr::WheelHands w;float x=0,y=0;
        const V rt{1,0,0};V L{-.2,1,-.3},R{.2,1,-.3};
        auto step=[&](bool tl=true,bool tr=true) {return c.update(true,{tl,tr},{L,R},rt,x,y);};
        w.update(true,false,true,0);
        require(w.hand()==0 && !step(),"left X opens; nothing selected at open");
        R={.2,1.01,-.3};step();R={.2,1.02,-.3};step();R={.2,1.05,-.3};
        require(step() && c.cursor()==1 && y>.99f && x==0,"right hand selects up while left X opened");
        R={.2,1.06,-.3};
        require(step() && c.cursor()==1,"cursor hand stays");
        require(!c.commit(true,{0,0}) && c.commit(true,{0,.9f}) && !c.commit(true,{0,.9f}),"fresh right trigger commits once");
        w.commit();
        require(!w.wheel(),"commit closes the wheel");
        w.update(false,false,true,10);
        w.update(false,true,true,20);w.update(false,true,true,400);
        require(w.hand()==1,"right A opens");
        c.update(false,{1,1},{L,R},rt,x,y);
        L={-.2,1,-.3};R={.2,1,-.3};step();
        L={-.25,1,-.3};step();L={-.3,1,-.3};
        require(step() && c.cursor()==0 && x<-.99f && y==0,"left hand selects left while right A opened");
        L={-.31,1,-.3};R={.21,1,-.3};
        require(step() && c.cursor()==0,"slower other hand does not steal");
        L={-.2,1,-.3};R={.2,1,-.3};c.update(false,{1,1},{L,R},rt,x,y);
        step();L={-.2,1.01,-.3};
        require(!step() && c.cursor()==0,"inside dead zone is neutral");
        float tx=0,ty=0,mx=0,my=1;
        d2vr::wheel_select(1,tx,ty,mx,my,true,1,0);
        require(tx==0 && ty==1,"stick deflection overrides the cursor");
        c.update(false,{1,1},{L,R},rt,x,y);
        require(!c.commit(true,{0,.9f}),"trigger held from before the press edge never commits without a cursor");
    }
    {
        d2vr::InteractOwner o;
        const std::array<float,2> idle{0,0};
        auto settle=[&](int hand,bool target,bool wheel,bool allowed) {o.update(idle,hand,target,wheel,allowed);};
        auto r=o.update(idle,1,true,false,true);
        require(!r.interact && !r.consume_trigger[0] && !r.consume_trigger[1],"idle triggers own nothing");
        r=o.update({0,0.5f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a pull under the press threshold starts no role");
        r=o.update({0,0.6f},1,true,false,true);
        require(r.interact && r.consume_trigger[1] && !r.consume_trigger[0],"the active hand with a target interacts and consumes");
        r=o.update({0,0.4f},1,true,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role survives the release hysteresis band");
        r=o.update({0,0.3f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"interact ends below the release threshold");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,false,false,true);
        require(!r.interact && !r.consume_trigger[1],"no target is native passthrough");
        r=o.update({0,0.7f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a target appearing mid-press cannot upgrade a native role");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the native role ends on release");
        settle(1,true,false,true);
        r=o.update({0.6f,0},1,true,false,true);
        require(!r.interact && !r.consume_trigger[0],"a non-active trigger is native");
        o.reset();settle(0,true,false,true);
        r=o.update({0.6f,0.6f},0,true,false,true);
        require(r.interact && r.consume_trigger[0] && !r.consume_trigger[1],"only the active side becomes an interact");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && !r.consume_trigger[1],"an interact press needs allowed at the press edge");
        o.reset();settle(1,true,false,true);
        r=o.update({0.6f,0.7f},1,true,true,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"an open wheel consumes either trigger");
        r=o.update({0.6f,0.7f},1,true,false,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"the wheel role latches past the wheel closing");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"an interact press starts");
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && r.consume_trigger[1],"allowed false cancels the interact but keeps consuming");
        r=o.update({0,0.6f},1,true,false,true);
        require(!r.interact && r.consume_trigger[1],"the cancelled press never resumes");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the cancelled press frees on release");
        require(o.update({0,0.6f},1,true,false,true).interact,"a fresh press interacts again");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"interact starts with a target");
        r=o.update({0,0.6f},1,false,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role latches when the target disappears");
        d2vr::InteractOwner held;
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation is consumed, not owned");
        for(int i=0;i<5;++i) r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation never starts a role");
        require(!held.update({0,0},0,true,false,true).consume_trigger[0],"release clears the created-hold drain");
        require(held.update({0.8f,0},0,true,false,true).interact,"a fresh press after release owns the interact");
        held.reset();
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"reset under a held trigger drains it");
        require(!held.update({0.4f,0},0,true,false,true).interact,"a partly released trigger stays in the drain");
        require(!held.update({0.3f,0},0,true,false,true).consume_trigger[0],"below the release threshold the drain ends");
        require(held.update({0.8f,0},0,true,false,true).interact,"a post-reset release allows a fresh interact");
    }
    if(failures) {std::printf("%d requirement(s) failed\n",failures);return 1;}
        {
        // A non-active trigger waits for its own ray, then interacts or fires.
        d2vr::InteractOwner owner;owner.update({0,0},1,false,false,true,1);
        auto r=owner.update({1,0},1,false,false,true,100);
        if(!r.consume_trigger[0] || r.interact) {std::puts("FAIL: pending press not consumed");return 1;}
        r=owner.update({1,0},0,true,false,true,170);
        if(!r.interact) {std::puts("FAIL: pending press did not become interact");return 1;}
        owner.update({0,0},0,false,false,true,200);
        r=owner.update({1,0},1,false,false,true,300);r=owner.update({1,0},1,false,false,true,560);
        if(r.consume_trigger[0]) {std::puts("FAIL: pending press did not fall back to native");return 1;}
    }
std::printf("PASS: X wheel open/close/cancel/no-reopen/late-press, "
        "interact role latch/cancel-consume/held-at-reset and native passthrough\n");
    {
        d2vr::InteractOwner owner;owner.update({0,0},1,false,false,true,1,false);
        auto r=owner.update({0,1},1,false,false,true,100,false);
        require(r.consume_trigger[1] && !r.interact,"selected hand waits for fresh query after locking");
        r=owner.update({0,1},1,true,false,true,170,false);
        require(r.consume_trigger[1] && !r.interact,"stale selected target cannot settle pending grip");
        r=owner.update({0,1},1,true,false,true,180,true);
        require(r.consume_trigger[1] && r.interact,"fresh locked-hand query settles into interact");
        owner.update({0,0},1,false,false,true,200,false);
        r=owner.update({0,1},1,false,false,true,300,false);
        r=owner.update({0,1},1,false,false,false,350,false);
        require(r.consume_trigger[1] && !r.interact,"focus loss drains pending grip");
        r=owner.update({0,1},1,true,false,true,400,true);
        require(r.consume_trigger[1] && !r.interact,"focus returning never replays pending grip");
    }
    {   // Laser pointer: head at origin facing -Z, plane 1 m ahead, radius .25 m.
        d2vr::WheelPointer p;float x=0,y=0;std::array<double,3> hit{};bool vis=false;
        const std::array<double,3> head{0,1.6,0},fwd{0,0,-1};
        std::array<std::array<double,3>,2> pos{{{.02,1.6,0},{.2,1.6,0}}},dir{{{0,0,-1},{0,0,-1}}};
        require(!p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && vis,"centre hit is neutral but shows a dot");
        require(std::abs(hit[0]-.02)<1e-9 && std::abs(hit[1]-1.6)<1e-9 && std::abs(hit[2]+1)<1e-9,"dot is on the plane at the ray hit");
        pos[1]={.2,1.6,0};dir[1]={.25,0,-1}; // right hand aims +.25 m right at 1 m
        require(p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && p.cursor()==1,"moving hand becomes the cursor");
        require(x>.99f && std::abs(y)<.01f,"right of centre selects the right wedge");
        dir[1]={0,.25,-1};pos[1]={.2,1.6,0};
        require(p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && y>.3f && x>0,"upward ray gives positive y");
        {   // Stability: 2 mm tremor around the right/up-right wedge border never flips the sector or moves the dot much.
            d2vr::WheelPointer q;float qx=0,qy=0;std::array<double,3> h0{},h1{};bool v=false;
            std::array<std::array<double,3>,2> qp{{{0,1.6,0},{0,1.6,0}}},qd{{{.2,0,-1},{.2,0,-1}}};
            q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h0,v);
            const float sx=qx,sy=qy;int flips=0;double drift=0;
            for(int i=0;i<60;++i) {
                const double j=(i&1?1:-1)*.002; // +-2 mm jitter at 1 m, and also across the 22.5 deg border
                qd[0]={.2+j,j*2,-1};qd[1]=qd[0];
                q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
                flips+=(qx!=sx||qy!=sy);drift=std::max(drift,std::abs(h1[0]-.2));
            }
            require(flips==0 && drift<.004,"tremor keeps the sector and the dot steady");
            // Hysteresis: at 25 deg (3 deg past the 22.5 edge) the wedge holds; at 35 deg it switches.
            qd[0]=qd[1]={.2,.2*std::tan(25*3.14159265/180),-1};
            q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
            require(qx==sx && qy==sy,"within 8 deg past the border the wedge holds");
            qd[0]=qd[1]={.2,.2*std::tan(35*3.14159265/180),-1};
            for(int i=0;i<30;++i) q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
            require(qy>.6f,"clearly inside the next wedge switches");
        }
        require(!p.update(true,{true,true},pos,{std::array<double,3>{0,0,1},std::array<double,3>{0,0,1}},head,fwd,0,x,y,hit,vis) && !vis,"rays pointing away show nothing");
        p.update(false,{true,true},pos,dir,head,fwd,0,x,y,hit,vis);
        require(!p.commit(true,{0,0}),"idle trigger does not commit");
        p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis);
        require(p.commit(true,{1,0}) && !p.commit(true,{1,0}),"fresh trigger on the cursor hand commits once");
    }
    {   // Wheel face buttons: fresh presses pass while open; held-through open/close stay masked.
        d2vr::WheelFaceButtons f;
        auto o=f.update(false,false,true,true);
        require(!o.y && o.active,"Y held at wheel open is masked");
        o=f.update(false,true,true,true);
        require(o.b && !o.y && !o.a,"fresh B passes while Y stays masked");
        o=f.update(true,true,false,true);
        require(o.a && o.b && !o.y,"A and B pass natively in the wheel");
        o=f.update(true,false,false,false);
        require(!o.a && !o.b && o.active,"A held through close is masked");
        o=f.update(true,false,false,false);
        require(!o.a && o.active,"still masked while held");
        o=f.update(false,false,false,false);
        require(!o.active,"release clears the mask");
        o=f.update(true,false,false,false);
        require(!o.active && !o.a,"closed wheel is never forced to pass");
    }
    if(failures) return 1;
        {   // A tap jumps on release; a hold (wheel) never jumps.
        d2vr::WheelHands h;
        require(!h.a_passthrough(true,1000) && !h.a_passthrough(true,1100),"A press held back");
        require(h.a_passthrough(false,1150) && h.a_passthrough(false,1200) && !h.a_passthrough(false,1240),"A tap pulses on release");
        require(!h.a_passthrough(true,2000) && !h.a_passthrough(true,2400) && !h.a_passthrough(false,2500),"A hold never jumps");
    }
    { // Journal: a hand at the left temple + grip edge -> one 150 ms pulse; the squeeze is consumed until release.
        d2vr::TempleReach r;const std::array<double,3> head{0,1.7,0},fwd{0,0,-1},up{0,1,0};
        std::array<std::optional<std::array<double,3>>,2> front{std::array<double,3>{-.2,1.2,-.4},std::array<double,3>{.2,1.2,-.4}};
        std::array<std::optional<std::array<double,3>>,2> temple{std::array<double,3>{-.1,1.72,-.02},front[1]};
        std::array<std::optional<std::array<double,3>>,2> right_temple{front[0],std::array<double,3>{.1,1.72,-.02}};
        assert(!r.update(head,fwd,up,front,{1.f,0.f},true,0).journal);                 // squeezing in front: not the journal
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,10).journal);
        assert(!r.update(head,fwd,up,right_temple,{0.f,1.f},true,15).journal);        // right temple: not the journal
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,16).journal);
        const auto e=r.update(head,fwd,up,temple,{0.f,0.f},true,20);assert(e.entered==0 && !e.journal);
        const auto j=r.update(head,fwd,up,temple,{1.f,0.f},true,30);assert(j.journal && j.consume[0]);
        assert(r.update(head,fwd,up,front,{1.f,0.f},true,100).consume[0]);            // still held after leaving: still consumed
        assert(!r.update(head,fwd,up,front,{1.f,0.f},true,200).journal);
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,210).consume[0]);
        assert(!r.update(head,fwd,up,temple,{1.f,0.f},false,300).journal);            // not in gameplay
    }
    { // Full-grip sword: draw at the shoulder, hold, drop elsewhere, pick up, sheathe at the shoulder, auto-sheathe, recall.
        using A=d2vr::SwordGrip::Action;d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;
        auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
        assert(step(0).action==A::none);
        in.zone=true;in.grip=1;in.draw_dot=.7;{const auto o=step(10);assert(o.action==A::draw && !o.reversed && o.owns);}
        in.zone=false;assert(step(200).action==A::none && s.held());                     // drawing: still in the hand
        in.drawn=true;assert(step(400).action==A::none);assert(step(2000).action==A::none); // held: never auto-sheathed
        in.grip=0;assert(step(2100).action==A::drop && !s.held());                       // released away from the shoulder
        in.copy=true;in.copy_distance=1;assert(step(2150).action==A::none);              // copy spawned, sword still leaving
        assert(step(2400).action==A::stow && step(2500).action==A::none && step(2700).action==A::stow); // the game kept it: again
        in.drawn=false;assert(step(2800).action==A::none);
        in.copy_reach=true;in.pickup_dot=-.5;in.grip=1;{const auto o=step(3000);assert(o.action==A::pickup && o.reversed);} // against the blade
        in.copy=in.copy_reach=false;in.drawn=true;assert(step(3400).action==A::none);
        in.zone=true;in.grip=0;assert(step(3500).action==A::sheathe);                     // released at the shoulder
        in.zone=false;in.drawn=false;assert(step(3600).action==A::none);
        in.zone=true;in.grip=1;in.draw_dot=-.8;assert(step(4000).reversed);              // fist down at the shoulder: reverse
        in.grip=0;assert(step(4050).action==A::sheathe);                                 // let go before it was out: put back
        in.zone=false;in.drawn=true;assert(step(5000).action==A::none);                  // the game drew it, nobody holds it
        assert(step(5700).action==A::sheathe && step(5800).action==A::none);             // put away once
        in.drawn=false;in.copy=true;in.copy_age_s=1;assert(step(6000).action==A::none);
        in.drawn=true;assert(step(6100).action==A::recall);                             // drawn again while its copy lies around
        in.drawn=false;in.copy_age_s=31;assert(step(7200).action==A::recall);           // left lying too long
        in.copy_age_s=1;in.grip=1;in.busy=true;assert(step(8000).action==A::none);       // carrying: the grip is not the sword
        in.busy=false;in.gameplay=false;in.grip=0;assert(step(8100).action==A::none);
        in.gameplay=true;in.copy=false;in.grip=1;in.drawn=true;assert(step(9000).action==A::none && s.held()); // gripping a drawn sword holds it
        in.gameplay=false;assert(step(9100).action==A::none);in.gameplay=true;assert(step(10000).action==A::none && s.held()); // wheel opened and closed
        in.gameplay=false;in.grip=0;assert(step(10100).action==A::none);in.gameplay=true;
        {const auto o=step(10200);assert(o.action==A::sheathe && !s.held());}           // let go inside the wheel: put away, not thrown
        in.grip=1;in.drawn=false;assert(step(11000).action==A::none);                   // a corpse held: the grip is down before the game draws
        in.drawn=true;assert(step(11100).action==A::none && !s.held());                 // drawn by the game under that grip: not taken
        assert(step(11800).action==A::sheathe);                                         // put away
        in.grip=0;in.drawn=false;assert(step(11900).action==A::none);                   // letting go of the corpse drops no sword
        // v692: a squeeze on something to use is that Use, not the game-drawn sword nor the lying copy
        in.drawn=true;in.use_target=true;in.grip=1;{const auto o=step(13000);assert(o.action==A::none && !s.held() && !o.owns);}
        in.grip=0;in.drawn=false;step(13100);
        in.copy=true;in.copy_reach=true;in.copy_age_s=1;in.copy_distance=.3;in.grip=1;{const auto o=step(13200);assert(o.action!=A::pickup && !o.owns);}
        in.grip=0;in.use_target=false;step(13300);in.grip=1;assert(step(13400).action==A::pickup); // nothing to use: the copy is taken
    }
    { // G2 regrab: the pickup dead band, and the grip changes only on a draw or pickup edge.
        using A=d2vr::SwordGrip::Action;static_assert(d2vr::SwordGrip::pickup_dead_band==.35);
        { // pickup_dead_band_keeps_previous: a reversed draw, a drop, then a floor pickup at pickup_dot .2 is still reversed
            d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
            in.zone=true;in.grip=1;in.draw_dot=-.8;{const auto o=step(10);assert(o.action==A::draw && o.reversed);}
            in.zone=false;in.drawn=true;step(400);
            in.grip=0;assert(step(2000).action==A::drop);
            in.drawn=false;in.copy=true;in.copy_distance=1;in.copy_reach=true;step(2500);
            in.pickup_dot=.2;in.grip=1;{const auto o=step(3000);assert(o.action==A::pickup && o.reversed);}
            in.pickup_dot=-.2;in.grip=0;in.copy=in.copy_reach=false;in.drawn=true;step(3400);in.grip=0;step(3500);
        }
        { // first_pickup_in_dead_band_is_forward
            d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
            in.copy=true;in.copy_reach=true;in.copy_distance=1;in.pickup_dot=-.3;in.grip=1;{const auto o=step(100);assert(o.action==A::pickup && !o.reversed);}
            d2vr::SwordGrip z;in={};in.gameplay=true;in.copy=true;in.zone=true;in.draw_dot=.3;in.grip=1;in.now=100;{const auto o=z.update(in);assert(o.action==A::pickup && !o.reversed);}
        }
        { // perpendicular_regrab_keeps_previous: pickup_dot alternating +-.2 over 50 frames of one hold never changes `reversed`
            for(const bool first_reversed:{false,true}) {
                d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
                in.copy=true;in.copy_reach=true;in.copy_distance=1;in.pickup_dot=first_reversed?-.8:.8;in.grip=1;assert(step(10).reversed==first_reversed);
                in.grip=0;in.drawn=true;in.copy=in.copy_reach=false;step(100);in.drawn=false;in.copy=true;in.copy_reach=true;step(2000);
                in.grip=1;for(int f=0;f<50;++f) {in.pickup_dot=f%2?.2:-.2;const auto o=step(3000+f*11);assert(o.reversed==first_reversed);}
            }
        }
        { // reverse_changes_only_on_edges: while held, draw_dot / pickup_dot sweeping -1..1 leaves `reversed` unchanged
            for(const bool rev:{false,true}) {
                d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
                in.zone=true;in.grip=1;in.draw_dot=rev?-.8:.8;assert(step(10).reversed==rev);in.drawn=true;
                for(int f=0;f<=40;++f) {in.draw_dot=in.pickup_dot=-1+f*.05;in.zone=f%3==0;in.copy=f%4==0;in.copy_reach=in.copy;assert(step(100+f*11).reversed==rev);}
            }
        }
    }
    { // Full-grip gun: the wheel's gun stays until gripped; let go anywhere: holster; squeeze at the hip: draw.
        using A=d2vr::GunGrip::Action;d2vr::GunGrip g;d2vr::GunGrip::In in{};in.gameplay=true;
        auto step=[&](std::uint64_t t){in.now=t;return g.update(in);};
        in.drawn=true;in.loaded=true;assert(step(0).action==A::none && step(5000).action==A::none); // wheel draw: kept
        in.grip=1;assert(step(5100).action==A::none && g.held());
        in.grip=0;assert(step(5200).action==A::holster);                                   // let go in front: back to the hip
        in.drawn=false;in.empty=true;assert(step(5600).action==A::none);
        in.grip=1;assert(step(6000).action==A::none);in.grip=0;step(6100);                 // squeeze away from the hip: nothing
        in.zone=true;in.grip=1;in.draw_authorized=true;
        {const auto o=step(7000);assert(o.action==A::none && !o.owns && o.reserved);}
        {const auto o=step(7099);assert(o.action==A::none && o.reserved);}
        {const auto o=step(7100);assert(o.action==A::draw && o.owns && !o.reserved);}
        in.zone=false;in.empty=false;in.drawn=true;assert(step(7400).action==A::none);
        in.zone=true;in.grip=0;assert(step(8000).action==A::holster);
        in.drawn=false;in.empty=true;in.loaded=false;in.grip=1;assert(step(9000).action==A::none); // the hip holds no gun
        in.grip=0;in.empty=false;in.drawn=true;in.busy=true;in.grip=1;assert(step(9500).action==A::none);
    }
    {
        using A=d2vr::GunGrip::Action;
        for(unsigned variant=0;variant<5;++variant) {
            d2vr::GunGrip gun;d2vr::GripIntent intent;
            d2vr::GunGrip::In in{};in.gameplay=in.zone=in.empty=in.loaded=true;
            const auto step=[&](std::uint64_t now,float grip,float trigger,bool object=false) {
                const auto classified=intent.update(grip,trigger,now,!gun.held(),100,true);
                in.now=now;in.grip=grip;in.trigger=trigger;in.object_claim=object;
                in.draw_authorized=classified.use && !classified.fist;
                return gun.update(in);
            };
            step(1000,0,0);
            assert(step(1010,1,variant==0?1.f:0.f,variant==3).action==A::none);
            if(variant==0 || variant==1) {
                assert(step(1109,1,1).action==A::none);
                assert(step(1110,1,1).action==A::none);
                assert(intent.kind()==d2vr::GripIntent::Kind::fist);
                assert(step(1200,1,0).action==A::none);
            } else if(variant==2 || variant==3) {
                assert(step(1109,1,0,true).action==A::none);
                assert(step(1110,1,0).action==A::none);
                assert(step(1500,1,0).action==A::none);
            } else {
                assert(step(1110,1,0).action==A::draw);
                assert(step(1120,1,1).action==A::none);
                assert(intent.kind()==d2vr::GripIntent::Kind::drain && gun.held());
                in.drawn=true;
                assert(step(1200,0,0).action==A::holster);
            }
        }
        d2vr::GunGrip gun;d2vr::GunGrip::In in{};
        in.gameplay=in.empty=in.loaded=true;in.grip=1;in.now=1000;in.draw_authorized=true;
        assert(gun.update(in).action==A::none);
        in.zone=true;in.now=1500;assert(gun.update(in).action==A::none);
        in.grip=0;gun.update(in);in.grip=1;in.now=1600;in.draw_authorized=false;gun.update(in);
        in.now=1700;assert(gun.update(in).action==A::none);
        in.busy=true;gun.update(in);in.busy=false;in.draw_authorized=true;in.now=1800;
        assert(gun.update(in).action==A::none);
        in.grip=0;gun.update(in);in.grip=1;in.now=1900;gun.update(in);in.gameplay=false;gun.update(in);
        in.gameplay=true;in.now=2100;assert(gun.update(in).action==A::none);
    }
    {
        using A=d2vr::GunGrip::Action;
        for(unsigned rejection=0;rejection<3;++rejection) {
            d2vr::GunGrip gun;d2vr::GunGrip::In in{};
            in.gameplay=in.zone=in.empty=in.loaded=in.draw_authorized=true;
            const auto step=[&](std::uint64_t now,float grip) {in.now=now;in.grip=grip;return gun.update(in);};
            assert(step(1000,0).action==A::none);
            assert(step(1010,1).reserved);
            assert(step(1110,1).action==A::draw && gun.held());
            if(rejection==0) gun.reject_draw();
            else if(rejection==1) {
                assert(step(1400,1).action==A::none && gun.held());
                gun.reject_draw();
            } else {
                assert(step(2610,1).action==A::none && gun.held());
                assert(step(2611,1).action==A::none && !gun.held());
            }
            assert(!gun.held());
            const auto rejected=step(3000,1);
            assert(rejected.action==A::none && !rejected.owns && !rejected.reserved && !gun.held());
            in.drawn=true;
            assert(step(3100,1).action==A::none && !gun.held());
            assert(step(3200,0).action==A::none && !gun.held());
            in.drawn=false;
            assert(step(3300,1).reserved);
            assert(step(3400,1).action==A::draw && gun.held());
            in.drawn=true;
            assert(step(3500,1).action==A::none && gun.held());
            assert(step(5200,1).action==A::none && gun.held());
            assert(step(5300,0).action==A::holster && !gun.held());
        }
        d2vr::GunGrip candidate;d2vr::GunGrip::In in{};
        in.gameplay=in.zone=in.empty=in.loaded=in.draw_authorized=true;in.grip=1;in.now=1000;
        assert(candidate.update(in).reserved);
        candidate.reject_draw();in.now=1200;
        const auto cancelled=candidate.update(in);
        assert(!cancelled.reserved && !cancelled.owns && cancelled.action==A::none);
    }
    {
        using Action=d2vr::GunGrip::Action;
        for(unsigned outcome=0;outcome<6;++outcome) {
            d2vr::GunGrip gun;d2vr::GunGrip::In input{};
            input.gameplay=input.drawn=true;input.release_owner=41;input.grip=1;input.now=1000;
            assert(gun.update(input).owns && gun.held());
            input.gameplay=false;input.now=1010;
            assert(gun.update(input).action==Action::none);
            input.grip=0;input.now=1020;
            const auto invalid_release=gun.update(input);
            assert(invalid_release.action==Action::none && !invalid_release.reserved && !gun.held());
            input.now=1030;assert(gun.update(input).action==Action::none);
            if(outcome==1)input.release_owner=42;
            if(outcome==2)input.release_owner=0;
            if(outcome==3)input.drawn=false;
            if(outcome==4)input.object_claim=true;
            if(outcome==5)gun.reject_draw();
            input.gameplay=true;input.busy=true;input.now=1040;
            assert(gun.update(input).action==Action::none);
            input.busy=false;input.now=1050;
            assert(gun.update(input).action==(outcome==0?Action::holster:Action::none));
            input.now=1060;assert(gun.update(input).action==Action::none);
        }
        d2vr::GunGrip regripped;d2vr::GunGrip::In input{};
        input.gameplay=input.drawn=true;input.release_owner=41;input.grip=1;input.now=2000;
        assert(regripped.update(input).owns);
        input.gameplay=false;input.grip=0;input.now=2010;regripped.update(input);
        input.grip=1;input.now=2020;assert(regripped.update(input).action==Action::none);
        input.gameplay=true;input.release_owner=42;input.now=2030;
        assert(regripped.update(input).action==Action::none && regripped.held());
        input.grip=0;input.now=2040;assert(regripped.update(input).action==Action::holster);
        d2vr::GunGrip busy_release;input={};
        input.gameplay=input.drawn=true;input.release_owner=51;input.grip=1;input.now=3000;
        busy_release.update(input);input.busy=true;input.grip=0;input.now=3010;
        assert(busy_release.update(input).action==Action::none);
        input.now=3020;assert(busy_release.update(input).action==Action::none);
        input.busy=false;input.now=3030;assert(busy_release.update(input).action==Action::holster);
    }
    { // Wheel art zones: measured capture (553x306 sim, 0.8 m quad) and a square headset capture (2496x2688).
        const double h=.8*306/553;
        const auto at=[&](double u,double v,unsigned w,unsigned hh,double width){return *d2vr::wheel_stage_point((u-.5)*width,(.5-v)*width*hh/w,width,w,hh);};
        auto p=at(.212,.082,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::health);
        p=at(.081,.384,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::mana);
        p=at(.94,.082,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::hide);
        p=at(.5,.52,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::ring);
        // Headset: stage fitted by width, centred vertically. Stage (397,89) -> pixel (516,758).
        p=at(516./2496,(758.)/2688,2496,2688,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::health);
        assert(std::abs(d2vr::wheel_metres_per_stage(.8,2496,2688)*326-.1358)<1e-3); // ring radius on the headset quad
        (void)h;
    }
    return 0;
}
