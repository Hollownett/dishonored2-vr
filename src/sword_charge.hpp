#pragma once
#include <algorithm>
#include <cstdint>
namespace d2vr {
// v963 (user: "add a power attack for a sword: hold the trigger on the controller with the sword to charge it"):
// while the sword is drawn and held, a still trigger charges a heavy strike. The charge is cancelled by a real swing
// (the hand moves) and by letting go of the sword, menus, the wheel or an authored camera. A release past the
// threshold fires the native heavy attack (AttackPress with its long hold) and marks the next contact hit as powered.
class SwordCharge {
public:
    static constexpr float charge_seconds=.8f;
    static constexpr float fire_at=.6f;          // 0.48 s of holding: a deliberate charge, not a tap
    static constexpr float swing_cancel_mps=2.5f; // a real swing moves the hand faster than this
    struct Out {float charge{};bool charging{},ready{},fired{},tick{};};
    void reset() noexcept {charging_=false;down_=false;charge_=0;level_=0;since_=0;}
    Out update(float trigger,bool drawn,bool held,bool allowed,float hand_speed,std::uint64_t now_ms) noexcept {
        Out out{};const bool down=trigger>=.55f;
        if(!allowed || !drawn || !held || hand_speed>=swing_cancel_mps) {   // a swing, a sheathe, a menu: no charge
            if(charging_) {charging_=false;charge_=0;level_=0;}
            down_=down;return out;
        }
        if(down && !down_) {charging_=true;since_=now_ms;charge_=0;level_=0;}   // the press that starts a charge
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
} // namespace d2vr
