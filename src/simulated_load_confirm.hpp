#pragma once
#include <cstdint>

namespace d2vr {
// Simulation-only native pad fixture. Each focused loading-ready appearance
// gets a neutral priming interval and one confirm pulse, regardless of uptime.
class SimulatedLoadConfirm {
    enum class Phase {idle,waiting,pulsing,spent};
    Phase phase_{Phase::idle};
    std::uint64_t start_{},last_{},pulse_{};
public:
    void reset() noexcept {phase_=Phase::idle;start_=last_=pulse_=0;}
    bool sample(std::uint64_t tick,bool ready,bool focused) noexcept {
        if(!ready || !focused){reset();return false;}
        if(phase_!=Phase::idle && tick<last_)reset();
        last_=tick;
        if(phase_==Phase::idle){phase_=Phase::waiting;start_=tick;return false;}
        if(phase_==Phase::waiting) {
            if(tick-start_<500)return false;
            phase_=Phase::pulsing;pulse_=tick;
        }
        if(phase_==Phase::pulsing) {
            if(tick-pulse_<200)return true;
            phase_=Phase::spent;
        }
        return false;
    }
};
} // namespace d2vr
