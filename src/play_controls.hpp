#pragma once
#include <array>
#include <cstdint>

namespace d2vr {
// Right-stick push down owns one native crouch pulse. Crouch is a native toggle,
// so a flick must fire exactly once per push: the pulse runs 120 ms and then
// stays spent until the stick returns past -0.3. A flick that began while the
// stick was disallowed is spent as well, so allowed returning under a held stick
// can never pulse late.
class CrouchFlick {
    enum class State {ready,active,spent};
    State state_{State::ready};
    std::uint64_t until_{};
public:
    bool update(float turn_y,bool allowed,std::uint64_t now) noexcept {
        if(state_==State::active) {
            if(!allowed || now>=until_) state_=State::spent;
            else return true;
        } else if(state_==State::ready && turn_y<=-.7f) {
            if(allowed) {until_=now+120;state_=State::active;return true;}
            state_=State::spent;
        }
        if(state_==State::spent && turn_y>=-.3f) state_=State::ready;
        return false;
    }
};
// Physical B sheathes the weapons by holding the native Interact button (X) for
// hold_ms. Only a fresh B press while allowed, with no target and no held
// trigger, starts it; a tap is enough because the hold then runs to completion.
// A lost allowed, a target appearing or a trigger pressed cancels the hold at
// once, and the press stays claimed until B is released, so neither a cancelled
// nor a completed hold can be restarted by the same held B.
class HolsterHold {
    std::uint64_t hold_ms_{},until_{};
    bool down_{},active_{},claimed_{};
public:
    explicit HolsterHold(std::uint64_t hold_ms=1000) noexcept : hold_ms_(hold_ms) {}
    bool update(bool b,bool allowed,bool target_valid,bool trigger_held,std::uint64_t now) noexcept {
        const bool edge=b && !down_;
        down_=b;
        if(active_ && (!allowed || target_valid || trigger_held || now>=until_)) {active_=false;claimed_=true;}
        if(!b) claimed_=false;
        else if(edge && !active_ && !claimed_ && allowed && !target_valid && !trigger_held) {
            active_=true;until_=now+hold_ms_;
        }
        return active_;
    }
};
// Per-side guard that keeps a grab from firing the trigger by accident. A grip
// press (.55) arms that side for 400 ms: any trigger press (.55) inside the
// window, or at any time while the side already holds an object, is consumed
// until that trigger releases. A press that began before the arm is never
// upgraded mid-press, so blocking and then counter-attacking still works, and
// swing_blocked() lets the caller suppress the motion sword swing while grabbing.
class GripTriggerGuard {
    std::array<bool,2> grip_down_{},trig_down_{},grabbed_{},consume_{};
    std::array<std::uint64_t,2> grip_since_{};
public:
    std::array<bool,2> update(const std::array<float,2>& grip,const std::array<float,2>& trigger,
            const std::array<bool,2>& holding,std::uint64_t now) noexcept {
        for(unsigned side=0;side<2;++side) {
            if(!grip_down_[side]) {
                if(grip[side]>=.55f) {grip_down_[side]=true;grip_since_[side]=now;}
            } else if(grip[side]<=.35f) grip_down_[side]=false;
            if(!trig_down_[side]) {
                if(trigger[side]>=.55f) {
                    trig_down_[side]=true;
                    consume_[side]=grip_down_[side]
                        && (holding[side] || (now>=grip_since_[side] && now-grip_since_[side]<=400));
                }
            } else if(trigger[side]<=.35f) {trig_down_[side]=false;consume_[side]=false;}
        }
        grabbed_=holding;
        return consume_;
    }
    bool swing_blocked(int side,std::uint64_t now) const noexcept {
        if(side<0 || side>1) return false;
        const unsigned s=static_cast<unsigned>(side);
        return grip_down_[s] && (grabbed_[s] || (now>=grip_since_[s] && now-grip_since_[s]<=400));
    }
};
} // namespace d2vr
