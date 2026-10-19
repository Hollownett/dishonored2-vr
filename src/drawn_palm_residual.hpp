#pragma once
#include "tracked_pose.hpp"
#include <cstdint>

namespace d2vr {
struct DrawnPalmContext {
    std::uint64_t pose_epoch{},reference_epoch{},mapping_revision{},fit_revision{};
    WorldTrackedPose root{};
    bool valid{};
};
struct DrawnPalmSource {
    DrawnPalmContext context{};
    WorldTrackedPose grip{},palm{};
    std::uint64_t tick{},source_tick{},tracking_revision{},frame{},callback{};
    bool valid{};
};
class DrawnPalmResidual {
public:
    void clear() noexcept {have_=false;context_={};source_={};for(auto& value:local_)value=0;}
    void note(const DrawnPalmSource& source,const DrawnPalmContext& context,std::uint64_t now) noexcept {
        if(!context.valid || !context.pose_epoch || !context.reference_epoch || !proper(context.root)) {clear();return;}
        if(!same(context_,context))have_=false;
        context_=context;
        if(!source.valid || !same(source.context,context) || !source.context.valid || !proper(source.context.root) ||
           !proper(source.grip) || !proper(source.palm) || !source.tick || !source.source_tick || !source.tracking_revision || !source.frame || !source.callback ||
           !fresh(source.tick,now,100) || !fresh(source.source_tick,now,100))return;
        if(have_ && (source.tracking_revision<source_.tracking_revision || source.tick<source_.tick || source.source_tick<source_.source_tick ||
           source.frame<source_.frame || source.callback<source_.callback))return;
        if(have_ && source.callback==source_.callback)return;
        const auto residual=source.palm.position-source.grip.position;
        if(length(residual)>=.5)return;
        for(unsigned axis=0;axis<3;++axis)local_[axis]=dot(residual,source.context.root.basis[axis]);
        source_=source;have_=true;
    }
    // Diagnostics: how old the arm-solve sample behind the residual is (frame() ignores it past 500 ms); -1 when there is none.
    std::int64_t age_ms(std::uint64_t now) const noexcept {
        if(!have_ || !source_.tick) return -1;
        return now>=source_.tick?static_cast<std::int64_t>(now-source_.tick):0;
    }
    WorldTrackedPose frame(const WorldTrackedPose& grip,std::uint64_t now) const noexcept {
        auto palm=grip;
        if(!have_ || !proper(grip) || !fresh(source_.tick,now,500) ||
           !fresh(source_.source_tick,now,500))return palm;

        for(unsigned axis=0;axis<3;++axis)palm.position=palm.position+context_.root.basis[axis]*local_[axis];
        return palm;
    }
private:
    static bool same(const DrawnPalmContext& first,const DrawnPalmContext& second) noexcept {
        return first.pose_epoch==second.pose_epoch && first.reference_epoch==second.reference_epoch &&
            first.mapping_revision==second.mapping_revision && first.fit_revision==second.fit_revision;
    }
    static bool fresh(std::uint64_t tick,std::uint64_t now,std::uint64_t age) noexcept {
        return tick && now>=tick && now-tick<=age;
    }
    static bool proper(const WorldTrackedPose& pose) noexcept {
        if(!finite(pose.position))return false;
        for(const auto axis:pose.basis)if(!finite(axis) || std::abs(dot(axis,axis)-1)>1e-5)return false;
        return std::abs(dot(pose.basis[0],pose.basis[1]))<=1e-5 && std::abs(dot(pose.basis[0],pose.basis[2]))<=1e-5 &&
            std::abs(dot(pose.basis[1],pose.basis[2]))<=1e-5 && dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])>=1-1e-5;
    }
    DrawnPalmContext context_{};DrawnPalmSource source_{};
    double local_[3]{};bool have_{};
};
}
