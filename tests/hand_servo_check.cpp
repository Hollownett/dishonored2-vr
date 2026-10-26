#include "hand_servo.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
    HandServoState st;WorldTrackedPose target,body;Vec3 v{},w{};double held=0,tf=1,dt=1./90;bool blocked=false;double lever=kDefaultHeldLever;Vec3 ff{};
    HandServoConfig cfg;GrabTuning tun;
    HandServoOutput step() {
        auto o=servo_step(st,target,body,v,w,held,dt,tf,cfg,tun,blocked,lever,ff);
        v=o.linear;w=o.angular;
        body.position=body.position+v*std::min(dt,tun.dt_max);
        double a=length(w)*std::min(dt,tun.dt_max);
        if(a>1e-12) {Vec3 ax=w*(1/length(w));for(auto& b:body.basis) b=rot(b,ax,a);}
        return o;
    }
};
}
int main() {
    {   // Translation invariance: ordinary hand motion must behave the same while sprinting.
        // The v613 hand-target gate cuts 6 m/s feedforward to ~1.1 m/s on the back stroke,
        // then restores it on the forward stroke, adding up to 15 cm of cyclic tracking error.
        double old_peak=0;
        for(bool root_gate:{false,true}) {
            HandServoState moving{},rest{};HandServoConfig cfg;GrabTuning tuning;
            auto world=pose({}),local=pose({});Vec3 world_v{},local_v{};double peak=0,ff_low=100;
            const double dt=1./90,V=6;
            for(unsigned i=0;i<900;++i) {
                const double t=i*dt;const Vec3 root{V*t,0,0};
                const auto hand=pose({.12*std::sin(t*10),0,0});auto target=hand;target.position=target.position+root;
                const auto a=servo_step(moving,target,world,world_v,{},0,dt,1,cfg,tuning,false,kDefaultHeldLever,{V,0,0},0,
                    root_gate?std::optional<Vec3>{root}:std::nullopt);
                const auto b=servo_step(rest,hand,local,local_v,{},0,dt,1,cfg,tuning,false);
                world_v=a.linear;local_v=b.linear;
                if(i>180) {peak=std::max(peak,std::abs((world.position.x-root.x)-local.position.x));ff_low=std::min(ff_low,moving.prev_ff.x);}
                world.position=world.position+world_v*dt;local.position=local.position+local_v*dt;
            }
            if(root_gate) {CHECK(peak<.001);CHECK(ff_low>5.99);CHECK(old_peak>.10);}
            else old_peak=peak;
        }
    }
    {   // The publication clock uses elapsed time, even when force/snap integration caps a slow frame.
        HandServoState st;HandServoConfig cfg;GrabTuning tuning;const double dt=.04;
        const auto target=pose({});servo_step(st,target,target,{}, {},0,dt,1,cfg,tuning,false);
        CHECK(std::abs(st.clock-dt)<1e-12);
    }

    {   // feedforward_holds_running_hand: the target moves at 6 m/s along x for 2 s at 90 Hz from a matched start
        double gap_with=0,gap_without=0;
        for(const bool use:{true,false}) {
            Sim s;s.body=pose({});s.target=pose({});if(use) s.ff={6,0,0};
            HandServoOutput o;
            for(int i=0;i<180;++i) {s.target.position.x+=6./90;o=s.step();}
            (use?gap_with:gap_without)=o.gap;   // the gap the servo sees at the last tick
        }
        CHECK(gap_with<=.02);CHECK(gap_without>=.2);
    }
    {   // repeated_target_frame_bounded: 6 m/s at 90 Hz with a repeated target frame (a hitch) every 30 or every 10 ticks: a single unchanged tick is not a stop
        for(const int every:{30,10}) {
            Sim s;s.body=pose({});s.target=pose({});s.ff={6,0,0};
            double maxgap=0,sum=0;int n=0;
            for(int i=1;i<=270;++i) {
                if(i%every!=0) s.target.position.x=6.*i/90;   // a repeated frame keeps the last pose; the next one catches up
                auto o=s.step();
                if(i>90) {maxgap=std::max(maxgap,o.gap);sum+=o.gap;++n;}
            }
            std::printf("repeat every %d: max gap %.4f mean gap %.4f\n",every,maxgap,sum/n);
            CHECK(maxgap<=.07);CHECK(sum/n<=(every==30?.01:.02));   // every 10: the repeated frame alone costs .016 (the same with no gate at all), so .02
        }
    }
    {   // sudden_stop_bounded: 2 s at V along x then a hard stop, no sign reversal after it. RAW peak body position past the stopped target (window estimator):
        // .062 m at 3 m/s, .125 m at 6 m/s (instant ff: .033 / .067); of that .033 / .067 is the body's already-travelled lead from the last running tick (the sim
        // moves the body after the target) and .029 / .058 is the extra tick a stop needs to read as a stop (2 unchanged ticks, so a repeated frame is not one). Old behaviour 0, unfixed Fix B .110 / .255 excess
        for(const double V:{3.,6.}) for(const bool window:{false,true}) {
            Sim s;s.body=pose({});s.target=pose({});s.ff={V,0,0};
            std::vector<std::pair<int,double>> hist;   // the 0.2 s window estimator of the player's velocity (V until it has a baseline)
            double gap_run=0,peak=-1e9,last_err=0,lead=0;int flips=0;
            for(int i=0;i<180+90;++i) {
                const bool run=i<180;
                if(run) s.target.position.x+=V/90;
                hist.push_back({i,s.target.position.x});
                if(window) {
                    size_t j=0;while(j+1<hist.size()&&hist[j+1].first<=i-18) ++j;   // 0.2 s back
                    const double dt=(i-hist[j].first)/90.;
                    s.ff=dt>.05?Vec3{(s.target.position.x-hist[j].second)/dt,0,0}:Vec3{V,0,0};
                } else s.ff=run?Vec3{V,0,0}:Vec3{};
                // the body as the tick starts (the drive reads it before writing): the last running tick's own feedforward step is the sim's ordering, not an overshoot
                const double err=s.body.position.x-s.target.position.x;   // > 0: past the stopped target
                auto o=s.step();
                if(i==179) gap_run=o.gap;
                if(!run) {
                    if(i==180) lead=err;   // the body's committed lead from the last running tick
                    peak=std::max(peak,err);
                    if(i>180&&std::fabs(err)>1e-4&&std::fabs(last_err)>1e-4&&(err>0)!=(last_err>0)) ++flips;
                    last_err=err;
                }
            }
            std::printf("stop V=%g window=%d gap_run=%.4f peak=%.4f lead=%.4f flips=%d final=%.4f\n",V,window,gap_run,peak,lead,flips,last_err);
            CHECK(gap_run<=.02);CHECK(peak<=.13);CHECK(peak-lead<=.06);CHECK(flips==0);CHECK(std::fabs(last_err)<.01);
        }
    }
    {   // v613 item 6, running_beat_no_swing: the render thread publishes the target at 90 Hz and the dispatcher drives at another rate (the game frame, 60..144 Hz,
        // QPC dt). v612's interval-quantised target velocity swung the body by up to 14 cm at 6 m/s at a 91 Hz drive (a 1 Hz beat: hands that lag, catch up, lag again);
        // the windowed gate with the target's publication age holds the error to a constant. Error measured against the continuous target V*t.
        for(const bool legacy:{false,true}) for(const double hz:{60.,72.,85.,89.,91.,100.,120.,144.}) for(const double V:{1.4,3.,6.}) {
            hand_servo_legacy_gate=legacy;
            HandServoState st;HandServoConfig cfg;GrabTuning tun;WorldTrackedPose body=pose({}),target=pose({});Vec3 v{};
            const double Tr=1./90,Td=1./hz;double t=0,next_r=Tr,next_d=Td,pub=0,mn=1e9,mx=-1e9;
            while(t<6) {
                if(next_r<=next_d) {t=next_r;target.position.x=V*t;pub=t;next_r+=Tr;}
                else {
                    t=next_d;next_d+=Td;
                    const auto o=servo_step(st,target,body,v,{},0,Td,1,cfg,tun,false,kDefaultHeldLever,Vec3{V,0,0},t-pub);
                    v=o.linear;body.position=body.position+v*std::min(Td,tun.dt_max);
                    if(t>2) {const double e=V*t-body.position.x;mn=std::min(mn,e);mx=std::max(mx,e);}
                }
            }
            hand_servo_legacy_gate=false;
            if(!legacy) CHECK(mx-mn<=.01);                                  // constant lag: no oscillation
            else if(hz==91.&&V==6.) CHECK(mx-mn>=.08);                      // the v612 gate: the swing this test pins
        }
    }
    {   // stamp-less drive (the fallback on the servo's own clock) still beats v612's swing at the worst rates
        for(const double hz:{85.,89.,91.,100.}) {
            HandServoState st;HandServoConfig cfg;GrabTuning tun;WorldTrackedPose body=pose({}),target=pose({});Vec3 v{};
            const double V=6,Tr=1./90,Td=1./hz;double t=0,next_r=Tr,next_d=Td,mn=1e9,mx=-1e9;
            while(t<6) {
                if(next_r<=next_d) {t=next_r;target.position.x=V*t;next_r+=Tr;}
                else {t=next_d;next_d+=Td;const auto o=servo_step(st,target,body,v,{},0,Td,1,cfg,tun,false,kDefaultHeldLever,Vec3{V,0,0});
                    v=o.linear;body.position=body.position+v*std::min(Td,tun.dt_max);if(t>2) {const double e=V*t-body.position.x;mn=std::min(mn,e);mx=std::max(mx,e);}}
            }
            CHECK(mx-mn<=.07);
        }
    }
    {   // a stop at a 100 Hz drive is still a stop (two unchanged publications to read it, the body-ahead cap fades the feedforward within 2 cm of overshoot, so the net overshoot is ~6 cm at 6 m/s, as for synchronous ticks) and one dropped render frame at 6 m/s is not
        for(const bool stop:{true,false}) {
            HandServoState st;HandServoConfig cfg;GrabTuning tun;WorldTrackedPose body=pose({}),target=pose({});Vec3 v{};
            const double V=6,Tr=1./90,Td=1./100;double t=0,next_r=Tr,next_d=Td,pub=0,stop_at=3,peak=-1e9,frame=0,worst_gap=0;
            while(t<5) {
                if(next_r<=next_d) {t=next_r;++frame;const bool drop=!stop && std::fmod(frame,45.)==0;if(!drop) {target.position.x=V*std::min(t,stop?stop_at:1e9);pub=t;}next_r+=Tr;}   /* published every frame, a stopped hand included */
                else {t=next_d;next_d+=Td;
                    const bool moving=!stop||t<=stop_at;
                    const auto o=servo_step(st,target,body,v,{},0,Td,1,cfg,tun,false,kDefaultHeldLever,moving?Vec3{V,0,0}:Vec3{V,0,0}*(t<stop_at+.1?1:0),t-pub);
                    v=o.linear;body.position=body.position+v*std::min(Td,tun.dt_max);
                    if(stop&&t>stop_at) peak=std::max(peak,body.position.x-target.position.x);
                    if(!stop&&t>2) worst_gap=std::max(worst_gap,std::fabs(V*t-body.position.x));
                }
            }
            if(stop) {CHECK(peak<=.13);CHECK(peak-V*Td<=.065);}   /* the body leads by its last committed tick (V*Td) in this sim's ordering */ else CHECK(worst_gap<=.12);          // a dropped frame costs at most ~1 frame of travel (6.7 cm) plus the usual lag
        }
    }
    {   // zero_feedforward_identical: bit-identical to the call without the argument over a 500-step random sequence
        unsigned seed=12345;auto rnd=[&]{seed=seed*1664525u+1013904223u;return ((seed>>8)&0xffff)/65535.-.5;};
        HandServoState a,b;HandServoConfig cfg;GrabTuning tun;
        for(int i=0;i<500;++i) {
            WorldTrackedPose t=pose({rnd(),rnd(),rnd()}),bd=pose({rnd()*.3,rnd()*.3,rnd()*.3});
            Vec3 v{rnd()*4,rnd()*4,rnd()*4},w{rnd(),rnd(),rnd()};double kg=i%7?0:rnd()+.5;
            auto o1=servo_step(a,t,bd,v,w,kg,1./90,1,cfg,tun,i%11==0);
            auto o2=servo_step(b,t,bd,v,w,kg,1./90,1,cfg,tun,i%11==0,kDefaultHeldLever,Vec3{});
            CHECK(std::memcmp(&o1.linear,&o2.linear,sizeof o1.linear)==0&&std::memcmp(&o1.angular,&o2.angular,sizeof o1.angular)==0);
            CHECK(o1.valid==o2.valid&&o1.snap==o2.snap&&o1.capped==o2.capped&&std::memcmp(&o1.gap,&o2.gap,sizeof o1.gap)==0);
        }
    }
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
        for(int i=0;i<450;++i) {auto o=servo_step(a.st,a.target,a.body,{},{},0,1./90,1,a.cfg,a.tun,false);CHECK(!o.snap);}
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
    {   // jerk_filter_smooths: body held at rest under a target step; the commanded acceleration ramps up strictly, then saturates at the cap
        HandServoState st;WorldTrackedPose t=pose({.3,0,0}),b=pose({});
        double prev=0,top=0;
        for(int i=0;i<12;++i) {
            auto o=servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            double a=o.linear.x*90;
            CHECK(a>=prev-1e-9);if(i<4) CHECK(a>prev);
            prev=a;top=std::max(top,a);
        }
        CHECK(top>140&&top<=150+1e-6);   // approaches cap/mass = 300/2, unscaled
    }
    {   // step response at several rates, unheld and 20 kg held, tf 1/.5/.25
        for(double hz:{20.,45.,72.,90.,120.}) for(double held:{0.,20.}) for(double tf:{1.,.5,.25}) {
            if(held>0&&tf<1) continue;
            Sim s;s.body=pose({});s.target=pose({.3,0,0});s.dt=1/hz;s.held=held;s.tf=tf;
            double peak=0,pv=0;
            for(int i=0;i<static_cast<int>(hz*(held>0?8:4)/tf);++i) {s.step();peak=std::max(peak,s.body.position.x);pv=std::max(pv,length(s.v));}
            CHECK(peak<=.3+(held>0?.005:.001));
            CHECK(pv<=s.cfg.max_speed+1e-9);
            CHECK(std::fabs(s.body.position.x-.3)<.01);
        }
    }
    {   // time_factor above 1 behaves as 1; 0 / negative / NaN mean unknown (1)
        Sim a,b;a.body=b.body=pose({});a.target=b.target=pose({.3,.2,0});b.tf=2;
        for(int i=0;i<20;++i) {auto oa=a.step();auto ob=b.step();CHECK(oa.linear.x==ob.linear.x&&oa.linear.y==ob.linear.y);}
        for(double tf:{0.,-1.,nan_value}) {
            Sim c,d;c.body=d.body=pose({});c.target=d.target=pose({.3,0,0});d.tf=tf;
            CHECK(c.step().linear.x==d.step().linear.x);
        }
    }
    {   // read-back is in written (scaled) units: tf .5 with half the velocity is the same loop as tf 1, output halved
        Vec3 V{1.5,.7,0};HandServoState s1,s2;
        auto o1=servo_step(s1,pose({.3,.1,0}),pose({}),V,{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
        auto o2=servo_step(s2,pose({.3,.1,0}),pose({}),V*.5,{},0,1./90,.5,HandServoConfig{},GrabTuning{},false);
        CHECK(std::fabs(o2.linear.x-.5*o1.linear.x)<1e-12&&std::fabs(o2.linear.y-.5*o1.linear.y)<1e-12);
    }
    {   // garbage read-back never reaches the output
        for(Vec3 bad:{Vec3{1e300,0,0},Vec3{101,0,0},Vec3{nan_value,0,0}}) {
            HandServoState st;
            auto o1=servo_step(st,pose({.3,0,0}),pose({}),bad,{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            auto o2=servo_step(st,pose({.3,0,0}),pose({}),{},bad,0,1./90,1,HandServoConfig{},GrabTuning{},false);
            CHECK(!o1.valid&&!o2.valid&&length(o1.linear)==0&&length(o2.angular)==0&&length(o2.linear)==0);
        }
        HandServoState st;   // NaN / negative held mass does not poison the output
        for(double kg:{nan_value,-5.}) {
            auto o=servo_step(st,pose({.3,0,0}),pose({}),{},{},kg,1./90,1,HandServoConfig{},GrabTuning{},false);
            CHECK(kg>0||!std::isnan(kg)?o.valid&&std::isfinite(o.linear.x):!o.valid);
        }
    }
    {   // moving target: 2 m/s tracked closely, 2 and 6 m/s never snap
        for(double speed:{2.,6.}) {
            Sim s;s.body=pose({});s.target=pose({.1,0,0});double worst=0;
            for(int i=0;i<450;++i) {
                s.target.position.x+=speed/90;
                auto o=s.step();
                CHECK(!o.snap);
                if(i>45) worst=std::max(worst,o.gap);
            }
            if(speed==2.) CHECK(worst<.2);
        }
    }
    {   // a stuck body (zero velocity, target 0.4 m away) does snap, in the window
        HandServoState st;WorldTrackedPose t=pose({.4,0,0}),b=pose({});
        double first=-1;
        for(int i=1;i<=90;++i) if(servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false).snap) {first=i/90.;break;}
        CHECK(first>=.24&&first<=.26);
    }
    {   // hitch: a 0.5 s dt counts as dt_max (1/30) toward the snap window
        HandServoState st;WorldTrackedPose t=pose({.5,0,0}),b=pose({});
        for(int i=0;i<7;++i) CHECK(!servo_step(st,t,b,{},{},0,.5,1,HandServoConfig{},GrabTuning{},false).snap);
        CHECK(servo_step(st,t,b,{},{},0,.5,1,HandServoConfig{},GrabTuning{},false).snap);
    }
    {   // degenerate / non-unit / left-handed basis rejected
        HandServoState st;
        WorldTrackedPose z=pose({}),big=pose({}),lh=pose({});
        z.basis={Vec3{},Vec3{},Vec3{}};big.basis[0]={2,0,0};lh.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,-1}};
        for(const auto* bad:{&z,&big,&lh}) {
            auto o1=servo_step(st,*bad,pose({}),{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            auto o2=servo_step(st,pose({.3,0,0}),*bad,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            CHECK(!o1.valid&&!o2.valid&&length(o1.angular)==0&&length(o2.linear)==0);
        }
    }
    {   // near 180 degrees: spin axis is continuous across the branch boundary

        for(double d:{-3e-3,-1e-3,-3e-4,0.,3e-4,1e-3,3e-3}) {
            WorldTrackedPose t=pose({}),b=pose({});
            double ang=3.14159265358979323846+d;   // rotate about Z, slightly off pi
            t.basis={Vec3{std::cos(ang),std::sin(ang),0},Vec3{-std::sin(ang),std::cos(ang),0},Vec3{0,0,1}};
            HandServoState st;
            auto o=servo_step(st,t,b,{},{},0,1./90,1,HandServoConfig{},GrabTuning{},false);
            CHECK(o.valid&&std::fabs(length(o.angular)-std::sqrt(2*40*3.14159265))<.05);   // brake-limited spin
            if(d!=0) {   // shortest rotation: sign follows d (ang just past pi means rotating the other way round, -z)
                CHECK((d>0)==(o.angular.z<0));
            }

        }
    }
    {   // angular overshoot <= 2 degrees at 0/5/20 kg, 90 degrees and 0.5 rad, 45-120 Hz; settles
        for(double hz:{45.,72.,90.,120.}) for(double held:{0.,5.,20.}) for(double ang:{1.5707963267948966,.5}) {
            Sim s;s.body=pose({});s.target=pose({});s.dt=1/hz;s.held=held;
            s.target.basis={Vec3{std::cos(ang),std::sin(ang),0},Vec3{-std::sin(ang),std::cos(ang),0},Vec3{0,0,1}};
            double peak=0;
            for(int i=0;i<static_cast<int>(hz*8);++i) {s.step();peak=std::max(peak,std::atan2(s.body.basis[0].y,s.body.basis[0].x));}
            CHECK(peak<=ang+.0349066);
            CHECK(std::fabs(std::atan2(s.body.basis[0].y,s.body.basis[0].x)-ang)<.0175);
        }
    }
    {   // held_lever is per call: a longer lever lags more; garbage falls back to the default
        Sim a,b,c,d;a.body=b.body=c.body=d.body=pose({});a.target=b.target=c.target=d.target=pose({});
        a.target.basis=b.target.basis=c.target.basis=d.target.basis={Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};
        a.held=b.held=c.held=d.held=20;b.lever=.6;c.lever=nan_value;d.lever=-1;
        double wa=length(a.step().angular),wb=length(b.step().angular),wc=length(c.step().angular),wd=length(d.step().angular);
        CHECK(wb<wa&&wc==wa&&wd==wa);
    }
    {   // 45 Hz stuck snap lands within one tick of the window
        HandServoState st;WorldTrackedPose t=pose({.4,0,0}),b=pose({});double first=-1;
        for(int i=1;i<=90;++i) if(servo_step(st,t,b,{},{},0,1./45,1,HandServoConfig{},GrabTuning{},false).snap) {first=i/45.;break;}
        CHECK(first>=.24&&first<=.26+1./45);
    }
    {   // a heavy hold lags in rotation (held inertia is in the torque cap); the bare hand does not
        Sim a,b;a.body=b.body=pose({});a.target=b.target=pose({});
        a.target.basis=b.target.basis={Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};b.held=20;
        auto oa=a.step();auto ob=b.step();
        CHECK(length(ob.angular)<length(oa.angular)*.5);
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
