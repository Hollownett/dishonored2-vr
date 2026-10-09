#pragma once
#include <array>
#include <cstdint>

namespace d2vr {
// Physical Quest X owns the native weapon/power wheel: the caller turns a held
// wheel() into a held native LB and equips on that LB release. Opens only on a
// fresh X edge while allowed; losing allowed cancels the press (wheel closes
// without equipping) and the press stays claimed until X is released, so a
// tracking/focus/menu interruption can never late-open the wheel.
class XWheelOwner {
    bool down_{},claimed_{},open_{};
public:
    void update(bool physical_x,bool allowed) noexcept {
        if(!physical_x) {down_=claimed_=open_=false;return;}
        if(!down_) {down_=true;if(allowed) {claimed_=true;open_=true;}}
        else if(!allowed) open_=false;
    }
    bool wheel() const noexcept {return open_;}
    bool x_consumed() const noexcept {return claimed_;}
    void reset() noexcept {down_=claimed_=open_=false;}
};
// Active interaction hand: right by default. The hand keeps pointing until the
// other hand is valid and clearly more forward (>=.15) for a full dwell, or
// until the pointing hand loses tracking. A held trigger latches the hand for
// the whole press, so an interact role cannot migrate mid-press (that latch
// also holds an untracked hand until both triggers are released).
class PointingHandSelector {
    int active_{1};
    bool candidate_{};
    std::uint64_t since_{};
public:
    // pressed: a fresh grip or trigger press on that hand this frame. Reaching with a hand
    // and squeezing is the clearest intention there is, so it takes over at once.
    void update(const std::array<bool,2>& valid,const std::array<double,2>& forwardness,
            const std::array<bool,2>& trigger_held,std::uint64_t now_ms,const std::array<bool,2>& pressed={}) noexcept {
        const unsigned current=static_cast<unsigned>(active_),other=1-current;
        if(pressed[other] && valid[other] && !trigger_held[current]) {active_=static_cast<int>(other);candidate_=false;return;}
        if(trigger_held[0] || trigger_held[1]) {candidate_=false;return;}
        if(!valid[current]) {
            candidate_=false;
            if(valid[other]) active_=static_cast<int>(other);
            return;
        }
        if(valid[other] && forwardness[other]-forwardness[current]>=.15) {
            if(!candidate_) {candidate_=true;since_=now_ms;}
            else if(now_ms>=since_ && now_ms-since_>=150) {active_=static_cast<int>(other);candidate_=false;}
        } else candidate_=false;
    }
    int active() const noexcept {return active_;}
    void reset() noexcept {active_=1;candidate_=false;since_=0;}
};
// Per-press trigger ownership. Each side picks a role at its press edge and
// keeps it until that trigger releases; no role depends on later state except
// the allowed cancellation of an interact press. An owner created or reset
// while a trigger is already held consumes it until it releases, so a
// re-created owner can never fire an interact for a press it did not see.
class InteractOwner {
    enum class Role {none,wheel,interact,native,drain,pending};
    std::array<Role,2> role_{Role::drain,Role::drain};
    std::array<bool,2> down_{true,true};
    std::array<std::uint64_t,2> since_{};
public:
    struct Result {bool interact;std::array<bool,2> consume_trigger;};
    // now_ms!=0 enables the pending role: a press on the non-active hand waits (consumed)
    // until that hand owns the interaction ray for 60 ms, then resolves to interact or
    // native; after 150 ms it is native. This stops a reach-and-pull from firing a weapon.
    Result update(const std::array<float,2>& trigger,int active_hand,bool target_valid,bool wheel_open,bool allowed,
            std::uint64_t now_ms=0) noexcept {
        Result out{};
        for(unsigned side=0;side<2;++side) {
            const float value=trigger[side];
            const bool mine=static_cast<int>(side)==active_hand;
            if(!down_[side]) {
                if(value>=.55f) {
                    down_[side]=true;since_[side]=now_ms;
                    role_[side]=wheel_open?Role::wheel
                        :(mine && target_valid && allowed?Role::interact
                        :(!mine && allowed && now_ms?Role::pending:Role::native));
                }
            } else if(value<=.35f) {down_[side]=false;role_[side]=Role::none;}
            if(role_[side]==Role::pending) {
                const auto waited=now_ms>=since_[side]?now_ms-since_[side]:0;
                if(!allowed || waited>=150) role_[side]=Role::native;
                else if(mine && waited>=60) role_[side]=target_valid?Role::interact:Role::native;
            }
            if(role_[side]==Role::interact && !allowed) role_[side]=Role::drain;
            out.interact|=role_[side]==Role::interact;
            out.consume_trigger[side]=role_[side]==Role::wheel || role_[side]==Role::interact || role_[side]==Role::drain ||
                role_[side]==Role::pending;
        }
        return out;
    }
    void reset() noexcept {role_={Role::drain,Role::drain};down_={true,true};}
};
} // namespace d2vr
