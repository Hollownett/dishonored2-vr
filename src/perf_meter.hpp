#pragma once
// Performance telemetry: rolling window of the last 900 submitted XR frames (10 s at 90 Hz) so before/after
// comparisons of a change need no external profiler. feed() is O(1) with no allocation; json() sorts copies of the
// ring and is called once per status write. No locks of its own: the caller serialises feed/json (see comfort_meter.hpp).
// pace_ns: present-to-present interval (display-locked, >= one period). mod_cpu_ns ("to XR submit": time inside our Present hook up to the end of the XR submit; the JSON key mod_cpu_* stays stable for tools/perf_run_report.py; the full hook is frame_split hook_ms)
// excluding xrWaitFrame. A frame with period_ns==0 is ignored (no display period known yet).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
class PerfMeter {
public:
    static constexpr std::size_t window=900;
    void feed(std::uint64_t pace_ns,std::uint64_t period_ns,bool missed,std::uint64_t mod_cpu_ns,float render_fraction) noexcept {
        if(!period_ns) return;
        const std::size_t slot=head_;
        pace_[slot]=pace_ns;cpu_[slot]=mod_cpu_ns;miss_[slot]=missed;
        head_=(head_+1)%window;
        if(count_<window) ++count_;
        period_=period_ns;fraction_=render_fraction;
    }
    std::string json() const {
        std::vector<std::uint64_t> pace(pace_.begin(),pace_.begin()+static_cast<std::ptrdiff_t>(count_)),cpu(cpu_.begin(),cpu_.begin()+static_cast<std::ptrdiff_t>(count_));
        // the ring is filled from slot 0 until it wraps, so the first count_ slots are exactly the live window
        std::sort(pace.begin(),pace.end());std::sort(cpu.begin(),cpu.end());
        std::size_t missed=0;double cpu_sum=0;
        for(std::size_t i=0;i<count_;++i) {if(miss_[i]) ++missed;cpu_sum+=static_cast<double>(cpu_[i]);}
        const double n=static_cast<double>(count_);
        std::ostringstream o;
        o<<"{\"pace_ms_p50\":"<<ms(rank(pace,.50))<<",\"pace_ms_p95\":"<<ms(rank(pace,.95))
         <<",\"missed_fraction\":"<<(count_?static_cast<double>(missed)/n:0.)
         <<",\"mod_cpu_ms_mean\":"<<(count_?cpu_sum/n/1e6:0.)<<",\"mod_cpu_ms_p95\":"<<ms(rank(cpu,.95))
         <<",\"fraction\":"<<fraction_<<",\"period_ms\":"<<ms(period_)<<",\"frames\":"<<count_<<'}';
        return o.str();
    }
private:
    static double ms(std::uint64_t ns) noexcept {return static_cast<double>(ns)/1e6;}
    static std::uint64_t rank(const std::vector<std::uint64_t>& sorted,double p) noexcept { // nearest rank
        if(sorted.empty()) return 0;
        auto k=static_cast<std::size_t>(p*static_cast<double>(sorted.size())+.999999);
        if(k<1) k=1;
        if(k>sorted.size()) k=sorted.size();
        return sorted[k-1];
    }
    std::array<std::uint64_t,window> pace_{},cpu_{};
    std::array<bool,window> miss_{};
    std::size_t head_{},count_{};
    std::uint64_t period_{};float fraction_{1.f};
};
} // namespace d2vr
