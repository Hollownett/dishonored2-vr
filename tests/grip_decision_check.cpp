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
FocusScore physical(const PalmTarget& t) {const auto s=palm_cost(probe(),t,.25,1.);assert(s);return {t.id,t.kind,s->cost,0,s->distance};}
GripAloneInput input(std::optional<PalmTarget> native,std::optional<FocusScore> phys,std::uint64_t at=1000) {
    GripAloneInput in;in.native=native;in.physical=phys;in.probe=probe();in.native_fresh=true;in.since_ms=1000;in.now_ms=at;return in;
}
}
int main() {
    { // v651: the sword's Block steps aside from a left Use squeeze, never from a gripped gun, a choke or an open hand
        assert(block_yields_to_left_use(true,false,false,true));
        assert(!block_yields_to_left_use(false,false,false,true) && !block_yields_to_left_use(true,true,false,true) &&
               !block_yields_to_left_use(true,false,true,true) && !block_yields_to_left_use(true,false,false,false));
    }
    { // A grip-only prop grab uses the complete hand sphere, not the former 10cm gate.
        GrabTuning tuning;GripAloneInput in;in.native_fresh=true;in.probe=hand_sphere_probe({});
        in.physical=FocusScore{42,PalmKind::prop,.20,0.,.20,0x1234};
        assert(decide_grip_alone(in,tuning,.25)==GripAloneChoice::grab);
        in.physical->distance=.26;in.now_ms=1000;assert(decide_grip_alone(in,tuning,.25)!=GripAloneChoice::grab);
        in.physical->distance=.20;in.physical->kind=PalmKind::bone;
        assert(decide_grip_alone(in,tuning,.25)!=GripAloneChoice::grab);
    }

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
    {   // v1086 severed limb: the game's Use target IS the prop the grab can hold (same entity): the sticking grab wins whatever the costs;
        // the same distances with another entity keep the old cost rule (Use)
        const auto limb_use=box(7,PalmKind::usable,{.06,0,.015},.01,t);auto limb=physical(box(8,PalmKind::prop,{0,.15,.015},.05,t));
        limb.entity=7;assert(decide_grip_alone(input(limb_use,limb),t,hover)==GripAloneChoice::grab);
        limb.entity=9;assert(decide_grip_alone(input(limb_use,limb),t,hover)==GripAloneChoice::use);
        auto loot_limb=limb;loot_limb.entity=10;const auto loot=box(10,PalmKind::loot,{.06,0,.015},.01,t);
        assert(decide_grip_alone(input(loot,loot_limb),t,hover)==GripAloneChoice::use);   // loot stays Use
        // v1087: the game's Use target of a severed limb is kind LOOT (takeable(): it owns arkCpntPickable_Simple, like a bottle) - v1086's test said
        // usable, so it passed while the game kept Use (v1086 log: native = physical = the limb, native_kind 0, 7 presses -> Use). A carryable's
        // own Use is only the game's carry: the grab takes it, whatever the costs (here the limb's whole box holds the palm: native cost 0).
        auto carry_in=input(loot,loot_limb);carry_in.native_carry=true;
        assert(decide_grip_alone(carry_in,t,hover)==GripAloneChoice::grab);
        auto other_in=carry_in;other_in.physical->entity=11;assert(decide_grip_alone(other_in,t,hover)==GripAloneChoice::use);   // another entity: the costs decide
        // v1088: a severed limb is NEVER the game's carry (v1087 headset: every right-hand press without a palm pick went to Use, the floating carry)
        auto limb_in=carry_in;limb_in.native_limb=true;
        assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::grab);                                   // its own pick: grabbed
        limb_in.physical->distance=.45;assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::grab);   // out to the limb reach (where the game's Use reaches)
        limb_in.physical->distance=.6;limb_in.now_ms=limb_in.since_ms+20;
        assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::pending);                                // farther: the press waits for the pick ...
        limb_in.now_ms=limb_in.since_ms+1000;assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::none);   // ... then nothing - never Use
        limb_in.physical.reset();assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::none);         // no pick at all: nothing
        limb_in.physical=physical(box(10,PalmKind::prop,{.06,0,.015},.01,t));limb_in.physical->entity=10;limb_in.occupied=true;
        assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::none);                                   // a busy hand: nothing, not Use
        limb_in.occupied=false;limb_in.fast_native=true;assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::grab);   // the hip / gun rule does not hand a limb to Use
        limb_in.fast_native=false;limb_in.physical->entity=12;limb_in.physical->distance=.2;
        assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::grab);   // another prop under the palm (a book on the leg): grabbed, as before
        limb_in.physical->distance=.3;assert(decide_grip_alone(limb_in,t,hover)==GripAloneChoice::none);   // ... only in its own 25 cm gate; never the limb's Use
    }
    {   // prop_on_palm_wins_grab: a prop under the palm (0.02 m, faced) against the table shell 0.2 m below
        const auto table=box(5,PalmKind::usable,{0,0,-.685},.5,t);const auto prop=physical(box(6,PalmKind::prop,{.07,0,.015},.05,t));
        assert(std::abs(prop.distance-.02)<1e-9 && palm_cost(probe(),table,hover,grab_acquire_distance)->distance>.19);
        assert(decide_grip_alone(input(table,prop),t,hover)==GripAloneChoice::grab);
        auto living=prop;living.kind=PalmKind::living;   // ruling H6: an awake NPC's bone is a grab too
        assert(decide_grip_alone(input(table,living),t,hover)==GripAloneChoice::grab);
    }
    {   // Outside the hand sphere: buffer the press, then use; entering the sphere catches it.
        const auto prop=physical(box(7,PalmKind::prop,{.31,0,.015},.05,t));assert(std::abs(prop.distance-.26)<1e-9);
        assert(decide_grip_alone(input(std::nullopt,prop,1050),t,hover)==GripAloneChoice::pending);
        assert(decide_grip_alone(input(std::nullopt,prop,1150),t,hover)==GripAloneChoice::use);
        const auto on=physical(box(8,PalmKind::prop,{.145,0,.015},.05,t));   // H1: a prop reaching 0.10 m inside the window is caught
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
    {   // The game's selected loot action on a corpse wins over a bone of that same corpse.
        const auto loot=box(0x9000,PalmKind::loot,{.07,0,.015},.3,t);
        auto bone=physical(box(15,PalmKind::bone,{.07,0,.015},.05,t));bone.entity=0x9000;
        assert(decide_grip_alone(input(loot,bone),t,hover)==GripAloneChoice::use);
        auto corpse=loot;corpse.kind=PalmKind::usable;
        assert(decide_grip_alone(input(corpse,bone),t,hover)==GripAloneChoice::grab);
        // v674: the live Loot offer wins even when the body's bounds are outside the palm's cost gate (a thin note on a
        // belt, a stale model box): its own bone still must not steal the press. A bone of another body keeps the grab.
        const auto far_loot=box(0x9000,PalmKind::loot,{2,0,0},.3,t);
        assert(!palm_cost(probe(),far_loot,hover,grab_acquire_distance));
        assert(decide_grip_alone(input(far_loot,bone),t,hover)==GripAloneChoice::use);
        assert(decide_grip_alone(input(far_loot,std::nullopt),t,hover)==GripAloneChoice::use);
        auto other_bone=bone;other_bone.entity=0x9001;other_bone.distance=.05;
        assert(decide_grip_alone(input(far_loot,other_bone),t,hover)==GripAloneChoice::grab);
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
    {   // a corpse let go at a shoulder stays there (the physical dock); a fresh squeeze there takes it back; the other hand or any release ends it
        const auto held=[](float grip,bool zone,std::uint64_t ms=1000,double lag=.1) {ShoulderDockIn in;in.corpse_hold=true;in.entity=0x77;in.held_ms=ms;in.lag_m=lag;in.in_zone=zone;in.grip=grip;return in;};
        ShoulderDock d;
        d.update(1,held(1,false));assert(!d.update(1,held(0,false)).anchored);         // let go away from the shoulder: an ordinary release
        d.update(1,held(1,true,100));assert(!d.update(1,held(0,true,100)).anchored);   // grabbed at shoulder height and let go at once: not lifted there
        d.update(1,held(1,true,1000,.8));assert(!d.update(1,held(0,true,1000,.8)).anchored);   // the body left behind, the hand alone at the shoulder
        d.update(1,held(1,true));
        {const auto o=d.update(1,held(0,true));assert(o.docked && o.anchored && d.anchored(1) && !d.anchored(0));}
        assert(d.update(1,held(0,false)).anchored && d.update(1,held(.3f,false)).anchored);   // the free hand moves and half-squeezes elsewhere: still on the shoulder
        assert(d.update(1,held(.9f,false)).anchored && !d.consumes(1));                      // a full squeeze away from the shoulder is the hand's own (no take)
        d.update(1,held(0,false));
        {const auto o=d.update(1,held(.9f,true));assert(o.resumed && !o.anchored && d.consumes(1));}   // a squeeze at the shoulder takes it back
        assert(d.update(1,held(.9f,false)).anchored==false && d.consumes(1));                // carried in the hand now; the squeeze stays the dock's until let go
        d.update(1,held(0,false));assert(!d.consumes(1));
        d.update(1,held(1,true));d.update(1,held(0,true));assert(d.anchored(1));
        {auto in=held(0,true);in.other_holds=true;assert(!d.update(1,in).anchored);}         // the other hand took the body
        d.update(1,held(1,true));d.update(1,held(0,true));assert(d.anchored(1));
        {auto in=held(0,true);in.corpse_hold=false;assert(!d.update(1,in).anchored);}        // the hold ended (pulled off, the lag rule, a fault)
        d.update(1,held(1,true));d.update(1,held(0,true));
        {auto in=held(0,true);in.entity=0x78;assert(!d.update(1,in).anchored);}              // a different body is not the docked one
        {auto in=held(1,true);in.lag_m=std::nan("");d.update(0,in);in.grip=0;assert(!d.update(0,in).anchored);}
        d.update(1,held(1,true));d.update(1,held(0,true));assert(d.anchored(1));
        {auto in=held(0,true);in.native_carry=true;const auto o=d.update(1,in);assert(o.take && !o.anchored && !d.anchored(1) && !d.consumes(1));}   // v1019: the game's own carry took the body; the dock's hand lets go and the squeeze stays the hand's
        assert(d.update(1,held(0,false)).anchored==false);
        // v1067: held at the shoulder 0.12 s docks without letting go; lifted with both hands docks and stays; a hold the other hand starts later ends it
        {ShoulderDock e;auto in=held(1,true);in.now=5000;assert(!e.update(0,in).anchored);in.now=5080;assert(!e.update(0,in).anchored);
         in.now=5130;{const auto o=e.update(0,in);assert(o.docked && o.anchored);}
         in.now=5300;assert(e.update(0,in).anchored && !e.update(0,in).resumed);                 // the squeeze that lifted it does not take it back
         auto out=held(1,false);out.now=6000;e.reset(0);e.update(0,out);out.now=6300;assert(!e.update(0,out).anchored);   // away from the shoulder: never
         auto two=held(1,true);two.other_holds=true;two.now=7000;e.update(0,two);two.now=7250;assert(e.update(0,two).docked);
         two.now=7300;assert(e.update(0,two).anchored);                                           // both hands lifted it: stays docked
         two.other_holds=false;e.update(0,two);two.other_holds=true;assert(!e.update(0,two).anchored);}   // the other hand grabs it again later: ends
    }
    {   // the shoulder zones (tracking space, head at the origin looking down -Z, +Y up): either shoulder, not in front of the face, not at the waist
        const std::array<double,3> head{0,1.7,0},fwd{0,0,-1},up{0,1,0};
        const auto at=[&](double x,double y,double z) {return at_shoulder(head,fwd,up,{x,y,z});};
        assert(at(.18,1.52,.03) && at(-.18,1.52,.03));            // right and left shoulder (left = -X here)
        assert(at(.12,1.6,.1) && at(-.25,1.45,0));                 // a hand near either one
        assert(!at(0,1.6,-.25));                                   // held up in front of the face
        assert(!at(.2,1.1,-.2) && !at(-.2,1.0,0));                 // at the chest / waist
        const std::array<double,3> turned{1,0,0};                  // the zones turn with the head: looking down +X, the right shoulder is +Z
        assert(at_shoulder(head,turned,up,{-.03,1.52,.18}) && !at_shoulder(head,turned,up,{.25,1.6,0}));
        assert(!at_shoulder(head,fwd,up,{std::nan(""),1.5,0}));
    }
    {   // Ruling V1: a corpse as the native target never gets the carry Use from a grip alone: the physical grab at the fist gate (0.45 m), else nothing
        const auto body=box(0x9300,PalmKind::usable,{.07,0,.015},.3,t);
        auto bone=physical(box(1,PalmKind::bone,{.07,0,.015},.05,t));bone.entity=0x9300;
        auto in=input(body,bone);in.native_corpse=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab);
        bone.distance=.09;in=input(body,bone);in.native_corpse=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab);     // all grip-only targets share the 10 cm contact gate
        bone.distance=.11;in=input(body,bone,1200);in.native_corpse=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::none);
        auto prop=bone;prop.kind=PalmKind::prop;prop.distance=.05;in=input(body,prop);in.native_corpse=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::grab); // nearer prop wins beside a corpse
        in.takedown_offered=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use); // a real stealth offer keeps priority
        bone.distance=.5;in=input(body,bone,1000);in.native_corpse=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::pending);   // not in reach yet: the buffer runs
        in.now_ms=1000+static_cast<std::uint64_t>(t.grab_buffer_ms)+1;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::none);     // then nothing, never Use
        in=input(body,std::nullopt,1000);in.native_corpse=true;in.now_ms=1200;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::none);
        in=input(body,std::nullopt,1000);in.native_corpse=false;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);           // doors/levers/loot unchanged
        in=input(body,bone);in.native_corpse=true;in.occupied=true;assert(decide_grip_alone(in,t,hover)==GripAloneChoice::use);         // an occupied hand keeps today's path
    }
    std::cout<<"PASS: grip-alone one-list decision (corpse self-carry yields the grab),  (loot/door Use, prop on palm grabs, 0.12 m gate, block stays Use, choke/takedown/occupied, 100 ms fresh wait, H1 buffer)\n";
}
