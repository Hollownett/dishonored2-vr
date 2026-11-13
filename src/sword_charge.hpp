#pragma once
#include <algorithm>
#include <cstdint>
namespace d2vr {
// v963/v980 (user, v980: "when I hold grip and trigger with a sword in hand it should charge a power attack"): while the
// sword is drawn and held, a still trigger charges a heavy strike. The charge is cancelled by a real swing (the hand
// moves) and by letting go of the sword, menus, the wheel or an authored camera. A release past the threshold fires the
// native heavy attack (AttackPress with its long hold) and marks the next contact hit as powered.
// v980: the sword hand's trigger is this charge and nothing else. The v963 charge could never start: the same press also
// asked for the native Block (the attack gate's explicit_block), and that closed the very gate the charge needs, on its
// first held frame. The press now belongs to the charge while the hand holds the sword; blocking stays the
// sword-in-front pose.
inline constexpr bool sword_trigger_owns_press(bool sword_in_hand,bool trigger_down,bool trigger_consumed) noexcept {
    return sword_in_hand && trigger_down && !trigger_consumed;
}
class SwordCharge {
public:
    static constexpr float down_level=.55f;      // the trigger level the pad reads as a press
    static constexpr float charge_seconds=.8f;
    static constexpr float fire_at=.6f;          // 0.48 s of holding: a deliberate charge, not a tap
    static constexpr float swing_cancel_mps=2.5f; // a real swing moves the hand faster than this
    struct Out {float charge{};bool charging{},ready{},fired{},tick{},started{};};   // started: the charge began this frame
    void reset() noexcept {charging_=false;down_=false;charge_=0;level_=0;since_=0;}
    Out update(float trigger,bool drawn,bool held,bool allowed,float hand_speed,std::uint64_t now_ms) noexcept {
        Out out{};const bool down=trigger>=down_level;
        if(!allowed || !drawn || !held || hand_speed>=swing_cancel_mps) {   // a swing, a sheathe, a menu: no charge
            if(charging_) {charging_=false;charge_=0;level_=0;}
            down_=down;return out;
        }
        if(down && !down_) {charging_=true;since_=now_ms;charge_=0;level_=0;out.started=true;}   // the press that starts a charge
        if(charging_ && down) {
            const float seconds=now_ms>=since_?static_cast<float>(now_ms-since_)/1000.f:0.f;
            charge_=std::min(1.f,seconds/charge_seconds);
            const int level=charge_>=1.f?3:charge_>=.66f?2:charge_>=.33f?1:0;
            if(level>level_) {level_=level;out.tick=true;}                       // one haptic tick per third
        }
        if(charging_ && !down) {                                                // release
            charging_=false;
            out.fired=charge_>=fire_at;
            charge_=0;level_=0;
        }
        out.charging=charging_;out.charge=charge_;out.ready=charge_>=fire_at;
        down_=down;return out;
    }
private:
    bool charging_{},down_{};
    float charge_{};
    int level_{};
    std::uint64_t since_{};
};
// v1298 (user: the guide card "Fist by your right ear, blade pointing forward: it charges. Thrust forward to release it. Lower
// the blade to cancel."): a trigger-free power attack. The sword fist held still beside the head with the blade forward
// charges; a forward thrust of the hand releases it (fires past fire_at). Lowering the blade cancels, and so does a charge
// that leaves the pose without a thrust (after leave_ms). The caller computes the geometry (stage metres, head-relative).
class PoseCharge {
public:
    static constexpr float charge_seconds=.8f,fire_at=.6f;
    static constexpr float still_mps=.6f;          // the hand rests at the ear while charging
    static constexpr float thrust_mps=1.5f;        // hand speed along head-forward that releases
    static constexpr float blade_fwd_min=.7f;      // blade . flat head-forward (about 45 deg)
    static constexpr float blade_low=-.5f;         // blade.y below this = lowered: cancel
    static constexpr std::uint64_t leave_ms=400;   // out of the pose this long without a thrust: cancel
    struct Pose {bool near_ear{};float blade_fwd{},blade_y{},hand_speed{},fwd_speed{};};
    struct Out {float charge{};bool charging{},ready{},fired{},tick{},started{};};
    void reset() noexcept {charge_=0;level_=0;last_ms_=0;in_pose_ms_=0;charging_=false;}
    Out update(const Pose& p,bool drawn,bool held,bool allowed,std::uint64_t now_ms) noexcept {
        Out out{};
        const std::uint64_t dt=last_ms_ && now_ms>last_ms_?std::min<std::uint64_t>(now_ms-last_ms_,100):0;last_ms_=now_ms;
        if(!allowed || !drawn || !held || p.blade_y<blade_low) {reset();last_ms_=now_ms;return out;}
        const bool in_pose=p.near_ear && p.blade_fwd>=blade_fwd_min && p.hand_speed<still_mps;
        if(in_pose) {
            if(!charging_) {charging_=true;charge_=0;level_=0;out.started=true;}
            in_pose_ms_=now_ms;
            charge_=std::min(1.f,charge_+static_cast<float>(dt)/1000.f/charge_seconds);
            const int level=charge_>=1.f?3:charge_>=.66f?2:charge_>=.33f?1:0;
            if(level>level_) {level_=level;out.tick=true;}
        } else if(charging_) {
            if(charge_>=fire_at && p.fwd_speed>=thrust_mps && p.blade_fwd>=blade_fwd_min) {out.fired=true;reset();last_ms_=now_ms;return out;}
            if(now_ms-in_pose_ms_>leave_ms) {reset();last_ms_=now_ms;return out;}
        }
        out.charging=charging_;out.charge=charge_;out.ready=charge_>=fire_at;return out;
    }
private:
    float charge_{};int level_{};bool charging_{};
    std::uint64_t last_ms_{},in_pose_ms_{};
};
} // namespace d2vr
