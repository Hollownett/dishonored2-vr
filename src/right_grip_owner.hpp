#pragma once
#include <cmath>
namespace d2vr {
// v609 G3: an empty fist never presses the native RB. The physical right grip reaches the native shoulder only when something is in the hand:
// a hold, a carry, a held sword (blocks) or a drawn sword (chokes). Pure, so the routing is tested on its own.
inline bool fist_may_press_native_rb(bool physical_hold,bool native_carry,bool sword_held,bool sword_drawn) noexcept {return physical_hold || native_carry || sword_held || sword_drawn;}
class RightGripOwner {
public:
    enum class Role {release,ready,native,physical};
private:
    Role role_=Role::release;
public:
    bool deciding(float grip,bool focused) const noexcept {return focused && std::isfinite(grip) && grip>=.35f && role_==Role::ready;}
    void revoke() noexcept {if(role_==Role::physical) role_=Role::release;}
    // An occupied native hand (weapon, power or carried body) is never ours:
    // any latched physical role is revoked and the raw native squeeze passes.
    float filter(float grip,bool focused,bool physical_eligible,bool occupied=false) noexcept {
        if(occupied) {role_=Role::release;return grip;}
        if(!focused || !std::isfinite(grip) || grip<0 || grip>1) {role_=Role::release;return 0;}
        if(grip<.35f) role_=Role::ready;
        else if(role_==Role::ready) role_=physical_eligible?Role::physical:Role::native;
        return role_==Role::native || role_==Role::ready?grip:0;
    }
    bool physical() const noexcept {return role_==Role::physical;}
    // A squeeze the game took (nothing to grab at press) becomes physical when something to hold appears under it: a guard
    // the hand just yanked into ragdoll, a body it woke (NativeWeaponHands).
    void claim_physical() noexcept {if(role_==Role::native || role_==Role::release) role_=Role::physical;}
    Role role() const noexcept {return role_;}
};
} // namespace d2vr
