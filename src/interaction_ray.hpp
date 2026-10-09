#pragma once
#include "tracked_pose.hpp"
#include <array>
#include <cstring>
#include <optional>

namespace d2vr {
// Native BVH and queued-query ray. 0x08ffb0 copies all 48 bytes before queuing.
struct InteractionRay {std::array<float,12> f;};
static_assert(sizeof(InteractionRay)==48);
inline std::optional<InteractionRay> controller_interaction_ray(const InteractionRay& native,
    const std::array<float,3>& native_origin,const std::array<float,9>& native_axis,const WorldTrackedPose& hand) {
    std::uint32_t flags{};std::memcpy(&flags,&native.f[11],4);
    if((flags&~7u)!=0x3f000000 || native.f[3]!=0 || !std::isfinite(native.f[7]) || native.f[7]<=0 || native.f[7]>1000) return {};
    const Vec3 from{native.f[0],native.f[1],native.f[2]},direction{native.f[4],native.f[5],native.f[6]};
    const Vec3 origin{native_origin[0],native_origin[1],native_origin[2]},forward{native_axis[0],native_axis[1],native_axis[2]};
    const double extent=length(direction);
    for(unsigned i=0;i<3;++i) {
        if(!finite(hand.basis[i]) || std::abs(length(hand.basis[i])-1)>.01) return {};
        for(unsigned j=i+1;j<3;++j) if(std::abs(dot(hand.basis[i],hand.basis[j]))>.01) return {};
    }
    if(dot(cross(hand.basis[0],hand.basis[1]),hand.basis[2])<.99) return {};
    if(!finite(from) || !finite(origin) || !finite(forward) || !finite(hand.position) ||
       !std::isfinite(extent) || extent<.001 || extent>1000 || std::abs(length(forward)-1)>.01 ||
       std::abs(length(hand.basis[0])-1)>.01 || length(hand.position-origin)>1.5) return {};
    const double offset=dot(from-origin,forward);
    if(std::abs(offset)>5 || length(from-origin-forward*offset)>.02) return {};
    const auto at=hand.position+hand.basis[0]*offset;
    const auto vector=hand.basis[0]*extent;
    InteractionRay result=native;
    result.f[0]=static_cast<float>(at.x);result.f[1]=static_cast<float>(at.y);result.f[2]=static_cast<float>(at.z);
    result.f[4]=static_cast<float>(vector.x);result.f[5]=static_cast<float>(vector.y);result.f[6]=static_cast<float>(vector.z);
    flags=0x3f000000;
    for(unsigned axis=0;axis<3;++axis) {
        const auto v=result.f[axis+4];result.f[axis+8]=v==0?3.402820018e38f:1.f/v;
        if(!std::isfinite(result.f[axis+8])) return {};
        if(v>=0) flags|=1u<<axis;
    }
    std::memcpy(&result.f[11],&flags,4);return result;
}
} // namespace d2vr
