// v1224: a native gun thrown from one hand and caught by the other is always the same native gun (src/gun_catch.hpp, ArmSet::drop_slot1).
// Replays the two logged races (headset v1209 tick 179921843 -> 179921890: right drops the pistol, left catches 47 ms later; v1216 tick
// 183843546 -> 183843578: left drops the crossbow, right catches 32 ms later) and a sweep of catch delays against a model of the engine's
// slot 1: the drop's instant unequip lands U ms later (the logs bound it: a 47 ms catch still saw the gun, every catch from 78 ms on was
// native), an equip lands one game tick after it is sent, and each change bumps the equip revision. The game thread runs every 16 ms,
// the control thread every 11 ms; the real ArmSet and gun_catch code decide. The old input rule is run too: it must make a copy.
#include "../src/arm_state.hpp"
#include "../src/gun_catch.hpp"
#include <cassert>
#include <cstdio>
using namespace d2vr;
using H=arm::Held;
struct Result {unsigned copies{},native_hand_mask{};bool lying{},engine_has{},consistent{},claimed{};};
// U: unequip delay (-1: the engine never finishes it, a running clip cancelled it); C: catch delay after the drop.
static Result run(unsigned dropper,H kind,int U,int C,bool old_rule) {
    const unsigned catcher=dropper^1u;
    arm::ArmSet arms;std::uint64_t rev=1;H slot1=kind;
    std::uint64_t t=1000;
    arms.rest_slot1=dropper;
    for(;t<1100;++t) if(t%11==0) arms.native_changed({slot1,false,rev},t);   // the gun is natively in the dropper's arm
    assert(arms.a[dropper].what==kind && arms.a[dropper].home==arm::Home::slot1);
    const std::uint64_t drop=t;
    arms.drop_slot1(dropper,drop);                                          // GunGrip let go away from the hip (control thread)
    bool lying=false,published=false,published_unequipping=false,published_claimed=false;gun_catch::Lying lc{};
    bool pickup_queued=false,taken=false;std::uint64_t equip_lands=0;
    for(;t<drop+4000;++t) {
        // engine
        if(U>=0 && t==drop+static_cast<std::uint64_t>(U) && slot1==kind) {slot1=H::none;++rev;}
        if(equip_lands && t>=equip_lands) {if(slot1!=kind) {slot1=kind;++rev;}equip_lands=0;}
        // game thread
        if(t%16==0) {
            if(!lying && !taken && t>drop) {lying=true;lc=gun_catch::Lying{};lc.dropped=t;}   // Op::gun_drop ran: the lying gun exists
            if(pickup_queued) {   // Op::gun_pickup (native): claimed during the unequip, else equipped now (gun_take_native)
                pickup_queued=false;const bool in_slot=slot1==kind;
                if(!in_slot) lc.left_slot=true;
                if(!old_rule && !lc.left_slot) lc.claim_for(catcher,t);
                else {if(!in_slot) equip_lands=t+16;lying=false;}             // old: "already back in the hand" when it still showed: released
            }
            if(lying && (lc.claim>=0 || !lc.left_slot)) {
                using D=gun_catch::Lying::Do;
                switch(lc.step(slot1==kind,t)) {
                    case D::arrived: lying=false;lc=gun_catch::Lying{};break;   // gun_release
                    case D::give_up: break;
                    case D::equip: if(slot1==H::none) {equip_lands=t+16;lc.equipped(t);}break;
                    default: break;
                }
            }
            published=lying;published_unequipping=lying && lc.unequipping();published_claimed=lying && lc.claim>=0;
        }
        // control thread
        if(t%11==0) {
            arms.native_changed({slot1,false,rev},t);
            const bool fresh=t>=drop+static_cast<std::uint64_t>(C) && t<=drop+static_cast<std::uint64_t>(C)+150;   // the catching hand closed at C (fresh_squeeze)
            if(fresh && !taken && published && !published_claimed && arms.free_for_pickup(catcher)) {
                const bool shows=slot1!=H::none;
                const bool ok=old_rule?!shows:gun_catch::native_ok(true,shows,published_unequipping,false);
                auto plan=arms;const auto fx=plan.pickup(catcher,kind,ok,t,rev);
                if(!fx.refused) {arms=plan;taken=true;
                    if(fx.native) pickup_queued=true;
                    else {lying=published=false;}}                   // as_copy: the lying entity becomes this hand's copy
            }
        }
    }
    Result r;r.copies=(arms.copy_at(0)!=H::none?1u:0u)+(arms.copy_at(1)!=H::none?1u:0u);
    for(unsigned h=0;h<2;++h) if(arms.a[h].home==arm::Home::slot1 && arms.a[h].what==kind) r.native_hand_mask|=1u<<h;
    r.lying=lying;r.engine_has=slot1==kind;r.claimed=lc.claim>=0;
    r.consistent=arms.valid() && !arms.any_pending() && arms.derive().slot1_arm==catcher;
    return r;
}
static void expect_native(unsigned dropper,H kind,int U,int C) {
    const auto r=run(dropper,kind,U,C,false);
    if(!(r.copies==0 && r.native_hand_mask==(1u<<(dropper^1u)) && !r.lying && r.engine_has && r.consistent && !r.claimed)) {
        std::printf("FAIL dropper %u kind %u unequip %d catch %d: copies %u native mask %u lying %d engine %d consistent %d claimed %d\n",dropper,static_cast<unsigned>(kind),U,C,
            r.copies,r.native_hand_mask,r.lying,r.engine_has,r.consistent,r.claimed);std::fflush(stdout);
        assert(false);
    }
}
int main() {
    // the two logged races, as the logs show them; and the old rule makes the copy the logs show ("native":0)
    expect_native(1,H::pistol,64,47);
    expect_native(0,H::crossbow,64,32);
    {const auto a=run(1,H::pistol,64,47,true),b=run(0,H::crossbow,64,32,true);assert(a.copies==1 && !a.native_hand_mask && b.copies==1 && !b.native_hand_mask);}
    // every catch delay, both directions, both kinds, the unequip anywhere in the logged bound (47..78 ms)
    for(const int U:{48,64,77}) for(const int C:{0,16,32,47,60,120,300}) for(unsigned d=0;d<2;++d) for(const H k:{H::pistol,H::crossbow}) expect_native(d,k,U,C);
    // the put-away window: an unequip the engine never finishes (a running clip cancelled it), caught at once and late in the window
    for(const int C:{0,32,1200}) for(unsigned d=0;d<2;++d) expect_native(d,H::pistol,-1,C);
    // a drop nobody catches still ends with the gun out of every hand (the floor's), and no copy
    {   arm::ArmSet arms;arms.rest_slot1=0;std::uint64_t t=1000;for(;t<1100;++t) if(t%11==0) arms.native_changed({H::pistol,false,1},t);
        arms.drop_slot1(0,t);for(const auto end=t+3000;t<end;++t) if(t%11==0) arms.native_changed({t<1164?H::pistol:H::none,false,t<1164?1u:2u},t);
        assert(arms.slot1_holder()<0 && arms.copy_at(0)==H::none && arms.copy_at(1)==H::none && arms.valid());}
    // a power in slot 1 is not a native gun: drop_slot1 refuses (the power swap stays ArmSet::place's)
    {arm::ArmSet arms;arms.a[0]={H::power,arm::Home::slot1};assert(arms.drop_slot1(0,5).refused && arms.a[0].what==H::power);}
    std::printf("gun_catch_check ok\n");
    return 0;
}
