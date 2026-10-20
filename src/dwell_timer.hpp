#pragma once
// Reports when a condition has held continuously for a duration (e.g. grasp lag > 0.5 m for 0.25 s).
// Usage: DwellTimer t{.25}; if(t.update(lag>.5,dt)) release();   t.reset() on a new hold.
// A false condition, a non-finite or non-positive dt restarts nothing but a false condition; a bad dt is ignored.
#include <cmath>
namespace d2vr {
struct DwellTimer {
    double duration{};   // seconds the condition must hold; <= 0 fires on the first true update
    double seconds{};    // continuous time the condition has held so far
    bool update(bool condition,double dt) noexcept {
        if(!condition) {seconds=0;return false;}
        if(std::isfinite(dt) && dt>0) seconds+=dt;
        return seconds>=duration;
    }
    void reset() noexcept {seconds=0;}
};
}
