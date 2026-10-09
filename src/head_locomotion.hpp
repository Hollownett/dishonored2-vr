#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2vr {
inline double wrapped_yaw(double yaw) {return std::remainder(yaw,6.283185307179586);}
inline double tracked_yaw(Quaternion orientation,Quaternion origin) {
    const auto q=normalized(multiplied({-origin.x,-origin.y,-origin.z,origin.w},normalized(orientation)));
    const auto forward=rotated(q,{0,0,-1});
    // Keep the last usable heading at the vertical pole; callers suppress the
    // publication rather than interpreting pitch as a half turn.
    if(std::hypot(forward.x,forward.z)<.05) return NAN;
    return std::atan2(-forward.x,-forward.z);
}
inline void head_directed_stick(float& x,float& y,double relative_yaw) {
    if(!std::isfinite(relative_yaw)) {x=y=0;return;}
    const auto c=std::cos(relative_yaw),s=std::sin(relative_yaw);
    const auto right=x*c-y*s,forward=y*c+x*s;
    x=static_cast<float>(right);y=static_cast<float>(forward);
}
// Only physical body-follow deltas enter compensation. Stick turns rotate the
// whole tracking frame. Visibility changes preserve it; room recenter/owner
// replacement resets it. The native adapter serializes access to this policy.
class HeadFollowing {
    std::uint64_t reference_{},tick_{};
    std::uintptr_t player_{},camera_{};
    std::int32_t generation_{};
    double yaw_{},compensation_{};
    bool usable_{},has_yaw_{};
public:
    void publish(double yaw,std::uint64_t tick,std::uint64_t reference,
        std::uintptr_t player,std::uintptr_t camera,std::int32_t generation,bool usable) {
        if(reference && player && camera && generation!=-1 &&
           (reference_!=reference || player_!=player || camera_!=camera || generation_!=generation)) {
            reference_=reference;player_=player;camera_=camera;generation_=generation;compensation_=0;has_yaw_=false;
        }
        if(std::isfinite(yaw)) {yaw_=wrapped_yaw(yaw);has_yaw_=true;}
        usable_=usable && reference && player && camera && generation!=-1 && has_yaw_;
        tick_=tick;
    }
    bool fresh(std::uint64_t now) const {return usable_ && now>=tick_ && now-tick_<=250;}
    double delta(double dt,bool native_ready,std::uint64_t now) const {
        if(!native_ready || !fresh(now) || !std::isfinite(dt) || dt<=0 || dt>.1) return 0;
        const auto error=wrapped_yaw(yaw_-compensation_);
        return std::abs(error)<.001?0:std::clamp(error,-6*dt,6*dt);
    }
    void commit(double delta) {compensation_=wrapped_yaw(compensation_+delta);}
    double compensation(std::uint64_t reference) const {return reference==reference_?compensation_:0;}
    double relative_yaw(std::uint64_t now) const {return fresh(now)?wrapped_yaw(yaw_-compensation_):NAN;}
};
} // namespace d2vr
