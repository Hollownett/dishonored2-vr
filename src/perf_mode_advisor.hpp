#pragma once
// v1399 (docs/PERF-PLAN-90HZ-2026-11-19.md 3.1): should this player try performance mode (alternate-eye rendering)?
//
// The mode is chosen at session start (the one-view descriptor), so it cannot switch mid-session. What the session can tell is WHY
// stereo misses its budget. The 2026-11-19 sim on a Ryzen 5 5600X + RTX 5070 showed both cases: CPU-bound (~9.5 ms whatever the
// resolution; one view saved ~0.05 ms, performance mode does not help) and GPU-bound (2621x2822 with the old FSR 3 fallback: ~4 ms of
// pre-XR stall per frame; one view removes a view's GPU passes). Performance mode only pays in the second case, and only by the view's
// GPU cost minus its rebuild (2.04 ms in that sim; docs/PERFORMANCE-MODE-AFW-COMPARISON.md).
//
// Per stereo gameplay frame: work (present-to-present minus the runtime's wait), the display period and the pre-XR stall (the render
// thread blocked on a full GPU queue inside the Present hook). A frame is over budget when work > period; GPU-bound when the stall is
// above gpu_stall_ns. Over a rolling window the advice is:
//   gpu_bound   - most over-budget frames are GPU-bound: performance mode (or a lower resolution / DLSS Quality) can help;
//   cpu_bound   - most over-budget frames are not: performance mode will not help; a lower refresh rate will;
//   none        - the budget holds (or too few frames yet).
// Pure: no clocks or engine state; the lab is tests/perf_mode_advisor_check.cpp.
#include <cstdint>

namespace d2vr {
enum class PerfModeAdvice {none,gpu_bound,cpu_bound};
inline const char* perf_mode_advice_name(PerfModeAdvice a) noexcept {
    return a==PerfModeAdvice::gpu_bound?"gpu_bound":a==PerfModeAdvice::cpu_bound?"cpu_bound":"none";
}
struct PerfModeAdvisor {
    unsigned window=900;                  // frames per decision (10 s at 90 Hz)
    double over_share=.25;                // an over-budget share of the window that counts as "misses its budget"
    std::uint64_t gpu_stall_ns=1'000'000; // the GPU-bound gate (vr_perf_governor.hpp uses the same signal)
    PerfModeAdvice advice=PerfModeAdvice::none;
    unsigned decisions{},gpu_windows{},cpu_windows{};
    double last_over{},last_gpu_share{};  // of the latest window: over-budget share, GPU-bound share of the over-budget frames
    void feed(std::uint64_t work_ns,std::uint64_t period_ns,std::uint64_t stall_ns) noexcept {
        if(!period_ns || !work_ns || work_ns>period_ns*4) return; // loads, menus, hitches
        ++n_;
        if(work_ns>period_ns) {++over_;if(stall_ns>gpu_stall_ns) ++gpu_over_;}
        if(n_<window) return;
        last_over=static_cast<double>(over_)/n_;
        last_gpu_share=over_?static_cast<double>(gpu_over_)/over_:0.;
        ++decisions;
        if(last_over<over_share) advice=PerfModeAdvice::none;
        else if(last_gpu_share>=.5) {advice=PerfModeAdvice::gpu_bound;++gpu_windows;}
        else {advice=PerfModeAdvice::cpu_bound;++cpu_windows;}
        n_=over_=gpu_over_=0;
    }
private:
    unsigned n_{},over_{},gpu_over_{};
};
} // namespace d2vr
