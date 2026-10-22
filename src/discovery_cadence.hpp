#pragma once
// How often the per-frame logic-mask discovery (game_logic_mask) re-reads the engine's manager table. Every frame until the result has
// been identical for stable_needed consecutive frames, then only every period-th frame. Anything that can change the answer without the
// table itself changing (level load, epoch change, a failed validation) calls invalidate() and the next frame is a full read again.
// Pure and header-only so it runs on Linux; the caller owns the cached result and decides what "stable" means.
#include <cstdint>
namespace d2vr {
class DiscoveryCadence {
public:
    static constexpr unsigned stable_needed=120,period=8;
    // frame: a counter that grows by one per Present. stable: the last full read equalled the one before it (the cached answer is current).
    // Repeated calls for the same frame return the first answer and count nothing, so several callers in one frame agree.
    bool due(std::uint64_t frame,bool stable) noexcept {
        if(known_ && frame==last_frame_) return last_due_;
        const bool throttled=stable && run_>=stable_needed;
        bool due_now=true;
        if(!stable) run_=0;
        else if(throttled) due_now=frame%period==0;
        else ++run_;
        known_=true;last_frame_=frame;last_due_=due_now;
        return due_now;
    }
    void invalidate() noexcept {run_=0;known_=false;++invalidations_;}
    unsigned stable_run() const noexcept {return run_;}
    bool throttling() const noexcept {return run_>=stable_needed;}
    std::uint64_t invalidations() const noexcept {return invalidations_;}
private:
    unsigned run_{};bool known_{},last_due_{true};std::uint64_t last_frame_{},invalidations_{};
};
} // namespace d2vr
