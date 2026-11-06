#pragma once
#include <cstdint>
#include <optional>
namespace d2vr {
// A render-resolution change recreates the game's output and both XR eye swapchains. The slider fires one request per
// step, so a drag would stack several recreations (the runtime has faulted on that path). Keep only the newest value and
// hand it out once the slider has been quiet for settle_ms. A failed apply is deferred (a few bounded retries), never
// thrown into the XR session's recovery.
class ResolutionDebounce {
    std::optional<double> value_;
    std::uint64_t due_{};
    unsigned attempts_{};
public:
    static constexpr std::uint64_t settle_ms=700,retry_ms=250;
    static constexpr unsigned max_attempts=8;
    void note(double value,std::uint64_t now) noexcept {value_=value;due_=now+settle_ms;attempts_=0;}
    bool pending() const noexcept {return value_.has_value();}
    std::optional<double> take(std::uint64_t now) noexcept {
        if(!value_ || now<due_) return std::nullopt;
        const auto out=value_;value_.reset();return out;
    }
    // The apply step could not run (native cached size mid-write, ...): try again shortly, give up after max_attempts.
    bool defer(double value,std::uint64_t now) noexcept {
        if(++attempts_>max_attempts) {attempts_=0;return false;}
        if(!value_) {value_=value;due_=now+retry_ms;}
        return true;
    }
};
} // namespace d2vr
