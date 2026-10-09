#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
// Start the native grab trial only after the required measured fit is accepted.
// Paused calibration time must not consume pickup/release/focus-loss commands.
class SimulatedGrabClock {
    std::optional<std::uint64_t> start_;
public:
    std::optional<std::uint64_t> sample(std::uint64_t tick,bool fit_required,bool fit_ready,bool gameplay) {
        if(fit_required && !fit_ready){start_.reset();return std::nullopt;}
        if(!gameplay)return std::nullopt;
        if(!start_ || tick<*start_)start_=tick;
        return tick-*start_;
    }
};
class SimulatedGrabFixtureChoice {
    bool decided_{},ordinary_{};
    double initial_gap_{-1};
public:
    bool decided() const {return decided_;}
    bool ordinary() const {return ordinary_;}
    double initial_gap() const {return initial_gap_;}
    // The caller supplies contact distance from a revalidated native body.
    void choose(double verified_gap) {
        if(decided_) return;
        decided_=true;
        if(std::isfinite(verified_gap) && verified_gap>=0) initial_gap_=verified_gap;
        ordinary_=initial_gap_>=0 && initial_gap_<=.3;
    }
};
} // namespace d2vr
