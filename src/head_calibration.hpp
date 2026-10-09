#pragma once
#include "tracked_pose.hpp"
#include <cstdint>
#include <optional>

namespace d2vr {
inline TrackedPose eye_centre_pose(const TrackedPose& left,const TrackedPose& right) {
    const auto a=normalized(left.orientation);auto b=normalized(right.orientation);
    if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0) b={-b.x,-b.y,-b.z,-b.w};
    const auto position=(left.position+right.position)*.5;
    if(!finite(position)) throw std::invalid_argument("Invalid tracked eye centre");
    return {position,normalized({(a.x+b.x)*.5,(a.y+b.y)*.5,(a.z+b.z)*.5,(a.w+b.w)*.5})};
}
// Room calibration survives pause/menu and transient gameplay-state reads.
// Explicit recenter/reference epoch or a new native player/camera resets it.
class HeadCalibration {
    std::uint64_t epoch_{},captures_{};
    std::uintptr_t player_{},camera_{};
    std::int32_t generation_{};
    std::optional<TrackedPose> origin_;
public:
    void clear() noexcept {origin_.reset();}
    std::uint64_t captures() const noexcept {return captures_;}
    std::optional<TrackedPose> get(const TrackedPose& current,std::uint64_t epoch,
        std::uintptr_t player,std::uintptr_t camera,std::int32_t generation,bool gameplay) {
        if(!gameplay) return {};
        if(!epoch || !player || !camera || generation==-1 || !finite(current.position)) return {};
        const auto q=normalized(current.orientation);
        if(origin_ && epoch_==epoch && player_==player && camera_==camera && generation_==generation) return origin_;
        const auto yaw=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
        origin_=TrackedPose{current.position,{0,std::sin(yaw*.5),0,std::cos(yaw*.5)}};
        epoch_=epoch;player_=player;camera_=camera;generation_=generation;++captures_;
        return origin_;
    }
};
} // namespace d2vr
