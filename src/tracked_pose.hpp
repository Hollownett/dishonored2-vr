#pragma once
#include "arm_ik.hpp"
#include "game_camera.hpp"

namespace d2vr {
// OpenXR convention: right +X, up +Y, backward +Z; quaternion x,y,z,w.
struct Quaternion { double x{},y{},z{},w{1}; };
struct TrackedPose { Vec3 position; Quaternion orientation; };
struct WorldTrackedPose {
    Vec3 position;
    std::array<Vec3,3> basis; // forward, left, up. Wrist rotation stays independent.
};
// Until collision-aware roomscale is connected, rendering holds the head at
// the native camera origin. Hands and interaction rays must use that same
// translation policy or leaning makes them drift away from the visible head.
inline TrackedPose camera_relative_controller(TrackedPose controller,Vec3 current_head,Vec3 calibrated_head) {
    if(!finite(current_head) || !finite(calibrated_head) || !finite(controller.position))
        throw std::invalid_argument("Invalid camera-relative controller position");
    controller.position=controller.position-current_head+calibrated_head;
    return controller;
}
inline GameCameraPose level_heading(GameCameraPose camera) {
    // The camera's lateral axis retains yaw at vertical native pitch. Using
    // tiny forward X/Y near that pole amplified crouch/animation perturbations
    // and could flip heading when pitch crossed 90 degrees.
    double x=camera.basis[4],y=-camera.basis[3],n=std::hypot(x,y);
    if(n<1e-6) {x=camera.basis[0];y=camera.basis[1];n=std::hypot(x,y);}
    if(!std::isfinite(n) || n<1e-6) throw std::invalid_argument("Cannot find level game heading");
    x/=n;y/=n;
    camera.basis={static_cast<float>(x),static_cast<float>(y),0,
                  static_cast<float>(-y),static_cast<float>(x),0,0,0,1};
    return camera;
}
inline Vec3 cross(Vec3 a,Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline Quaternion normalized(Quaternion q) {
    double n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(n) || n<0.5 || n>1.5)
        throw std::invalid_argument("Invalid tracked orientation");
    double s=1/std::sqrt(n);
    return {q.x*s,q.y*s,q.z*s,q.w*s};
}
inline Vec3 rotated(Quaternion q,Vec3 v) {
    Vec3 u{q.x,q.y,q.z};
    return v+cross(u,v)*(2*q.w)+cross(u,cross(u,v))*2;
}
inline Quaternion multiplied(Quaternion a,Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}

// The caller supplies the character's LEVEL heading frame and a yaw-only XR
// recenter pose. Head pitch/roll must not rotate the locomotion/shoulder frame.
// Scale is explicit until measured locally. This function neither writes game
// memory nor substitutes head direction for a controller's aiming orientation.
inline WorldTrackedPose to_game_pose(const TrackedPose& tracked,const TrackedPose& origin,
                                     const GameCameraPose& anchor,double units_per_metre) {
    if(!finite(tracked.position) || !finite(origin.position) ||
       !std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000)
        throw std::invalid_argument("Invalid tracked position or world scale");
    auto reference=normalized(origin.orientation);
    if(std::abs(reference.x)>1e-6 || std::abs(reference.z)>1e-6)
        throw std::invalid_argument("Recenter orientation must contain yaw only");
    reference={-reference.x,-reference.y,-reference.z,reference.w};
    auto orientation=normalized(multiplied(reference,normalized(tracked.orientation)));
    std::array<Vec3,3> frame{};
    for(std::size_t i=0;i<3;++i) {
        frame[i]={anchor.basis[i*3],anchor.basis[i*3+1],anchor.basis[i*3+2]};
        if(!finite(frame[i]) || std::abs(dot(frame[i],frame[i])-1)>0.02)
            throw std::invalid_argument("Invalid game heading frame");
    }
    if(std::abs(dot(frame[0],frame[1]))>0.02 || std::abs(dot(frame[0],frame[2]))>0.02 ||
       std::abs(dot(frame[1],frame[2]))>0.02 || dot(cross(frame[0],frame[1]),frame[2])<0.98 ||
       std::abs(frame[2].x)>0.02 || std::abs(frame[2].y)>0.02 || frame[2].z<0.98)
        throw std::invalid_argument("Game heading frame must be level and right handed");
    auto convert=[&](Vec3 xr) {return frame[0]*(-xr.z)+frame[1]*(-xr.x)+frame[2]*xr.y;};
    Vec3 base{anchor.position[0],anchor.position[1],anchor.position[2]};
    auto position=base+convert(rotated(reference,tracked.position-origin.position))*units_per_metre;
    if(!finite(position)) throw std::invalid_argument("Tracked position overflow");
    return {position,{convert(rotated(orientation,{0,0,-1})),
                      convert(rotated(orientation,{-1,0,0})),
                      convert(rotated(orientation,{0,1,0}))}};
}
} // namespace d2vr
