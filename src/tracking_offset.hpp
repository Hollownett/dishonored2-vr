#pragma once
#include "tracked_pose.hpp"
#include <cstdint>
#include <optional>

namespace d2vr {
struct TrackingOffsetOwner {
    std::uintptr_t player{},camera{};
    std::int32_t camera_generation{-1};
};
inline bool operator==(const TrackingOffsetOwner& a,const TrackingOffsetOwner& b) {
    return a.player==b.player && a.camera==b.camera && a.camera_generation==b.camera_generation;
}
// Pure request geometry, not collision approval. No runtime consumer is enabled.
struct TrackingOffsetSnapshot {
    TrackingOffsetOwner owner;
    std::uint64_t reference_epoch{},tick_ms{};
    TrackedPose origin,head;
    GameCameraPose native_anchor;
    double units_per_metre{};
    Vec3 requested_world_offset;
    bool current_for(const TrackingOffsetOwner& current,std::uint64_t reference,std::uint64_t now,
                     std::uint64_t maximum_age_ms=100) const {
        return owner==current && reference_epoch==reference && now>=tick_ms && now-tick_ms<=maximum_age_ms;
    }
    // Future physical tracking supplies one coherent offset for eyes, hands
    // and interaction rays. Collision limits body admission and drives fading;
    // it must not clamp the physical head by moving the tracking origin.
    GameCameraPose anchor_with_offset(Vec3 world_offset) const {
        if(!finite(world_offset)) throw std::invalid_argument("Invalid tracking offset");
        auto anchor=native_anchor;
        for(unsigned i=0;i<3;++i) anchor.position[i]+=static_cast<float>(i==0?world_offset.x:i==1?world_offset.y:world_offset.z);
        return anchor;
    }
    WorldTrackedPose relative_pose(const TrackedPose& tracked,Vec3 world_offset) const {
        return to_game_pose(camera_relative_controller(tracked,head.position,origin.position),
                            origin,anchor_with_offset(world_offset),units_per_metre);
    }
};
inline std::optional<TrackingOffsetSnapshot> tracking_offset_snapshot(const TrackingOffsetOwner& owner,
    std::uint64_t reference,std::uint64_t tick,const TrackedPose& origin,const TrackedPose& head,
    const GameCameraPose& anchor,double units_per_metre,bool focused,bool head_position_tracked,bool head_orientation_tracked) {
    if(!focused || !head_position_tracked || !head_orientation_tracked || !owner.player || !owner.camera ||
       owner.camera_generation==-1 || !reference || !tick) return {};
    try {
        const auto world=to_game_pose(head,origin,anchor,units_per_metre);
        Vec3 native{anchor.position[0],anchor.position[1],anchor.position[2]};
        return TrackingOffsetSnapshot{owner,reference,tick,origin,head,anchor,units_per_metre,world.position-native};
    } catch(const std::invalid_argument&) {return {};}
}
} // namespace d2vr
