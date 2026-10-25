// v613 (v612 headset feedback 7 and 3): a weapon drawn again (holster -> unholster) or caught again (thrown sword -> pickup) must sit in
// the closed hand on its first frame. Before: the hold started from the first frame of the native equip clip and the drawn fingers were
// the clip's own, so the hand opened around the hilt for ~0.45 s (sim 20261025T155808: hilt_miss 1.3 -> 7.7 cm, attachment local swung
// 175 deg) and the socket settled 2 cm off; and for a frame or two the sword showed in the open (empty) hand before the game reported it.
// Uses only the rig fixture (no decoded clips), so it also runs in the Linux harness.
#include "../src/body_ik.hpp"
#include "player_rig_fixture.hpp"
#include <cstdio>
#include <iostream>
#include <string>

using namespace d2vr;
using namespace d2vr::body;

static Skeleton load(const std::vector<FixtureJoint>& joints) {
    Skeleton s;
    for(const auto& j:joints) {
        s.names.push_back(j.name);s.parent.push_back(j.parent);
        s.local.push_back({normalized({j.q[0],j.q[1],j.q[2],j.q[3]}),{j.t[0],j.t[1],j.t[2]}});
    }
    s.finish();return s;
}
static int failures=0;
static void expect(bool ok,const std::string& what) {if(!ok) {++failures;std::cerr<<"FAIL "<<what<<"\n";}}
static double angle_between(Quaternion a,Quaternion b) {return 2*std::acos(std::min(1.,std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)));}

