// Reverse sword grip: the flip must turn the hilt about the fist (hilt stays in the drawn fist, blade points the other way), keep a fixed
// offset from the wrist when the hand is reach-clamped, and not stack with the sneak-stance socket correction.
// Uses only the rig fixture (no decoded clips), so it also runs in the Linux harness.
#include "../src/body_ik.hpp"
#include "player_rig_fixture.hpp"
#include <cstdio>
#include <string>
#include <iostream>

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
static double angle_between(Quaternion a,Quaternion b) {
    const double d=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
    return 2*std::acos(std::min(1.,d));
}

int main() {
    const double deg=kPi/180,eye_height=1.75,span=1.55;
    const auto body=load(kFixtureBody),left=load(kFixtureLeftArm),right=load(kFixtureRightArm);
    std::vector<Xf> native;
    for(const auto& arm:kFixtureNativeArms) if(std::string("sword_idle")==arm.label && arm.side==1)
        for(const auto& j:arm.joints) {
            const auto* c=j.columns;
            native.push_back({from_axes(unit({c[0],c[1],c[2]}),unit({c[3],c[4],c[5]}),unit({c[6],c[7],c[8]})),{j.t[0],j.t[1],j.t[2]}});
        }
    expect(native.size()==69,"sword_idle right-arm fixture present");
    if(native.size()!=69) return 1;
    const int wr=right.find("right_wrist_jnt"),at=right.find("right_hand_attachment_jnt"),par=right.parent[at];
    const int s3=body.find("center_spine03_jnt");
    const auto bind_hand=mul(body.bind[s3],right.bind[at]).q;
    const auto hand_at=[&](Vec3 eye,Vec3 offset) {
        return Xf{mul(frame_map({0,-1.,0},{0,0,-1},unit({1,0,-.35}),{0,1,0}),bind_hand),eye+offset};
    };
    const Vec3 eye{0,0,eye_height};
    struct Grip {Xf socket;Vec3 wrap;Vec3 blade;Vec3 fist_attach;double miss;bool reversed;bool reached;};
    // Settle the right arm holding the given native pose and report the attachment in the wrist frame.
    const auto settle=[&](const std::vector<Xf>& nat,bool reverse,Vec3 hand_offset) {
        BodySolver sol(body,left,right,{eye_height,span});
        Inputs in;in.eye={rot_z(0),eye};in.hand_valid[0]=false;in.hand[1]=hand_at(eye,hand_offset);
        in.native_arm[1]=&nat;in.reverse_grip[1]=reverse;in.dt=1/90.;in.grip[1]=.45;in.trigger[1]=.25;
        Output out;for(int n=0;n<60;++n) out=sol.solve(in);
        const auto& a=out.arm[1];Grip g{};
        g.socket=mul(inverse(a[wr]),a[at]);
        Vec3 sum{};int count=0;
        for(const char* f:{"index","medium","ring","pinky"}) for(const char* k:{"02","03","04"}) {sum=sum+a[right.find(std::string("right_")+f+"_"+k+"_jnt")].p;++count;}
        g.wrap=rotated(conj(a[wr].q),sum*(1./count)-a[wr].p);
        g.blade=rotated(g.socket.q,{1,0,0});
        g.fist_attach={out.fist_attach[1][0],out.fist_attach[1][1],out.fist_attach[1][2]};g.miss=out.hilt_miss[1];
        g.reversed=out.grip_reversed[1];g.reached=out.hand_reached[1];
        return g;
    };
    // Distance of the fist's wrap centre from the hilt line (the attachment X axis through its origin), and the hilt point nearest it.
    const auto miss=[&](const Grip& g) {const auto d=g.wrap-g.socket.p;return length(d-g.blade*dot(d,g.blade));};
    const auto grip_point=[&](const Grip& g) {return g.socket.p+g.blade*dot(g.wrap-g.socket.p,g.blade);};

    const Vec3 near_hand{.35,-.2,-.35};
    const auto fwd=settle(native,false,near_hand),rev=settle(native,true,near_hand);
    std::printf("wrap centre in wrist frame (%.4f %.4f %.4f); attach fwd (%.4f %.4f %.4f) rev (%.4f %.4f %.4f); hilt miss fwd %.1f mm rev %.1f mm\n",
        fwd.wrap.x,fwd.wrap.y,fwd.wrap.z,fwd.socket.p.x,fwd.socket.p.y,fwd.socket.p.z,rev.socket.p.x,rev.socket.p.y,rev.socket.p.z,miss(fwd)*1000,miss(rev)*1000);
    expect(!fwd.reversed && rev.reversed,"diagnostic reports the reverse flip only when it is applied");
    expect(length(fwd.wrap-rev.wrap)<1e-6,"flip does not move the fingers");
    // The fixture hold: blade along the knuckle line (wrist X), palm normal = attachment Y = wrist Z, finger axis = attachment Z.
    expect(rotated(fwd.socket.q,{0,1,0}).z>.9,"hold: attachment Y lies along the wrist's palm normal");
    expect(std::abs(fwd.blade.x)>.9,"hold: the blade (attachment X) lies across the hand, along the knuckle line");
    expect(dot(fwd.blade,rev.blade)<-.99,"reverse grip points the blade the other way (does not roll it)");
    expect(std::abs(dot(rotated(fwd.socket.q,{0,0,1}),rotated(rev.socket.q,{0,0,1})))>.99,"flip axis (attachment Z) is the one the blade turns about");
    expect(miss(fwd)<.015,"forward grip: hilt passes through the fist");
    expect(miss(rev)<.015,"reverse grip: hilt passes through the fist");
    expect(length(grip_point(fwd)-grip_point(rev))<.015,"same handle point in the fist in both grips");
    expect(std::abs(fwd.miss-miss(fwd))<1e-6 && std::abs(rev.miss-miss(rev))<1e-6,"status hilt_miss matches the geometry");
    expect(fwd.miss<.015 && rev.miss<.015,"status hilt_miss within 1.5 cm both grips");
    {   // the drawn-fist-frame attachment: same perpendicular offset from the hilt line, reversed along it
        const auto f=fwd.fist_attach,r=rev.fist_attach;
        expect(std::abs(std::abs(f.x)-std::abs(r.x))<.015 && std::abs(f.y-r.y)<.015 && std::abs(f.z-r.z)<.015,"fist-frame attachment is the same up to the reversal");
    }

    {   // reach-clamped hand: the attachment keeps its fixed offset from the wrist
        const auto far_fwd=settle(native,false,{3.,-.2,-.35}),far_rev=settle(native,true,{3.,-.2,-.35});
        expect(!far_fwd.reached && !far_rev.reached,"fixture hand really is beyond reach");
        expect(length(far_fwd.socket.p-fwd.socket.p)<1e-6 && angle_between(far_fwd.socket.q,fwd.socket.q)<1e-6,"reach clamp: forward attachment keeps its wrist offset");
        expect(length(far_rev.socket.p-rev.socket.p)<1e-6 && angle_between(far_rev.socket.q,rev.socket.q)<1e-6,"reach clamp: reversed attachment keeps its wrist offset");
    }
    {   // sneak stance socket (idle attachment turned 180 deg about its own Z): the correction and the reverse flip do not stack
        auto sneak=native;
        const auto l=mul(inverse(sneak[par]),sneak[at]);
        sneak[at]=mul(sneak[par],{mul(l.q,axis_angle({0,0,1},kPi)),l.p-Vec3{-.0453,-.0262,.0012}});
        const auto s_fwd=settle(sneak,false,near_hand),s_rev=settle(sneak,true,near_hand);
        expect(angle_between(s_fwd.socket.q,fwd.socket.q)<2*deg && length(s_fwd.socket.p-fwd.socket.p)<.002,"sneak forward grip equals the standing forward grip");
        expect(angle_between(s_rev.socket.q,rev.socket.q)<2*deg,"sneak + reverse is one flip of the standing grip, not two (angle)");
        expect(length(s_rev.socket.p-rev.socket.p)<.002,"sneak + reverse is one flip of the standing grip, not two (offset)");
        expect(dot(s_rev.blade,s_fwd.blade)<-.99,"sneak + reverse points the blade away from the sneak forward grip");
    }
    {   // G2 sneak_flip_hysteresis: the sneak socket correction enters at attachment-Y z < -.5 and leaves at > -.2
        const auto l=mul(inverse(native[par]),native[at]);
        const auto z_of=[&](double phi) {return rotated(mul(l.q,axis_angle({1,0,0},phi)),{0,1,0}).z;};
        const auto native_z=[&](double z) { // the idle attachment turned about its blade axis until its Y has the given z in the wrist frame
            double lo=0,hi=kPi;for(int n=0;n<60;++n) {const double mid=(lo+hi)/2;(z_of(mid)>z?lo:hi)=mid;}
            auto out=native;out[at]=mul(out[par],{mul(l.q,axis_angle({1,0,0},(lo+hi)/2)),l.p});return out;
        };
        expect(z_of(0)>.9,"hysteresis fixture: the idle attachment Y is along the palm normal");
        BodySolver sol(body,left,right,{eye_height,span});
        const bool want[5]{true,true,true,false,false};const double zs[5]{-.6,-.4,-.3,-.1,-.3};
        for(int k=0;k<5;++k) {
            const auto nat=native_z(zs[k]);Inputs in;in.eye={rot_z(0),eye};in.hand_valid[0]=false;in.hand[1]=hand_at(eye,near_hand);
            in.native_arm[1]=&nat;in.dt=1/90.;in.grip[1]=.45;in.trigger[1]=.25;
            Output out;for(int n=0;n<5;++n) out=sol.solve(in);
            expect(out.sneak_flip[1]==want[k],"sneak flip hysteresis step "+std::to_string(k)+" (z "+std::to_string(zs[k])+")");
        }
    }
    {   // G2 reverse_waits_for_calibrated_hold / reverse_blends_in_0_1s: weight 0 until hold_steady >= .25 s, then 1 in 9-10 frames at 90 Hz, hilt in the fist throughout
        BodySolver sol(body,left,right,{eye_height,span});
        Inputs in;in.eye={rot_z(0),eye};in.hand_valid[0]=false;in.hand[1]=hand_at(eye,near_hand);
        in.native_arm[1]=&native;in.reverse_grip[1]=true;in.dt=1/90.;in.grip[1]=.45;in.trigger[1]=.25;
        int first=-1,full=-1;double worst_miss=0;
        for(int f=0;f<80;++f) {
            const auto out=sol.solve(in);const double w=out.reverse_weight[1];
            if(first<0 && w>0) first=f;
            if(first>=0 && full<0) {worst_miss=std::max(worst_miss,out.hilt_miss[1]);if(w>=1) full=f;}
            if(f<20) expect(w==0,"reverse weight stays 0 before the hold has calibrated (frame "+std::to_string(f)+")");
        }
        expect(first>=22 && first<=26,"the reverse starts when hold_steady reaches .25 s (frame "+std::to_string(first)+")");
        expect(full>=0 && full-first+1>=9 && full-first+1<=10,"the reverse blend takes 9-10 frames at 90 Hz (took "+std::to_string(full-first+1)+")");
        // the fixture hand rests 13.9 mm off the hilt line even in the forward grip (the .015 cases above); the blend must add nothing to that
        expect(worst_miss<=fwd.miss+.001,"the hilt stays in the fist during the blend (worst miss "+std::to_string(worst_miss)+", forward "+std::to_string(fwd.miss)+")");
        in.reverse_grip[1]=false;Output out=sol.solve(in);expect(out.reverse_weight[1]<1 && out.reverse_weight[1]>0,"the weight falls back at the same rate");
        for(int f=0;f<12;++f) out=sol.solve(in);expect(out.reverse_weight[1]==0,"and reaches 0");
        in.reverse_grip[1]=true;in.native_socket_token[1]=7;out=sol.solve(in);expect(out.reverse_weight[1]==0,"a token change resets the weight to 0");
        for(int f=0;f<10;++f) {out=sol.solve(in);expect(out.reverse_weight[1]==0,"and it waits for the new hold to calibrate");}
    }
    if(failures) {std::cerr<<failures<<" failure(s)\n";return 1;}
    std::puts("reverse grip check ok");return 0;
}
