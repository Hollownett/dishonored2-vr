#include "../src/grip_pose_blend.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
namespace r=d2vr::rig;
namespace {
using Pose=r::GripPoseBlend::Pose;
Pose filled(float v) {Pose p{};for(auto& f:p) f.fill(v);return p;}
Pose ramp(float base,float step) {Pose p{};for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) p[f][s]=base+step*static_cast<float>(f*3+s);return p;}
// The largest per-joint rate (rad/s) a blend may move at: the farthest any target it saw lies from the start, over the duration.
double rate_of(const Pose& from,std::initializer_list<Pose> targets,double duration=.25) {
    double most=0;
    for(const auto& t:targets) for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) most=std::max(most,static_cast<double>(std::abs(t[f][s]-from[f][s])));
    return most/duration;
}
double step_of(const Pose& a,const Pose& b) {
    double most=0;for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) most=std::max(most,static_cast<double>(std::abs(a[f][s]-b[f][s])));return most;
}
}
int main() {
    {   // no_step_above_rate: a fixed target is reached linearly; each frame moves at most |target - start| * dt / 0.25
        const auto from=filled(.1f),target=ramp(.2f,.08f);const double dt=1/72.,rate=rate_of(from,{target});
        r::GripPoseBlend b;b.start(from,10.);assert(b.active(10.));
        auto last=from;
        for(int i=1;i<=30;++i) {
            const double t=10.+i*dt;const auto out=b.apply(target,t);
            assert(step_of(out,last)<=rate*dt+1e-6);
            // linear: the output is the lerp at the weight
            const double w=b.weight(t);
            for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) assert(std::abs(out[f][s]-(from[f][s]+w*(target[f][s]-from[f][s])))<1e-5);
            last=out;
        }
        assert(step_of(last,target)<1e-6 && !b.active(10.+30*dt));
    }
    {   // late_shape_no_second_jump: the target changes at +80 ms (the fitter's shape arrives): no frame steps above the rate, and it still arrives
        const auto from=filled(0.f),first=filled(1.2f),late=ramp(.3f,.04f);const double dt=1/90.,rate=rate_of(from,{first,late});
        r::GripPoseBlend b;b.start(from,0.);
        auto last=from;bool arrived=false;
        for(int i=1;i<=90;++i) {
            const double t=i*dt;const auto out=b.apply(t<.08?first:late,t);
            assert(step_of(out,last)<=rate*dt+1e-6);
            last=out;
            if(step_of(out,late)<1e-6) arrived=true;
        }
        assert(arrived && !b.active(1.));
    }
    {   // ease_with_hitch_no_step_above_rate: one 100 ms frame inside the ease: the step is at most rate * 0.1
        const auto from=filled(.05f),target=filled(1.25f);const double rate=rate_of(from,{target});
        r::GripPoseBlend b;b.start(from,0.);
        auto last=b.apply(target,1/72.);
        const auto hitch=b.apply(target,1/72.+.1);
        assert(step_of(hitch,last)<=rate*.1+1e-6 && step_of(hitch,last)>rate*.1*.9);
        last=hitch;
        const auto next=b.apply(target,1/72.+.1+1/72.);
        assert(step_of(next,last)<=rate/72.+1e-6);
    }
    {   // weight_one_after_025: linear in time, clamped; inactive before a start and after stop
        r::GripPoseBlend b;assert(!b.active(0.) && b.weight(5.)==1.);
        const auto target=filled(.7f);assert(step_of(b.apply(target,1.),target)==0); // no blend: the target itself
        b.start(filled(0.f),2.);
        assert(std::abs(b.weight(2.)-0.)<1e-12 && std::abs(b.weight(2.125)-.5)<1e-12 && b.weight(2.25)==1. && b.weight(3.)==1. && b.weight(1.)==0.);
        assert(b.active(2.1));
        b.stop();assert(!b.active(2.1) && step_of(b.apply(target,2.1),target)==0);
        // a custom duration and a bad one (non-finite or not positive: the default)
        b.start(filled(0.f),0.,.5);assert(std::abs(b.weight(.25)-.5)<1e-12);
        b.start(filled(0.f),0.,std::nan(""));assert(std::abs(b.weight(.125)-.5)<1e-12);
        b.start(filled(0.f),0.,-1.);assert(std::abs(b.weight(.125)-.5)<1e-12);
    }
    std::cout<<"grip_pose_blend_check passed"<<std::endl;
    return 0;
}
