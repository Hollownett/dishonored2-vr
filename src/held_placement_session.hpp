#pragma once
#include <cstdint>
#include <optional>
#include "physical_grab.hpp"

namespace d2vr {
// A translation match alone cannot certify a rotating model's AABB and axes.
inline std::optional<Vec3> held_placement_mesh_centre(const WorldTrackedPose& model,const WorldTrackedPose& expected,
        Vec3 minimum,Vec3 maximum) {
    const auto rotation=[](const WorldTrackedPose& pose) {
        if(!grab_pose_valid(pose))return false;
        for(unsigned axis=0;axis<3;++axis) {
            if(std::abs(length(pose.basis[axis])-1.)>1e-4)return false;
            for(unsigned other=axis+1;other<3;++other)if(std::abs(dot(pose.basis[axis],pose.basis[other]))>1e-4)return false;
        }
        return true;
    };
    if(!rotation(model) || !rotation(expected) || !finite(minimum) || !finite(maximum) ||
       maximum.x<=minimum.x || maximum.y<=minimum.y || maximum.z<=minimum.z || length(model.position-expected.position)>=.02)return {};
    for(unsigned axis=0;axis<3;++axis)if(length(model.basis[axis]-expected.basis[axis])>1e-4)return {};
    const auto centre=grab_local(model,(minimum+maximum)*.5-model.position);
    return finite(centre)?std::optional<Vec3>{centre}:std::nullopt;
}
// Render-thread latch. The caller clears it when the native hold ends or its
// placement proof is refused, so taking the same object again starts a new fit.
class HeldPlacementSession {
public:
    struct Key {
        std::uintptr_t entity{},rf{},state{};
        std::uint32_t spawn{};
        unsigned side{2};
        std::uint64_t pose_epoch{},reference_epoch{},acquisition{};
    };
    bool update(const Key& key) noexcept {
        const bool changed=!key_ || !same(*key_,key);
        key_=key;
        return changed;
    }
    bool update(const std::optional<Key>& key) noexcept {
        if(key)return update(*key);
        const bool changed=key_.has_value();clear();return changed;
    }
    void clear() noexcept {key_.reset();}
private:
    static bool same(const Key& a,const Key& b) noexcept {
        return a.entity==b.entity && a.rf==b.rf && a.state==b.state && a.spawn==b.spawn &&
            a.side==b.side && a.pose_epoch==b.pose_epoch && a.reference_epoch==b.reference_epoch && a.acquisition==b.acquisition;
    }
    std::optional<Key> key_;
};
}
