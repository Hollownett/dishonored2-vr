#pragma once
// Frame split telemetry (plan E task 1): where does the frame time go? Per-eye CPU spans, the Present hook, GPU busy and
// per-thread busy fractions, so a run can say CPU-bound or GPU-bound. Same shape as perf_meter.hpp: rolling window of the
// last 900 frames, feed() is O(1) with no allocation, json() sorts copies of the ring and runs once per status write.
// No locks of its own: the caller serialises feed/thread_sample/json (see comfort_meter.hpp, comfort_mutex).
// Stamps are ticks of one monotonic clock (qpc_freq ticks per second; the probe passes steady_clock nanoseconds with
// freq 1e9, the same clock as GameXr::cpu_time_ns). eye0_begin is missing when <0 (so a test clock may start at 0; the probe
// passes -1 for "eye 0 did not begin this frame"); eye1_begin, present_enter and present_exit are missing when <=0;
// prev_present_enter 0 = none (first frame after a gap).
// Eye samples are accepted only when prev_present_enter < eye0_begin <= eye1_begin <= present_enter, i.e. both eyes began
// after the previous Present and before this one. Anything else (missing, out of order, stale eye stamps from an earlier
// frame, or no previous Present) is counted in split_skipped and contributes no eye samples; its interval and hook
// samples still count. split_skipped is cumulative since start; everything else is the 900-frame window.
// gpu_busy: the probe feeds native_gpu_summary.last_list_sum_ms (the SUM of the native command-list durations; no FSR or
// immediate draws) from a timer that lags 1-3 frames. Read it as a lagged lower bound on GPU busy; null when the
// native-gpu-timing knob is off. gpu_span is the same ratio from last_elapsed_ms (first list start to last list end), which also
// counts idle gaps between lists, so gpu_span >= gpu_busy; a run that looks GPU-bound only by its span is not GPU-bound.
// Eye identity: the begin-view function's raw index is NOT trusted to name the eye (in paired XR mode eye 0 was never stamped
// by `index==0`). EyeStamps names the eye by view address against the published per-eye views (PostTonemapPass::eye_views,
// the same identity the TAA/FSR/eye-source path uses), or, before those are published, by begin order (left, then right).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
struct FrameStamps {std::int64_t eye0_begin{-1},eye1_begin{},present_enter{},present_exit{};};
// Per-Present eye begin stamps. begin() runs on the render thread for every begin-view call, under the caller's lock.
class EyeStamps {
public:
    // Returns the eye (0 left, 1 right) the begin was stamped for, -1 when it is the null/GUI view or an auxiliary one.
    int begin(std::uintptr_t view,std::uintptr_t left,std::uintptr_t right,int raw_index,std::int64_t now) noexcept {
        if(!view) return -1; // GUI/null view
        const unsigned order=begins_;
        if(begins_<255) ++begins_;
        if(order==0) idx_a_=raw_index; else if(order==1) idx_b_=raw_index;
        int eye=-1;
        if(left && right && left!=right) eye=view==left?0:view==right?1:-1;
        else eye=order<2?static_cast<int>(order):-1;
        if(eye>=0) ns_[eye]=now;
        return eye;
    }
    std::int64_t eye_ns(int eye) const noexcept {return ns_[eye];}
    unsigned begins() const noexcept {return begins_;} // non-null begins since the last reset (saturates at 255)
    int idx_a() const noexcept {return idx_a_;} // raw index of the first / second non-null begin (diagnostic)
    int idx_b() const noexcept {return idx_b_;}
    void reset() noexcept {ns_[0]=-1;ns_[1]=0;begins_=0;idx_a_=idx_b_=-9;}
private:
    std::int64_t ns_[2]{-1,0};unsigned begins_{};int idx_a_{-9},idx_b_{-9};
};
class FrameSplit {
public:
    static constexpr std::size_t window=900;
    void feed(const FrameStamps& s,std::int64_t prev_present_enter,std::int64_t qpc_freq,std::uint64_t gpu_list_sum_ns,std::uint64_t gpu_span_ns) noexcept {
        if(qpc_freq<=0 || s.present_enter<=0) return;
        const double ns=1e9/static_cast<double>(qpc_freq);
        auto span=[&](std::int64_t a,std::int64_t b)->std::uint64_t {return b>a?static_cast<std::uint64_t>(static_cast<double>(b-a)*ns):0;};
        const std::size_t slot=head_;
        interval_[slot]=prev_present_enter!=0?span(prev_present_enter,s.present_enter):0;
        hook_[slot]=s.present_exit>0?span(s.present_enter,s.present_exit):0;
        accepted_=prev_present_enter!=0 && s.eye0_begin>=0 && s.eye0_begin>prev_present_enter && s.eye1_begin>=s.eye0_begin && s.eye1_begin>0 && s.present_enter>=s.eye1_begin;
        const bool eyes=accepted_;
        eye0_[slot]=eyes?span(s.eye0_begin,s.eye1_begin):0;
        eye1_[slot]=eyes?span(s.eye1_begin,s.present_enter):0;
        if(!eyes) ++skipped_;
        gpu_[slot]=gpu_list_sum_ns;span_[slot]=gpu_span_ns;
        head_=(head_+1)%window;
        if(count_<window) ++count_;
    }
    bool last_accepted() const noexcept {return accepted_;} // whether the latest feed passed the eye check (for the trace)
    // render/game busy fraction of the latest window; negative = unknown (coarse or zero thread clock) -> null.
    void thread_sample(double render_busy,double game_busy) noexcept {render_=render_busy;game_=game_busy;}
    std::string json() const {
        const auto n=static_cast<std::ptrdiff_t>(count_);
        // the ring fills from slot 0 until it wraps, so the first count_ slots are exactly the live window
        auto live=[&](const std::array<std::uint64_t,window>& a){std::vector<std::uint64_t> v;v.reserve(count_);
            for(std::ptrdiff_t i=0;i<n;++i) if(a[static_cast<std::size_t>(i)]) v.push_back(a[static_cast<std::size_t>(i)]);
            std::sort(v.begin(),v.end());return v;};
        const auto interval=live(interval_),eye0=live(eye0_),eye1=live(eye1_),hook=live(hook_);
        auto ratio=[&](const std::array<std::uint64_t,window>& g){std::vector<double> v;
            for(std::size_t i=0;i<count_;++i) if(g[i] && interval_[i]) v.push_back(static_cast<double>(g[i])/static_cast<double>(interval_[i]));
            std::sort(v.begin(),v.end());return v;};
        auto median=[&](std::ostringstream& o,const std::vector<double>& v){
            if(v.empty()) o<<"null"; else o<<v[std::min(v.size()-1,static_cast<std::size_t>(.5*static_cast<double>(v.size())+.999999)-1)];};
        const auto busy=ratio(gpu_),span=ratio(span_);
        const double i50=ms(rank(interval,.50)),e1=ms(rank(eye1,.50));
        std::ostringstream o;
        o<<"{\"interval_ms_p50\":"<<i50<<",\"interval_ms_p95\":"<<ms(rank(interval,.95))
         <<",\"eye0_ms_p50\":"<<ms(rank(eye0,.50))<<",\"eye1_ms_p50\":"<<e1<<",\"hook_ms_p50\":"<<ms(rank(hook,.50))
         <<",\"eye1_share\":"<<(i50>0?e1/i50:0.)<<",\"gpu_busy\":";
        median(o,busy);o<<",\"gpu_span\":";median(o,span);
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
    std::array<std::uint64_t,window> interval_{},eye0_{},eye1_{},hook_{},gpu_{},span_{};
    std::size_t head_{},count_{};std::uint64_t skipped_{};
    double render_{-1},game_{-1};bool accepted_{};
};
// Optional per-frame CSV trace (knob build/frame-split-trace). push() runs on the render thread under the caller's lock:
// fixed array, no allocation; drain() formats text on the status thread. Capped so a long run cannot fill the disk.
class FrameSplitTrace {
public:
    static constexpr std::size_t ring=4096,max_rows=20000;
    static const char* header() {return "frame,eye0,eye1,enter,exit,prev,accepted,begins,idx_a,idx_b";}
    void push(std::int64_t eye0,std::int64_t eye1,std::int64_t enter,std::int64_t exit_ns,std::int64_t prev,bool accepted,unsigned begins=0,int idx_a=-9,int idx_b=-9) noexcept {
        if(pushed_>=max_rows) return;
        if(head_-tail_>=ring) {++dropped_;return;}
        rows_[head_%ring]={pushed_,eye0,eye1,enter,exit_ns,prev,accepted,begins,idx_a,idx_b};++head_;++pushed_;
    }
    std::string drain() {
        std::ostringstream o;
        for(;tail_<head_;++tail_) {const auto& r=rows_[tail_%ring];
            o<<r.frame<<','<<r.eye0<<','<<r.eye1<<','<<r.enter<<','<<r.exit<<','<<r.prev<<','<<(r.accepted?1:0)<<','<<r.begins<<','<<r.idx_a<<','<<r.idx_b<<'\n';}
        return o.str();
    }
    std::uint64_t dropped() const noexcept {return dropped_;}
private:
    struct Row {std::uint64_t frame;std::int64_t eye0,eye1,enter,exit,prev;bool accepted;unsigned begins;int idx_a,idx_b;};
    std::array<Row,ring> rows_{};std::uint64_t head_{},tail_{},pushed_{},dropped_{};
};
} // namespace d2vr
