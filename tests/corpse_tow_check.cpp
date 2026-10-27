#include "corpse_tow.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
const double nan_value=std::nan("");
// Strength 1, tow on, a light pelvis so the force cap does not hide the speed law unless the test wants it to.
GrabTuning tow_on() {GrabTuning t;t.strength_scale=1;t.tow=true;return t;}
TowInput base(const GrabTuning& t,double reach,double bind) {
    TowInput in;in.tuning=&t;in.pelvis={0,0,0};in.target={reach,0,0};in.bind_reach=bind;in.pelvis_kg=12;in.dt=1./30;return in;
}
}
int main() {
    {   // tow is ON by default (Task 10 wired limb_saturated); the knob `tow 0` still turns it off and then nothing is written
        CHECK(GrabTuning{}.tow);
        GrabTuning t;t.tow=false;
        TowInput in=base(t,2,.5);const auto out=plan_tow(in);CHECK(!out.write && !out.disabled_saturated && near(out.bind_reach,.5));
    }
    {   // first_call_sets_reach_clamped
        const auto t=tow_on();
        auto a=plan_tow(base(t,.1,0));CHECK(!a.write && near(a.bind_reach,.4));
        auto b=plan_tow(base(t,3,0));CHECK(!b.write && near(b.bind_reach,1.4));
        auto c=plan_tow(base(t,.9,0));CHECK(!c.write && near(c.bind_reach,.9));
        auto d=plan_tow(base(t,2,-1));CHECK(!d.write && near(d.bind_reach,1.4));
    }
    {   // inside_deadband_no_write
        auto t=tow_on();
        auto out=plan_tow(base(t,.85,.8));CHECK(!out.write);
        out=plan_tow(base(t,.8501,.8));CHECK(out.write);
    }
    {   // speed_6_per_metre_max_tow_max_mps (force cap loosened so the law shows)
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,1.3,.8);in.pelvis_kg=.5;in.dt=1./30;
        auto out=plan_tow(in);CHECK(out.write && near(length(out.linear),3,1e-6) && near(out.linear.x,3,1e-6));
        in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=1./30;
        out=plan_tow(in);CHECK(out.write && near(length(out.linear),tow_max_mps,1e-6));
    }
    {   // stale_dt_scales_the_catch_up: a 0.5 s stale frame asks for dt_max/dt of the normal speed (2.0 m x 6 / 15 = 0.8), dt <= dt_max is unchanged
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=.5;
        auto out=plan_tow(in);CHECK(out.write && near(length(out.linear),.8,1e-6));
        in.dt=t.dt_max;out=plan_tow(in);CHECK(near(length(out.linear),tow_max_mps,1e-6));
    }
    {   // stale_recovery: a hitch (dt > stale_dt_s) starts a ramped cap (catch_up_mps + ramp * t) up to the normal tow_max_mps, where it ends with no step
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=1./30;
        auto out=plan_tow(in);CHECK(near(length(out.linear),tow_max_mps,1e-6) && out.recovery_s==0);   // normal play: unchanged law, no state
        in.dt=.5;out=plan_tow(in);CHECK(out.write && length(out.linear)<=t.catch_up_mps+1e-9 && out.recovery_s>0);
        in.dt=1./90;in.recovery_s=out.recovery_s;double tt=0,prev=length(out.linear),worst=0;int frames=0;
        while(in.recovery_s>0 && frames<500) {
            out=plan_tow(in);tt+=in.dt;++frames;
            const double v=length(out.linear);
            if(out.recovery_s>0) CHECK(near(v,t.catch_up_mps+t.catch_up_ramp_mps2*tt,1e-6));
            if(frames>1) worst=std::max(worst,std::fabs(v-prev));prev=v;in.recovery_s=out.recovery_s;
        }
        CHECK(out.recovery_s==0 && frames>5 && frames<500);
        CHECK(worst<=t.catch_up_ramp_mps2*in.dt+1e-6);   // no step across the end of recovery
        CHECK(near(prev,tow_max_mps,1e-6));
        {   // consecutive stale frames (dt 0.15) never re-arm an active ramp: it reaches the normal cap and ends
            auto st=base(t,2.8,.8);st.pelvis_kg=.5;st.dt=.15;int ended=-1;
            for(int i=0;i<20 && ended<0;++i) {auto o=plan_tow(st);st.recovery_s=o.recovery_s;if(i>0 && o.recovery_s==0) ended=i;}
            CHECK(ended>=1 && ended<=13);   // cap = 1 + 4 t reaches 8 at t = 1.75 s = 12 frames
        }
        in.recovery_s=.4;in.target={.84,0,0};out=plan_tow(in);CHECK(!out.write && out.recovery_s==0);   // reach closed to within 5 cm of the bind reach
        in.target={2.8,0,0};in.recovery_s=std::nan("");out=plan_tow(in);CHECK(out.recovery_s==0 && near(length(out.linear),tow_max_mps,1e-6));   // junk state is ignored
        in.recovery_s=0;in.dt=.04;in.target={1.3,0,0};out=plan_tow(in);CHECK(out.recovery_s==0 && near(length(out.linear),3*t.dt_max/.04,1e-6));   // 25 fps is not a hitch: only the dt_max/dt scaling
    }
    {   // routing (source-level): native_grab.hpp carries plan_tow's recovery state in the Lane and resets it wherever the tow reach resets
        std::ifstream f("src/native_grab.hpp");std::stringstream ss;ss<<f.rdbuf();const std::string src=ss.str();
        CHECK(!src.empty());
        auto count=[&](const std::string& n) {int c=0;for(std::size_t p=src.find(n);p!=std::string::npos;p=src.find(n,p+1)) ++c;return c;};
        CHECK(count("lane.tow=0")==0 && count("lane.reset_tow();")==3);   // the three bind sites; every release goes through Lane::end_hold, which resets it too
        CHECK(count("bind_motion=0;bind_owner.reset();anchor_view={};reset_tow();")==1);
        CHECK(count("void reset_tow() {tow=0;tow_recovery=0;last_tow=0;}")==1);
        CHECK(count("in.recovery_s=lane.tow_recovery;")==1 && count("lane.tow_recovery=plan.recovery_s;")==1);
    }
    {   // horizontal_only
        auto t=tow_on();auto in=base(t,1.3,.8);in.target={1.3,0,1};in.pelvis_velocity={0,0,5};
        const auto out=plan_tow(in);CHECK(out.write && out.linear.z==0 && out.linear.x>0);
    }
    {   // mass_cap_binds: 300 N * 0.01 s / (12 kg + 2 kg hand)
        auto t=tow_on();auto in=base(t,2.4,.8);in.dt=.01;in.pelvis_kg=12;
        const auto out=plan_tow(in);CHECK(out.write);
        CHECK(near(length(out.linear),300*.01/14,1e-6));
    }
    {   // moving pelvis: the cap limits the DELTA from the current velocity, not the absolute speed
        auto t=tow_on();auto in=base(t,2.8,.8);in.dt=.01;in.pelvis_kg=12;in.pelvis_velocity={3,0,0};   // wanted tow_max_mps
        auto out=plan_tow(in);const double cap=300*.01/14;
        CHECK(out.write && near(out.linear.x-3,cap,1e-6) && near(out.linear.y,0));
        in.pelvis_velocity={1.5,0,0};out=plan_tow(in);CHECK(near(out.linear.x-1.5,cap,1e-6));
        in.pelvis_velocity={tow_max_mps-.1,0,0};out=plan_tow(in);CHECK(near(out.linear.x,tow_max_mps,1e-9)); // inside the cap: reaches the wanted speed
        in.pelvis_velocity={tow_max_mps+1,0,0};out=plan_tow(in);CHECK(out.write && near(out.linear.x,tow_max_mps+1-cap,1e-6)); // faster than wanted: eased down by the cap
    }
    {   // hand feed (user, headset v627: a walk dragged the body until the lag rule dropped it): the hand's speed away from the pelvis is fed
        // forward, also inside the 5 cm band; across the pull it is not
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,.82,.8);in.pelvis_kg=.5;in.dt=1./30;in.hand_velocity={2,0,5};
        auto out=plan_tow(in);CHECK(out.write && near(out.linear.x,.02*6+2,1e-6) && out.linear.z==0);
        in.hand_velocity={0,2,0};CHECK(!plan_tow(in).write);
        in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=1./30;in.hand_velocity={6,0,0};CHECK(near(plan_tow(in).linear.x,tow_max_mps,1e-6));   // still capped
        in.hand_velocity={nan_value,0,0};CHECK(near(plan_tow(in).linear.x,tow_max_mps,1e-6));   // a bad feed is no feed
        in.hand_velocity={2,0,0};in.limb_saturated=true;   // a saturated limb (sim v632): the catch-up stands down, the walk does not
        auto sat=plan_tow(in);CHECK(sat.write && !sat.disabled_saturated && near(sat.linear.x,2,1e-6));
        in.hand_velocity={.2,0,0};sat=plan_tow(in);CHECK(!sat.write && sat.disabled_saturated);
    }
    {   // the hand holds back a body coasting into it (closer than the bind reach and closing faster than the hand walks away)
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,.5,.8);in.pelvis_kg=.5;in.dt=1./30;in.pelvis_velocity={3,1,0};
        auto out=plan_tow(in);CHECK(out.write && near(out.linear.x,0,1e-6) && near(out.linear.y,1,1e-6));
        in.hand_velocity={2.9,0,0};CHECK(!plan_tow(in).write);   // the hand walks away about as fast: nothing to hold back
        in=base(t,.5,.8);in.pelvis_kg=.5;in.dt=1./30;in.pelvis_velocity={-1,0,0};CHECK(!plan_tow(in).write);   // moving off: not the brake's
        in=base(t,.78,.8);in.pelvis_kg=.5;in.dt=1./30;in.pelvis_velocity={3,0,0};CHECK(!plan_tow(in).write);   // inside the band
        in=base(t,.5,.8);in.pelvis_kg=.5;in.dt=1./30;in.pelvis_velocity={3,0,0};in.limb_saturated=true;CHECK(!plan_tow(in).write);
    }
    {   // heavier pelvis -> smaller step (mass reaches the cap)
        auto t=tow_on();auto in=base(t,2.4,.8);in.dt=.01;in.pelvis_kg=30;
        CHECK(near(length(plan_tow(in).linear),300*.01/32,1e-6));
    }
    {   // horizontal speed equals the law with a loose cap; vertical offset does not count as reach
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,1.3,.8);in.pelvis_kg=.5;in.dt=1./30;in.target={1.3,0,2};
        auto out=plan_tow(in);CHECK(out.write && near(out.linear.x,3,1e-6) && out.linear.z==0);
        in.target={.84,0,5};CHECK(!plan_tow(in).write);
    }
    {   // saturation only reports when the tow would have written
        auto t=tow_on();auto in=base(t,.84,.8);in.limb_saturated=true;CHECK(!plan_tow(in).disabled_saturated);
        in=base(t,2,0);in.limb_saturated=true;const auto out=plan_tow(in);CHECK(!out.disabled_saturated && !out.write);
    }
    {   // saturated_limb_disables
        auto t=tow_on();auto in=base(t,2,.8);in.limb_saturated=true;
        const auto out=plan_tow(in);CHECK(!out.write && out.disabled_saturated && near(out.bind_reach,.8));
        in.limb_saturated=false;CHECK(plan_tow(in).write);
    }
    {   // non_finite_input_no_write
        auto t=tow_on();
        auto in=base(t,2,.8);in.target.x=nan_value;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);in.pelvis.y=INFINITY;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);in.pelvis_velocity.x=nan_value;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);in.pelvis_kg=nan_value;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);in.dt=nan_value;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);in.dt=0;CHECK(!plan_tow(in).write);
        in=base(t,2,nan_value);const auto o=plan_tow(in);CHECK(!o.write && std::isfinite(o.bind_reach));
        in=base(t,2,.8);in.tuning=nullptr;CHECK(!plan_tow(in).write);
        in=base(t,2,.8);const auto ok=plan_tow(in);CHECK(ok.write && std::isfinite(ok.linear.x) && std::isfinite(ok.linear.y));
    }
    std::puts("corpse_tow_check passed");
}