int main() {
    const double deg=kPi/180,eye_height=1.75,span=1.55;
    const auto body=load(kFixtureBody),left=load(kFixtureLeftArm),right=load(kFixtureRightArm);
    std::vector<Xf> idle;
    for(const auto& arm:kFixtureNativeArms) if(std::string("sword_idle")==arm.label && arm.side==1)
        for(const auto& j:arm.joints) {
            const auto* c=j.columns;
            idle.push_back({from_axes(unit({c[0],c[1],c[2]}),unit({c[3],c[4],c[5]}),unit({c[6],c[7],c[8]})),{j.t[0],j.t[1],j.t[2]}});
        }
    expect(idle.size()==69,"sword_idle right-arm fixture present");
    if(idle.size()!=69) return 1;
    const int wr=right.find("right_wrist_jnt"),at=right.find("right_hand_attachment_jnt"),par=right.parent[at];
    const int s3=body.find("center_spine03_jnt");
    std::vector<int> knuckles;
    for(const char* f:{"index","medium","ring","pinky"}) knuckles.push_back(right.find(std::string("right_")+f+"_02_jnt"));
    const auto bind_hand=mul(body.bind[s3],right.bind[at]).q;
    const Vec3 eye{0,0,eye_height};
    const Xf hand{mul(frame_map({0,-1.,0},{0,0,-1},unit({1,0,-.35}),{0,1,0}),bind_hand),eye+Vec3{.35,-.2,-.35}};
    // A native "equip clip" frame: the attachment local turned by `swing` about its own Z and moved, every finger opened by `open`.
    // Each joint is moved in model space with its subtree, so the children keep their locals.
    const auto clip=[&](double swing,double open) {
        auto pose=idle;
        const auto turn_subtree=[&](int root,const Xf& delta_local) {
            const auto before=pose[root],after=mul(before,delta_local);
            const auto map=mul(after,inverse(before));
            for(std::size_t j=0;j<pose.size();++j) {int a=static_cast<int>(j);while(a>=0 && a!=root) a=right.parent[a];if(a==root) pose[j]=mul(map,pose[j]);}
        };
        turn_subtree(at,{axis_angle({0,0,1},swing),{.03*swing/kPi,0,0}});
        for(int k:knuckles) turn_subtree(k,{axis_angle({0,0,1},-open),{}});
        (void)par;return pose;
    };
    struct Drawn {Xf socket;double miss;int finger_mode;};
    const auto drawn=[&](const Output& out) {const auto& a=out.arm[1];return Drawn{mul(inverse(a[wr]),a[at]),out.hilt_miss[1],out.finger_mode[1]};};
    const auto inputs=[&](const std::vector<Xf>* native,bool empty,std::uint64_t key,bool incoming) {
        Inputs in;in.eye={rot_z(0),eye};in.hand_valid[0]=false;in.hand[1]=hand;in.native_arm[1]=native;in.hand_empty[1]=empty;
        in.item_key[1]=key;in.weapon_incoming[1]=incoming;in.dt=1/90.;in.grip[1]=.9;in.trigger[1]=.25;return in;
    };
    const std::uint64_t sword=0x53574f5244ull;
    // The clip: starts 40 deg off the idle socket with the hand half open, swings to 170 deg fully open, returns to idle (0.5 s at 90 Hz).
    std::vector<std::vector<Xf>> draw;
    for(int f=0;f<45;++f) {const double u=f/44.,bump=std::sin(kPi*u);draw.push_back(clip((40+130*bump)*deg*(1-u*u*u),(.5+.5*bump)*(1-u)*70*deg));}

    for(const bool incoming_first:{false,true}) {
        const std::string tag=incoming_first?"incoming: ":"draw: ";
        BodySolver sol(body,left,right,{eye_height,span});
        Output out;
        for(int f=0;f<60;++f) out=sol.solve(inputs(&idle,false,sword,false));   // a calibrated hold of the sword
        const auto held=drawn(out);
        expect(held.finger_mode==3,tag+"the held sword uses the game's hand");
        for(int f=0;f<30;++f) out=sol.solve(inputs(&idle,true,sword,false));   // holstered: the hand is empty
        expect(drawn(out).finger_mode!=3,tag+"the empty hand is posed by the player");
        if(incoming_first) {   // the mod asked for the draw: the game has not reported the sword yet
            for(int f=0;f<2;++f) {
                const auto d=drawn(sol.solve(inputs(&idle,true,sword,true)));
                expect(d.finger_mode==3 && angle_between(d.socket.q,held.socket.q)<1*deg && length(d.socket.p-held.socket.p)<.002 && d.miss<=held.miss+.001,
                    tag+"the hand takes the remembered sword grip before the sword is reported (frame "+std::to_string(f)+")");
            }
        }
        double worst_deg=0,worst_m=0,worst_miss=0;
        for(std::size_t f=0;f<draw.size();++f) {
            const auto d=drawn(sol.solve(inputs(&draw[f],false,sword,false)));
            worst_deg=std::max(worst_deg,angle_between(d.socket.q,held.socket.q)/deg);worst_m=std::max(worst_m,length(d.socket.p-held.socket.p));
            worst_miss=std::max(worst_miss,d.miss);
        }
        std::printf("%sheld miss %.1f mm; through the clip: socket off by at most %.2f deg %.1f mm, hilt miss at most %.1f mm\n",
            tag.c_str(),held.miss*1000,worst_deg,worst_m*1000,worst_miss*1000);
        expect(worst_deg<1 && worst_m<.002,tag+"the socket stays at the remembered grip from the first frame of the draw clip");
        expect(worst_miss<=held.miss+.002,tag+"the fingers stay closed on the hilt through the draw clip");
        for(int f=0;f<90;++f) out=sol.solve(inputs(&idle,false,sword,false));
        const auto settled=drawn(out);
        expect(angle_between(settled.socket.q,held.socket.q)<1*deg && length(settled.socket.p-held.socket.p)<.002 && settled.miss<=held.miss+.001,
            tag+"after the clip the grip is the idle grip");
    }
    {   // no remembered grip (first draw of a session, or another item): the hold starts from the native pose as before, and an
        // incoming draw without a remembered grip keeps the hand the player's
        BodySolver sol(body,left,right,{eye_height,span});
        expect(drawn(sol.solve(inputs(&idle,true,sword,true))).finger_mode!=3,"incoming without a remembered grip: the hand stays the player's");
        auto d=drawn(sol.solve(inputs(&draw[0],false,sword,false)));
        const auto native_socket=mul(inverse(draw[0][par]),draw[0][at]);
        Output first;for(int f=0;f<60;++f) first=sol.solve(inputs(&idle,false,sword,false));
        const auto held=drawn(first);
        expect(angle_between(d.socket.q,held.socket.q)>20*deg,"without a memory the first hold frame is the clip's own socket ("+std::to_string(angle_between(d.socket.q,held.socket.q)/deg)+" deg)");
        (void)native_socket;
        for(int f=0;f<30;++f) sol.solve(inputs(&idle,true,0,false));
        d=drawn(sol.solve(inputs(&draw[0],false,0x1234,false)));
        expect(angle_between(d.socket.q,held.socket.q)>20*deg,"another item's hold does not start from the sword's grip");
    }
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::puts("weapon hold start check ok");return 0;
}
