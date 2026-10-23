#include "hand_servo.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
const double nan_value=std::nan("");
WorldTrackedPose pose(Vec3 p) {WorldTrackedPose w;w.position=p;w.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};return w;}
Vec3 rot(Vec3 v,Vec3 axis,double a) {   // Rodrigues, axis unit
    double c=std::cos(a),s=std::sin(a);
    return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));
}
struct Sim {
    HandServoState st;WorldTrackedPose target,body;Vec3 v{},w{};double held=0,tf=1,dt=1./90;bool blocked=false;
    HandServoConfig cfg;GrabTuning tun;
    HandServoOutput step() {
        auto o=servo_step(st,target,body,v,w,held,dt,tf,cfg,tun,blocked);
        v=o.linear;w=o.angular;
        body.position=body.position+v*std::min(dt,tun.dt_max);
        double a=length(w)*std::min(dt,tun.dt_max);
        if(a>1e-12) {Vec3 ax=w*(1/length(w));for(auto& b:body.basis) b=rot(b,ax,a);}
        return o;
    }
};
}
int main() {
    {   // step_response_no_overshoot
        Sim s;s.body=pose({});s.target=pose({.3,0,0});
        double peak=0;bool reached=false;
        for(int i=0;i<180;++i) {s.step();peak=std::max(peak,s.body.position.x);if(std::fabs(s.body.position.x-.3)<.01) reached=true;}
        CHECK(reached);CHECK(peak<=.301);
    }
    {   // asymmetric_caps: first-tick velocity change, axial vs sideways
        Sim a,b;a.body=b.body=pose({});a.target=pose({.3,0,0});b.target=pose({0,.3,0});
        a.target.basis[0]={1,0,0};b.target.basis[0]={1,0,0};
        auto oa=a.step();auto ob=b.step();
        CHECK(oa.capped&&ob.capped);
        // first tick the filter scales both by the same alpha, so the ratio is the cap ratio
        CHECK(std::fabs(length(oa.linear)/length(ob.linear)-300./90.)<.01);
    }
    {   // heavy_held_lags_no_runaway
        Sim light,heavy;light.body=heavy.body=pose({});light.target=heavy.target=pose({5,0,0});heavy.held=20;
        auto l=light.step();auto h=heavy.step();
        CHECK(length(h.linear)<length(l.linear)*.2);
        double peak=0;
        for(int i=0;i<900;++i) {heavy.target.position.x=5+i*.05;heavy.step();peak=std::max(peak,length(heavy.v));}
        CHECK(peak<=heavy.cfg.max_speed+1e-9);
    }
    {   // time_factor_scales
        Sim a,b;a.body=b.body=pose({});a.target=b.target=pose({.3,.2,0});b.tf=.5;
        auto oa=a.step();auto ob=b.step();
        CHECK(std::fabs(length(ob.linear)-.5*length(oa.linear))<1e-12);
    }
    {   // non_finite_and_huge_rejected
        Sim s;s.body=pose({});
        for(Vec3 p:{Vec3{nan_value,0,0},Vec3{51,0,0},Vec3{0,1e300,0}}) {
            s.target=pose(p);s.v={};
            auto o=s.step();
            CHECK(!o.valid&&length(o.linear)==0&&length(o.angular)==0&&!o.snap);
        }
        s.target=pose({1,0,0});s.v={nan_value,0,0};CHECK(!s.step().valid);
        s.v={};s.target.basis[0]={nan_value,0,0};CHECK(!s.step().valid);
    }
    {   // hitch_dt_clamped
        Sim a,b;a.body=b.body=pose({});a.target=b.target=pose({.3,.1,0});a.dt=.5;b.dt=1./30;
        auto oa=a.step();auto ob=b.step();
        CHECK(oa.valid&&oa.linear.x==ob.linear.x&&oa.linear.y==ob.linear.y&&oa.gap==ob.gap);
        for(double bad:{0.,-1.,nan_value}) {
            Sim c;c.body=pose({});c.target=pose({.3,0,0});c.dt=bad;
            auto o=c.step();CHECK(!o.valid&&length(o.linear)==0&&length(o.angular)==0);
        }
    }
    {   // snap_hysteresis
        Sim a;a.body=pose({});a.target=pose({.34,0,0});
        for(int i=0;i<450;++i) {a.st.has=false;auto o=servo_step(a.st,a.target,a.body,{},{},0,1./90,1,a.cfg,a.tun,false);CHECK(!o.snap);}
        HandServoState st;WorldTrackedPose t=pose({.36,0,0}),b=pose({});
        double first=-1;
        for(int i=1;i<=90;++i) {
            auto o=servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            if(o.snap) {first=i/90.;break;}
        }
        CHECK(first>=.24&&first<=.26);
        // closes at 0.2 s: reset, then needs a full period again
        HandServoState s2;int n=0;
        for(;n<18;++n) CHECK(!servo_step(s2,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false).snap);
        servo_step(s2,pose({.2,0,0}),b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
        CHECK(s2.gap_time==0);
        for(int i=0;i<20;++i) CHECK(!servo_step(s2,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false).snap);
    }
    {   // snap_target_overlap_refused
        HandServoState st;WorldTrackedPose t=pose({1,0,0}),b=pose({});
        for(int i=0;i<900;++i) CHECK(!servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},true).snap);
        CHECK(servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false).snap==false);   // timer was held at zero
        bool snapped=false;
        for(int i=0;i<40;++i) snapped=servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false).snap||snapped;
        CHECK(snapped);
    }
    {   // jerk_filter_smooths: a target step ramps the commanded acceleration monotonically
        Sim s;s.body=pose({});s.target=pose({.3,0,0});
        double prev=0;
        for(int i=0;i<3;++i) {s.step();double a=s.st.filtered_accel.x;CHECK(a>prev);prev=a;}
    }
    {   // x9_corpse_scale_and_floor_do_not_reach_the_hand
        Sim a,b;a.body=b.body=pose({});a.target=b.target=pose({.3,.2,0});
        b.tun.strength_scale=6;b.tun.mass_floor_kg=3;
        for(int i=0;i<30;++i) {auto oa=a.step();auto ob=b.step();CHECK(oa.linear.x==ob.linear.x&&oa.linear.y==ob.linear.y&&oa.angular.z==ob.angular.z);}
        // and the numbers are the unscaled ones: first axial tick = 300/90/2 m/s through the filter
        Sim c;c.body=pose({});c.target=pose({3,0,0});
        auto o=c.step();
        double alpha=(1./90)/(c.cfg.jerk_tau+1./90);
        CHECK(std::fabs(o.linear.x-300./90/2*alpha)<1e-9);
    }
    {   // angular: 90 degree yaw error converges, overshoot <= 2 degrees
        Sim s;s.body=pose({});s.target=pose({});
        s.target.basis={Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};   // body +X forward -> target +Y forward: +90 about Z
        double peak=0;
        for(int i=0;i<270;++i) {
            s.step();
            double yaw=std::atan2(s.body.basis[0].y,s.body.basis[0].x);
            peak=std::max(peak,yaw);
        }
        double yaw=std::atan2(s.body.basis[0].y,s.body.basis[0].x);
        CHECK(std::fabs(yaw-1.5707963267948966)<.0175);
        CHECK(peak<=1.5707963267948966+.0349066);
    }
    std::puts("hand_servo_check ok");
    return 0;
}
