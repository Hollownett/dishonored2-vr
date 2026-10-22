#pragma once
// CPU time of the per-frame OpenXR-facing steps inside the Present hook, summed over both eyes, with a 900-frame median and p95 each
// (plan E task 4, measurement only: nothing here changes behaviour). Under Proton's wineopenxr every xr call is a trip through the
// Windows-to-Linux bridge, so the acquire/wait/release triples and the flush-causing copies are where the "transfer" time can hide.
// Pure and header-only so it runs on Linux.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace d2vr {
enum class XrSpan : unsigned {acquire_wait,aa_sharpen,copy,overlay_draws,transfer_observer,release,overlay_layers};
inline constexpr unsigned xr_span_count=7;
inline constexpr const char* xr_span_name(unsigned i) noexcept {
    constexpr const char* names[xr_span_count]{"eye_acquire_wait","eye_aa_sharpen","eye_copy","eye_overlay_draws","eye_transfer_observer","eye_release","overlay_layers"};
    return names[i<xr_span_count?i:0];
}
class XrCpuSpans {
public:
    static constexpr std::size_t window=900;
    void add(XrSpan span,std::int64_t ns) noexcept {if(ns>0) frame_[static_cast<unsigned>(span)%xr_span_count]+=ns;}
    // One call per submitted frame, after the transfer stage.
    void end_frame() noexcept {
        const auto slot=next_%window;
        for(unsigned i=0;i<xr_span_count;++i) {ring_[i][slot]=static_cast<float>(static_cast<double>(frame_[i])*1e-6);frame_[i]=0;}
        ++next_;
    }
    std::uint64_t frames() const noexcept {return next_;}
    double percentile(XrSpan span,double q) const {
        const std::size_t n=static_cast<std::size_t>(std::min<std::uint64_t>(next_,window));
        if(!n) return 0;
        const auto& r=ring_[static_cast<unsigned>(span)%xr_span_count];
        std::vector<float> copy(r.begin(),r.begin()+static_cast<std::ptrdiff_t>(n));
        const auto at=std::min(n-1,static_cast<std::size_t>(q*static_cast<double>(n)));
        std::nth_element(copy.begin(),copy.begin()+static_cast<std::ptrdiff_t>(at),copy.end());
        return copy[at];
    }
    std::string json() const {
        std::string out="{\"frames\":"+std::to_string(next_);
        for(unsigned i=0;i<xr_span_count;++i) {
            out+=",\""+std::string(xr_span_name(i))+"\":["+std::to_string(percentile(static_cast<XrSpan>(i),.5))+','+std::to_string(percentile(static_cast<XrSpan>(i),.95))+']';
        }
        return out+'}';
    }
private:
    std::array<std::int64_t,xr_span_count> frame_{};
    std::array<std::array<float,window>,xr_span_count> ring_{};std::uint64_t next_{};
};
} // namespace d2vr
