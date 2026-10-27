#pragma once
// The sphere grip (user, headset 2026-10-27, v638): a physical prop (book, folder, candlestick, spoon, fork) or a body part the hand holds is wrapped by the
// fingers on ONE invisible ball fixed in the palm for the whole hold, not on the object's own shapes. The object lags and swings behind the hand (motor-held:
// 4 rad/s, 13 mm a frame in the v638 headset run), so the per-frame contact against its real shape moved under the fingers every frame and they twitched;
// a ball in the palm's own frame is the same shape for the solver every frame (finger_contact's memo keys on the shapes in the palm frame) and the curls settle once.
// Build/legacy-finger-grab restores the contact against the object's shapes.
#include "finger_ik.hpp"
#include <algorithm>
#include <cmath>
namespace d2vr::rig {
// What a hand can wrap: 1.5 cm (a spoon's or a fork's handle is thinner, a fist still closes on that) to 6 cm (a skull or a head: the fingers curl round its front).
inline constexpr double sphere_grip_min=.015,sphere_grip_max=.06;
// The ball's radius from the half extents of the thing held in its own axes: the geometric mean of its two SMALLER half sides, which is the cross-section
// the fingers go across (the grip lies across the thin dimension). A round shaft gives its radius (a candlestick's 2-3 cm), a slab grows with its width but
// slower than the width (a book 6 x 4 x 1.5 cm half: 2.4 cm; a folder 15 x 11 x 1: 3.3 cm), a spoon or fork falls under the floor, a skull is over the cap.
inline double sphere_grip_radius(Vec3 half) {
    double a=std::abs(half.x),b=std::abs(half.y),c=std::abs(half.z);
    if(!std::isfinite(a+b+c)) return sphere_grip_max*.5;
    if(a>b) std::swap(a,b);
    if(b>c) std::swap(b,c);
    if(a>b) std::swap(a,b); // a <= b <= c
    return std::clamp(std::sqrt(a*b),sphere_grip_min,sphere_grip_max);
}
// A prop: its half extents in its own axes (the world box solved for the drawn axes, held_local_extents), else the world box's own as it is.
inline double sphere_grip_radius(const WorldTrackedPose& pose,const NativeGrabBounds& bounds) {
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(const auto local=held_local_extents(pose.basis,e)) return sphere_grip_radius(*local);
    return sphere_grip_radius(e);
}
// A ragdoll bone (a corpse's limb, held for a drag): the visible limb, about 0.6 of its padded collision capsule (sim v640: a dragged wrist's 5-6 cm
// capsule, or its box's x half that can be the forearm's half LENGTH, gave the 6 cm cap: a fist round a ball, not a wrist); never above the bone box's thinnest half side.
inline constexpr double sphere_grip_bone_share=.6;
inline double sphere_grip_radius_of_bone(int bone,const std::optional<NativeGrabBounds>& bounds) {
    if(bone<0 || bone>=17) return sphere_grip_max;
    double r=ragdoll_bone_radius[static_cast<std::size_t>(bone)]*sphere_grip_bone_share;
    if(bounds && finite(bounds->minimum) && finite(bounds->maximum)) {
        const auto h=(bounds->maximum-bounds->minimum)*.5;const double thin=std::min({h.x,h.y,h.z});if(thin>.005) r=std::min(r,thin);}
    return std::clamp(r,sphere_grip_min,sphere_grip_max);
}
// The ball in the palm: a sphere (half 0) of radius r lying on the palm skin, in the palm's own axes, so the same shape in the palm frame every frame.
inline HeldShape sphere_grip_shape(const WorldTrackedPose& palm,const HandGeometry& hand,double radius) {
    HeldShape s{};s.frame.position=palm.position+palm_normal(palm,hand)*(radius+held_palm_depth);s.frame.basis=palm.basis;s.radius=radius;
    require_shape(s);
    return s;
}
// One hold's ball, decided when the hold starts and never again: `radius` is the first reading kept for that entity (a changing box or a lagging pose does not move it).
struct SphereGripLatch {
    std::uintptr_t entity{};double radius{};
    template<class Decide> double radius_for(std::uintptr_t held,Decide decide) {if(held!=entity || !(radius>0)) {entity=held;radius=decide();}return radius;}
    void clear() {entity=0;radius=0;}
};
}
