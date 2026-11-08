#include "../src/arm_state.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace d2vr::arm;

namespace {
struct Clock {std::uint64_t t=1000,rev=1;};
// Feed the engine's report until it has settled (settle_ms) so the model applies it.
Fx engine(ArmSet& s,Clock& c,Held slot1,bool sword,bool new_item=true,std::uint64_t settle=ArmSet::settle_ms+5) {
    if(new_item) ++c.rev;
    Fx fx=s.native_changed({slot1,sword,c.rev},c.t);c.t+=settle;
    fx.merge(s.native_changed({slot1,sword,c.rev},c.t));return fx;
}
Fx idle(ArmSet& s,Clock& c,Held slot1,bool sword,std::uint64_t ms) {   // time passes, the engine reports the same thing
    Fx fx;for(std::uint64_t e=0;e<ms;e+=20) {c.t+=20;fx.merge(s.native_changed({slot1,sword,c.rev},c.t));}return fx;
}
unsigned lcg_state=12345;
unsigned rnd(unsigned n) {lcg_state=lcg_state*1664525u+1013904223u;return (lcg_state>>8)%n;}
}

int main() {
    {   // defaults and the packed plan: the game's hands
        ArmSet s;const Plan p=s.derive();
        assert(p.sword_arm==1 && p.slot1_arm==0 && p.cross==0 && p.copy[0]==Held::none && p.copy[1]==Held::none);
        const unsigned pk=pack(p);assert(pk==pack_default);
        assert(hand_bits(pk,1)==1 && hand_bits(pk,2)==2 && hand_bits(pk,3)==3 && hand_bits(pk,0)==0);
        assert(s.valid());
    }
    {   // the left hand takes the sword: roles flip, the pack mirrors; the engine's drawn sword lands in that arm
        ArmSet s;Clock c;const Fx fx=s.sword_take(0,c.t);assert(!fx.refused);
        Plan p=s.derive();assert(p.sword_arm==0 && p.slot1_arm==1 && p.cross==3);
        engine(s,c,Held::none,true);assert(s.a[0].what==Held::sword && s.a[0].home==Home::slot0 && s.valid());
        const unsigned pk=pack(s.derive());assert(hand_bits(pk,2)==1 && hand_bits(pk,1)==2 && hand_bits(pk,3)==3);
        engine(s,c,Held::none,false);assert(s.a[0].what==Held::none && s.derive().sword_arm==0);   // sheathed: the seat is sticky
        s.sword_take(1,c.t);assert(s.derive().sword_arm==1 && s.derive().cross==0);
    }
    {   // the sword hand's gun: a pistol copy in the other arm stays through a sword take and a sheathe (no coupling)
        ArmSet s;Clock c;s.set_copy(0,Held::pistol,c.t);
        assert(s.sword_take(0,c.t).refused);                // an arm with a copy cannot take the sword
        s.sword_take(1,c.t);engine(s,c,Held::none,true);
        assert(s.copy_at(0)==Held::pistol && s.a[1].what==Held::sword);
        engine(s,c,Held::none,false);assert(s.copy_at(0)==Held::pistol);
    }
    {   // POWER SWAP from a sword take: Blink moves to the other (empty) arm; with that arm occupied it is put away
        ArmSet s;Clock c;engine(s,c,Held::power,false);assert(s.a[0].what==Held::power && s.derive().slot1_arm==0);
        Fx fx=s.sword_take(0,c.t);assert(!fx.refused && (fx.power_moved&2u) && !fx.unequip_slot1);
        assert(s.a[1].what==Held::power && s.a[0].what==Held::none && s.derive().slot1_arm==1 && s.derive().sword_arm==0 && s.valid());
        ArmSet t;Clock d;engine(t,d,Held::power,false);t.set_copy(1,Held::pistol,d.t);
        fx=t.sword_take(0,d.t);assert(!fx.refused && fx.unequip_slot1 && !fx.power_moved);
        assert(t.a[0].what==Held::none && t.copy_at(1)==Held::pistol && t.valid());
        engine(t,d,Held::power,false,false);   // the engine has not caught up: not re-adopted
        idle(t,d,Held::power,false,100);assert(t.a[0].what==Held::none && t.a[1].home==Home::copy);
        engine(t,d,Held::none,false);assert(t.slot1_holder()<0);
    }
    {   // ported from hand_loadout_check: an arm showing its native gun never takes the sword (v784); a copy-holding arm neither; Blink in the sword's
        // would-be arm with the other arm holding a copy is put away by the sword take (the swap rule), the copy stays (v773)
        ArmSet s;Clock c;s.hip_draw(0,c.t,c.rev);engine(s,c,Held::pistol,false);assert(s.sword_take(0,c.t).refused && s.a[0].what==Held::pistol);
        ArmSet p;Clock d;p.set_copy(0,Held::pistol,d.t);p.hip_draw(1,d.t,d.rev);engine(p,d,Held::power,false);   // Blink in arm 1, a pistol copy in arm 0
        assert(p.a[1].what==Held::power && p.copy_at(0)==Held::pistol);
        const Fx fx=p.sword_take(1,d.t);assert(!fx.refused && fx.unequip_slot1 && p.a[1].what==Held::none && p.copy_at(0)==Held::pistol && p.derive().sword_arm==1);
    }
    {   // Heart / gadget in the arm: the sword and pickups are refused (held items are things in the hand)
        ArmSet s;Clock c;engine(s,c,Held::heart,false);
        assert(s.sword_take(0,c.t).refused && s.pickup(0,Held::pistol,true,c.t,c.rev).refused);
        ArmSet g;Clock d;engine(g,d,Held::gadget,false);assert(g.sword_take(0,d.t).refused && g.hip_draw(0,d.t,d.rev).refused);
    }
    {   // POWER SWAP from a pickup: the picked gun is the arm's copy, Blink moves; with the other arm occupied it is put away
        ArmSet s;Clock c;engine(s,c,Held::power,false);
        Fx fx=s.pickup(0,Held::pistol,true,c.t,c.rev);assert(!fx.refused && !fx.native && (fx.power_moved&2u));
        assert(s.copy_at(0)==Held::pistol && s.a[1].what==Held::power && s.derive().slot1_arm==1 && s.valid());
        ArmSet t;Clock d;engine(t,d,Held::power,false);engine(t,d,Held::power,true,false);
        assert(t.a[1].what==Held::sword);
        fx=t.pickup(0,Held::crossbow,true,d.t,d.rev);assert(fx.unequip_slot1 && t.copy_at(0)==Held::crossbow && t.a[1].what==Held::sword);
    }
    {   // a gun picked up while the other arm holds a power or gadget is a copy, never an overwrite (K7)
        ArmSet s;Clock c;engine(s,c,Held::power,false);   // Blink on the left arm (slot 1)
        Fx fx=s.pickup(1,Held::pistol,true,c.t,c.rev);assert(!fx.native && !fx.refused);
        assert(s.copy_at(1)==Held::pistol && s.a[0].what==Held::power && s.derive().slot1_arm==0 && s.derive().cross==0);
        ArmSet g;Clock d;engine(g,d,Held::gadget,false);fx=g.pickup(1,Held::pistol,true,d.t,d.rev);
        assert(!fx.native && g.copy_at(1)==Held::pistol && g.a[0].what==Held::gadget);
    }
    {   // native pickup into an empty slot: the arm claims slot 1, the engine's late equip lands there (78 .. 2172 ms)
        const std::uint64_t lands[]={78,600,1500,2172,3000};
        for(const auto ms:lands) {
            ArmSet s;Clock c;const Fx fx=s.pickup(1,Held::pistol,true,c.t,c.rev);assert(fx.native);
            assert(s.derive().slot1_arm==1 && s.derive().sword_arm==0 && s.derive().cross==3);
            if(ms>1) idle(s,c,Held::none,false,ms);
            engine(s,c,Held::pistol,false);assert(s.a[1].what==Held::pistol && s.a[1].home==Home::slot1 && s.valid());
            assert(!s.pend[1].on);
        }
        ArmSet e;Clock c;e.pickup(1,Held::crossbow,true,c.t,c.rev);   // never lands: the pending request expires, the claim is sticky
        const Fx fx=idle(e,c,Held::none,false,ArmSet::pend_ms+200);assert((fx.expired&2u) && !e.pend[1].on && e.derive().slot1_arm==1);
    }
    {   // a native pickup into the arm that has the sword drawn (not gripped): the sword leaves that arm only, the gun lands there
        ArmSet s;Clock c;s.sword_take(1,c.t);engine(s,c,Held::none,true);s.set_copy(0,Held::pistol,c.t);
        const Fx fx=s.pickup(1,Held::crossbow,true,c.t,c.rev);assert(fx.native && (fx.sword_left&2u) && s.a[1].what==Held::none && s.derive().slot1_arm==1);
        engine(s,c,Held::crossbow,true);assert(s.a[1].what==Held::crossbow && s.copy_at(0)==Held::pistol && s.sword_holder()<0 && s.valid());
    }
    {   // hip draw: the hand at the hip claims slot 1, the sword goes to the other arm; refused when the arm holds a sword or a copy
        ArmSet s;Clock c;assert(!s.hip_draw(1,c.t,c.rev).refused);
        assert(s.derive().slot1_arm==1 && s.derive().sword_arm==0);
        engine(s,c,Held::pistol,false);assert(s.a[1].what==Held::pistol);
        engine(s,c,Held::none,false);assert(s.a[1].what==Held::none && s.derive().slot1_arm==1);   // holstered
        ArmSet t;Clock d;t.sword_take(1,d.t);engine(t,d,Held::none,true);assert(t.hip_draw(1,d.t,d.rev).refused && !t.hip_draw(0,d.t,d.rev).refused);
        ArmSet u;Clock e;u.set_copy(0,Held::pistol,e.t);assert(u.hip_draw(0,e.t,e.rev).refused);
    }
    {   // hip draw with the sword drawn in the other arm: no flip, the sword keeps its seat (K1/K2)
        ArmSet s;Clock c;s.sword_take(1,c.t);engine(s,c,Held::none,true);
        assert(!s.hip_draw(0,c.t,c.rev).refused && s.derive().sword_arm==1 && s.derive().slot1_arm==0);
        engine(s,c,Held::pistol,true,true);assert(s.a[1].what==Held::sword && s.a[0].what==Held::pistol);
    }
    {   // WHEEL, a different item: the old holder keeps its seat until the equip lands, then becomes a copy; the arm gets the new item
        ArmSet s;Clock c;s.hip_draw(1,c.t,c.rev);engine(s,c,Held::pistol,false);   // pistol in the right arm
        s.wheel_open(c.t);const Fx e0=engine(s,c,Held::none,false);(void)e0;           // the game empties slot 1 for the wheel: held
        assert(s.a[1].what==Held::pistol && s.a[1].home==Home::slot1);
        s.wheel_commit(0,c.t,c.rev);s.wheel_choice(0,false,c.t);
        assert(s.derive().slot1_arm==1);   // the previous item does not ride the opening arm in the meantime
        const Fx fx=engine(s,c,Held::crossbow,false);assert((fx.arrived&1u));
        assert(s.a[0].what==Held::crossbow && s.a[0].home==Home::slot1 && s.a[1].what==Held::pistol && s.a[1].home==Home::copy && s.valid());
        assert(s.derive().slot1_arm==0 && s.derive().sword_arm==1);
    }
    {   // WHEEL late landings: the opening arm still gets it at 78 .. 3000 ms; a later landing is engine-initiated (holder / sticky arm)
        const std::uint64_t lands[]={78,235,900,2172,3000};
        for(const auto ms:lands) {
            ArmSet s;Clock c;s.hip_draw(1,c.t,c.rev);engine(s,c,Held::pistol,false);
            s.wheel_commit(0,c.t,c.rev);s.wheel_choice(0,false,c.t);idle(s,c,Held::pistol,false,ms);
            engine(s,c,Held::crossbow,false);
            assert(s.a[0].what==Held::crossbow && s.a[1].home==Home::copy && s.a[1].what==Held::pistol);
        }
    }
    {   // WHEEL, the same item (exact): the opening arm gets a copy at once, the holder keeps the native one
        ArmSet s;Clock c;s.hip_draw(1,c.t,c.rev);engine(s,c,Held::pistol,false);
        s.wheel_commit(0,c.t,c.rev);const Fx fx=s.wheel_choice(0,true,c.t);assert(fx.same_copy && !s.pend[0].on);
        assert(s.copy_at(0)==Held::pistol && s.a[1].what==Held::pistol && s.a[1].home==Home::slot1 && s.derive().slot1_arm==1 && s.valid());
        engine(s,c,Held::pistol,false,false);assert(s.copy_at(0)==Held::pistol);   // nothing to withdraw later (no guess)
        // the same item chosen from the holder's own wheel, a power or nothing: no copy
        ArmSet t;Clock d;t.hip_draw(1,d.t,d.rev);engine(t,d,Held::pistol,false);t.wheel_commit(1,d.t,d.rev);assert(!t.wheel_choice(1,true,d.t).same_copy && t.copy_at(1)==Held::none);
        ArmSet u;Clock e;engine(u,e,Held::power,false);u.wheel_commit(1,e.t,e.rev);
        {const Fx m=u.wheel_choice(1,true,e.t);assert(!m.same_copy && u.copy_at(1)==Held::none);
         // v1090: the same power (Heart, gadget) chosen from the other arm's wheel MOVES there; the engine keeps it equipped
         assert((m.power_moved&2u) && u.a[1].what==Held::power && u.a[1].home==Home::slot1 && u.a[0].what==Held::none && u.derive().slot1_arm==1 && u.valid());}
        engine(u,e,Held::power,false,false);assert(u.a[1].what==Held::power && u.a[0].what==Held::none);
        // the Heart held in the left, the right arm (holding a gun copy) chooses it: the copy falls, the Heart moves right
        ArmSet v;Clock f;engine(v,f,Held::heart,false);v.set_copy(1,Held::pistol,f.t);v.wheel_commit(1,f.t,f.rev);
        {const Fx m=v.wheel_choice(1,true,f.t);assert((m.copy_displaced&2u) && v.a[1].what==Held::heart && v.a[0].what==Held::none && v.valid());}
    }
    {   // a stale request for the other arm does not steal the arrival; a wheel closed on nothing lets the holder go at once
        ArmSet s;Clock c;s.hip_draw(0,c.t,c.rev);s.wheel_commit(1,c.t,c.rev);s.wheel_choice(1,false,c.t);
        assert(!s.pend[0].on && s.pend[1].on);engine(s,c,Held::heart,false);assert(s.a[1].what==Held::heart && s.a[0].what==Held::none);
        ArmSet t;Clock d;t.hip_draw(1,d.t,d.rev);engine(t,d,Held::pistol,false);t.wheel_open(d.t);engine(t,d,Held::none,false);assert(t.a[1].what==Held::pistol);
        t.wheel_commit(1,d.t,d.rev);t.wheel_nothing(1);engine(t,d,Held::none,false,false);idle(t,d,Held::none,false,100);assert(t.a[1].what==Held::none);
    }
    {   // WHEEL fallback (the hook gave no chosen item): no engine change within 700 ms = the same item; a change = an arrival
        ArmSet s;Clock c;s.hip_draw(1,c.t,c.rev);engine(s,c,Held::pistol,false);
        s.wheel_commit(0,c.t,c.rev);idle(s,c,Held::pistol,false,ArmSet::same_fallback_ms+100);
        assert(s.copy_at(0)==Held::pistol && s.a[1].what==Held::pistol);
        ArmSet t;Clock d;t.hip_draw(1,d.t,d.rev);engine(t,d,Held::pistol,false);
        t.wheel_commit(0,d.t,d.rev);idle(t,d,Held::pistol,false,300);engine(t,d,Held::crossbow,false);idle(t,d,Held::crossbow,false,800);
        assert(t.a[0].what==Held::crossbow && t.a[1].home==Home::copy);
    }
    {   // WHEEL: a gun chosen for this arm while the other holds a power or Heart: the gun is this arm's copy and the item is equipped again
        for(const Held pw:{Held::power,Held::gadget,Held::heart}) {
            ArmSet s;Clock c;engine(s,c,pw,false);   // left arm holds it
            s.wheel_commit(1,c.t,c.rev);s.wheel_choice(1,false,c.t);
            const Fx fx=engine(s,c,Held::pistol,false);assert(fx.restore_slot1);
            assert(s.copy_at(1)==Held::pistol && s.a[0].what==pw && s.a[0].home==Home::slot1 && s.derive().slot1_arm==0 && s.valid());
            engine(s,c,pw,false);assert(s.a[0].what==pw && s.copy_at(1)==Held::pistol);   // the engine restored it: nothing moves
        }
    }
    {   // v1041 (headset v1040, journal 26174343): the arm holds a pistol copy, the other a power; a wheel choice makes it a crossbow copy.
        // The pistol copy is displaced (fx.copy_displaced: the caller drops it with gun_drop), the crossbow copy is the arm's at once, the power stays.
        ArmSet s;Clock c;engine(s,c,Held::power,false);s.set_copy(1,Held::pistol,c.t);
        s.wheel_commit(1,c.t,c.rev);s.wheel_choice(1,false,c.t);
        const Fx fx=engine(s,c,Held::crossbow,false);
        assert((fx.copy_displaced&2u) && fx.restore_slot1 && s.copy_at(1)==Held::crossbow && s.a[0].what==Held::power && s.valid());
    }
    {   // WHEEL POWER SWAP: the arm that holds Blink picks a gun: the gun becomes its copy, Blink moves to the other (empty) arm
        ArmSet s;Clock c;engine(s,c,Held::power,false);
        s.wheel_commit(0,c.t,c.rev);s.wheel_choice(0,false,c.t);
        const Fx fx=engine(s,c,Held::pistol,false);assert(fx.restore_slot1 && (fx.power_moved&2u));
        assert(s.copy_at(0)==Held::pistol && s.a[1].what==Held::power && s.derive().slot1_arm==1 && s.valid());
        ArmSet t;Clock d;engine(t,d,Held::power,false);t.set_copy(1,Held::crossbow,d.t);
        t.wheel_commit(0,d.t,d.rev);t.wheel_choice(0,false,d.t);
        const Fx f2=engine(t,d,Held::pistol,false);assert(!f2.restore_slot1 && t.a[0].what==Held::pistol && t.a[0].home==Home::slot1 && t.copy_at(1)==Held::crossbow);   // other arm busy: the power is gone, the gun native
    }
    {   // WHEEL: a non-gun chosen replaces the held item; the other arm's gun becomes a copy; the sword in the opening arm leaves only that arm
        ArmSet s;Clock c;s.sword_take(1,c.t);engine(s,c,Held::none,true);s.hip_draw(0,c.t,c.rev);engine(s,c,Held::pistol,true);
        s.wheel_commit(1,c.t,c.rev);s.wheel_choice(1,false,c.t);
        const Fx fx=engine(s,c,Held::heart,true);assert((fx.sword_left&2u) && s.a[1].what==Held::heart && s.a[0].home==Home::copy && s.a[0].what==Held::pistol && s.valid());
        assert(s.derive().slot1_arm==1 && s.derive().sword_arm==0);
        engine(s,c,Held::heart,true,false);idle(s,c,Held::heart,true,200);   // the engine still has the sword out: not re-placed anywhere
        assert(s.sword_holder()<0 && s.a[0].what==Held::pistol);
        engine(s,c,Held::heart,false);assert(s.sword_holder()<0 && !s.sword_leaving);
    }
    {   // engine truth: ammo out / unequip clears the arm; a kind change updates it in place; the seat stays
        ArmSet s;Clock c;s.hip_draw(0,c.t,c.rev);engine(s,c,Held::gadget,false);assert(s.a[0].what==Held::gadget);
        engine(s,c,Held::none,false);assert(s.a[0].what==Held::none && s.derive().slot1_arm==0);
        engine(s,c,Held::pistol,false);assert(s.a[0].what==Held::pistol);
        engine(s,c,Held::crossbow,false);assert(s.a[0].what==Held::crossbow && s.a[0].home==Home::slot1);
        ArmSet t;Clock d;engine(t,d,Held::pistol,false);assert(t.a[0].what==Held::pistol);   // engine-initiated: the sticky arm
    }
    {   // an intermediate report that does not settle is not applied (flicker through the equip)
        ArmSet s;Clock c;s.hip_draw(1,c.t,c.rev);++c.rev;s.native_changed({Held::none,false,c.rev},c.t);c.t+=20;++c.rev;s.native_changed({Held::pistol,false,c.rev},c.t);
        assert(s.a[1].what==Held::none);c.t+=ArmSet::settle_ms;s.native_changed({Held::pistol,false,c.rev},c.t);assert(s.a[1].what==Held::pistol);
    }
    {   // a gadget catch: the arm claims slot 1 when it is free; refused with a holder
        ArmSet s;Clock c;assert(!s.catch_gadget(1,c.t,c.rev).refused && s.derive().slot1_arm==1);
        engine(s,c,Held::gadget,false);assert(s.a[1].what==Held::gadget);
        // v1046: a free hand catches even when the other arm holds a gun natively: the take lands in the catching arm, the gun stays in the other as its copy
        ArmSet t;Clock d;engine(t,d,Held::pistol,false);assert(t.a[0].what==Held::pistol && t.can_catch_gadget(1));
        assert(!t.catch_gadget(1,d.t,d.rev).refused && t.pend[1].on && t.a[0].what==Held::pistol && t.valid());   // the ticket only: the other arm is untouched until the arrival
        engine(t,d,Held::heart,false);assert(t.a[1].what==Held::heart && t.a[1].home==Home::slot1 && t.copy_at(0)==Held::pistol && t.derive().slot1_arm==1 && t.valid());
        ArmSet u;Clock e;engine(u,e,Held::power,false);assert(u.can_catch_gadget(1) && !u.catch_gadget(1,e.t,e.rev).refused);
        engine(u,e,Held::heart,false);assert(u.a[1].what==Held::heart && u.a[0].what==Held::none && u.valid());   // a power in the other arm is put away
        ArmSet w;Clock f;w.set_copy(1,Held::pistol,f.t);assert(!w.can_catch_gadget(1) && w.catch_gadget(1,f.t,f.rev).refused && w.copy_at(1)==Held::pistol);   // a hand holding a gun copy is not free
        ArmSet x;Clock g;x.hip_draw(0,g.t,g.rev);assert(!x.can_catch_gadget(1) && x.catch_gadget(1,g.t,g.rev).refused);   // the other arm's request is outstanding
    }
    {   // release / test copies; guard weapons are copies only
        ArmSet s;Clock c;s.set_copy(1,Held::guard_sword,c.t);assert(s.copy_at(1)==Held::guard_sword && s.valid());
        const Fx fx=s.pickup(0,Held::guard_pistol,true,c.t,c.rev);assert(!fx.native && s.copy_at(0)==Held::guard_pistol);
        s.release(0);assert(s.copy_at(0)==Held::none && s.copy_at(1)==Held::guard_sword && s.a[0].home==Home::none);
        assert(s.set_copy(0,Held::power,c.t).refused);
    }
    {   // a native item arriving in an arm that holds a copy displaces the copy (it falls); the other arm is untouched
        ArmSet s;Clock c;s.set_copy(0,Held::pistol,c.t);s.set_copy(1,Held::crossbow,c.t);
        s.wheel_commit(0,c.t,c.rev);s.wheel_choice(0,false,c.t);
        const Fx fx=engine(s,c,Held::heart,false);assert((fx.copy_displaced&1u) && s.a[0].what==Held::heart && s.copy_at(1)==Held::crossbow);
    }
    {   // property: random events on one arm never change the other arm's item, except the power swap moving a power into an empty one
        for(unsigned round=0;round<400;++round) {
            ArmSet s;Clock c;Held eng=Held::none;bool sw=false;
            for(unsigned step=0;step<250;++step) {
                const unsigned h=rnd(2),o=h^1u,which=rnd(14);const Arm before=s.a[o];const Plan pb=s.derive();(void)pb;
                Fx fx;bool local=true;
                switch(which) {
                case 0:fx=s.sword_take(h,c.t);break;
                case 1:fx=s.hip_draw(h,c.t,c.rev);break;
                case 2:fx=s.pickup(h,rnd(2)?Held::pistol:Held::crossbow,rnd(2)!=0,c.t,c.rev);break;
                case 3:fx=s.pickup(h,rnd(2)?Held::guard_sword:Held::guard_pistol,true,c.t,c.rev);break;
                case 4:fx=s.catch_gadget(h,c.t,c.rev);break;
                case 5:s.release(h);break;
                case 6:fx=s.set_copy(h,rnd(2)?Held::pistol:Held::none,c.t);break;
                case 7:s.wheel_open(c.t);break;
                case 8:fx=s.wheel_commit(h,c.t,c.rev);break;
                case 9:fx=s.wheel_choice(h,rnd(2)!=0,c.t);break;
                default: { // engine truth: anything, time passes
                    local=false;c.t+=rnd(200);if(rnd(3)==0) {++c.rev;const Held k[]={Held::none,Held::pistol,Held::crossbow,Held::gadget,Held::heart,Held::power};eng=k[rnd(6)];}
                    if(rnd(5)==0) sw=!sw;fx=s.native_changed({eng,sw,c.rev},c.t);break; }
                }
                assert(s.valid());
                const Plan p=s.derive();assert(p.slot1_arm!=p.sword_arm && p.cross==((p.slot1_arm==1?1u:0u)|(p.sword_arm==0?2u:0u)));
                if(local) {
                    const bool moved_in=(fx.power_moved>>o)&1u;
                    // wheel_choice(same) copies a gun into arm h only; nothing here may touch arm o's item except a power moving in
                    // v1090: or a power the other arm held moving to arm h (the same power chosen from h's wheel)
                    const bool moved_out=which==9 && ((fx.power_moved>>h)&1u) && slot1_only(before.what) && s.a[o].what==Held::none;
                    assert(s.a[o].what==before.what || (moved_in && before.what==Held::none) || moved_out);
                    // the other arm's copy never changes through a local event
                    if(before.home==Home::copy) assert(s.a[o].home==Home::copy && s.a[o].what==before.what);
                }
                c.t+=rnd(40);
            }
        }
    }
    std::puts("arm_state_check: ok");
    return 0;
}
