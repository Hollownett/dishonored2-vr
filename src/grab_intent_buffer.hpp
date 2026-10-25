#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
namespace d2vr {
// G3 (plan Task 5): a squeeze with nothing in reach stays a pending physical grab for `window` ms (grab_buffer_ms, 150 by default:
// about two reach-arrival frames at 72 Hz with margin; Bonelab gives no number). A candidate that reaches `gate` inside the window is
// caught then (0.45 m for a fist, the 0.12 m grip-alone gate for a grip alone, ruling H1). On expiry the squeeze goes native as today.
// A release, an epoch change or an occupied hand cancels it (the caller). A window of 0 disables the buffer: start() leaves it idle.
// Ids are entity ids (registry index << 16 | serial): a candidate whose registry slot was seen holding another id in this window is a
// recycled slot (the object it held is gone) and is never caught.
class GrabIntentBuffer {
public:
    enum class Step {idle,waiting,caught,expired};
    void start(std::uint64_t now,std::uint64_t window_ms,double gate_m) noexcept {
        waiting_=false;seen_count_=0;
        if(!window_ms || !std::isfinite(gate_m) || !(gate_m>0)) return;
        start_=now;window_=window_ms;gate_=gate_m;waiting_=true;
    }
    // caught only if the distance is within the gate and now < start + window; the window's end is an expiry (counted once).
    Step step(std::uint64_t now,std::optional<double> candidate_distance,std::uint32_t id=0) noexcept {
        if(!waiting_) return Step::idle;
        if(now<start_ || now-start_>=window_) {waiting_=false;++expired_;return Step::expired;}
        if(id && recycled(id)) return Step::waiting;
        if(candidate_distance && std::isfinite(*candidate_distance) && *candidate_distance>=0 && *candidate_distance<=gate_) {
            waiting_=false;++buffered_;return Step::caught;
        }
        return Step::waiting;
    }
    bool waiting() const noexcept {return waiting_;}
    double gate() const noexcept {return gate_;}
    std::uint64_t started() const noexcept {return start_;}
    std::uint64_t buffered() const noexcept {return buffered_;}
    std::uint64_t expired() const noexcept {return expired_;}
    void cancel() noexcept {waiting_=false;seen_count_=0;}
private:
    bool recycled(std::uint32_t id) noexcept {
        for(unsigned i=0;i<seen_count_;++i) if((seen_[i]>>16)==(id>>16)) return seen_[i]!=id;
        if(seen_count_<seen_.size()) seen_[seen_count_++]=id;
        return false;
    }
    bool waiting_{};std::uint64_t start_{},window_{},buffered_{},expired_{};double gate_{};
    std::array<std::uint32_t,8> seen_{};unsigned seen_count_{};
};
} // namespace d2vr
