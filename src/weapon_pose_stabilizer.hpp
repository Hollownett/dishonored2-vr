#pragma once
#include "tracked_pose.hpp"
#include <optional>
#include <cstdint>

namespace d2vr {
class WeaponPoseStabilizer {
public:
    struct Key {
        std::uintptr_t entity{};
        std::uint64_t pose_epoch{},reference_epoch{};
        std::uint32_t full_spawn{};
        unsigned hand{};
        bool operator==(const Key& other) const noexcept {
            return entity==other.entity && pose_epoch==other.pose_epoch && reference_epoch==other.reference_epoch &&
                full_spawn==other.full_spawn && hand==other.hand;
        }
    };
    static constexpr double tau_seconds=.006;
    static constexpr double maximum_degrees=.1;
    static constexpr double fast_degrees_per_second=20.;
    void clear() noexcept {valid_=false;}
    std::optional<WorldTrackedPose> sample(const WorldTrackedPose& raw,const Key& key,std::uint64_t tick_us,bool focused) noexcept {
        if(!focused || !key.entity || !key.full_spawn || !key.pose_epoch || !key.reference_epoch || key.hand>1 || !tick_us ||
           !finite(raw.position) || !proper(raw.basis)) {clear();return {};}
        if(!valid_ || !(key==key_))return seed(raw,key,tick_us);
        if(tick_us<tick_us_)return {};
        if(tick_us==tick_us_) {
            if(!same(raw.basis,previous_raw_))return {};
            auto result=raw;result.basis=filtered_;return result;
        }
        const double elapsed=static_cast<double>(tick_us-tick_us_)*1e-6;
        if(elapsed>.05)return seed(raw,key,tick_us);
        auto output=raw;
        const double step=angle(previous_raw_,raw.basis);
        if(step/elapsed<=fast_degrees_per_second*radians && !same(raw.basis,filtered_)) {
            const double distance=angle(raw.basis,filtered_);
            const double lag=std::min(maximum_degrees*radians,distance*std::exp(-elapsed/tau_seconds));
            Vec3 axis{};
            for(unsigned index=0;index<3;++index)axis=axis+cross(raw.basis[index],filtered_[index]);
            const double magnitude=length(axis);
            if(distance<radians && magnitude>1e-12 && lag>1e-12) {
                axis=axis*(1/magnitude);
                for(unsigned index=0;index<3;++index) {
                    const auto vector=raw.basis[index];
                    output.basis[index]=vector*std::cos(lag)+cross(axis,vector)*std::sin(lag)+axis*(dot(axis,vector)*(1-std::cos(lag)));
                }
            }
        }
        previous_raw_=raw.basis;filtered_=output.basis;tick_us_=tick_us;
        return output;
    }
    static bool proper(const std::array<Vec3,3>& basis) noexcept {
        for(const auto axis:basis)if(!finite(axis) || std::abs(dot(axis,axis)-1)>1e-5)return false;
        return std::abs(dot(basis[0],basis[1]))<=1e-5 && std::abs(dot(basis[0],basis[2]))<=1e-5 &&
            std::abs(dot(basis[1],basis[2]))<=1e-5 && dot(cross(basis[0],basis[1]),basis[2])>=1-1e-5;
    }
private:
    static constexpr double radians=3.14159265358979323846/180.;
    static bool same(const std::array<Vec3,3>& first,const std::array<Vec3,3>& second) noexcept {
        for(unsigned index=0;index<3;++index)
            if(first[index].x!=second[index].x || first[index].y!=second[index].y || first[index].z!=second[index].z)return false;
        return true;
    }
    static double angle(const std::array<Vec3,3>& first,const std::array<Vec3,3>& second) noexcept {
        double trace{};Vec3 axis{};
        for(unsigned index=0;index<3;++index) {trace+=dot(first[index],second[index]);axis=axis+cross(first[index],second[index]);}
        return std::atan2(length(axis)*.5,std::clamp((trace-1)*.5,-1.,1.));
    }
    WorldTrackedPose seed(const WorldTrackedPose& raw,const Key& key,std::uint64_t tick_us) noexcept {
        key_=key;tick_us_=tick_us;previous_raw_=filtered_=raw.basis;valid_=true;return raw;
    }
    Key key_{};std::uint64_t tick_us_{};bool valid_{};
    std::array<Vec3,3> previous_raw_{},filtered_{};
};
}
