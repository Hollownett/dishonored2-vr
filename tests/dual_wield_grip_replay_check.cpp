// v1370 (user, headset v1360, dual wield, 2026-11-15: "I took the crossbow, then the pistol: after that the crossbow was held with the pistol's
// grip; swapping them back and forth it became correct at some point, but then the crossbow kept blinking, as if it tried to change its pose in
// the hand, and went back"). Replays that run's dual-wield sequence (artifacts/headset-v1360-20261115T2225/dual-wield-events.jsonl: the arm plan
// ArmSet derived after every wheel choice, copy, release, drop and pickup) through the production hand routing (hand_route.hpp), FullBodyDriver
// (the per-type kept gun clips) and the body solver (the hold latch and its remembered grip), and checks per hand:
//   keys    - a hand's item_key names what it holds: the same while that does not change, another for another item (it keys the hold and its memory);
//   starts  - a hold starts only when the hand's item changes (a restart re-latches the clip's current frame and waits 0.25 s of calm to follow it);
//   grips   - a gun is held with its own type's grip (attachment socket and index finger as a native gun of that type in that hand), a copy at once.
// The game's left arm (the slot-1 arm) plays the slot-1 item's own idle (the engine's clips, weapon_grip_clips.hpp; an open hand for a power or the
// Heart) and shows the previous item for the one frame the slot changes on (the control tick reads the item, the render thread uploads the arm).
// Then three robustness cases the run's frames point at (held-burst-weapon-3, S5: the left crossbow copy with spread fingers while the right
// hand's native crossbow, the same live clip mirrored, was closed; and one frame with the copy at another pose round the unmoved hand):
//   R1 a one-frame failed gun read (slot 1's model 0) while a copy is held, R2 a copy taken while the other hand's native arm is in a transitional
//   pose and then walking (never calm), R3 the game's arm still in the pistol idle for 0.5 s after an instant crossbow equip.
#define NOMINMAX
#include "../src/hand_route.hpp"
#include "../src/held_copy.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace d2vr;
using namespace d2vr::fullbody;
using H=arm::Held;

static int failures=0;
static void expect(bool ok,const std::string& what) {if(!ok) {++failures;std::cerr<<"FAIL "<<what<<"\n";}}
static double angle_deg(const Quaternion& a,const Quaternion& b) {return 2*std::acos(std::min(1.,std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)))*180/body::kPi;}

enum class Slot {none,power,heart,pistol,crossbow};
struct Step {const char* what;double seconds;unsigned sword_arm,slot1_arm;H copy[2];Slot slot1;};

