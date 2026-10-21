#pragma once
// Frame split telemetry (plan E task 1): where does the frame time go? Per-eye CPU spans, the Present hook, GPU busy and
// per-thread busy fractions, so a run can say CPU-bound or GPU-bound. Same shape as perf_meter.hpp: rolling window of the
// last 900 frames, feed() is O(1) with no allocation, json() sorts copies of the ring and runs once per status write.
// No locks of its own: the caller serialises feed/thread_sample/json (see comfort_meter.hpp, comfort_mutex).
// Stamps are ticks of one monotonic clock (qpc_freq ticks per second; the probe passes steady_clock nanoseconds with
// freq 1e9, the same clock as GameXr::cpu_time_ns). A missing stamp is <=0; prev_present_enter 0 = none (eye0_begin only: <0, so a clock that
// starts at 0 still works in tests; the probe passes -1 for "eye 0 did not begin this frame").
// A frame whose eye stamps are missing or out of order is counted in split_skipped and contributes no eye samples;
// its interval and hook samples still count.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
struct FrameStamps {std::int64_t eye0_begin{-1},eye1_begin{},present_enter{},present_exit{};};
class FrameSplit {
public:
    static constexpr std::size_t window=900;
    void feed(const FrameStamps& s,std::int64_t prev_present_enter,std::int64_t qpc_freq,std::uint64_t gpu_elapsed_ns) noexcept {
        if(qpc_freq<=0 || s.present_enter<=0) return;
        const double ns=1e9/static_cast<double>(qpc_freq);
        auto span=[&](std::int64_t a,std::int64_t b)->std::uint64_t {return b>a?static_cast<std::uint64_t>(static_cast<double>(b-a)*ns):0;};
        const std::size_t slot=head_;
        interval_[slot]=prev_present_enter!=0?span(prev_present_enter,s.present_enter):0;
        hook_[slot]=s.present_exit>0?span(s.present_enter,s.present_exit):0;
        const bool eyes=s.eye0_begin>=0 && s.eye1_begin>0 && s.eye1_begin>s.eye0_begin && s.present_enter>=s.eye1_begin;
        eye0_[slot]=eyes?span(s.eye0_begin,s.eye1_begin):0;
        eye1_[slot]=eyes?span(s.eye1_begin,s.present_enter):0;
        if(!eyes) ++skipped_;
        gpu_[slot]=gpu_elapsed_ns;
        head_=(head_+1)%window;
        if(count_<window) ++count_;
    }
    // render/game busy fraction of the latest window; negative = unknown (coarse or zero thread clock) -> null.
    void thread_sample(double render_busy,double game_busy) noexcept {render_=render_busy;game_=game_busy;}
    std::string json() const {
        const auto n=static_cast<std::ptrdiff_t>(count_);
        // the ring fills from slot 0 until it wraps, so the first count_ slots are exactly the live window
        auto live=[&](const std::array<std::uint64_t,window>& a){std::vector<std::uint64_t> v;v.reserve(count_);
            for(std::ptrdiff_t i=0;i<n;++i) if(a[static_cast<std::size_t>(i)]) v.push_back(a[static_cast<std::size_t>(i)]);
            std::sort(v.begin(),v.end());return v;};
        const auto interval=live(interval_),eye0=live(eye0_),eye1=live(eye1_),hook=live(hook_);
        std::vector<double> busy;
        for(std::size_t i=0;i<count_;++i) if(gpu_[i] && interval_[i]) busy.push_back(static_cast<double>(gpu_[i])/static_cast<double>(interval_[i]));
        std::sort(busy.begin(),busy.end());
        const double i50=ms(rank(interval,.50)),e1=ms(rank(eye1,.50));
        std::ostringstream o;
        o<<"{\"interval_ms_p50\":"<<i50<<",\"interval_ms_p95\":"<<ms(rank(interval,.95))
         <<",\"eye0_ms_p50\":"<<ms(rank(eye0,.50))<<",\"eye1_ms_p50\":"<<e1<<",\"hook_ms_p50\":"<<ms(rank(hook,.50))
         <<",\"eye1_share\":"<<(i50>0?e1/i50:0.)<<",\"gpu_busy\":";
        if(busy.empty()) o<<"null"; else o<<busy[std::min(busy.size()-1,static_cast<std::size_t>(.5*static_cast<double>(busy.size())+.999999)-1)];
        o<<",\"render_thread_busy\":";opt(o,render_);o<<",\"game_thread_busy\":";opt(o,game_);
        o<<",\"frames\":"<<count_<<",\"split_skipped\":"<<skipped_<<'}';
        return o.str();
    }
private:
    static void opt(std::ostringstream& o,double v) {if(v<0) o<<"null"; else o<<v;}
    static double ms(std::uint64_t ns) noexcept {return static_cast<double>(ns)/1e6;}
    static std::uint64_t rank(const std::vector<std::uint64_t>& sorted,double p) noexcept { // nearest rank
        if(sorted.empty()) return 0;
        auto k=static_cast<std::size_t>(p*static_cast<double>(sorted.size())+.999999);
        if(k<1) k=1;
        if(k>sorted.size()) k=sorted.size();
        return sorted[k-1];
    }
    std::array<std::uint64_t,window> interval_{},eye0_{},eye1_{},hook_{},gpu_{};
    std::size_t head_{},count_{};std::uint64_t skipped_{};
    double render_{-1},game_{-1};
};
} // namespace d2vr
