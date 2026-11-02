#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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
// Dialogue choices at the person spoken to (v722): centred on the point where that NPC's Talk prompt stood, kept 0.8-3 m from
// the eyes along the same line, upright and turned (yaw only) to face the eyes. Returns the pose and its distance.
inline std::pair<TrackedPose,double> world_ui_speaker_pose(const TrackedPose& head,Vec3 at) {
    if(!finite(head.position) || !finite(at)) throw std::invalid_argument("Invalid dialogue placement");
    auto d=at-head.position;const double l=length(d);
    if(l<1e-6) return {world_ui_front_pose(head,.8),.8};
    const double distance=std::clamp(l,.8,3.);
    const auto p=head.position+d*(distance/l);
    const double yaw=std::atan2(head.position.x-p.x,head.position.z-p.z);
    return {{p,{0,std::sin(yaw*.5),0,std::cos(yaw*.5)}},distance};
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
// Dual wield: the weapon wheel as a ring in front of the hand that opened it. Width is the whole quad; the ring is 652/1920 of it.
// v776 (user, headset v775: "draw it near the hand and choose by pointing the hand at it"; moving the hand along the ring, v752-v775, ignored
// turning it): the ring's centre sits wheel_hand_ahead_m along that hand's aim ray (-Z of its aim pose) and faces the eyes, fixed for the opening.
// The hand then points at the centre (nothing chosen) and picks a wedge by aiming at it (WheelPointer::update_plane).
inline constexpr double wheel_hand_lift_m=0,wheel_hand_ahead_m=.22,wheel_hand_width_m=.6; // ponytail: the ring art is 652/1920 of the quad (r 10.2 cm), so its edge is
// atan(r/ahead)=24.8 deg off the aim at .22 m: nearer = steadier pointing but more wrist travel, farther = a nervous pointer; tune in the headset
// The ring's pose for a hand whose aim pose is `aim` (OpenXR local space), eyes at `head`. scale: the quad's HUD size factor (width = wheel_hand_width_m *
// scale), so the ring keeps its angle from the hand at every HUD size (the user runs 100%: 1.54x the quad, so 1.54x the distance).
inline TrackedPose wheel_aim_pose(Vec3 head,const TrackedPose& aim,double scale=1) {
    return world_ui_hand_pose(head,aim.position+rotated(normalized(aim.orientation),{0,0,-1})*(wheel_hand_ahead_m*scale),wheel_hand_lift_m);
}
// A quad's plane in the space of its pose: centre, unit normal (towards the eyes, +Z), right (+X) and up (+Y), and the quad width.
struct WheelPlane {Vec3 centre,normal,right,up;double width;};
inline WheelPlane wheel_plane_of(const TrackedPose& pose,double width) {
    const auto q=normalized(pose.orientation);
    return {pose.position,rotated(q,{0,0,1}),rotated(q,{1,0,0}),rotated(q,{0,1,0}),width};
}
} // namespace d2vr
