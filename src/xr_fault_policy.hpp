#pragma once
// v1393 (user report 2026-11-18: "constant freezes from time to time and Steam Link connection drops; Virtual Desktop works").
//
// Before this, every exception out of the Present hook ran recover_xr_session: the XR session AND instance were destroyed, the
// OpenXR loader unloaded, and a new GameXr was built after a backoff of 1, 2, 4, 8, then 10 s. The backoff count was never reset
// during a play session, so from the fifth fault on every fault cost 10 s without frames. On SteamVR an instance teardown is the
// game leaving VR: SteamVR shows its own scene and Steam Link drops or resets the stream. VDXR (Virtual Desktop) tolerates the same
// teardown far better, which is why only Steam Link showed it.
//
// Policy now:
//   - A fault that does not say the session is gone is ridden through: the open frame is ended with a fallback layer (the last good
//     projection or a black quad, never zero layers) and VR keeps running (GameXr::abandon_frame).
//   - The full recovery remains for a lost session/instance (XrSessionFault, or an XrResult from xr_result_session_fatal), a session
//     that is not running, and for a fault storm: faults that keep coming (gaps under `run_gap_ms`) for `storm_ms` and at least
//     `storm_faults` times (a state that faults every frame or every other frame), or faults with no clean Present in between for
//     `storm_ms` (a fault that itself blocks, e.g. a 1 s image wait timing out on every Present). A blip of a second or two is ridden.
//   - The recovery backoff starts over once a recovered session has shown frames for `healthy_reset_ms`.
// Header-only and pure (OpenXR types only) so the decisions run on Linux (tests/xr_fault_policy_check.cpp).
#include <openxr/openxr.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2vr {
// Thrown where the session itself is over (the runtime asked the app to exit, a forced test fault, a frame rejected for seconds):
// always a full recovery, never ridden through.
struct XrSessionFault:std::runtime_error {using std::runtime_error::runtime_error;};

// XrResults after which the session or instance cannot be used again: ending a frame cannot help, only a new session can.
inline bool xr_result_session_fatal(XrResult result) noexcept {
    switch(result) {
    case XR_ERROR_SESSION_LOST: case XR_ERROR_INSTANCE_LOST: case XR_ERROR_HANDLE_INVALID: case XR_ERROR_SESSION_NOT_RUNNING:
    case XR_ERROR_RUNTIME_UNAVAILABLE: case XR_ERROR_GRAPHICS_DEVICE_INVALID: case XR_ERROR_OUT_OF_MEMORY:
        return true;
    default: return false;
    }
}

struct XrFaultPolicy {
    static constexpr std::uint64_t run_gap_ms=500;         // a fault within this of the previous one continues the run
    static constexpr std::uint64_t storm_ms=3000;          // a run this long ...
    static constexpr unsigned storm_faults=30;             // ... with at least this many faults ends the session
    static constexpr std::uint64_t healthy_reset_ms=30000; // frames shown this long after a recovery reset the backoff
    static constexpr std::uint64_t max_backoff_ms=10000;
    enum class Action {ride_through,recover};

    // fatal: the fault says the session is gone. session_ok: a GameXr exists with a session that is not exiting or retiring.
    Action fault(std::uint64_t now_ms,bool fatal,bool session_ok) noexcept {
        if(!run_faults_ || now_ms-last_fault_>run_gap_ms) {run_start_=now_ms;run_faults_=0;}
        last_fault_=now_ms;++run_faults_;
        if(!faults_since_ok_) first_since_ok_=now_ms;
        ++faults_since_ok_;
        if(fatal || !session_ok) return Action::recover;
        if((run_faults_>=storm_faults && now_ms-run_start_>=storm_ms) || (faults_since_ok_>=2 && now_ms-first_since_ok_>=storm_ms)) {++storms;return Action::recover;}
        ++ridden;return Action::ride_through;
    }
    // A Present that ran without a fault. shows_frames: the session is running and visible (only that time counts as healthy).
    void frame_ok(std::uint64_t now_ms,bool shows_frames) noexcept {
        faults_since_ok_=0;
        if(!shows_frames) {healthy_since_=0;return;}
        if(!healthy_since_) {healthy_since_=now_ms?now_ms:1;return;}
        if(recoveries && now_ms-healthy_since_>=healthy_reset_ms) {recoveries=0;++backoff_resets;}
    }
    // A full recovery runs now; returns the wait before the next session is created (1, 2, 4, 8, then 10 s while they keep coming).
    std::uint64_t recovered() noexcept {
        ++recoveries;healthy_since_=0;run_faults_=0;faults_since_ok_=0;
        const unsigned shift=std::min(recoveries-1u,4u);
        return std::min<std::uint64_t>(max_backoff_ms,1000ull<<shift);
    }
    unsigned recoveries{};      // recoveries since the last healthy stretch (drives the backoff)
    std::uint64_t ridden{},storms{},backoff_resets{}; // status counters
private:
    std::uint64_t run_start_{},last_fault_{};unsigned run_faults_{};
    std::uint64_t first_since_ok_{};unsigned faults_since_ok_{};
    std::uint64_t healthy_since_{};
};
} // namespace d2vr
