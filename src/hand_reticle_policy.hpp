#pragma once
// Which crosshairs show when a hand holds the sword or is busy (grab/hold/carry/fist take).
// Pure logic, host-testable (tests/hand_reticle_policy_check.cpp).
#include <cstdint>

namespace d2vr {

struct ReticleInputs {
    bool sword_held[2];     // that hand's grip holds the sword (or is drawing it)
    bool busy[2];           // copy_busy: grab / hold / carry / fist take
    bool copy_at[2];        // a copy gun is attached to that hand
    unsigned voltaic_arms;  // bit h = the engine's gun is shown on arm h
    int aim_hand;           // hand the main crosshair ray comes from
    bool pinned;            // DOTO forearm gun (false in D2)
};

class ReticlePolicy {
public:
    static constexpr std::uint64_t release_ms = 150;

    void update(const ReticleInputs& in, std::uint64_t now_ms) {
        for(int h=0;h<2;++h) {
            const bool blocked = in.sword_held[h] || in.busy[h];
            if(!blocked && blocked_[h]) release_ms_[h]=now_ms;   // release edge starts the hysteresis
            blocked_[h]=blocked;
            if(blocked) ever_blocked_[h]=true;
            const bool settled = !ever_blocked_[h] || (now_ms-release_ms_[h]>=release_ms);
            ring_[h] = !blocked && settled;
        }
        main_=true;reason_=nullptr;
        if(in.pinned) {
            const int a = (in.aim_hand==1)?1:0;
            if(in.sword_held[a]) {main_=false;reason_="sword";}
            else if(!(in.voltaic_arms&1u)) {main_=false;reason_="gun_hidden";}
        }
    }
    bool ring_visible(int h) const {return (h==0||h==1)&&ring_[h];}
    bool main_visible() const {return main_;}
    const char* main_blocked_reason() const {return reason_;}

private:
    bool blocked_[2]{false,false};
    bool ever_blocked_[2]{false,false};
    std::uint64_t release_ms_[2]{0,0};
    bool ring_[2]{true,true};
    bool main_=true;
    const char* reason_=nullptr;
};

} // namespace d2vr
