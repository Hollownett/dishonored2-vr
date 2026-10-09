#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace d2vr {
class ProbeSession {
    std::atomic<bool> stop_{false};
    std::uint64_t start_{},duration_ms_{};
public:
    void begin(int seconds,std::uint64_t now) {
        if(seconds!=0 && (seconds<10 || seconds>300))
            throw std::invalid_argument("Diagnostic duration is out of range");
        start_=now;duration_ms_=static_cast<std::uint64_t>(seconds)*1000;
        stop_.store(false,std::memory_order_release);
    }
    void request_stop() noexcept {stop_.store(true,std::memory_order_release);}
    bool stop_requested() const noexcept {return stop_.load(std::memory_order_acquire);}
    bool running(std::uint64_t now) const noexcept {
        return !stop_requested() && (!duration_ms_ || now-start_<duration_ms_);
    }
};
}
