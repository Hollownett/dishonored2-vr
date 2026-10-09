#pragma once
#include "rig_transform.hpp"

namespace d2vr::rig {
inline Transform bind_transform(const std::array<float,12>& inverse_rows) {
    Transform inverse{},identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col)
        inverse.f[col*4+row]=inverse_rows[row*4+col];
    return rigid_retarget(identity,inverse,identity);
}
// OpenXR grip: -Z runs little finger -> thumb; +X is the palm normal
// outward for the left hand and inward for the right hand. Native mirrored
// wrist/MCP geometry supplies that sign; WorldTrackedPose stores -X, not +X.
// Match WorldTrackedPose's forward/left/up column order, not bone axes.
inline Transform hand_bind_grip(const Transform& wrist,const std::array<Transform,4>& knuckles) {
    if(!valid(wrist)) throw std::invalid_argument("Invalid bind wrist");
    Vec3 mean{};
    for(const auto& joint:knuckles) {
        if(!valid(joint)) throw std::invalid_argument("Invalid bind knuckle");
        mean=mean+position(joint)*.25;
    }
    const auto fingers=mean-position(wrist);
    const auto width=position(knuckles[0])-position(knuckles[3]);
    const auto palm_length=length(fingers),palm_width=length(width);
    if(palm_length<.03 || palm_length>.2 || palm_width<.02 || palm_width>.15 ||
       length(cross(fingers,width))<palm_length*palm_width*.5)
        throw std::invalid_argument("Invalid bind palm geometry");
    const auto forward=unit(width),left=unit(cross(fingers,forward))*-1,up=cross(forward,left);
    Transform result{};const std::array<Vec3,3> axes{forward,left,up};
    for(unsigned axis=0;axis<3;++axis) {
        result.f[axis*4]=static_cast<float>(axes[axis].x);
        result.f[axis*4+1]=static_cast<float>(axes[axis].y);
        result.f[axis*4+2]=static_cast<float>(axes[axis].z);
    }
    // ponytail: this is an interior skeletal estimate, not a surface palm point.
    // The current span wizard does not measure a controller-to-palm offset or
    // wrist orientation. Keep this origin until that alignment is measured;
    // shifting to the mesh surface would assume a different XR pose convention.
    const auto centre=(position(wrist)+mean)*.5;
    result.f[12]=static_cast<float>(centre.x);result.f[13]=static_cast<float>(centre.y);result.f[14]=static_cast<float>(centre.z);
    if(!valid(result)) throw std::invalid_argument("Invalid bind grip frame");
    return result;
}
} // namespace d2vr::rig
