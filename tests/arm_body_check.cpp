#include "../src/arm_tracking.hpp"
#include <cassert>
#include <iostream>

namespace r=d2vr::rig;
using d2vr::Vec3;
static r::ArmTrackingFrame make(double yaw,std::uint64_t tick,Vec3 left,Vec3 right) {
    r::ArmTrackingFrame f{};f.tick=tick;f.epoch=1;f.reference_epoch=1;f.hands=3;f.has_head_anchor=true;
    f.head_anchor.position={0,0,1.7};
    f.head_anchor.basis[0]={std::cos(yaw),std::sin(yaw),0};f.head_anchor.basis[1]={-std::sin(yaw),std::cos(yaw),0};f.head_anchor.basis[2]={0,0,1};
    f.grips[0].position=left;f.grips[1].position=right;
    for(auto& g:f.grips) {g.basis[0]={1,0,0};g.basis[1]={0,1,0};g.basis[2]={0,0,1};}
    return f;
}
int main() {
    const double deg=r::kPi/180;
    // 1+2: head turns inside the dead zone leave the shoulders alone; beyond it the chest follows smoothly.
    {
        r::ArmTracking t;
        const Vec3 l{.3,.2,1.2},rt{.3,-.2,1.2}; // hands low, not both far forward -> no hand pull on the 0 deg frame
        t.publish(make(0,0,{0,.3,1.0},{0,-.3,1.0}));
        auto s=t.snapshot();const auto base=r::ArmTracking::shoulder_world(make(0,0,l,rt),{},0,&s.chest_yaw);
        for(unsigned i=1;i<=40;++i) t.publish(make(30*deg,i*16,{0,.3,1.0},{0,-.3,1.0}));
        s=t.snapshot();assert(s.has_chest && std::abs(s.chest_yaw)<1e-9);
        const auto still=r::ArmTracking::shoulder_world(make(30*deg,0,l,rt),{},0,&s.chest_yaw);
        assert(d2vr::length(still-base)<.02); // only the head anchor moved (head pos fixed here)
        double last=0,prev=0;
        for(unsigned i=1;i<=120;++i) {
            t.publish(make((30+std::min(90.,i*1.5))*deg,640+i*16,{0,.3,1.0},{0,-.3,1.0}));
            s=t.snapshot();last=s.chest_yaw;assert(last>=prev-1e-9 && last-prev<8*deg);prev=last; // monotone, smooth
        }
        for(unsigned i=121;i<=300;++i) t.publish(make(120*deg,640+i*16,{0,.3,1.0},{0,-.3,1.0}));
        last=t.snapshot().chest_yaw;
        assert(std::abs(r::wrap_pi(120*deg-last))<=40.5*deg && last>70*deg);
    }
    // Frame-rate independence: 60 Hz and 240 Hz converge to the same chest yaw.
    {
        double out[2];unsigned k=0;
        for(unsigned step:{16u,4u}) {
            r::ChestYaw c;c.step(0,nullptr,0);double t=0;
            while(t<1.0-1e-9) {c.step(100*deg,nullptr,step/1000.);t+=step/1000.;}
            out[k++]=c.yaw;
        }
        assert(std::abs(out[0]-out[1])<2*deg);
    }
    // Hands forward pull the chest toward them within the dead zone.
    {
        r::ChestYaw c;c.step(0,nullptr,0);const double hand=30*deg;
        for(unsigned i=0;i<300;++i) c.step(0,&hand,.016);
        assert(c.yaw>10*deg && c.yaw<=r::ChestYaw::dead_zone+1e-9);
    }
    // 2: reach overhead raises the shoulder within the cap and never detaches more.
    {
        const double L=.62,cap=.08*L;
        assert(d2vr::length(r::shoulder_reach_offset({0,0,.3},L,cap))==0);
        double last=0;
        for(double h=.3;h<=1.2;h+=.05) {
            const auto o=r::shoulder_reach_offset({0,0,h},L,cap);const auto m=d2vr::length(o);
            assert(m<=cap+1e-12 && m>=last-1e-12 && (h<.45 || o.z>=0));last=m;
        }
        assert(std::abs(last-cap)<1e-9);
        const auto sw=r::clavicle_swing({0,0,0},{.1,0,0},{0,.1,0},20*deg);
        assert(std::abs(2*std::acos(sw.w)-20*deg)<1e-6);
        assert(std::abs(2*std::acos(r::clavicle_swing({0,0,0},{.1,0,0},{.1,0,.01},20*deg).w)-std::atan2(.01,.1))<1e-6);
    }
    // 3: elbow defaults down/out, rises overhead, palm-up turns it inward, no flip on a slow sweep, bounded step.
    {
        const Vec3 f{1,0,0},out{0,1,0},up{0,0,1};const double L=.62;
        const auto rest=r::boneworks_elbow_pole({.3,.1,-.4},f,out,up,L,0);
        assert(rest.z<-.3 && rest.y>0);
        const auto high=r::boneworks_elbow_pole({.1,.1,.55},f,out,up,L,0);
        assert(high.z>rest.z+.3);
        const auto behind=r::boneworks_elbow_pole({-.2,.4,.0},f,out,up,L,0);
        assert(behind.z>rest.z);
        const auto palm_up=r::boneworks_elbow_pole({.3,.1,-.4},f,out,up,L,1),palm_down=r::boneworks_elbow_pole({.3,.1,-.4},f,out,up,L,-1);
        assert(palm_up.y<rest.y-.05 && palm_down.y>rest.y);
        Vec3 prev{};bool have=false;double worst=0;
        for(double a=0;a<=3.1;a+=.01) { // hand sweeps from low-front over the head to behind
            const Vec3 reach{.5*std::cos(a)*std::cos(a*.5),.15,-.5*std::cos(a)+.2*std::sin(a)+.01};
            const auto p=r::boneworks_elbow_pole(reach,f,out,up,L,0,have?&prev:nullptr,.016);
            assert(std::abs(d2vr::length(p)-1)<1e-9);
            if(have) {const auto d=std::acos(std::clamp(d2vr::dot(p,prev),-1.,1.));worst=std::max(worst,d);assert(d<=6*.016+1e-6);}
            prev=p;have=true;
        }
        std::cout<<"max pole step "<<worst<<"\n";
    }
    // 5: calibration scales the arm reach and shoulder width; defaults stay sane.
    {
        auto f=make(0,0,{0,.3,1},{0,-.3,1});
        assert(r::ArmTracking::arm_length_metres(f,{})==.62 && r::ArmTracking::arm_scale(f,{})==1);
        const d2vr::AvatarCalibrationResult c{1,1.9,.4,.22,1.18,{}};
        assert(std::abs(r::ArmTracking::arm_length_metres(f,c)-.75)<1e-9 && r::ArmTracking::arm_scale(f,c)==1.18);
        const auto a=r::ArmTracking::shoulder_world(f,c,0),b=r::ArmTracking::shoulder_world(f,c,1);
        assert(std::abs(d2vr::length(a-b)-.4)<1e-9 && std::abs(a.z-(1.7-.22))<1e-9);
        f.reference_epoch=2;assert(r::ArmTracking::arm_scale(f,c)==1); // other space: ignored
        // head pitch cannot move shoulders: basis is level, so they depend only on head position and chest yaw.
        const double y=0;auto g=f;g.reference_epoch=1;
        assert(d2vr::length(r::ArmTracking::shoulder_world(g,c,0,&y)-a)<1e-9);
    }
    std::cout<<"arm body PASS\n";
}
