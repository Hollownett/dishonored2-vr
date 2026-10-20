#include "corpse_tow.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    {   // off_by_default_no_write (Task 10 turns it on when it wires limb_saturated)
        const GrabTuning t;CHECK(!t.tow);
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
    {   // speed_6_per_metre_max_4 (force cap loosened so the law shows)
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,1.3,.8);in.pelvis_kg=.5;in.dt=1./30;
        auto out=plan_tow(in);CHECK(out.write && near(length(out.linear),3,1e-6) && near(out.linear.x,3,1e-6));
        in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=1./30;
        out=plan_tow(in);CHECK(out.write && near(length(out.linear),4,1e-6));
    }
    {   // stale_dt_scales_the_catch_up: a 0.5 s stale frame asks for dt_max/dt of the normal speed (2.0 m x 6 / 15 = 0.8), dt <= dt_max is unchanged
        auto t=tow_on();t.strength_scale=10;t.axial_n=600;t.mass_floor_kg=.1;
        auto in=base(t,2.8,.8);in.pelvis_kg=.5;in.dt=.5;
        auto out=plan_tow(in);CHECK(out.write && near(length(out.linear),.8,1e-6));
        in.dt=t.dt_max;out=plan_tow(in);CHECK(near(length(out.linear),4,1e-6));
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
        auto t=tow_on();auto in=base(t,2.8,.8);in.dt=.01;in.pelvis_kg=12;in.pelvis_velocity={3,0,0};   // wanted 4
        auto out=plan_tow(in);const double cap=300*.01/14;
        CHECK(out.write && near(out.linear.x-3,cap,1e-6) && near(out.linear.y,0));
        in.pelvis_velocity={1.5,0,0};out=plan_tow(in);CHECK(near(out.linear.x-1.5,cap,1e-6));
        in.pelvis_velocity={3.9,0,0};out=plan_tow(in);CHECK(near(out.linear.x,4,1e-9)); // inside the cap: reaches the wanted speed
        in.pelvis_velocity={5,0,0};out=plan_tow(in);CHECK(out.write && near(out.linear.x,5-cap,1e-6)); // faster than wanted: eased down by the cap
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
