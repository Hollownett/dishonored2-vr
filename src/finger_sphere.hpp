#pragma once
// The sphere grip (user, headset 2026-10-27, v638): a physical prop (book, folder, candlestick, spoon, fork) or a body part the hand holds is wrapped by the
// fingers on ONE invisible ball fixed in the palm for the whole hold, not on the object's own shapes. The object lags and swings behind the hand (motor-held:
// 4 rad/s, 13 mm a frame in the v638 headset run), so the per-frame contact against its real shape moved under the fingers every frame and they twitched;
// a ball in the palm's own frame is the same shape for the solver every frame (finger_contact's memo keys on the shapes in the palm frame) and the curls settle once.
// Build/legacy-finger-grab restores the contact against the object's shapes.
//
// The ball's SIZE is the grip ball (user, headset 2026-11-05): a small ball at the grip place, the width of the thing's thin side under the palm, not the ball
// round its whole box (v642). A bottle's box gave 15 cm, a plank's the 50 cm cap: the hand opened flat round a ball the prop floated inside, which is not a grip.
// The motor seats the prop's own surface on the palm (the grasp point, Task 12), so the ball on the palm skin IS the thing's cross-section under the hand:
// a bottle's 3.5 cm radius, a book's 2 cm thickness, a spoon's 1.5 cm floor. Build/legacy-sphere-grip-whole-object restores the v642 rule.
#include "finger_ik.hpp"
#include <algorithm>
#include <cmath>
namespace d2vr::rig {
// A thing's ball: 1.5 cm at least (a fist still closes on it), 0.5 m at most (what a hand can still hold out in front of it).
inline constexpr double sphere_grip_min=.015,sphere_grip_max=.5,sphere_grip_unknown=.06;
// The ball's radius from the half extents of the thing held in its own axes: the sphere round its whole box (user, after v641: "the whole object inside the
// ball, so the hand never seems to pass through it"), the tightest ball that holds every corner: a spoon 9 cm, a candlestick 15 cm, a book 14 cm, a head 15 cm.
inline double sphere_grip_radius(Vec3 half) {
    const double a=std::abs(half.x),b=std::abs(half.y),c=std::abs(half.z);
    if(!std::isfinite(a+b+c)) return sphere_grip_unknown;
    return std::clamp(std::sqrt(a*a+b*b+c*c),sphere_grip_min,sphere_grip_max);
}
// A prop: its half extents in its own axes (the world box solved for the drawn axes, held_local_extents), else the world box's own as it is.
inline double sphere_grip_radius(const WorldTrackedPose& pose,const NativeGrabBounds& bounds) {
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(const auto local=held_local_extents(pose.basis,e)) return sphere_grip_radius(*local);
    return sphere_grip_radius(e);
}
// The grip ball (user, headset 2026-11-05: "a small sphere at a grip place ... more native grip"): the thing's thin side is what the fingers wrap, so its
// half is the ball's radius, floored at sphere_grip_min (a fist still closes on it) and capped at sphere_grip_ball_max (a 4 cm radius, 8 cm ball: the widest the
// fingers still wrap; past ~37 mm they only open, research-hand-grip-placement Â§"SphereGrip"). A spoon 1.5 cm, a book 2 cm, a bottle 3.5 cm, a crate 4 cm.
inline constexpr double sphere_grip_ball_max=.03;   // v1037: a 6 cm ball at most - past it the fingers only spread ("palm stretches too much"), which pushed the thumb away from them
inline double sphere_grip_ball_radius(Vec3 half) {
    const double a=std::abs(half.x),b=std::abs(half.y),c=std::abs(half.z);
    if(!std::isfinite(a+b+c)) return sphere_grip_unknown;
    return std::clamp(std::min({a,b,c}),sphere_grip_min,sphere_grip_ball_max);
}
// A prop's grip ball from its own local box (the world box as it is when the axes cannot be solved).
inline double sphere_grip_ball_radius(const WorldTrackedPose& pose,const NativeGrabBounds& bounds) {
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(const auto local=held_local_extents(pose.basis,e)) return sphere_grip_ball_radius(*local);
    return sphere_grip_ball_radius(e);
}
// A flat shape for a flat thing (user, headset 2026-11-05: "grip is unnatural on books"). A wide, thin prop - a book, a folder, a plank, a sheet - cannot be
// closed on: it lies on the palm. Its grip shape is the thing's FACE at the skin (finger_ik's slab_shape), so the fingers rest on it as they do on a real book
// instead of closing through its cover. Same cost as the ball: one shape, fixed in the palm frame, never re-read.
// "Wide and thin": at most 3 cm on its thinnest side, at least 4 cm on its middle side, and the thin side at most half the middle one (a spoon's 1.5 cm middle
// is a handle to close on, not a face to lie on; a bottle's 3.5 x 3.5 cross-section is a shaft).
inline constexpr double grip_slab_thin_max=.03,grip_slab_wide_min=.04,grip_slab_ratio=.5;
inline bool grip_is_flat(Vec3 half) noexcept {
    const double lo=std::min({half.x,half.y,half.z}),hi=std::max({half.x,half.y,half.z}),mid=half.x+half.y+half.z-lo-hi;
    return std::isfinite(lo+hi+mid) && lo<=grip_slab_thin_max && mid>=grip_slab_wide_min && lo<=grip_slab_ratio*mid;
}
// A prop's flatness from its own local box (the world box as it is when the axes cannot be solved).
inline bool grip_is_flat(const WorldTrackedPose& pose,const NativeGrabBounds& bounds) noexcept {
    try {
        const auto e=(bounds.maximum-bounds.minimum)*.5;
        if(const auto local=held_local_extents(pose.basis,e)) return grip_is_flat(*local);
        return grip_is_flat(e);
    } catch(...) {return false;}
}
// The flat grip itself: a wide thin prop gets NO contact shape at all - the hand is not closing on anything, it lies on the thing's face - and the fingers are
// asked for a relaxed curl (a fraction of the hold's) instead of the closed one. Nothing to collide with every frame, so it is also the cheapest grip: the
// solver has one shape fewer than a ball, and nothing that can twitch (user, headset 2026-11-05: "grip is unnatural on books").
inline constexpr float grip_flat_reach=.35f;
// The surface grip's contact shape (the thing's own, fixed in the palm frame): an ORIENTED box in its own axes - a book, crate, plank or spoon is a box, and
// fitting it as a fat capsule (classify_held's fit_cylinder takes a 22x16x4 cm book within 10%) made the fingers fan round a ball with the thumb far off -
// unless its cross-section is genuinely round (two smaller half extents within 25%, the long one at least 1.5x longer): then the capsule along the long
// axis. A corpse bone is classify_held's limb capsule. Local extents unsolvable (axes 45 degrees to the box): classify_held.
inline HeldShape surface_grip_shape(const WorldTrackedPose& body,const std::optional<NativeGrabBounds>& bounds,int bone,const WorldTrackedPose& palm) {
    if(bone<0 && bounds && finite(bounds->minimum) && finite(bounds->maximum)) {
        const auto local=held_local_extents(body.basis,(bounds->maximum-bounds->minimum)*.5);
        if(local) {
            const auto h=held_clamped_extents(*local);const double v[3]{h.x,h.y,h.z};
            unsigned a=0;for(unsigned i=1;i<3;++i) if(v[i]>v[a]) a=i;
            const double p=v[(a+1)%3],q=v[(a+2)%3],wide=std::max(p,q);
            HeldShape shape{};shape.frame.position=(bounds->maximum+bounds->minimum)*.5;
            if(std::abs(p-q)<=.25*wide && v[a]>=1.5*wide) { // round: a capsule along the long axis (half = the straight part)
                const double r=(p+q)*.5;
                shape.frame.basis={unit(body.basis[a]),unit(body.basis[(a+1)%3]),unit(body.basis[(a+2)%3])};shape.half={std::max(0.,v[a]-r),0,0};shape.radius=r;
            } else {shape.frame.basis={unit(body.basis[0]),unit(body.basis[1]),unit(body.basis[2])};shape.half=h;shape.radius=0;}
            return shape;
        }
    }
    return classify_held(body,bounds,bone,palm);
}
// A ragdoll bone (a corpse's limb, held for a drag): the visible limb, about 0.6 of its padded collision capsule (sim v640: a dragged wrist's 5-6 cm
// capsule, or its box's x half that can be the forearm's half LENGTH, gave the 6 cm cap: a fist round a ball, not a wrist); never above the bone box's thinnest half side.
inline constexpr double sphere_grip_bone_share=.6;
inline double sphere_grip_radius_of_bone(int bone,const std::optional<NativeGrabBounds>& bounds) {
    if(bone<0 || bone>=17) return sphere_grip_unknown;
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
