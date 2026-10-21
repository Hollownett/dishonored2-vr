#pragma once
#include "body_anchor.hpp"

namespace d2vr {
// Native listener setter order is position, top, front. Coordinates stay in
// game space; the sound world performs its own Wwise coordinate conversion.
struct AudioListenerPose {
    std::array<float,3> position{},top{},front{};
};
inline std::optional<AudioListenerPose> audio_listener_pose(const WorldTrackedPose& head) {
    if(!finite(head.position)) return {};
    for(unsigned i=0;i<3;++i) {
        if(!finite(head.basis[i])) return {};
        for(unsigned j=0;j<=i;++j)
            if(std::abs(dot(head.basis[i],head.basis[j])-(i==j?1.:0.))>.02) return {};
    }
    if(dot(cross(head.basis[0],head.basis[1]),head.basis[2])<.98) return {};
    const auto pack=[](Vec3 v) {return std::array<float,3>{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)};};
    AudioListenerPose result{pack(head.position),pack(head.basis[2]),pack(head.basis[0])};
    for(const auto* vector:{&result.position,&result.top,&result.front})
        for(float f:*vector) if(!std::isfinite(f) || std::abs(f)>1e7f) return {};
    return result;
}
// Use the physical eye centre, never a pupil or a canted render orientation.
// Authored scenes: `authored` is the pose the caller wants heard from (the head-owned view at the native camera position,
// or the native camera itself under legacy-authored-camera).
inline std::optional<AudioListenerPose> head_audio_pose(const TrackedPose& centre,const TrackedPose& origin,
    const GameCameraPose& anchor,Vec3 admitted_offset,const GameCameraPose* authored=nullptr) {
    try {
        if(authored) {
            WorldTrackedPose world{};world.position={authored->position[0],authored->position[1],authored->position[2]};
            for(unsigned i=0;i<3;++i) world.basis[i]={authored->basis[i*3],authored->basis[i*3+1],authored->basis[i*3+2]};
            return audio_listener_pose(world);
        }
        return audio_listener_pose(to_game_pose({origin.position+admitted_offset,centre.orientation},origin,anchor,1.0));
    } catch(const std::invalid_argument&) {return {};}
}
} // namespace d2vr
