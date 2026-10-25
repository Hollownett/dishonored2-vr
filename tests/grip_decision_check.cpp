#include "../src/grip_decision.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
namespace {
// The palm at the origin facing +x (palm axis +z): palm point (0,0,0.015), uncalibrated arm (0.65 m).
PalmProbe probe() {PalmProbe p;p.palm={0,0,0};p.forward={0,0,1};p.direction={1,0,0};p.valid=true;return p;}
PalmTarget box(std::uint64_t id,PalmKind kind,Vec3 centre,double half,const GrabTuning& t) {
    PalmTarget x;x.id=id;x.kind=kind;x.priority=t.priority(kind);x.minimum=centre-Vec3{half,half,half};x.maximum=centre+Vec3{half,half,half};return x;
}
// The physical lane's score of a prop or bone, computed with the same probe (as NativeGrab::focus_score reports it).
FocusScore physical(const PalmTarget& t) {const auto s=palm_cost(probe(),t,.25,grab_retain_distance);assert(s);return {t.id,t.kind,s->cost,0,s->distance};}
GripAloneInput input(std::optional<PalmTarget> native,std::optional<FocusScore> phys,std::uint64_t at=1000) {
    GripAloneInput in;in.native=native;in.physical=phys;in.probe=probe();in.native_fresh=true;in.since_ms=1000;in.now_ms=at;return in;
}
}
int main() {
    const GrabTuning t;const double hover=t.hover_radius_m;
    {   // loot_wins_use: a coin the palm is on (0.03 m, loot 0.8) against a prop 0.08 m ahead
        const auto coin=box(1,PalmKind::loot,{.06,0,.015},.01,t);const auto prop=physical(box(2,PalmKind::prop,{.13,0,.015},.05,t));
        assert(std::abs(prop.distance-.08)<1e-9);
        assert(decide_grip_alone(input(coin,prop),t,hover)==GripAloneChoice::use);
    }
    {   // door_wins_use: a door leaf faced at 0.03 m against a prop beside the hand at 0.10 m
        const auto door=box(3,PalmKind::usable,{.53,0,.015},.5,t);const auto prop=physical(box(4,PalmKind::prop,{0,.15,.015},.05,t));
        assert(std::abs(prop.distance-.10)<1e-9);
        assert(decide_grip_alone(input(door,prop),t,hover)==GripAloneChoice::use);
    }
    {   // prop_on_palm_wins_grab: a prop under the palm (0.02 m, faced) against the table shell 0.2 m below
        const auto table=box(5,PalmKind::usable,{0,0,-.685},.5,t);const auto prop=physical(box(6,PalmKind::prop,{.07,0,.015},.05,t));
        assert(std::abs(prop.distance-.02)<1e-9 && palm_cost(probe(),table,hover,grab_acquire_distance)->distance>.19);
        assert(decide_grip_alone(input(table,prop),t,hover)==GripAloneChoice::grab);
        auto living=prop;living.kind=PalmKind::living;   // ruling H6: an awake NPC's bone is a grab too
        assert(decide_grip_alone(input(table,living),t,hover)==GripAloneChoice::grab);
    }
    {   // prop_at_013_is_not_grip_alone: 0.13 m is outside the 0.12 m gate (a fist's 0.45 m gate takes it); nothing else: the H1 buffer, then use
        const auto prop=physical(box(7,PalmKind::prop,{.18,0,.015},.05,t));assert(std::abs(prop.distance-.13)<1e-9);
        assert(decide_grip_alone(input(std::nullopt,prop,1050),t,hover)==GripAloneChoice::pending);
        assert(decide_grip_alone(input(std::nullopt,prop,1150),t,hover)==GripAloneChoice::use);
        const auto on=physical(box(8,PalmKind::prop,{.165,0,.015},.05,t));   // H1: a prop reaching 0.10 m inside the window is caught
        assert(decide_grip_alone(input(std::nullopt,on,1120),t,hover)==GripAloneChoice::grab);
        GrabTuning off=t;off.grab_buffer_ms=0;assert(decide_grip_alone(input(std::nullopt,prop,1000),off,hover)==GripAloneChoice::use);
    }
    {   // block_next_to_table_stays_use: a table shell usable at 0.2 m and no prop within 0.12 m
        const auto table=box(9,PalmKind::usable,{0,0,-.685},.5,t);const auto prop=physical(box(10,PalmKind::prop,{.265,0,.015},.05,t));
        assert(prop.distance>.12 && decide_grip_alone(input(table,prop),t,hover)==GripAloneChoice::use);
        assert(decide_grip_alone(input(table,std::nullopt),t,hover)==GripAloneChoice::use);
    }
    const auto on_palm=physical(box(11,PalmKind::prop,{.07,0,.015},.05,t));
    {   // choke_offer_wins and takedown_offer_wins: chokes and takedowns keep priority over a prop on the palm
        auto in=input(std::nullopt,on_palm);in.choke_owns=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
        in=input(std::nullopt,on_palm);in.takedown_offered=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
    }
    {   // waits_for_fresh_native_up_to_100: pending at 99 ms, decided at 100 ms (never longer)
        auto in=input(std::nullopt,on_palm,1099);in.native_fresh=false;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::pending);
        in.now_ms=1100;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab);
        in=input(std::nullopt,on_palm,1000);assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab);   // ruling H2: a fresh "none" decides now
    }
    {   // occupied_hand_never_grabs
        auto in=input(std::nullopt,on_palm);in.occupied=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
    }
    {   // a native target beyond the palm's 0.45 m gate (a gaze Use) beats nothing, but a prop on the palm beats it; a tie goes to Use
        const auto far_door=box(12,PalmKind::usable,{2,0,0},.1,t);
        assert(decide_grip_alone(input(far_door,std::nullopt),t,hover)==GripAloneChoice::use);
        assert(decide_grip_alone(input(far_door,on_palm),t,hover)==GripAloneChoice::grab);
        auto twin=box(13,PalmKind::usable,{.07,0,.015},.05,t);auto tie=physical(box(14,PalmKind::prop,{.07,0,.015},.05,t));
        assert(decide_grip_alone(input(twin,tie),t,hover)==GripAloneChoice::use);
    }
    {   // corpse_own_carry_yields_grab: the corpse's own native carry (its whole-body box holds the palm: always cheaper) against its bone on the
        // palm: the user's decision (grip alone grabs corpses) -> grab. Another entity's usable with the same geometry still wins Use.
        const auto body=box(0x9000,PalmKind::usable,{.07,0,.015},.3,t);auto bone=physical(box(15,PalmKind::bone,{.07,0,.015},.05,t));
        assert(palm_cost(probe(),body,hover,grab_acquire_distance)->cost<bone.cost);
        bone.entity=0x9000;assert(decide_grip_alone(input(body,bone),t,hover)==GripAloneChoice::grab);
        bone.entity=0x9001;assert(decide_grip_alone(input(body,bone),t,hover)==GripAloneChoice::use);
        auto far_bone=bone;far_bone.entity=0x9000;far_bone.distance=.2;assert(decide_grip_alone(input(body,far_bone,1200),t,hover)==GripAloneChoice::use);   // not on it
        auto in=input(body,bone);bone.entity=0x9000;in.physical=bone;in.choke_owns=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
    }
    {   // I1: a living NPC's native target on HIM (choke, talk, knock-out) is Use; a corpse's own carry stays grab. His hold waits for a current verdict.
        const auto npc_box=box(0x9100,PalmKind::usable,{.07,0,.015},.3,t);auto bone=physical(box(16,PalmKind::living,{.07,0,.015},.05,t));
        bone.entity=0x9100;assert(decide_grip_alone(input(npc_box,bone),t,hover)==GripAloneChoice::use);
        bone.kind=PalmKind::bone;assert(decide_grip_alone(input(npc_box,bone),t,hover)==GripAloneChoice::grab);
        bone.kind=PalmKind::living;
        auto in=input(std::nullopt,bone,1050);in.native_fresh=false;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::pending);   // stale verdict: pending
        in.now_ms=1120;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::pending);   // past the 100 ms fresh wait, inside the 150 ms window: still pending for a living NPC
        in.now_ms=1160;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);        // window over: native, never an unverified living hold
        in=input(std::nullopt,bone,1000);assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab);   // fresh "none": the hold
    }
    {   // I2: the left hand at the hip or with a gun out is Use at the edge (no 150 ms buffer), even for a prop on the palm or nothing in reach
        auto in=input(std::nullopt,std::nullopt,1000);in.fast_native=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
        in=input(std::nullopt,on_palm);in.fast_native=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);
        in=input(std::nullopt,std::nullopt,1000);assert(decide_grip_alone(in,t,hover)==GripAloneChoice::pending);   // without it the H1 buffer still runs
    }
    {   // v613 item 4: the torso hold of 0.5 s hands off to the native carry; limbs, head, collar (neck), a quick hold and the legacy knob never do
        CarryHandoffInput in;in.corpse_hold=true;in.grip_held=true;in.native_is_corpse=true;in.held_ms=corpse_carry_hold_ms;
        for(int bone=0;bone<17;++bone) {in.bone=bone;assert(carry_handoff(in)==(bone<=2));}   // pelvis, spine, chest only
        in.bone=1;in.held_ms=corpse_carry_hold_ms-1;assert(!carry_handoff(in));                   // a quick torso grab stays the drag
        in.held_ms=corpse_carry_hold_ms;in.grip_held=false;assert(!carry_handoff(in));
        in.grip_held=true;in.corpse_hold=false;assert(!carry_handoff(in));                         // a prop or nothing
        in.corpse_hold=true;in.legacy=true;assert(!carry_handoff(in));                             // build/legacy-corpse-carry
        in.legacy=false;in.bone=-1;assert(!carry_handoff(in));
        in.bone=1;assert(carry_handoff(in));in.native_is_corpse=false;assert(!carry_handoff(in));   // the Use target must be this body
        in.native_is_corpse=true;in.occupied=true;assert(!carry_handoff(in));                      // never while carrying or holding something
    }
    std::cout<<"PASS: grip-alone one-list decision (corpse self-carry yields the grab),  (loot/door Use, prop on palm grabs, 0.12 m gate, block stays Use, choke/takedown/occupied, 100 ms fresh wait, H1 buffer)\n";
}
