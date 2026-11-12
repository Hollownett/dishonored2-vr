#include "../src/vr_controls.hpp"
#include "../src/vr_combat.hpp"
#include "../src/world_ui_pose.hpp"
#include <cassert>
#include <cstdio>
#include <string>

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
int main() {
    { // v1141 OfferHold: a takedown offer survives sensor drop-outs on the same victim, not a dead one, a new epoch or the grace
        d2vr::OfferHold h;bool alive=true,behind=false;const auto still=[&](std::uint32_t,std::uint64_t age){return alive && (age<=d2vr::OfferHold::grace_ms || behind);};
        require(h.update(7,1000,1,still)==7,"offer passes");
        require(h.update(0,1200,1,still)==7,"drop-out held");
        require(h.update(0,1000+d2vr::OfferHold::grace_ms+1,1,still)==0,"grace ends");
        behind=true;require(h.update(7,1500,1,still)==7 && h.update(0,1500+900,1,still)==7,"behind him: held to the long grace");
        require(h.update(0,1500+d2vr::OfferHold::long_grace_ms+1,1,still)==0,"long grace ends");behind=false;
        require(h.update(7,2000,1,still)==7 && h.update(9,2050,1,still)==9,"new victim replaces");
        alive=false;require(h.update(0,2100,1,still)==0,"dead victim not held");
        alive=true;h.update(5,3000,1,still);require(h.update(0,3010,2,still)==0,"new epoch clears");
        // regression: the choke grip fed the held offer keeps RB through a one-frame drop-out
        d2vr::OfferHold c;d2vr::OfferedChokeGrip g;
        g.update(false,false,false,true,1,0,0,false);
        bool rb=true;
        for(std::uint64_t t=0;t<500;t+=11) {const std::uint32_t raw=(t/11)%5==2?0u:4u;const auto v=c.update(raw,t,1,still);
            const auto r=g.update(true,t==0,true,true,1,v,0,false);rb=rb && r.rb;}
        require(rb,"choke held through flicker");
    }
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
        // Combat: the grip became a living hold (no Use) before the guard turned chokeable; the still-held grip takes the later offer.
        update(false,false,false,0);
        assert(!update(false,false,true,0).rb);
        assert(update(true,true,true,123).rb);
        assert(update(true,true,true,0,123).rb);
        assert(owner.victim()==123);owner.cancel();assert(owner.victim()==0);
        assert(!update(true,false,true,123,0,false,true,2).rb);
        // v1235 (v1232 follow-up; user, headset: "when I grab an enemy in combat and I only click grip ... I cannot choke the guardian"): OfferedChokeGrip's
        // three cases. press(use,started,held,offered,active,occupied,npc): npc = the NPC the squeeze is on (living hold/pick or the kill sensor's target).
        const auto press=[&](bool use,bool started,bool held,std::uint32_t offered,std::uint32_t active,bool occupied,std::uint32_t npc) {
            return owner.update(use,started,held,true,1,offered,active,occupied,false,npc);};
        // (1) grip alone became a living hold of guard 123 (intent grab: no Use); he turns chokeable: the held grip chokes (NativeGrab releases the hold:
        // reserve_npc -> npc_reserved in living_tick, not modelled here)
        update(false,false,false,0);
        require(!press(false,false,true,0,0,false,123).rb && press(true,true,true,123,0,false,123).rb,"living hold of the offered victim: the held grip chokes");
        // (2) a crank/lever/door Use held by the grip, a guard offered later: no NPC press, never a choke
        update(false,false,false,0);
        require(!press(true,true,true,0,0,false,0).rb && !press(true,true,true,123,0,false,0).rb,"crank squeeze + later offer: no choke");
        // (3) a held corpse (native_grab.holding: occupied) + an offer: no choke
        update(false,false,false,0);
        require(!press(true,true,true,123,0,true,0).rb && !press(true,true,true,123,0,true,0).rb,"held corpse + offer: no choke");
        // (4a) combat: the press on guard 123 went to a Use (kill sensor target) before he was offered; the still-held grip takes his later offer, no other's
        update(false,false,false,0);
        require(!press(true,true,true,0,0,false,123).rb,"combat: the press with no offer is a Use");
        require(!press(true,true,true,456,0,false,123).rb,"combat: another guard's offer is not taken");
        require(press(true,true,true,123,0,false,123).rb,"combat: the still-held grip takes his offer that appears later");
        // (4b) an offer refused at the press (a weapon's squeeze: occupied) stays refused for that squeeze, whatever `occupied` says later (v920)
        update(false,false,false,0);
        require(!press(true,true,true,123,0,true,123).rb && !press(true,true,true,123,0,false,123).rb,"an offer refused at the press stays refused");
        // (4c) click -> release -> press again: the click started the game's choke (active 123, no offer any more); the new press takes it again
        update(false,false,false,0);
        require(update(true,true,true,123).rb,"click: the press chokes");
        require(!update(false,false,false,0,123).rb,"click: released, RB off");
        require(update(true,true,true,0,123).rb,"press again while the game's choke runs: RB again");
        update(false,false,false,0,123);
        require(!update(true,true,true,0,123,true).rb,"press again: occupied still blocks");
        {d2vr::OfferedChokeGrip fresh;fresh.update(false,false,false,true,1,0,0,false);
         require(!fresh.update(true,true,true,true,1,0,77,false).rb,"a running choke this owner never chose is not taken");}
    }
    {   // The choke goes to the free hand, and a squeeze that holds a weapon is no choke press (user, headset v891: "when I tried a stealth kill from behind
        // with the sword, I often got a choke instead"). With a crossbow in the left hand the sword hand is the choke hand, and v698/v793 let any squeezed
        // choke-hand grip take the sensor's offer: the sword's own hold-to-hold squeeze chose the guard before the blade reached him.
        constexpr unsigned right=1,left=0;
        static_assert(d2vr::choke_hand(right,false,false)==right && d2vr::choke_hand(right,true,false)==left &&
                      d2vr::choke_hand(right,true,true)==right && d2vr::choke_hand(right,false,true)==right,"the sword's hand chokes unless it holds the sword and the other is free");
        static_assert(d2vr::choke_hand(left,true,false)==right && d2vr::choke_hand(left,true,true)==left,"dual wield: the sword in the left hand");
        // spoken_for(hand, sword_hand, sword_owns, gun_hand_holds, copy_owns, taken)
        static_assert(d2vr::grip_spoken_for(right,right,true,false,false,false) && !d2vr::grip_spoken_for(left,right,true,false,false,false),"the sword's squeeze is the sword's");
        static_assert(d2vr::grip_spoken_for(left,right,false,true,false,false) && !d2vr::grip_spoken_for(right,right,false,true,false,false),"the gun hand's squeeze is the gun's");
        static_assert(d2vr::grip_spoken_for(left,right,false,false,true,false) && d2vr::grip_spoken_for(left,right,false,false,false,true),"a copy or another gesture takes the squeeze");
        static_assert(!d2vr::grip_spoken_for(left,right,false,false,false,false) && !d2vr::grip_spoken_for(right,right,false,false,false,false),"an empty hand's squeeze is free");
        struct Walk {bool rb{},consume{};unsigned rt_frames{};unsigned choke_frames{};};
        // One approach from behind: the sensor offers the choke from frame 10 on and the blade touches the guard at frame 30. The choke hand's grip is
        // squeezed the whole time (`held`); `spoken_for` is what the control thread passes as `occupied`. The combat module sees the sensor's choke (no Block
        // meanwhile) and the contact that presses the attack once.
        const auto approach=[&](unsigned sword_hand,bool sword_owns,bool gun_owns,bool squeeze_choke_hand,bool gate) {
            d2vr::OfferedChokeGrip owner;d2vr::VrCombat combat;Walk walk;
            const unsigned cs=d2vr::choke_hand(sword_hand,sword_owns,gun_owns);
            const bool occupied=gate && d2vr::grip_spoken_for(cs,sword_hand,sword_owns,gun_owns,false,false);
            owner.update(false,false,false,true,1,0,0,false);
            d2vr::CombatFrame frame{};frame.tracked=frame.equipped=true;frame.head_fwd={0,0,-1};frame.head_right={1,0,0};frame.aim={0,0,-1};frame.trigger=1;frame.explicit_block=true;
            for(unsigned step=0;step<60;++step) {
                const bool offered=step>=10;
                const bool held=offered && squeeze_choke_hand;   // stealth_held: the raw grip squeezed when the offer shows takes it
                const auto route=owner.update(held,held,squeeze_choke_hand,true,1,offered?123:0,0,occupied,false);
                walk.rb|=route.rb;walk.consume|=route.consume_use;
                frame.allowed=!route.consume_use;frame.choke=offered;frame.now_ms=1000+step*16;frame.pos={0,1,-.4f};frame.contact_attack=step==30;
                const auto decision=combat.update(frame);
                walk.rt_frames+=decision.rt?1u:0u;walk.choke_frames+=route.rb?1u:0u;
            }
            return walk;
        };
        // sword in the right hand, a crossbow in the left: the sword hand is the choke hand and its squeeze holds the sword
        auto stab=approach(right,true,true,true,true);
        require(!stab.rb && !stab.consume && stab.rt_frames>0,"gun left, sword right: the held sword grip never chokes and the stab is pressed");
        stab=approach(right,true,true,true,false);
        require(stab.rb && stab.consume && stab.rt_frames==0,"control: without the gate the sword's squeeze chokes and the stab is dropped (the v891 bug)");
        // the free left hand: its squeeze (held or fresh) chokes as before, the sword hand's squeeze is not read at all
        stab=approach(right,true,false,true,true);
        require(stab.rb && stab.consume,"sword right, left hand free: the left squeeze chokes");
        stab=approach(right,true,false,false,true);
        require(!stab.rb && !stab.consume && stab.rt_frames>0,"sword right, left hand free and open: the stab is pressed, no choke");
        // an empty sword hand (sword away) chokes with its squeeze; with the sword in the left (dual wield) the right hand is free
        stab=approach(right,false,false,true,true);
        require(stab.rb,"no sword in hand: the right squeeze chokes");
        stab=approach(left,true,false,true,true);
        require(stab.rb,"dual wield, sword left: the free right hand chokes");
        stab=approach(left,true,true,true,true);
        require(!stab.rb && stab.rt_frames>0,"dual wield, sword left and a gun right: the held sword grip never chokes");
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
    {   // Two-handed wheel: left X and right A each open at once (v779, user, headset v778: "A holds the wheel, mirrored with the left"); the
        // first hand owns until release.
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
        u(0,0,1500);u(0,1,1510);
        require(w.wheel() && w.hand()==1 && w.a_consumed(),"A opens at once for the right hand, as X for the left");
        u(1,1,1600);
        require(w.hand()==1,"X cannot take over while A owns it");
        u(1,0,1700);u(1,0,1800);
        require(!w.wheel() && !w.a_consumed(),"A release closes; X pressed during A stays ignored");
        u(0,0,3800);u(1,0,4000);u(1,0,4100,false);u(1,0,4200);
        require(!w.wheel(),"lost allowed cancels, no reopen");
        u(0,0,4300);u(0,1,5000,false);u(0,1,5500);
        require(!w.wheel() && !w.a_consumed(),"A pressed while disallowed never opens late");
        float tx=.1f,ty=0,mx=0,my=-1;
        d2vr::wheel_select(1,tx,ty,mx,my,false,1,0);
        require(tx==0 && ty==-1 && mx==0 && my==0,"right-owned: left stick selects while the hand has not moved, move dropped");
        tx=.1f;ty=0;mx=0;my=-1;d2vr::wheel_select(1,tx,ty,mx,my,true,1,0);
        require(tx==1 && ty==0,"right-owned: walking (left stick) never steals the hand's choice (v771)");
        tx=.9f;ty=0;mx=0;my=1;
        d2vr::wheel_select(0,tx,ty,mx,my,true,0,1);
        require(tx==.9f && ty==0 && my==0,"left-owned: a deliberate right stick wins over dial and left stick is dropped");
        tx=.4f;ty=0;d2vr::wheel_select(0,tx,ty,mx,my,true,0,1);
        require(tx==0 && ty==1,"left-owned: a resting thumb (.4) does not steal the hand's choice (v771)");
        tx=ty=0;d2vr::wheel_select(1,tx,ty,mx,my,true,0,1);
        require(tx==0 && ty==1,"hand dial fills in when the stick is idle");
        tx=.9f;ty=0;mx=0;my=1;d2vr::wheel_select(0,tx,ty,mx,my,true,0,1,true);
        require(tx==0 && ty==1 && mx==0 && my==0,"hand-only (v772, user: the hand, not the sticks): a pushed stick never selects");
        tx=.9f;ty=0;mx=0;my=1;d2vr::wheel_select(1,tx,ty,mx,my,false,0,1,true);
        require(tx==0 && ty==0 && mx==0 && my==0,"hand-only: the hand not moved yet selects nothing");
    }
    {   // v762 (headset v761: the bottom of the hand ring was hard to reach): the ring's own axes, and a lone tracked hand selects at once.
        using V=std::array<double,3>;
        d2vr::WheelCursor c;float x=0,y=0;
        const V rt{1,0,0},up{0,.5,-.8660254};   // a ring seen from 60 degrees above: its up leans away from the eyes
        V L{-.2,1,-.3};const V R{.2,1,-.3};
        auto step=[&]{return c.update(true,{true,false},{L,R},rt,x,y,.035,.004,&up);};
        require(!step(),"lone hand: nothing selected at open");
        for(int i=1;i<=30;++i) {const double k=.002*i;L={-.2-.7071*k,1-.7071*k*up[1],-.3-.7071*k*up[2]};step();} // 2 mm a frame along the ring's lower left
        require(c.cursor()==0 && x<-.70f && x>-.72f && y<-.70f && y>-.72f,"a slow move along the tilted ring selects its lower left");
        L={-.2,1,-.3};x=y=0;  // v763 (headset v762: commits without a sector): back at the centre the choice stays until another is made
        require(step() && x<-.70f && y<-.70f,"the last choice is held inside the dead zone");
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
        float tx=0,ty=1,mx=0,my=0;
        d2vr::wheel_select(0,tx,ty,mx,my,true,1,0);
        require(tx==0 && ty==1,"a deliberate stick deflection overrides the cursor");
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
    {   // Dual wield: the wheel ring at the left hand; the right hand's ray selects on the ring plane that moves with it.
        using V=std::array<double,3>;
        const d2vr::Vec3 head{0,1.6,0};
        const double radius=326*d2vr::wheel_metres_per_stage(d2vr::wheel_hand_width_m,1920,1080);
        const auto plane=[&](d2vr::Vec3 hand){return d2vr::wheel_plane_of(d2vr::world_ui_hand_pose(head,hand,d2vr::wheel_hand_lift_m),d2vr::wheel_hand_width_m);};
        const auto arr=[](d2vr::Vec3 v){return V{v.x,v.y,v.z};};
        const auto aim=[&](V from,d2vr::Vec3 to){const d2vr::Vec3 d=to-d2vr::Vec3{from[0],from[1],from[2]};const auto n=d*(1/d2vr::length(d));return V{n.x,n.y,n.z};};
        d2vr::WheelPointer p;float x=0,y=0;V hit{};bool vis=false;
        d2vr::Vec3 hand{-.25,1.15,-.45};auto w=plane(hand);
        const V rpos{.2,1.3,-.1};std::array<V,2> pos{V{-.25,1.15,-.45},rpos},dir{V{0,0,1},aim(rpos,w.centre)}; // owner's ray points away
        const auto call=[&](const d2vr::WheelPlane& pl){return p.update_plane(true,{true,true},pos,dir,arr(pl.centre),arr(pl.normal),arr(pl.right),arr(pl.up),0,x,y,hit,vis,radius);};
        require(!call(w) && vis && p.cursor()==1,"ray at the ring centre is neutral, the pointing hand is the cursor");
        require(std::abs(hit[0]-w.centre.x)<1e-9 && std::abs(hit[1]-w.centre.y)<1e-9 && std::abs(hit[2]-w.centre.z)<1e-9,"dot is on the ring centre");
        dir[1]=aim(rpos,w.centre+w.right*radius);
        for(int i=0;i<30;++i) call(w);
        require(call(w) && x>.99f && std::abs(y)<.01f,"right edge of the ring selects the right wedge");
        dir[1]=aim(rpos,w.centre+w.up*radius);
        for(int i=0;i<30;++i) call(w);
        require(call(w) && std::abs(x)<.01f && y>.99f,"top of the (tilted) ring selects the top wedge");
        // The hand moves 12 cm left: the ring goes with it, the same ray now sits 12 cm right of the new centre.
        hand=hand+d2vr::Vec3{-.12,0,0};const auto w2=plane(hand);
        require(std::abs(w2.centre.x-w.centre.x+.12)<1e-9 && std::abs(w2.centre.y-w.centre.y)<1e-9,"the ring centre follows the hand");
        for(int i=0;i<60;++i) call(w2);
        require(call(w2) && x>.5f && y<.8f && hit[0]-w2.centre.x>0,"a fixed ray is now right of the ring that moved left (no longer the top wedge)");
        dir[1]=aim(rpos,w2.centre+w2.up*radius);
        for(int i=0;i<60;++i) call(w2);
        require(call(w2) && std::abs(x)<.01f && y>.99f,"re-aimed at the moved ring selects the top wedge");
        require(std::abs(d2vr::dot(d2vr::Vec3{hit[0],hit[1],hit[2]}-w2.centre,w2.normal))<1e-9,"the dot lies in the ring plane");
        dir[1]=V{0,0,1};
        require(!call(w2) && !vis,"a ray pointing away from the ring shows nothing");
        p.update_plane(false,{true,true},pos,dir,arr(w2.centre),arr(w2.normal),arr(w2.right),arr(w2.up),0,x,y,hit,vis,radius);
        require(p.cursor()==-1,"closing resets the cursor");
    }
    {   // v776 (user, headset v775: "I turn my hand to the right and the indicator stays on the left"): the hand that opened the wheel POINTS at its ring.
        // The ring sits on that hand's aim ray (wheel_hand_ahead_m, facing the eyes) from the opening on; each frame the ray meets that fixed plane and the
        // WheelPointer gives the pointed direction (continuous: the native ring holds N items, not 8), only the opening hand tracked as dxgi_probe feeds it.
        using V=std::array<double,3>;
        const d2vr::Vec3 head{0,1.65,0};const double rad=3.14159265358979/180;
        const auto arr=[](d2vr::Vec3 v){return V{v.x,v.y,v.z};};
        const auto aim_of=[&](d2vr::Vec3 at,double yaw,double pitch) { // a hand's aim pose: yaw degrees right, pitch degrees up from straight ahead (-Z)
            return d2vr::TrackedPose{at,d2vr::multiplied({0,std::sin(-yaw*rad/2),0,std::cos(yaw*rad/2)},{std::sin(pitch*rad/2),0,0,std::cos(pitch*rad/2)})};};
        const auto ray_of=[&](const d2vr::TrackedPose& a) {const auto d=d2vr::rotated(a.orientation,{0,0,-1});return V{d.x,d.y,d.z};};
        const auto angle_of=[&](float dx,float dy) {return std::atan2(static_cast<double>(dy),static_cast<double>(dx))/rad;};
        const auto yaw_pitch=[&](d2vr::Vec3 d) {return std::array<double,2>{std::atan2(d.x,-d.z)/rad,std::asin(d.y/d2vr::length(d))/rad};}; // the aim that points along d
        std::printf("wheel pointing lab (ring radius 326 stage px; the native ring's items sit on a 307 px circle, discs of 52 px radius, evenly spaced from the top)\n");
        for(const double scale:{1.,100./65.}) for(const int hand:{0,1}) { // HUD 65% and the user's HUD 100%; the left and the right hand
            const std::size_t me=static_cast<std::size_t>(hand),other=1-me;
            const double width=d2vr::wheel_hand_width_m*scale,radius=326*d2vr::wheel_metres_per_stage(width,1920,1080),mps=radius/326;
            std::array<V,2> pos{V{-.2,1.3,-.4},V{.2,1.3,-.4}},dir{V{0,0,-1},V{0,0,-1}}; // the sim's default hand places, level, pointing at -Z
            const d2vr::Vec3 at{pos[me][0],pos[me][1],pos[me][2]},other_at{pos[other][0],pos[other][1],pos[other][2]};
            const auto open_aim=aim_of(at,0,0);
            const auto pl=d2vr::wheel_plane_of(d2vr::wheel_aim_pose(head,open_aim,scale),width); // placed once, from the opening aim
            const std::array<bool,2> tracked{hand==0,hand==1};                                    // the other hand is never tracked here
            d2vr::WheelPointer p;float x=0,y=0;V hit{};bool vis=false;
            const auto frame=[&](bool continuous=true) {return p.update_plane(true,tracked,pos,dir,arr(pl.centre),arr(pl.normal),arr(pl.right),arr(pl.up),hand,x,y,hit,vis,radius,continuous);};
            const auto cursor_mm=[&](double& u,double& v) {const d2vr::Vec3 d=d2vr::Vec3{hit[0],hit[1],hit[2]}-pl.centre;u=d2vr::dot(d,pl.right)*1000;v=d2vr::dot(d,pl.up)*1000;};
            int n=0;
            const auto point=[&](const char* what,double yaw,double pitch,bool say=true) { // aim there; up to 3 frames to choose
                dir[me]=ray_of(aim_of(at,yaw,pitch));bool on=false;n=0;
                while(n<3 && !on) {on=frame();++n;}
                if(!say) return on;
                double u,v;cursor_mm(u,v);
                if(on) std::printf("  %-24s aim yaw/pitch %+5.1f/%+5.1f: wedge %d, dir %+6.1f deg, after %d frame(s), cursor %+6.1f %+6.1f mm (%.2f ring radii)\n",what,yaw,pitch,p.sector(),angle_of(x,y),n,u,v,std::hypot(u,v)/1000/radius);
                else std::printf("  %-24s aim yaw/pitch %+5.1f/%+5.1f: no wedge (dead zone), after %d frame(s), cursor %+6.1f %+6.1f mm (%.2f ring radii)\n",what,yaw,pitch,n,u,v,std::hypot(u,v)/1000/radius);
                return on;};
            const auto aim_at=[&](d2vr::Vec3 target,const char* what,bool say=true) {const auto a=yaw_pitch(target-at);return point(what,a[0],a[1],say);}; // aim at a point of the plane
            const auto recentre=[&] {point("",0,0,false);};
            const auto press=[&] {return p.commit(true,{hand?0.f:.9f,hand?.9f:0.f});}; // the opening hand's trigger
            const auto release=[&] {p.commit(true,{0,0});};
            const d2vr::Vec3 ray=d2vr::rotated(open_aim.orientation,{0,0,-1}),to_ring=pl.centre-at;
            std::printf(" %s hand, HUD factor %.2f: ring radius %.1f mm, centre %.1f mm along the aim ray and %.3f mm off it, dead zone %.1f mm (%.1f deg of aim)\n",hand?"right":"left",scale,radius*1000,
                d2vr::dot(to_ring,ray)*1000,d2vr::length(to_ring-ray*d2vr::dot(to_ring,ray))*1000,.2*radius*1000,std::atan(.2*radius/(d2vr::wheel_hand_ahead_m*scale))/rad);
            require(!point("opening, aim at the centre",0,0) && vis,"the hand aiming at the ring centre chooses nothing");
            require(d2vr::length(d2vr::Vec3{hit[0],hit[1],hit[2]}-pl.centre)<1e-3,"the opening ray hits the ring centre within 1 mm");
            const struct {const char* what;double yaw,pitch;int wedge;double deg;} turns[]{{"yaw 25 right",25,0,0,0},{"yaw 25 left",-25,0,4,180},{"pitch 25 up",0,25,2,90},{"pitch 25 down",0,-25,6,-90}};
            for(const auto& t:turns) {
                require(point(t.what,t.yaw,t.pitch) && n<=3 && p.sector()==t.wedge && std::abs(angle_of(x,y)-t.deg)<=15,"25 degrees of turn points at that side of the ring within 3 frames");
                require(!point("back to the centre",0,0),"back at the centre chooses nothing");
            }
            // Aiming at the item position of each compass wedge (0.9 radius out) points at that wedge.
            std::string row;bool compass=true;
            for(int k=0;k<8;++k) {
                recentre();
                aim_at(pl.centre+pl.right*(.9*radius*std::cos(k*45*rad))+pl.up*(.9*radius*std::sin(k*45*rad)),"",false);
                row+=" "+std::to_string(p.sector());compass=compass && p.sector()==k && std::abs(std::remainder(angle_of(x,y)-k*45,360))<=8;
            }
            std::printf("  aiming at the 8 compass item positions (right, up-right, up ...) points at wedges:%s\n",row.c_str());
            require(compass,"aiming at an item position of wedge k points at wedge k");
            // Tremor: +-1 degree of aim keeps the wedge and the direction, on a held wedge and 1 deg (of 45) inside a wedge border (22.5 deg round the ring);
            // +-3 degrees across that border still keeps the wedge (the 8 deg hysteresis).
            const struct {double amp,border;} tremors[]{{1,0},{1,21.5},{2,21.5}};
            for(const auto& tr:tremors) {
                recentre();std::array<double,2> base{25,0}; // yaw, pitch: 25 degrees right, or the item position 21.5 degrees round the ring
                if(tr.border>0) base=yaw_pitch(pl.centre+pl.right*(.9*radius*std::cos(tr.border*rad))+pl.up*(.9*radius*std::sin(tr.border*rad))-at);
                point("",base[0],base[1],false);
                const int held=p.sector();const double dir0=angle_of(x,y);int flips=0;double worst=0,lo=1e9,hi=-1e9;
                for(int i=0;i<60;++i) {
                    dir[me]=ray_of(aim_of(at,base[0]+(i&1?tr.amp:-tr.amp),base[1]+(i&2?tr.amp:-tr.amp)));frame();
                    flips+=p.sector()!=held;worst=std::max(worst,std::abs(std::remainder(angle_of(x,y)-dir0,360)));
                    double u,v;cursor_mm(u,v);lo=std::min(lo,u);hi=std::max(hi,u);
                }
                std::printf("  tremor +-%.0f deg x 60 frames, %s: wedge %d, wedge changes %d, direction moved at most %.1f deg, cursor spread %.1f mm\n",tr.amp,
                    tr.border>0?"1 deg inside a wedge border":"on a held wedge",held,flips,worst,hi-lo);
                require(flips==0 && (tr.amp>1 || worst<3),"a tremor keeps the wedge (and, at 1 degree, the direction)");
            }
            // The other hand: aimed anywhere on the ring it never moves the cursor (it is not tracked); tracked it would take the cursor when it moves.
            point("hold yaw 25 right",25,0,false);const double held_angle=angle_of(x,y);int taken=0;
            for(const d2vr::Vec3 target:{pl.centre,pl.centre+pl.up*(.9*radius),pl.centre-pl.right*(.9*radius)}) {
                const auto d=target-other_at;const auto u=d*(1/d2vr::length(d));dir[other]=V{u.x,u.y,u.z};
                for(int i=0;i<20;++i) {frame();taken+=p.cursor()!=hand || std::abs(std::remainder(angle_of(x,y)-held_angle,360))>5;}
            }
            d2vr::WheelPointer q;float qx=0,qy=0;V qhit{};bool qv=false; // control: both hands tracked
            for(int i=0;i<3;++i) {q.update_plane(true,{true,true},pos,dir,arr(pl.centre),arr(pl.normal),arr(pl.right),arr(pl.up),hand,qx,qy,qhit,qv,radius,true);
                const auto d=pl.centre+pl.up*(.3*radius*(i+1))-other_at;const auto u=d*(1/d2vr::length(d));dir[other]=V{u.x,u.y,u.z};}
            std::printf("  the other hand aimed at the centre, the top and the left of the ring for 60 frames: cursor taken %d times, hand %d keeps it (both tracked, it would take it: %d)\n",taken,hand,q.cursor()==static_cast<int>(other));
            require(taken==0 && q.cursor()==static_cast<int>(other),"the other hand never takes the cursor (a control with both tracked does)");
            dir[other]=V{0,0,-1};
            // Elixirs and "Hide/Show all" (stage 397,89 / 142,415 / 1680,90) are click zones: no ring item there, a trigger press there commits nothing.
            const struct {const char* what;double sx,sy;} buttons[]{{"health elixir",397,89},{"mana elixir",142,415},{"Hide/Show all",1680,90}};
            for(const auto& b:buttons) {
                recentre();release();
                const bool reach=aim_at(pl.centre+pl.right*((b.sx/1920-.5)*width)+pl.up*((.5-b.sy/1080)*width*1080/1920),b.what);const auto cur=p.plane();
                const bool on=cur && d2vr::wheel_on_button((*cur)[0],(*cur)[1],width,1920,1080),fired=press();
                require(reach && on && !fired,"the cursor on an elixir or Hide/Show all is on a button, and a trigger press there commits no ring item");
            }
            recentre();release();point("yaw 25 right",25,0);{const auto cur=p.plane();require(cur && !d2vr::wheel_on_button((*cur)[0],(*cur)[1],width,1920,1080),"a ring item is not a button");}
            require(press(),"a trigger press on the ring commits");
            // The native ring holds as many items as the player owns, evenly spaced from the top, clockwise (13 on the user's save, measured on its capture).
            // The game's pointer sits 0.9 radius out in the output direction; an item is reached when that is inside its disc.
            for(const int items:{5,8,13,16,20}) {
                int reached[2]{};
                for(int mode=0;mode<2;++mode) for(int i=0;i<items;++i) {
                    const double a=(90-i*360./items)*rad,px=307*std::cos(a),py=307*std::sin(a);
                    recentre();const auto aim=yaw_pitch(pl.centre+pl.right*(px*mps)+pl.up*(py*mps)-at);dir[me]=ray_of(aim_of(at,aim[0],aim[1]));
                    bool on=false;for(int f=0;f<3 && !on;++f) on=frame(mode==0);
                    if(on) reached[mode]+=std::hypot(.9*326*x-px,.9*326*y-py)<52;
                }
                std::printf("  ring of %2d items: pointing at each reaches %2d with the pointed direction, %2d with the 8 wedge centres\n",items,reached[0],reached[1]);
                require(reached[0]==items,"every item of the ring can be reached by pointing at it");
                if(items==13) require(reached[1]<items,"(control) 8 wedge centres cannot reach every item of the user's 13-item ring");
            }
        }
        std::printf("wheel pointing lab: done\n");
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
        in.zone=true;in.grip=1;in.draw_dot=-.8;assert(!step(4000).reversed);             // v1068: fist down at the shoulder is still a straight draw
        in.grip=0;assert(step(4050).action==A::sheathe);                                 // let go before it was out: put back
        in.zone=false;in.drawn=true;assert(step(5000).action==A::none);                  // the game drew it, nobody holds it
        assert(step(5700).action==A::sheathe && step(5800).action==A::none);             // put away once
        in.drawn=false;in.copy=true;in.copy_age_s=1;assert(step(6000).action==A::none);
        in.drawn=true;assert(step(6100).action==A::recall);                             // drawn again while its copy lies around
        in.drawn=false;in.copy_age_s=31;assert(step(7200).action==A::recall);           // left lying too long
        in.copy_age_s=1;in.grip=1;in.busy=true;assert(step(8000).action==A::none);       // carrying: the grip is not the sword
        in.busy=false;in.gameplay=false;in.grip=0;assert(step(8100).action==A::none);
        in.gameplay=true;in.copy=false;in.grip=1;in.drawn=true;{const auto o=step(9000);assert(o.action==A::none && !s.held() && !o.owns);} // v694: never takes a game-drawn sword
        assert(step(9200).action==A::sheathe);                                          // put away at once
        in.grip=0;in.drawn=false;step(9300);in.zone=true;in.grip=1;assert(step(9400).action==A::draw);in.zone=false;in.drawn=true;step(9500); // drawn by hand
        in.gameplay=false;assert(step(9600).action==A::none);in.gameplay=true;assert(step(10000).action==A::none && s.held()); // wheel opened and closed
        in.gameplay=false;in.grip=0;assert(step(10100).action==A::none);in.gameplay=true;
        {const auto o=step(10200);assert(o.action==A::sheathe && !s.held());}           // let go inside the wheel: put away, not thrown
        in.grip=1;in.drawn=false;assert(step(11000).action==A::none);                   // a corpse held: the grip is down before the game draws
        in.drawn=true;assert(step(11100).action==A::none && !s.held());                 // drawn by the game under that grip: not taken
        assert(step(11800).action==A::sheathe);                                         // put away
        in.grip=0;in.drawn=false;assert(step(11900).action==A::none);                   // letting go of the corpse drops no sword
        // v692: a squeeze on something to use is that Use, not the game-drawn sword nor the lying copy
        in.use_target=true;in.grip=1;
        in.copy=true;in.copy_reach=true;in.copy_age_s=1;in.copy_distance=.3;in.grip=1;{const auto o=step(13200);assert(o.action!=A::pickup && !o.owns);}
        in.grip=0;in.use_target=false;step(13300);in.grip=1;assert(step(13400).action==A::pickup); // nothing to use: the copy is taken
    }
    for(bool keep:{true,false}) { // a mantle (busy) takes the held sword: drawn again after it, once, only while the hand still squeezes
        using A=d2vr::SwordGrip::Action;d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;
        auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
        in.zone=true;in.grip=1;assert(step(10).action==A::draw);in.zone=false;in.drawn=true;step(200);
        in.busy=true;in.drawn=false;in.grip=keep?1.f:0.f;
        for(std::uint64_t t=300;t<2000;t+=50) assert(step(t).action==A::none);        // during the scene: nothing
        in.busy=false;assert(step(2000).action==A::none && step(2200).action==A::none); // the game's own restore first
        assert(step(2400).action==(keep?A::draw:A::none));
        assert(step(2500).action==A::none && step(3000).action==A::none);             // once
        if(keep) {in.drawn=true;assert(step(3100).action==A::none && s.held());}
    }
    { // G2 regrab: the pickup dead band, and the grip changes only on a draw or pickup edge.
        using A=d2vr::SwordGrip::Action;static_assert(d2vr::SwordGrip::pickup_dead_band==.35);
        { // pickup_dead_band_keeps_previous: a reversed draw, a drop, then a floor pickup at pickup_dot .2 is still reversed
            d2vr::SwordGrip s;d2vr::SwordGrip::In in{};in.gameplay=true;auto step=[&](std::uint64_t t){in.now=t;return s.update(in);};
            in.copy=true;in.copy_reach=true;in.copy_distance=1;in.pickup_dot=-.8;in.grip=1;{const auto o=step(10);assert(o.action==A::pickup && o.reversed);}
            in.copy=in.copy_reach=false;in.drawn=true;step(400);
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
                in.copy=true;in.copy_reach=true;in.copy_distance=1;in.grip=1;in.pickup_dot=rev?-.8:.8;assert(step(10).reversed==rev);in.drawn=true;
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
    { // v963: NPC actions on the hand's trigger, and the door's held Use (keyhole peek).
        assert(d2vr::npc_trigger_owns_hand(true,true,false,false));
        assert(d2vr::npc_trigger_owns_hand(true,false,true,false));
        assert(!d2vr::npc_trigger_owns_hand(false,true,true,false));   // feature off: the grip keeps Talk/Pickpocket
        assert(!d2vr::npc_trigger_owns_hand(true,true,true,true));     // a weapon hand's trigger is its weapon's
        assert(d2vr::keyhole_use_hold(true,true,true,true));
        assert(!d2vr::keyhole_use_hold(true,false,true,true));         // never a held X on a non-door item
        assert(!d2vr::keyhole_use_hold(true,true,false,true));
        assert(!d2vr::keyhole_use_hold(false,true,true,true));
        d2vr::NpcTriggerOwner owner;const d2vr::TalkYOwner::Target target{0x1000,7,42,0x2000};
        const auto run=[&](bool pressed,bool allowed,std::uint64_t now,d2vr::TalkYOwner::Target at,d2vr::NpcAction action) {
            return owner.update(pressed,allowed,3,now,at,action);};
        auto r=run(false,true,990,target,d2vr::NpcAction::talk);assert(!r.owned);
        r=run(true,true,1000,target,d2vr::NpcAction::talk);
        assert(r.owned && r.consume_trigger && r.interact);               // a press claims the Talk offer
        r=run(true,true,1100,target,d2vr::NpcAction::talk);
        assert(r.owned && r.consume_trigger && r.interact);               // held: Use stays down
        r=run(true,true,1300,target,d2vr::NpcAction::talk);
        assert(r.owned && r.consume_trigger && !r.interact);              // past 220 ms: no endless Use
        r=run(false,true,1310,target,d2vr::NpcAction::talk);assert(!r.owned && !r.consume_trigger);
        r=run(true,false,1320,target,d2vr::NpcAction::talk);assert(!r.owned); // a weapon hand: no claim
        r=run(false,true,1330,target,d2vr::NpcAction::talk);assert(!r.owned);
        r=run(true,true,1340,target,d2vr::NpcAction::pickpocket);assert(r.owned && r.interact);
        const d2vr::TalkYOwner::Target other{0x3000,8,43,0x4000};
        r=run(true,true,1350,other,d2vr::NpcAction::talk);assert(!r.owned); // a replaced target cancels the claim
        r=run(false,true,1360,other,d2vr::NpcAction::talk);assert(!r.owned);
        r=run(true,true,1370,other,d2vr::NpcAction::none);assert(!r.owned); // no offer: nothing claimed
        r=run(false,true,1380,other,d2vr::NpcAction::loot);assert(!r.owned);
        r=run(true,true,1390,other,d2vr::NpcAction::loot);assert(r.owned && r.interact && r.consume_trigger);   // v1141 (user feedback 2026-11-11: "grabbing an enemy and a lying body are different controls"): loot offer + trigger = the game's Use
        r=run(true,true,1700,other,d2vr::NpcAction::loot);assert(r.owned && !r.interact);   // one 220 ms press, then only consumed
        assert(d2vr::npc_trigger_owns_hand(true,false,false,false,true) && !d2vr::npc_trigger_owns_hand(true,false,false,true,true));   // a body's loot offer owns the trigger, never a weapon hand's
    }
    return 0;
}
