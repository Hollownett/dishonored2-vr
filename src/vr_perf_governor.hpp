#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace d2vr {
// Dynamic-resolution governor. Feed one sample per submitted XR frame; read scale() (the render fraction).
// Pure logic: no clocks, no game state. Pace = present-to-present interval (robust to GPU back-pressure,
// unlike engine_cpu_gap_ns); a missed slot = display_time advanced by more than one period.
struct PerfGovernorConfig {
    float min_scale=.60f,max_scale=.95f,start_scale=.77f; // apply_upscale treats f<.5 or f>=.999 as "off"
    float step_down=.06f,step_up=.02f;                    // per decision; down is faster than up
    unsigned window=45;                                   // frames per decision (0.5 s at 90 Hz)
    double miss_high=.04,miss_low=.005;                   // missed-slot fraction thresholds
    double busy_high=.95,busy_low=.82;                    // mean pace / period thresholds
    unsigned up_hold=8;                                   // consecutive calm windows before stepping up
    unsigned down_cooldown=2;                             // unused; kept for compatibility, superseded by up_lockout_after_down
    unsigned up_lockout_after_down=60;                    // windows after a down step that changed scale before any up step (30 s at 90 Hz)
    float quantum=.01f;                                   // output granularity, keeps cvar churn low
    std::uint64_t max_pace_ns=250'000'000;                // ignore stalls (loading, menus) beyond this
    // Samples dropped after each change: the change itself costs a ~0.5 s stall (FSR3+FSR4 contexts are rebuilt per
    // render size, fsr_taa.hpp) whose post-stall frame is a 33-40 slot miss (v300); counting it fed a spiral to the floor.
    unsigned settle=30;
};
struct PerfSample {std::uint64_t pace_ns{},period_ns{};bool missed{};}; // pace_ns: frame work (present-to-present minus the runtime's wait)
inline bool missed_slot(std::uint64_t display_time_ns,std::uint64_t previous_display_time_ns,std::uint64_t period_ns) noexcept {
    return period_ns && previous_display_time_ns && display_time_ns>previous_display_time_ns &&
        display_time_ns-previous_display_time_ns>period_ns+period_ns/2;
}
class PerfGovernor {
    PerfGovernorConfig c_;float scale_;
    unsigned n_{},miss_{},calm_{},lock_{},settle_{};double busy_{};
    static float clamp(float v,float lo,float hi) noexcept {return std::min(hi,std::max(lo,v));}
public:
    unsigned changes{};
    explicit PerfGovernor(PerfGovernorConfig c={}) : c_(c) {
        if(!(c_.min_scale>0)) c_.min_scale=.5f;
        c_.max_scale=std::max(c_.max_scale,c_.min_scale);
        scale_=clamp(quantize(c_.start_scale),c_.min_scale,c_.max_scale);
        if(!c_.window) c_.window=1;
    }
    float scale() const noexcept {return scale_;}
    float quantize(float v) const noexcept {return c_.quantum>0?static_cast<float>(std::round(static_cast<double>(v)/c_.quantum)*static_cast<double>(c_.quantum)):v;}
    void reset() noexcept {n_=miss_=calm_=lock_=settle_=0;busy_=0;}
    // Returns true when scale() changed.
    bool feed(PerfSample s) noexcept {
        if(!s.period_ns || !s.pace_ns || s.pace_ns>c_.max_pace_ns) {return false;} // ponytail: stalls are dropped, not counted; fine for loads
        if(settle_) {--settle_;return false;}
        busy_+=static_cast<double>(s.pace_ns)/static_cast<double>(s.period_ns);miss_+=s.missed;
        if(++n_<c_.window) return false;
        const double busy=busy_/n_,miss=static_cast<double>(miss_)/n_;
        n_=miss_=0;busy_=0;
        float target=scale_;const bool locked=lock_!=0;if(lock_) --lock_;
        if(miss>c_.miss_high) { // display-locked frames always look busy: only missed slots step down (headset v307)
            // proportional to severity, capped at 2 steps so a single hitch cannot crash resolution
            const float k=miss>3*c_.miss_high?2.f:1.f;
            target=scale_-k*c_.step_down;calm_=0;
        } else if(locked) calm_=0; // anti-oscillation: each change stalls ~0.5 s, so no up step until the lockout expires
        else if(miss<=c_.miss_low && busy<c_.busy_low) {
            if(++calm_>=c_.up_hold) {target=scale_+c_.step_up;calm_=0;}
        } else calm_=0; // in the dead band: hold
        target=clamp(quantize(clamp(target,c_.min_scale,c_.max_scale)),c_.min_scale,c_.max_scale);
        if(target==scale_) return false;
        if(target<scale_) lock_=c_.up_lockout_after_down;
        scale_=target;++changes;settle_=c_.settle;return true;
    }
};
}
