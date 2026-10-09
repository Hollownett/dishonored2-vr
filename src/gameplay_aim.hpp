#pragma once
#include "tracked_pose.hpp"
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
// Exact native layout at camera+0x2527c. This is a pose, not InteractionRay.
struct GameplayAim {std::array<float,3> origin{};std::array<float,9> axis{};};
static_assert(sizeof(GameplayAim)==48);
struct AimOwner {std::uintptr_t player{},camera{};int generation{};};
struct TrackedGameplayAim {
    AimOwner owner{};
    std::uint64_t epoch{},tick{};
    WorldTrackedPose left{};
    bool active{};
};
inline bool same_aim_owner(const AimOwner& a,const AimOwner& b) noexcept {
    return a.player && a.camera && a.player==b.player && a.camera==b.camera && a.generation==b.generation;
}
inline bool valid_aim_frame(const std::array<float,9>& axis) noexcept {
    std::array<Vec3,3> basis{};
    for(unsigned i=0;i<3;++i) {
        basis[i]={axis[3*i],axis[3*i+1],axis[3*i+2]};
        if(!finite(basis[i]) || std::abs(length(basis[i])-1)>.01) return false;
        for(unsigned j=0;j<i;++j) if(std::abs(dot(basis[j],basis[i]))>.01) return false;
    }
    return dot(cross(basis[0],basis[1]),basis[2])>.99;
}
// Call only after native 7f4b30 returns to 7f727a on the verified camera job.
// This computes bytes only: proving scheduler reader barriers and performing a
// shared-field write belongs to the integrating hook, never to a render worker.
inline std::optional<GameplayAim> controller_gameplay_aim(const GameplayAim& native,
        const AimOwner& owner,const TrackedGameplayAim& tracked,
        std::uint64_t epoch,std::uint64_t now,bool ordinary_gameplay) noexcept {
    if(!ordinary_gameplay || !tracked.active || !epoch || tracked.epoch!=epoch ||
       now<tracked.tick || now-tracked.tick>250 || !same_aim_owner(owner,tracked.owner) ||
       !valid_aim_frame(native.axis)) return {};
    const Vec3 origin{native.origin[0],native.origin[1],native.origin[2]};
    if(!finite(origin) || !finite(tracked.left.position) || length(tracked.left.position-origin)>1.5) return {};
    GameplayAim result{};
    result.origin={static_cast<float>(tracked.left.position.x),static_cast<float>(tracked.left.position.y),
                   static_cast<float>(tracked.left.position.z)};
    for(unsigned i=0;i<3;++i) {
        const auto v=tracked.left.basis[i];
        result.axis[3*i]=static_cast<float>(v.x);result.axis[3*i+1]=static_cast<float>(v.y);
        result.axis[3*i+2]=static_cast<float>(v.z);
    }
    if(!valid_aim_frame(result.axis)) return {};
    return result;
}
enum class AimWriteResult {applied,changed_before_write,write_failed,rollback_failed};
// The caller must own the canonical native producer phase. The callbacks copy
// all 48 bytes or report failure; readback and rollback stay on that thread.
template<class Read,class Write> AimWriteResult write_gameplay_aim(Read& read,Write& write,
        std::uintptr_t destination,const GameplayAim& native,const GameplayAim& planned) {
    GameplayAim before{},after{};
    if(!read(destination,&before,sizeof(before)) || std::memcmp(&before,&native,sizeof(native))!=0)
        return AimWriteResult::changed_before_write;
    if(write(destination,&planned,sizeof(planned)) && read(destination,&after,sizeof(after)) &&
       std::memcmp(&after,&planned,sizeof(after))==0) return AimWriteResult::applied;
    GameplayAim restored{};
    if(!write(destination,&native,sizeof(native)) || !read(destination,&restored,sizeof(restored)) ||
       std::memcmp(&restored,&native,sizeof(restored))!=0) return AimWriteResult::rollback_failed;
    return AimWriteResult::write_failed;
}
} // namespace d2vr
