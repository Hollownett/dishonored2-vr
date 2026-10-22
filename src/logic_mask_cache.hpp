#pragma once
// Per-frame cache of the game's logic mask (plan E task 4, fix round 1). The mask is asked about 90 times per frame, from the render thread
// and from native hooks on other threads. One live read per displayed frame answers every call of that frame; the first call of a new frame
// (any thread) reads live and refreshes the cache, so an answer is never older than the frame it is asked in (a 1<<15 reload frame or a
// one-frame wheel is always seen). Validation (the game object and its state) is live on EVERY call: a load, where an engine write could
// fault, never sees a cached value. A cached answer is also dropped after max_age_ms in case the frame counter stops advancing.
// Pure and header-only; the reader is injected so the wiring is testable on Linux.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
namespace d2vr {
class LogicMaskCache {
public:
    static constexpr std::uint64_t max_age_ms=100;
    std::atomic<std::uint64_t> reads{},served{},resets{};
    // validate(): live check that the game object is usable (false -> 0 and the cache is dropped).
    // read(): the live table read, nullopt when it fails. enabled=false reads every call (knob no-discovery-throttle).
    template<class Validate,class Read> unsigned get(bool enabled,std::uint64_t frame,std::uint64_t now_ms,Validate&& validate,Read&& read) {
        if(!validate()) {drop();return 0;}
        if(enabled) {
            std::lock_guard<std::mutex> lock(mutex_);
            if(have_ && frame==frame_ && now_ms-at_ms_<max_age_ms) {served.fetch_add(1,std::memory_order_relaxed);return mask_;}
        }
        const std::optional<unsigned> live=read();
        reads.fetch_add(1,std::memory_order_relaxed);
        if(!live) {drop();return 0;}
        if(enabled) {std::lock_guard<std::mutex> lock(mutex_);mask_=*live;frame_=frame;at_ms_=now_ms;have_=true;}
        return *live;
    }
    void drop() {std::lock_guard<std::mutex> lock(mutex_);if(have_) resets.fetch_add(1,std::memory_order_relaxed);have_=false;}
private:
    std::mutex mutex_;bool have_{};std::uint64_t frame_{},at_ms_{};unsigned mask_{};
};
} // namespace d2vr
