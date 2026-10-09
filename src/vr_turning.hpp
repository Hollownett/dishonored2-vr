#pragma once
// Altered portable adaptation of Dishonored-VR snap_turn.cpp (VR-219),
// b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec, zlib/libpng.
// Copyright (c) 2026 GingasVR. See third_party/dishonored_vr/LICENSE.
// Void owns radians rather than UE rotation units. A mutex owns the queued
// step/timestamp/epoch together; focus loss cancels rather than postpones it.
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace d2vr {
class SnapTurn {
public:
    struct Config {bool enabled=false;double degrees=45,threshold=.6,rearm=.3;};
    struct Stats {std::uint64_t fired{},consumed{},cancelled{};};
private:
    Config config_;
    std::mutex mutex_;
    bool armed_=false;
    double pending_{};
    std::uint64_t queued_ms_{},epoch_{};
    Stats stats_;
    void cancel() {if(pending_!=0) ++stats_.cancelled;pending_=0;}
    void expire(std::uint64_t now) {if(pending_!=0 && (now<queued_ms_ || now-queued_ms_>500)) cancel();}
public:
    void configure(Config config) {
        if(!std::isfinite(config.degrees) || config.degrees<5 || config.degrees>180 ||
            !std::isfinite(config.threshold) || config.threshold<.3 || config.threshold>.95 ||
            !std::isfinite(config.rearm) || config.rearm<.05 || config.rearm>=config.threshold)
            throw std::invalid_argument("Invalid snap turning settings");
        std::lock_guard<std::mutex> lock(mutex_);cancel();config_=config;armed_=false;
    }
    // Return true only when a verified, fresh gameplay-heading writer can
    // replace the native look axis. Menus/wheels keep their original axis.
    bool observe(double x,bool gameplay_heading_writable,bool axis_available,
                 std::uint64_t now,std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);expire(now);
        if(epoch!=epoch_) {cancel();epoch_=epoch;armed_=false;}
        if(!std::isfinite(x) || std::abs(x)>1.01) {cancel();armed_=false;return false;}
        const auto magnitude=std::abs(x);
        if(!config_.enabled || !gameplay_heading_writable || !axis_available) {
            cancel();armed_=magnitude<config_.rearm;return false;
        }
        if(armed_ && magnitude>=config_.threshold) {
            pending_+=(x>0?1:-1)*config_.degrees*3.14159265358979323846/180.;
            queued_ms_=now;armed_=false;++stats_.fired;
        } else if(magnitude<config_.rearm) armed_=true;
        return true;
    }
    double take(bool fresh_gameplay_update,std::uint64_t now,std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);expire(now);
        if(epoch!=epoch_) {cancel();epoch_=epoch;armed_=false;return 0;}
        if(!fresh_gameplay_update) return 0;
        const auto delta=pending_;pending_=0;if(delta!=0) ++stats_.consumed;return delta;
    }
    void suspend() {std::lock_guard<std::mutex> lock(mutex_);cancel();armed_=false;}
    Stats stats() {std::lock_guard<std::mutex> lock(mutex_);return stats_;}
};
} // namespace d2vr