int main() {
    const auto stamp=[](Tracking x){x.seconds=x.tick/1000.;return x;};
    const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    std::array<Transform,20> native_body{};
    for(unsigned i=0;i<20;++i) native_body[i]=to_transform(b.bind[i]);
    Tracking base;base.eye={b.bind[b.find("camera_jnt")].q,b.bind[b.find("camera_jnt")].p};base.epoch=1;base.hand_state_fresh=true;base.weapon_type_fresh=true;
    std::array<std::array<Transform,69>,2> bind_arm{};
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?right:left;const std::string prefix=side?"right":"left";
        for(unsigned i=0;i<69;++i) bind_arm[side][i]=to_transform(a.bind[i]);
        std::array<Transform,4> knuckles;const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned i=0;i<4;++i) knuckles[i]=to_transform(a.bind[a.find(prefix+"_"+fingers[i]+"_02_jnt")]);
        const auto palm=rig::hand_bind_grip(bind_arm[side][a.find(prefix+"_wrist_jnt")],knuckles);
        base.grip[side]=body::mul(b.bind[b.find("center_spine03_jnt")],to_xf(palm));base.grip_valid[side]=true;
    }
    const auto baked=[](unsigned type) {
        std::array<Transform,69> a{};const auto* k=grip_clips::sampled(type);
        for(unsigned j=0;j<69;++j) a[j]=to_transform(Xf{normalized(Quaternion{(*k)[j].q[0],(*k)[j].q[1],(*k)[j].q[2],(*k)[j].q[3]}),Vec3{(*k)[j].p[0],(*k)[j].p[1],(*k)[j].p[2]}});
        return a;
    };
    const auto pistol_idle=baked(grip_clips::type_pistol),crossbow_idle=baked(grip_clips::type_crossbow);
    const auto& open_hand=bind_arm[0];   // a power's (or the Heart's) left arm: an open hand, no gun grip
    const auto idle_of=[&](Slot s)->const std::array<Transform,69>& {return s==Slot::pistol?pistol_idle:s==Slot::crossbow?crossbow_idle:open_hand;};
    const auto type_of=[](Slot s) {return s==Slot::pistol?1u:s==Slot::crossbow?2u:0u;};
    const auto name_of=[](Slot s) {return s==Slot::power?"Blink":s==Slot::heart?"Heart":s==Slot::pistol?"Gun":s==Slot::crossbow?"Crossbow":"";};
    // The drawn hold: the attachment local (its parent frame) and the index finger in the wrist frame, per hand.
    struct Grip {Xf socket,index;};
    const auto drawn=[&](unsigned side,const std::array<Transform,69>& r) {
        const auto& a=side?right:left;const std::string prefix=side?"right":"left";
        const int at=a.find(prefix+"_hand_attachment_jnt"),wr=a.find(prefix+"_wrist_jnt"),ix=a.find(prefix+"_index_03_jnt");
        return Grip{body::mul(body::inverse(to_xf(r[static_cast<unsigned>(a.parent[static_cast<std::size_t>(at)])])),to_xf(r[static_cast<unsigned>(at)])),
                    body::mul(body::inverse(to_xf(r[static_cast<unsigned>(wr)])),to_xf(r[static_cast<unsigned>(ix)]))};
    };
    // One frame of the game: the arm plan, slot 1, the game's left arm, a token for a native left gun; returns the routed tracking.
    struct Frame {unsigned pack;Slot slot1;bool gun_read=true;std::uint64_t token=0;const std::array<Transform,69>* arm0;};
    const auto route=[&](const Frame& f,std::uint32_t tick) {
        Tracking t=base;t.tick=tick;
        hand_route::In in;in.pack=f.pack;in.fresh=true;in.dual_wield=true;
        in.empty_bits=(f.slot1==Slot::none?1u:0u)|2u;in.weapon_bits=f.slot1!=Slot::none?1u:0u;   // the sword stays holstered (slot 0 empty)
        in.left_power=f.slot1==Slot::power;in.left_gun=f.gun_read && type_of(f.slot1)!=0;in.slot1_type=static_cast<int>(type_of(f.slot1));in.equip_name=name_of(f.slot1);
        hand_route::route(in,t);
        t.native_socket_token[0]=arm::cross_of(f.pack)&2u?0:f.token;   // the native left gun's socket lease (dxgi_probe), none while the sword rides the left arm
        return t;
    };
    struct Solved {std::array<Grip,2> grip;std::array<std::uint64_t,2> key,starts;};
    const auto frame=[&](Driver& d,const Frame& f,std::uint32_t tick) {
        const auto t=route(f,tick);
        (void)d.arm(0,*f.arm0,identity);(void)d.arm(1,bind_arm[1],identity);d.publish(stamp(t),{});
        if(!d.body(native_body,identity)) {expect(false,"the body solves");return Solved{};}
        Solved s{};
        for(unsigned side=0;side<2;++side) {
            const auto r=d.arm(side,side?bind_arm[1]:*f.arm0,identity);
            if(!r) {expect(false,"the arm solves");return s;}
            s.grip[side]=drawn(side,*r);s.key[side]=t.item_key[side];s.starts[side]=d.grip_state()[side].hold_starts;
        }
        return s;
    };
    const auto pack_of=[](unsigned sword_arm,unsigned slot1_arm,H c0,H c1) {
        arm::Plan p;p.sword_arm=sword_arm;p.slot1_arm=slot1_arm;p.cross=(slot1_arm==1?1u:0u)|(sword_arm==0?2u:0u);p.copy={c0,c1};return arm::pack(p);
    };
    // The grip of a gun of each type held natively in each hand (the left: its own arm's clip; the right: crossed, that clip mirrored): the reference.
    std::array<std::array<Grip,3>,2> want{};
    for(unsigned type=1;type<=2;++type) for(unsigned side=0;side<2;++side) {
        Driver d;d.tune(0,1);std::uint32_t tick=1000;Solved s{};
        const Slot slot=type==1?Slot::pistol:Slot::crossbow;
        for(int n=0;n<120;++n) s=frame(d,{side?pack_of(0,1,H::none,H::none):pack_of(1,0,H::none,H::none),slot,true,side?0u:1u,&idle_of(slot)},tick+=11);
        want[side][type]=s.grip[side];
    }
    {   const double mm=1000*length(want[0][1].socket.p-want[0][2].socket.p),deg=angle_deg(want[0][1].socket.q,want[0][2].socket.q);
        std::printf("own grips: the pistol and crossbow sockets %.1f mm %.1f deg apart (left), index finger %.1f deg\n",mm,deg,angle_deg(want[0][1].index.q,want[0][2].index.q));
        expect(mm>20,"the two gun types' grips are told apart");}
    const auto grip_error=[&](unsigned side,unsigned type,const Grip& g,double& mm,double& deg) {
        mm=1000*length(g.socket.p-want[side][type].socket.p);deg=std::max(angle_deg(g.socket.q,want[side][type].socket.q),angle_deg(g.index.q,want[side][type].index.q));
    };

    // ---- the run's sequence ------------------------------------------------------------------------------------------------------------------
    const std::vector<Step> steps={
        {"start: Blink in the left hand",1.,1,0,{H::none,H::none},Slot::power},
        {"137860421 the left wheel equips the crossbow over Blink",8/90.,1,0,{H::none,H::none},Slot::crossbow},
        {"137860484 the crossbow lands in the left as its copy, Blink equipped again on the right (power swap)",2.,0,1,{H::crossbow,H::none},Slot::power},
        {"137867859 the right wheel: the Heart",1.,0,1,{H::crossbow,H::none},Slot::heart},
        {"137869671 the right wheel: the pistol, native on the right",2.,0,1,{H::crossbow,H::none},Slot::pistol},
        {"137902296 S1 the left wheel: the crossbow, native in the left; the pistol stays in the right as its copy",2.,1,0,{H::none,H::pistol},Slot::crossbow},
        {"137908203 S2 the right wheel: the pistol, native on the right; the crossbow stays in the left as its copy",2.,0,1,{H::crossbow,H::none},Slot::pistol},
        {"137911562 S3 the left wheel: the pistol (same item): the left's copy becomes a pistol",2.,0,1,{H::pistol,H::none},Slot::pistol},
        {"137914203 S4 the right wheel: the crossbow, native on the right; the left keeps its pistol copy",2.,0,1,{H::pistol,H::none},Slot::crossbow},
        {"137921000 S5 the left wheel: the crossbow (same item): the left's copy becomes a crossbow",2.,0,1,{H::crossbow,H::none},Slot::crossbow},
        {"137929500 the left lets go of its copy",1.,0,1,{H::none,H::none},Slot::crossbow},
        {"137931562 the left wheel: the crossbow (same item) again",1.,0,1,{H::crossbow,H::none},Slot::crossbow},
        {"137934156 the left lets go, the right drops its crossbow",.5,0,1,{H::none,H::none},Slot::none},
        {"137934781 S6 the left picks the crossbow up: native in the left",2.,1,0,{H::none,H::none},Slot::crossbow},
        {"137937531 S7 the right wheel: the pistol, native on the right; the crossbow stays in the left as its copy",2.,0,1,{H::crossbow,H::none},Slot::pistol},
    };
    // What each hand holds in a step, as the keys and holds must see it: "copy:<kind>", "native:<slot item>", or "" (nothing that keeps a hold).
    const auto holding=[&](const Step& s,unsigned side)->std::string {
        if(s.copy[side]==H::pistol) return "copy:pistol";
        if(s.copy[side]==H::crossbow) return "copy:crossbow";
        if(side==s.slot1_arm && (s.slot1==Slot::pistol || s.slot1==Slot::crossbow || s.slot1==Slot::heart)) return std::string("native:")+name_of(s.slot1);
        return "";
    };
    const auto gun_type=[&](const Step& s,unsigned side)->unsigned {
        if(s.copy[side]==H::pistol) return 1;if(s.copy[side]==H::crossbow) return 2;
        return side==s.slot1_arm?type_of(s.slot1):0;
    };
    {
        Driver d;d.tune(0,1);std::uint32_t tick=5000;
        std::array<std::vector<std::pair<std::string,std::uint64_t>>,2> keys_seen{};
        std::array<std::string,2> held_before{};std::array<std::uint64_t,2> starts_before{};
        const std::array<Transform,69>* arm_before=&open_hand;
        for(std::size_t k=0;k<steps.size();++k) {
            const auto& st=steps[k];const unsigned pack=pack_of(st.sword_arm,st.slot1_arm,st.copy[0],st.copy[1]);
            const auto* arm_now=&idle_of(st.slot1);
            const int frames=std::max(1,static_cast<int>(std::lround(st.seconds*90)));
            std::array<bool,2> key_steady{true,true};std::array<std::uint64_t,2> first_key{};std::array<std::uint64_t,2> starts_at_first{};
            std::array<double,2> worst_mm{},worst_deg{};
            for(int n=0;n<frames;++n) {
                const auto s=frame(d,{pack,st.slot1,true,st.slot1_arm==0?static_cast<std::uint64_t>(k+1):0u,n==0?arm_before:arm_now},tick+=11);
                for(unsigned side=0;side<2;++side) {
                    if(n==0) {first_key[side]=s.key[side];starts_at_first[side]=s.starts[side];}
                    else if(s.key[side]!=first_key[side]) key_steady[side]=false;
                    if(n==frames-1 && !holding(st,side).empty())
                        expect(s.starts[side]==starts_at_first[side],"step "+std::to_string(k)+" "+(side?"right":"left")+": no hold starts after the step's first frame ("+
                            std::to_string(s.starts[side]-starts_at_first[side])+" more)");
                    // a copy from its second frame (at once: its hold starts from its type's clip); a native gun once its own clip has played 1 s
                    const unsigned type=gun_type(st,side);const int from=st.copy[side]!=H::none?2:frames>=90?frames-1:frames;
                    if(type && n>=from) {double mm,deg;grip_error(side,type,s.grip[side],mm,deg);worst_mm[side]=std::max(worst_mm[side],mm);worst_deg[side]=std::max(worst_deg[side],deg);}
                }
            }
            for(unsigned side=0;side<2;++side) {
                const auto what=holding(st,side);if(what.empty()) {held_before[side].clear();continue;}
                const std::string tag="step "+std::to_string(k)+" ("+st.what+") "+(side?"right ":"left ")+what;
                expect(key_steady[side],tag+": the item key stays the same while the hand holds it");
                bool known=false;   // keys_seen: the first key each item had in this hand
                for(const auto& [other,key]:keys_seen[side]) {
                    if(other==what) {known=true;expect(key==first_key[side],tag+": the same item as before has the same key");}
                    else expect(key!=first_key[side],tag+": another item ("+other+") never shares its key");
                }
                if(!known) keys_seen[side].push_back({what,first_key[side]});
                if(held_before[side]==what) expect(starts_at_first[side]==starts_before[side],tag+": the hand kept its item, so its hold does not start again ("+
                    std::to_string(starts_at_first[side]-starts_before[side])+" starts)");
                held_before[side]=what;
                if(const unsigned type=gun_type(st,side)) {
                    std::printf("%-108.108s %s %-14s grip off its own by %5.1f mm %5.1f deg\n",st.what,side?"right":"left ",what.c_str(),worst_mm[side],worst_deg[side]);
                    expect(worst_mm[side]<3 && worst_deg[side]<2,tag+": held with its own type's grip ("+std::to_string(worst_mm[side])+" mm, "+std::to_string(worst_deg[side])+" deg)");
                }
            }
            {   // the starts each hand has had by the end of the step
                const auto s=frame(d,{pack,st.slot1,true,st.slot1_arm==0?static_cast<std::uint64_t>(k+1):0u,arm_now},tick+=11);
                starts_before={s.starts[0],s.starts[1]};
            }
            arm_before=arm_now;
        }
    }

    // ---- R1: a failed gun read for one frame while a copy is held (the copy's type matches the other hand's native gun) ---------------------
    {
        Driver d;d.tune(0,1);std::uint32_t tick=5000;
        const unsigned s4=pack_of(0,1,H::pistol,H::none),s5=pack_of(0,1,H::crossbow,H::none);
        for(int n=0;n<90;++n) (void)frame(d,{s4,Slot::crossbow,true,0,&crossbow_idle},tick+=11);
        Solved s=frame(d,{s5,Slot::crossbow,true,0,&crossbow_idle},tick+=11);
        for(int n=0;n<45;++n) s=frame(d,{s5,Slot::crossbow,true,0,&crossbow_idle},tick+=11);
        const auto starts=s.starts[0];const auto grip=s.grip[0];double moved_mm=0,moved_deg=0;
        for(int n=0;n<270;++n) {   // 3 s, one failed read every 0.5 s
            s=frame(d,{s5,Slot::crossbow,n%45!=0,0,&crossbow_idle},tick+=11);
            moved_mm=std::max(moved_mm,1000*length(s.grip[0].socket.p-grip.socket.p));moved_deg=std::max(moved_deg,angle_deg(s.grip[0].socket.q,grip.socket.q));
        }
        std::printf("R1 one-frame failed gun reads (6 in 3 s), left crossbow copy: %llu hold starts, socket moved %.2f mm %.2f deg\n",
            static_cast<unsigned long long>(s.starts[0]-starts),moved_mm,moved_deg);
        expect(s.starts[0]==starts,"R1: a one-frame failed gun read never restarts the copy hand's hold");
        expect(moved_mm<.5 && moved_deg<.5,"R1: the copy stays put through failed reads");
    }
    // ---- R2: a copy taken while the other hand's native arm is not in its idle (after a wheel, an equip), then walking: never calm -----------
    {
        Driver d;d.tune(0,1);std::uint32_t tick=5000;
        const unsigned s4=pack_of(0,1,H::pistol,H::none),s5=pack_of(0,1,H::crossbow,H::none);
        for(int n=0;n<180;++n) (void)frame(d,{s4,Slot::crossbow,true,0,&crossbow_idle},tick+=11);   // standing: the crossbow's calm clip is captured
        for(int n=0;n<10;++n) (void)frame(d,{s5,Slot::crossbow,true,0,&open_hand},tick+=11);        // the copy arrives; the game's arm is elsewhere
        double worst_mm=0,worst_deg=0;
        for(int n=0;n<360;++n) {   // 4 s walking: the game's arm sways 5 deg at 1.6 Hz about the attachment (50 deg/s at most: never calm)
            auto arm=crossbow_idle;const int at=left.find("left_hand_attachment_jnt");
            const double sway=5*body::kPi/180*std::sin(2*body::kPi*1.6*n/90.);
            const auto pivot=to_xf(arm[static_cast<unsigned>(at)]);const Xf turn{body::mul(pivot.q,body::mul(body::axis_angle({0,0,1},sway),body::conj(pivot.q))),{}};
            for(unsigned j=0;j<69;++j) {int a=static_cast<int>(j);while(a>=0 && a!=at) a=left.parent[static_cast<std::size_t>(a)];
                if(a==at) {auto x=to_xf(arm[j]);x={body::mul(turn.q,x.q),pivot.p+rotated(turn.q,x.p-pivot.p)};arm[j]=to_transform(x);}}
            const auto s=frame(d,{s5,Slot::crossbow,true,0,&arm},tick+=11);
            if(n>=45) {double mm,deg;grip_error(0,2,s.grip[0],mm,deg);worst_mm=std::max(worst_mm,mm);worst_deg=std::max(worst_deg,deg);}
        }
        std::printf("R2 copy taken mid-transition, then walking: left crossbow grip off its own by %.1f mm %.1f deg\n",worst_mm,worst_deg);
        expect(worst_mm<3 && worst_deg<2,"R2: a copy is held with its own type's grip whatever the other hand's native arm is doing");
    }
    // ---- R3: after an instant crossbow equip the game's arm keeps the pistol idle for 0.5 s (calm): never kept as the crossbow's clip --------
    {
        Driver d;d.tune(0,1);std::uint32_t tick=5000;
        for(int n=0;n<90;++n) (void)frame(d,{pack_of(1,0,H::none,H::none),Slot::pistol,true,1,&pistol_idle},tick+=11);
        for(int n=0;n<45;++n) (void)frame(d,{pack_of(1,0,H::none,H::none),Slot::crossbow,true,2,&pistol_idle},tick+=11);   // the arm lags the item
        for(int n=0;n<5;++n) (void)frame(d,{pack_of(1,0,H::none,H::none),Slot::pistol,true,3,&pistol_idle},tick+=11);      // the pistol back before it caught up
        Solved s{};double worst_mm=0,worst_deg=0;
        for(int n=0;n<90;++n) {   // the right's wheel: the pistol native on the right, the crossbow the left's copy
            s=frame(d,{pack_of(0,1,H::crossbow,H::none),Slot::pistol,true,0,&pistol_idle},tick+=11);
            if(n>=2) {double mm,deg;grip_error(0,2,s.grip[0],mm,deg);worst_mm=std::max(worst_mm,mm);worst_deg=std::max(worst_deg,deg);}
        }
        std::printf("R3 the arm lagging an instant equip: left crossbow copy grip off its own by %.1f mm %.1f deg\n",worst_mm,worst_deg);
        expect(worst_mm<3 && worst_deg<2,"R3: a crossbow copy never takes the pistol's grip from an arm that lagged the equip");
    }
    // ---- v1371 (user, headset v1380: "when I first choose the crossbow into the hand where Blink was, the hand takes the pistol grip") --------
    // The v1380 run (artifacts/headset-v1380-20261115T2336): Blink in the left hand; the left wheel chose the crossbow (142119890): equipped over
    // the power, then the arrival made it the left hand's COPY and equipped Blink again on the right arm (copy_restore); the same for the pistol
    // (142129109). Each of those native guns was bound for ONE left-weapon receipt (receipts come every ~109 ms while a gun is bound), so the
    // copy service never measured it (12 calm samples 20 ms apart): the copy took the unmeasured grip of its kind, which the hand the solver
    // posed with that kind's clip must hold. A copy is drawn at hand grip * grip (weapon_draw_place space_grip), the fingers at the clip.
    {
        using held_copy::Kind;
        const auto pose_of=[](const Xf& x) {return held_copy::Pose{x.p,{rotated(x.q,{1,0,0}),rotated(x.q,{0,1,0}),rotated(x.q,{0,0,1})}};};
        const int at=left.find("left_hand_attachment_jnt");
        const auto frame_error=[](const held_copy::Pose& a,const Xf& b,double& mm,double& deg) {
            mm=1000*length(a.position-b.p);deg=0;
            for(unsigned k=0;k<3;++k) {const Vec3 e{k==0?1.:0.,k==1?1.:0.,k==2?1.:0.};
                deg=std::max(deg,std::acos(std::clamp(dot(a.basis[k],rotated(b.q,e)),-1.,1.))*180/body::kPi);}
        };
        // (1) the solver's grip of a type (FullBodyDriver::copy_grip) is where that type's native gun sits in the left hand, in the grip frame the
        // copy service measures a native gun in: the solved attachment joint 59 (the left slot's gun frame, identity tag)
        for(unsigned type=1;type<=2;++type) {
            Driver d;d.tune(0,1);std::uint32_t tick=1000;const Slot slot=type==1?Slot::pistol:Slot::crossbow;
            for(int n=0;n<120;++n) (void)frame(d,{pack_of(1,0,H::none,H::none),slot,true,1,&idle_of(slot)},tick+=11);
            const auto r=d.arm(0,idle_of(slot),identity);const auto derived=d.copy_grip(type);
            if(!r || !derived) {expect(false,"v1371: the solver's grip of type "+std::to_string(type));continue;}
            double mm,deg;frame_error(pose_of(*derived),body::mul(body::inverse(base.grip[0]),to_xf((*r)[static_cast<unsigned>(at)])),mm,deg);
            std::printf("v1371 solver grip of type %u against its native gun in the left hand: %.2f mm %.2f deg\n",type,mm,deg);
            expect(mm<1 && deg<.5,"v1371: the solver's grip of type "+std::to_string(type)+" is where its native gun sits in that hand");
        }
        // (2) the run: Blink in the left hand, then each gun taken over it as the left hand's copy before any native gun of its kind was measured
        for(const Kind kind:{Kind::crossbow,Kind::pistol}) {
            const unsigned type=kind==Kind::pistol?1u:2u;const Slot slot=type==1?Slot::pistol:Slot::crossbow;const H held=type==1?H::pistol:H::crossbow;
            Driver d;d.tune(0,1);std::uint32_t tick=5000;held_copy::SolverGrips::clear();
            const unsigned blink_left=pack_of(1,0,H::none,H::none),copy_left=pack_of(0,1,held,H::none);
            for(int n=0;n<90;++n) (void)frame(d,{blink_left,Slot::power,true,0,&open_hand},tick+=11);   // Blink in the left hand
            held_copy::OffsetFilter measure;   // the copy service measuring the native gun while it rides the left hand, one sample per 20 ms
            for(int n=0;n<18;++n) {   // 0.2 s: one receipt
                (void)frame(d,{blink_left,slot,true,1,n==0?&open_hand:&idle_of(slot)},tick+=11);
                if(n%2==0) if(const auto r=d.arm(0,idle_of(slot),identity)) measure.add(pose_of(body::mul(body::inverse(base.grip[0]),to_xf((*r)[static_cast<unsigned>(at)]))));
            }
            expect(!measure.ready(),"v1371: a gun bound for one receipt is never measured");
            double worst_mm=0,worst_deg=0;
            for(int n=0;n<180;++n) {   // 2 s: the copy in the left hand, Blink on the right arm
                (void)frame(d,{copy_left,Slot::power,true,0,&open_hand},tick+=11);
                for(unsigned t=1;t<=2;++t) if(const auto g=d.copy_grip(t)) held_copy::SolverGrips::set(t==1?Kind::pistol:Kind::crossbow,pose_of(*g));   // as dxgi_probe, every frame
                bool mirrored{};const auto offset=held_copy::unmeasured_grip(kind,0,mirrored);   // the copy service, nothing measured
                held_copy::Pose copy{};const auto r=d.arm(0,open_hand,identity);
                if(!r || !held_copy::place(pose_of(base.grip[0]),offset,mirrored,copy)) {expect(false,"v1371: the copy is placed");break;}
                if(n>=45) {double mm,deg;frame_error(copy,to_xf((*r)[static_cast<unsigned>(at)]),mm,deg);worst_mm=std::max(worst_mm,mm);worst_deg=std::max(worst_deg,deg);}
            }
            std::printf("v1371 first %s copy over Blink: drawn %.1f mm %.1f deg from where the hand holds it\n",type==1?"pistol":"crossbow",worst_mm,worst_deg);
            expect(worst_mm<2 && worst_deg<1,std::string("v1371: the first ")+(type==1?"pistol":"crossbow")+" copy over Blink sits where the hand holds it");
        }
        // The run's own numbers (status dual_wield.guns.held.kinds, measured on the right hand, here as seen on the left): the grips the copy
        // service measured once a native gun of each kind rode a hand, against this rig's solver grips and the constants (report only: the
        // rig's scale is not the user's fit).
        {
            const auto measured=[](double x,double y,double z,Vec3 b0,Vec3 b1,Vec3 b2) {return held_copy::mirror(held_copy::Pose{{x,y,z},{b0,b1,b2}});};
            const held_copy::Pose run[2]={measured(.0052,.0304,.0013,{.5051,-.0976,-.8576},{-.0338,.9906,-.1326},{.8624,.096,.497}),
                                          measured(.0491,.0381,-.0438,{.3691,-.1296,-.9203},{-.0062,.9899,-.1419},{.9294,.0581,.3645})};
            Driver d;d.tune(0,1);std::uint32_t tick=1000;
            for(int n=0;n<5;++n) (void)frame(d,{pack_of(1,0,H::none,H::none),Slot::power,true,0,&open_hand},tick+=11);
            for(unsigned type=1;type<=2;++type) {
                const auto g=d.copy_grip(type);const auto c=held_copy::grip_constant(type==1?Kind::pistol:Kind::crossbow);if(!g) continue;
                const auto& m=run[type-1];const auto mx=from_basis(m.position,m.basis[0],m.basis[1],m.basis[2]);
                double mm,deg,cmm,cdeg;frame_error(pose_of(*g),mx,mm,deg);frame_error(c.offset,mx,cmm,cdeg);
                std::printf("v1371 %s: the run's measured grip vs this rig's solver grip %.1f mm %.1f deg, vs the constant %.1f mm %.1f deg\n",type==1?"pistol":"crossbow",mm,deg,cmm,cdeg);
            }
        }
    }
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::puts("dual wield grip replay check ok");return 0;
}
