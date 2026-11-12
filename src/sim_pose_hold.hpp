#pragma once
// v1192 knob build/sim-pose-hold (simulated XR with a gameplay script only; "phase=1" flips the phase): ground truth for stale-warp dumps.
// The simulator's poses come from simxr::control, written only in the Present hook from the script sampled at that Present's clock; every
// locate until the next Present returns them. The Setup after Present N renders display frame D = alternate_display_frame(N), fresh eye
// parity_eye(D). Sampling the script only on Presents whose D has eye 0 fresh (changes() true) holds each pose Pj for two display frames:
// at D eye 0 renders Pj and eye 1 is warped from its kept P(j-1) to Pj; at D+1 eye 1 renders Pj for real (the dump's `next` = the truth).
#include <atomic>
#include <cstdint>

namespace d2vr::sim_pose_hold {
inline std::atomic<bool> on{false};
inline std::atomic<unsigned> phase{0};
inline std::atomic<std::uint64_t> changes_n{},holds_n{},verified{},broken{}; // Present samples; eye-1 Setups whose pose was / was not eye 0's

// The poses may change for display frame `display` (its fresh eye is eye 0, or eye 1 with phase 1).
inline bool changes(std::uint64_t display,bool parity_flip,unsigned phase_) noexcept {return ((display+(parity_flip?1u:0u)+phase_)&1u)==0;}

// The script clock: follows the wall clock on change Presents, else repeats the last change's sample.
struct Clock {
    std::uint64_t held{};bool have{};
    std::uint64_t sample(std::uint64_t elapsed,bool change) noexcept {if(change || !have) {held=elapsed;have=true;}return held;}
};

// Phase check at each one-view Setup: the pose generation that display frame was given (stamped at its Present) must, for the held eye,
// equal the generation of the change frame before it.
struct Check {
    struct Stamp {std::uint64_t display{},generation{};};
    Stamp ring[4]{};std::uint64_t generation{},last_change_generation{};bool have_change{};
    void stamp(std::uint64_t display,bool change) noexcept {if(change) ++generation;ring[display&3u]={display,generation};}
    // eye: the Setup's fresh eye; returns 1 verified, -1 broken, 0 not judged
    int setup(std::uint64_t display,int eye,bool change_eye) noexcept {
        const Stamp& s=ring[display&3u];if(s.display!=display || eye<0) return 0;
        if(change_eye) {last_change_generation=s.generation;have_change=true;return 0;}
        if(!have_change) return 0;
        return s.generation==last_change_generation?1:-1;
    }
};
} // namespace d2vr::sim_pose_hold
