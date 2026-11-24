#pragma once
// GitHub issue #6 (D2, Emily: "Far Reach doesn't cancel with grip. Grip instead executes the Far Reach"). Every hold-to-aim power
// (Blink, Far Reach, Domino, Doppelganger, Mesmerize, Windblast, Possession, DOTO Displace) aims while the game's LT is held and casts
// when LT is released (UsePower state 63, release handler D2 0x8566f0: Cast vt+0x120 when power+0xa4). The "fist never fires" rule
// zeroed LT as soon as the power hand's grip closed, which the game read as a release: the grip cast the power.
// docs/doto-research/2026-11-24-R4-grip-mines.md (P1).
//
// PowerCancelLatch (control thread, pure): a trigger pulled with the grip open while a power is in the hand is an AIM. While it is
// held the fist rule leaves LT alone (exempt_fist). A grip closing during the aim CANCELS it (cancel_edge, once): LT is released at
// once and stays released (suppress_trigger) until the trigger is let go, and NativePowerCancel turns that release into StopAim.
// A grip closed first and a trigger after is a fist, as before (no aim, the fist rule applies).
#include <cstdint>

namespace d2vr {
class PowerCancelLatch {
public:
    struct Out {bool exempt_fist{};bool suppress_trigger{};bool cancel_edge{};};
    static constexpr float trigger_press=.55f,trigger_release=.25f,grip_close=.55f,grip_open=.35f;
    Out update(float trigger,float grip,bool power_in_hand) noexcept {
        const bool grip_now=grip_was_?grip>=grip_open:grip>=grip_close;
        const bool grip_edge=grip_now && !grip_was_;
        grip_was_=grip_now;
        const bool trigger_now=trigger_was_?trigger>trigger_release:trigger>=trigger_press;
        const bool trigger_edge=trigger_now && !trigger_was_;
        trigger_was_=trigger_now;
        Out o{};
        if(!power_in_hand) {aim_=cancelled_=false;return o;}   // another item owns LT: the latch has no say
        if(cancelled_) {
            if(!trigger_now) cancelled_=false;
        } else if(aim_) {
            if(!trigger_now) aim_=false;                      // an ordinary release: the game casts
            else if(grip_edge) {aim_=false;cancelled_=true;++cancels_;o.cancel_edge=true;}
        } else if(trigger_edge && !grip_now) {                // a fresh pull with the grip open; a fist opened later stays a fist
            aim_=true;
        }
        o.exempt_fist=aim_;o.suppress_trigger=cancelled_;
        return o;
    }
    bool aim_held() const noexcept {return aim_;}
    bool cancelled() const noexcept {return cancelled_;}
    std::uint64_t cancels() const noexcept {return cancels_;}
private:
    bool aim_{},cancelled_{},grip_was_{},trigger_was_{};std::uint64_t cancels_{};
};
} // namespace d2vr
