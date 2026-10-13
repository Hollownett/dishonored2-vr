#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2vr {
// Place a readable surface upright in front of the eyes. Controller rest height
// must not pull a wheel or reading page into the player's lap.
inline TrackedPose world_ui_front_pose(const TrackedPose& head,double distance_m=1.05) {
    if(!finite(head.position) || !std::isfinite(distance_m) || distance_m<=0)
        throw std::invalid_argument("Invalid world UI placement");
    auto forward=rotated(normalized(head.orientation),{0,0,-1});forward.y=0;
    const auto length=std::hypot(forward.x,forward.z);
    forward=length>1e-6?forward*(1/length):Vec3{0,0,-1};
    const auto yaw=std::atan2(-forward.x,-forward.z);
    return {head.position+forward*distance_m,{0,std::sin(yaw*.5),0,std::cos(yaw*.5)}};
}
// A note held like a sheet of paper: just above the left hand, its face (+Z) turned to the eyes, so the
// player reads it by raising the hand and brings it closer by moving the hand (Dishonored VR does the same).
inline TrackedPose world_ui_hand_pose(Vec3 head,Vec3 hand,double lift=.12) {
    if(!finite(head) || !finite(hand)) throw std::invalid_argument("Invalid held-note placement");
    const Vec3 at=hand+Vec3{0,lift,0};
    auto d=head-at;const double l=length(d);d=l>1e-6?d*(1/l):Vec3{0,0,1};
    const double yaw=std::atan2(d.x,d.z),pitch=-std::asin(std::clamp(d.y,-1.,1.));
    return {at,multiplied({0,std::sin(yaw*.5),0,std::cos(yaw*.5)},{std::sin(pitch*.5),0,0,std::cos(pitch*.5)})};
}
} // namespace d2vr
