#pragma once
// Plan 1.6 (docs/PERF-PLAN-90HZ-2026-11-19.md): automatic display refresh rate through XR_FB_display_refresh_rate (offered by VDXR and
// SteamVR on 2026-11-19). Opt-in: knob build/auto-refresh-rate. A PC that needs 12.5 ms per frame should run a steady 80 Hz instead of
// flip-flopping between 90 Hz and the pacer's 45 Hz lock (E2: 37 cadence changes in 6 min).
//
// Rule: pick the highest offered rate whose period the measured work median fits with ~10 % headroom, within [floor_hz, ceiling].
//   - work: present-to-present minus the time spent in xrWaitFrame and the pacer's sleep (the same measure FramePacer uses), the median
//     of the last 32 frames. Frames over hitch_ns (menus, loading, hitches) restart the window; under half a window is "unknown".
//   - step down only after the current rate has been over budget (work > keep_share of its period) for sustain_down_ns;
//   - step up only after a higher rate has fitted with full headroom (work <= fit_share of its period) for sustain_up_ns, to the lowest
//     rate that fitted throughout that stretch;
//   - at most one change per min_interval_ns, so the display rate cannot ping-pong;
//   - the ceiling is the rate the first session started with (the player's choice in the headset app): never above it.
//   - a runtime that accepts a request but does not switch (the rate is unchanged a full interval later) twice stops the policy for
//     the session; a request the runtime rejects removes that rate.
// Pure: no OpenXR, so the lab (tests/refresh_rate_policy_check.cpp) runs it. Rates are a fixed array (the status thread reads them).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {

struct RefreshRatePolicy {
    static constexpr double fit_share=0.90;   // a rate fits when the work median is at most 90 % of its period (~10 % headroom)
    static constexpr double keep_share=0.95;  // the current rate is kept while the work median stays within 95 % of its period
    static constexpr std::int64_t min_interval_ns=10'000'000'000; // at most one change per ~10 s
    static constexpr std::int64_t sustain_down_ns=3'000'000'000;  // over budget this long before stepping down
    static constexpr std::int64_t sustain_up_ns=10'000'000'000;   // headroom this long before stepping up
    static constexpr float floor_hz=72.f;     // never below 72 Hz unless the runtime offers nothing at or above it (comfort)
    static constexpr std::int64_t hitch_ns=50'000'000; // a frame this long is a menu, a load or a hitch, not the scene's work
    static constexpr unsigned window=32,max_rates=16;

    std::array<float,max_rates> rates{}; // offered rates, ascending
    unsigned rate_count{};
    std::uint32_t rejected_mask{};       // rates whose request the runtime refused
    float current{};                     // the runtime's current rate (Hz)
    float ceiling{};                     // highest rate to pick (0: no cap)
    float pending{};                     // last requested rate not yet confirmed
    bool stopped{};                      // the runtime ignores requests: no more for this session
    unsigned requests{},changes{},ignored{},rejected{};

    // Offered rates (any order, duplicates and non-positive values dropped), current rate, ceiling (0: none).
    void configure(const float* offered,unsigned count,float current_hz,float ceiling_hz) noexcept {
        *this=RefreshRatePolicy{};
        for(unsigned i=0;i<count && rate_count<max_rates;++i) {
            const float r=offered[i];
            if(!(r>0.f)) continue;
            bool dup=false;for(unsigned k=0;k<rate_count;++k) dup=dup || std::abs(rates[k]-r)<0.01f;
            if(!dup) rates[rate_count++]=r;
        }
        std::sort(rates.begin(),rates.begin()+rate_count);
        current=current_hz;ceiling=ceiling_hz;
    }
    bool active() const noexcept {return rate_count>0 && current>0.f && !stopped;}

    void add_work(std::int64_t ns) noexcept {
        if(ns<=0 || ns>hitch_ns) {work_count_=work_next_=0;over_since_=under_since_=-1;return;}
        work_[work_next_]=ns;work_next_=(work_next_+1)%window;if(work_count_<window) ++work_count_;
    }
    std::int64_t work_median() const noexcept {
        if(!work_count_) return 0;
        std::array<std::int64_t,window> copy=work_;
        std::nth_element(copy.begin(),copy.begin()+work_count_/2,copy.begin()+work_count_);
        return copy[work_count_/2];
    }
    static double period_ns(float hz) noexcept {return 1e9/static_cast<double>(hz);}
    // The highest allowed rate the work fits with full headroom; the lowest allowed rate when none fits; 0 when no rate is allowed.
    float best_for(std::int64_t work) const noexcept {
        float best=0,lowest=0;bool any_at_floor=false;
        for(unsigned i=0;i<rate_count;++i) any_at_floor=any_at_floor || (allowed_(i,false) && rates[i]>=floor_hz);
        for(unsigned i=0;i<rate_count;++i) {
            if(!allowed_(i,any_at_floor)) continue;
            if(lowest==0.f) lowest=rates[i];
            if(static_cast<double>(work)<=fit_share*period_ns(rates[i])) best=rates[i];
        }
        return best>0.f?best:lowest;
    }
    // Once per frame after add_work: the rate to request now, or 0.
    float update(std::int64_t now) noexcept {
        if(!active()) return 0;
        if(pending>0.f && changed_at_>=0 && now-changed_at_>=min_interval_ns) { // the request had a full interval to take effect
            if(std::abs(current-pending)>0.5f) {++ignored;if(ignored>=2) {stopped=true;return 0;}}
            pending=0;
        }
        if(work_count_<window/2) {over_since_=under_since_=-1;return 0;}
        const auto work=work_median();
        const float best=best_for(work);
        if(best<=0.f) return 0;
        const bool over=static_cast<double>(work)>keep_share*period_ns(current) && best<current-0.5f;
        const bool under=best>current+0.5f;
        if(over) {if(over_since_<0) over_since_=now;} else over_since_=-1;
        if(under) {if(under_since_<0) {under_since_=now;up_target_=best;} else up_target_=std::min(up_target_,best);} else under_since_=-1;
        if(changed_at_>=0 && now-changed_at_<min_interval_ns) return 0;
        if(over_since_>=0 && now-over_since_>=sustain_down_ns) return best;
        if(under_since_>=0 && now-under_since_>=sustain_up_ns) return up_target_;
        return 0;
    }
    // The caller asked the runtime for `hz` at `now`; accepted = the call succeeded.
    void requested(float hz,std::int64_t now,bool accepted) noexcept {
        ++requests;changed_at_=now;over_since_=under_since_=-1;
        if(accepted) {pending=hz;return;}
        ++rejected;pending=0;
        for(unsigned i=0;i<rate_count;++i) if(std::abs(rates[i]-hz)<0.01f) rejected_mask|=1u<<i;
    }
    // The runtime reports a new current rate (event or query).
    void changed(float hz) noexcept {
        if(!(hz>0.f) || std::abs(hz-current)<0.01f) return;
        current=hz;++changes;
        if(pending>0.f && std::abs(hz-pending)<0.5f) pending=0;
    }
private:
    std::array<std::int64_t,window> work_{};unsigned work_next_{},work_count_{};
    std::int64_t over_since_{-1},under_since_{-1},changed_at_{-1};
    float up_target_{};
    bool allowed_(unsigned i,bool floor_applies) const noexcept {
        if(rejected_mask&(1u<<i)) return false;
        if(ceiling>0.f && rates[i]>ceiling+0.01f) return false;
        if(floor_applies && rates[i]<floor_hz) return false;
        return true;
    }
};

} // namespace d2vr
