#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// G4: the grip pose blend (Bonelab SetHandPose(pose, 0.25 s)). When a hold starts (a physical grab, a native hold, a living hold), the finger
// joint angles go from the pose the hand had at that moment to the solver's full wrap over `duration_s`, linearly: a pose blend, not a slower
// solver (the solver's target is the full wrap from the first frame). It is applied to the WANTED angles before the surface clamp (ruling H7:
// the contact stays the last stage). Each joint moves toward the current target at most |its start-to-target distance| / duration per second
// (also no slower than its distance from the start, so a target that moves back toward the start is met in time): a fixed target gives
// exactly the lerp at weight(t), and a target that changes late (the fitter's shape arriving at +80 ms) never makes a step above that
// rate. A frame hitch moves at most rate * dt. The blend stays active past the duration only until it has met a target that moved late.
// Pure: no engine, no clock (the caller passes seconds on one steady clock). Angles in rad, finger order thumb..pinky, joint order chain.
namespace d2vr::rig {
inline constexpr double grip_pose_blend_seconds=.25;
class GripPoseBlend {
public:
    using Pose=std::array<std::array<float,3>,5>;
    void start(const Pose& from,double t_s,double duration_s=grip_pose_blend_seconds) {
        from_=from;out_=from;t0_=std::isfinite(t_s)?t_s:0.;last_=t0_;
        duration_=std::isfinite(duration_s) && duration_s>0?duration_s:grip_pose_blend_seconds;
        on_=true;met_=false;
    }
    double weight(double t_s) const {
        if(!on_ || !std::isfinite(t_s)) return 1.;
        return std::clamp((t_s-t0_)/duration_,0.,1.);
    }
    // The blended pose for this frame's target (the target itself when no blend runs). Advances the blend: call once a frame, t non-decreasing.
    Pose apply(const Pose& target,double t_s) const {
        if(!on_) return target;
        const double dt=std::isfinite(t_s) && t_s>last_?t_s-last_:0.;
        last_=std::max(last_,std::isfinite(t_s)?t_s:last_);
        bool met=true;
        for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {
            const double start=from_[f][s],goal=target[f][s],at=out_[f][s];
            if(!std::isfinite(goal)) {met=false;continue;}
            const double rate=std::max(std::abs(goal-start),std::abs(at-start))/duration_;
            const double step=std::clamp(goal-at,-rate*dt,rate*dt);
            const double next=std::abs(goal-(at+step))<1e-9?goal:at+step;
            out_[f][s]=static_cast<float>(next);met=met && next==goal;
        }
        met_=met;
        return out_;
    }
    bool active(double t_s) const {return on_ && (!met_ || !std::isfinite(t_s) || t_s-t0_<duration_);}
    void stop() {on_=false;met_=false;}
private:
    Pose from_{};mutable Pose out_{};double t0_{},duration_{grip_pose_blend_seconds};mutable double last_{};bool on_{};mutable bool met_{};
};
}
