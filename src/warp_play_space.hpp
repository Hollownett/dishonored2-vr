#pragma once
// v1184: the play space (tracking space) of a rendered eye, for the warp's Metro-style rig attachment: rig texels move by the play space's
// exact move plus their own motion inside it, not by a world-frame decode.
//
// origin: the world pose of the play space, i.e. of the tracking-space frame at the calibrated head origin (the mod's to_game_pose with the
// render's head_origin and anchor): position = world point of the calibrated head origin; basis = world directions of the tracking axes
// (forward = -Z, left = -X, up = +Y). A tracking point p maps to origin.position + basis * (-(p-o).z, -(p-o).x, (p-o).y) (o = head origin).
// It moves with locomotion (anchor) and turning (recenter yaw); between two renders it is the exact play-space move.
// hands: each DRAWN grip (the frame the rig is drawn from: palm offsets and world clamps included) in play-space coordinates: position =
// (forward, left, up) metres from origin.position along origin.basis; basis = its forward/left/up axes in the same components.
// hand_bits: bit per hand drawn in that render (as WarpCamera::hand_bits).
#include <array>
#include "tracked_pose.hpp"

namespace d2vr {
struct WarpPlaySpace {WorldTrackedPose origin{};std::array<WorldTrackedPose,2> hands{};unsigned hand_bits{};bool valid{};};
inline WarpPlaySpace warp_play_space(const WorldTrackedPose& origin,const std::array<WorldTrackedPose,2>& world_grips,unsigned bits) noexcept {
    WarpPlaySpace out{origin,{},bits,true};
    const auto local=[&](Vec3 v) {return Vec3{dot(v,origin.basis[0]),dot(v,origin.basis[1]),dot(v,origin.basis[2])};};
    for(unsigned s=0;s<2;++s) if(bits>>s&1u) {
        out.hands[s].position=local(world_grips[s].position-origin.position);
        for(unsigned a=0;a<3;++a) out.hands[s].basis[a]=local(world_grips[s].basis[a]);
    }
    return out;
}
} // namespace d2vr
