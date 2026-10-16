#pragma once
#include "native_slash_speed.hpp"
#include "vr_combat.hpp"

namespace d2vr {
class CombatTargetMemory {
    std::optional<NativePlayerStateLease> owner_;
    std::uint64_t pose_epoch_{},last_tick_{},target_tick_{};
    CombatTarget target_{};
    void clear() noexcept {owner_.reset();pose_epoch_=last_tick_=target_tick_=0;target_={};}
public:
    struct Result {CombatTarget remembered{};bool context_changed{};};
    Result update(const std::optional<NativePlayerStateLease>& owner,std::uint64_t pose_epoch,CombatTarget current,
                  std::uint64_t now,bool context_allowed) noexcept {
        if(!context_allowed || !owner || !pose_epoch || !now) {
            const bool changed=owner_.has_value();clear();return {{},changed};
        }
        if(owner_ && now<last_tick_) {clear();return {{},true};}
        const bool changed=!(owner_==owner) || pose_epoch_!=pose_epoch;
        if(changed) {clear();owner_=owner;pose_epoch_=pose_epoch;}
        last_tick_=now;
        if(current) {target_=current;target_tick_=now;}
        else if(target_tick_ && now-target_tick_>1000) {target_={};target_tick_=0;}
        return {target_,changed};
    }
};
}
