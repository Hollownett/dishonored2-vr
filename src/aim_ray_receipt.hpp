#pragma once
#include "gameplay_aim.hpp"
#include <limits>
namespace d2vr {
// One finalized float launch frame, after barrel correction, smoothing and origin clamp.
// Local to one render update; it is never a last-good ray for another frame/owner.
struct AimRayReceipt {
    GameplayAim ray;
    AimOwner owner;
    std::uint64_t frame{},pose_epoch{},reference_epoch{},tick{};
    WorldTrackedPose hand() const noexcept {
        WorldTrackedPose out;out.position={ray.origin[0],ray.origin[1],ray.origin[2]};
        for(unsigned i=0;i<3;++i) out.basis[i]={ray.axis[3*i],ray.axis[3*i+1],ray.axis[3*i+2]};
        return out;
    }
    Vec3 target(float distance) const noexcept {
        return {double(ray.origin[0])+double(ray.axis[0])*distance,
                double(ray.origin[1])+double(ray.axis[1])*distance,
                double(ray.origin[2])+double(ray.axis[2])*distance};
    }
    bool current(AimOwner now,std::uint64_t render_frame,std::uint64_t pose,std::uint64_t reference) const noexcept {
        return same_aim_owner(owner,now) && frame==render_frame && pose_epoch==pose && reference_epoch==reference;
    }
};
inline std::optional<AimRayReceipt> finalize_aim_ray(const WorldTrackedPose& hand,AimOwner owner,
        std::uint64_t frame,std::uint64_t pose,std::uint64_t reference,std::uint64_t tick) noexcept {
    if(!owner.player || !owner.camera || !frame || !pose || !reference || !tick || !finite(hand.position)) return {};
    AimRayReceipt result;result.owner=owner;result.frame=frame;result.pose_epoch=pose;result.reference_epoch=reference;result.tick=tick;
    const auto representable=[](double v) {return std::isfinite(v) && std::abs(v)<=std::numeric_limits<float>::max();};
    const double xyz[]{hand.position.x,hand.position.y,hand.position.z};
    for(unsigned i=0;i<3;++i) {
        if(!representable(xyz[i]))return {};
        result.ray.origin[i]=static_cast<float>(xyz[i]);
        const auto v=hand.basis[i];if(!representable(v.x) || !representable(v.y) || !representable(v.z))return {};
        result.ray.axis[3*i]=static_cast<float>(v.x);result.ray.axis[3*i+1]=static_cast<float>(v.y);result.ray.axis[3*i+2]=static_cast<float>(v.z);
    }
    if(!valid_aim_frame(result.ray.axis))return {};
    return result;
}
} // namespace d2vr
