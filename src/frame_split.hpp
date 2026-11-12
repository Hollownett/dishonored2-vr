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
// GPU phase attribution (plan E task 3b). A native command list is tagged by the CPU phase it is EXECUTED in, taken from the
// same stamps the eye split uses: pre = after the previous Present's exit (stamps reset there) until the eye-0 begin;
// eye0 = eye-0 begin until the eye-1 begin; eye1 = eye-1 begin until Present enter; hook = inside the Present hook.
// Edge cases (all by construction of EyeStamps::phase): a list executed before the eye stamps of its own frame exist is "pre"
// (the engine is one frame deep, so this is the previous frame's setup/shadow/upload work consumed now); with eye 0 missing
// (auxiliary view, mono) a list after eye 1 is still eye1 and before it pre; an auxiliary/GUI begin (-1) stamps nothing so its
// lists fall into the phase already running; with the eye-begin hook off (no eye_boundary) every list outside the Present hook is pre.
enum class Phase : unsigned {pre=0,eye0=1,eye1=2,hook=3};
constexpr unsigned phase_count=4;
inline const char* phase_name(unsigned p) noexcept {static const char* const n[phase_count]{"pre","eye0","eye1","hook"};return n[p<phase_count?p:0];}
// The mod's own GPU work, timed with timestamp pairs (mod_gpu_timer.hpp). Reported as mod_gpu_ms_p50.
// v1191: stale_warp (the stale-eye warp call, old path or AFW v2), rig_target (the rig target's copy-out and clears), eye_motion (the motion
// pass) run inside other spans (FSR's, the layer submit) and are not added to the mod total (mod_span_summed).
enum class ModSpan : unsigned {fsr_eye0=0,fsr_eye1,aa_sharpen,eye_copy,eye_overlay,layer_overlays,eye_capture,stale_warp,rig_target,eye_motion};
constexpr unsigned mod_span_count=10,mod_span_summed=7;
inline const char* mod_span_name(unsigned i) noexcept {
    static const char* const n[mod_span_count]{"fsr_eye0","fsr_eye1","aa_sharpen","eye_copy","eye_overlay","layer_overlays","eye_capture","stale_warp","rig_target","eye_motion"};
    return n[i<mod_span_count?i:0];
}
struct PhaseGpu {double ms[phase_count]{};unsigned lists[phase_count]{};bool valid{};}; // one timer frame, summed per phase
struct ModGpu {double ms[mod_span_count]{};bool valid{};};
// v993: one frame of ExecuteCommandList replay timings (TaaDispatchHook::execute_original), summed per frame_split phase.
struct ReplayFrame {
    std::uint64_t ns[phase_count]{};
    std::uint32_t lists[phase_count]{};
    std::uint64_t total_ns{};
    std::uint32_t restore_true{};
    std::uint32_t restore_false{};
    // v997: the heavy tail. max_list_ns is the largest single ExecuteCommandList replay of this frame; hist[b] counts the
    // individual list replays by bucket (<1, 1-2, 2-4, 4-6, 6-8, 8-12, 12-16, >16 ms). Per frame, not per phase.
    std::uint64_t max_list_ns{};
    std::uint64_t hist[8]{};
    bool valid{};
};
// v997: nearest-rank percentile over the samples flagged by valid[0..count). Zero is a real value; a caller with no sample
// for a frame clears its valid flag. FrameSplit and ElisionAb share this so the two windows rank identically.
template<class A> inline double percentile_sample(const A& a,const bool* valid,std::size_t count,double p,bool& any) {
    std::vector<double> v;v.reserve(count);
    for(std::size_t i=0;i<count;++i) if(valid[i]) v.push_back(static_cast<double>(a[i]));
    any=!v.empty();if(v.empty()) return 0;
    std::sort(v.begin(),v.end());
    auto k=static_cast<std::size_t>(p*static_cast<double>(v.size())+.999999);
    if(k<1) k=1;
    if(k>v.size()) k=v.size();
    return v[k-1];
}
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
    // The phase an ExecuteCommandList called NOW belongs to (see the Phase comment). in_hook wins over the eye stamps.
    Phase phase(bool in_hook) const noexcept {
        if(in_hook) return Phase::hook;
        if(ns_[1]>0) return Phase::eye1;
        if(ns_[0]>=0) return Phase::eye0;
        return Phase::pre;
    }
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
    void feed(const FrameStamps& s,std::int64_t prev_present_enter,std::int64_t qpc_freq,std::uint64_t gpu_list_sum_ns,std::uint64_t gpu_span_ns,const PhaseGpu* phases=nullptr,const ModGpu* mod=nullptr,bool single_view=false,const ReplayFrame* replay=nullptr) noexcept {
        if(qpc_freq<=0 || s.present_enter<=0) return;
        const double ns=1e9/static_cast<double>(qpc_freq);
        auto span=[&](std::int64_t a,std::int64_t b)->std::uint64_t {return b>a?static_cast<std::uint64_t>(static_cast<double>(b-a)*ns):0;};
        const std::size_t slot=head_;
        interval_[slot]=prev_present_enter!=0?span(prev_present_enter,s.present_enter):0;
        hook_[slot]=s.present_exit>0?span(s.present_enter,s.present_exit):0;
        // single_view (alternate-eye): the engine renders one view per Present, so only eye 0's begin exists (eye1_begin missing). That frame is
        // accepted on eye 0 alone, and eye 0's span runs to Present; eye 1 contributes nothing. Frames that did stamp both eyes (paired refresh)
        // still use the two-eye rule. single_view false (every other mode) never takes this branch.
        const bool one_eye=single_view && s.eye1_begin<=0;
        accepted_=prev_present_enter!=0 && s.eye0_begin>=0 && s.eye0_begin>prev_present_enter &&
            (one_eye?s.present_enter>=s.eye0_begin:(s.eye1_begin>=s.eye0_begin && s.eye1_begin>0 && s.present_enter>=s.eye1_begin));
        const bool eyes=accepted_;
        eye0_[slot]=eyes?span(s.eye0_begin,one_eye?s.present_enter:s.eye1_begin):0;
        eye1_[slot]=eyes && !one_eye?span(s.eye1_begin,s.present_enter):0;
        if(!eyes) ++skipped_;
        gpu_[slot]=gpu_list_sum_ns;span_[slot]=gpu_span_ns;
        phase_valid_[slot]=phases && phases->valid;mod_valid_[slot]=mod && mod->valid;
        for(unsigned i=0;i<phase_count;++i) {phase_ms_[i][slot]=phase_valid_[slot]?static_cast<float>(phases->ms[i]):0.f;phase_lists_[i][slot]=phase_valid_[slot]?static_cast<std::uint16_t>(std::min(phases->lists[i],65535u)):0;}
        for(unsigned i=0;i<mod_span_count;++i) mod_ms_[i][slot]=mod_valid_[slot]?static_cast<float>(mod->ms[i]):0.f;
        mod_sum_[slot]=0.f;if(mod_valid_[slot]) for(unsigned i=0;i<mod_span_summed;++i) mod_sum_[slot]+=mod_ms_[i][slot];
        replay_valid_[slot]=replay && replay->valid;
        if(replay_valid_[slot]) {
            for(unsigned i=0;i<phase_count;++i) {replay_ns_[i][slot]=replay->ns[i];replay_lists_[i][slot]=replay->lists[i];}
            replay_total_[slot]=replay->total_ns;
            replay_true_[slot]=replay->restore_true;replay_false_[slot]=replay->restore_false;
            replay_max_[slot]=replay->max_list_ns;
            for(unsigned b=0;b<8;++b) replay_hist_[b][slot]=replay->hist[b];
        }
        head_=(head_+1)%window;
        if(count_<window) ++count_;
    }
    bool last_accepted() const noexcept {return accepted_;} // whether the latest feed passed the eye check (for the trace)
    // v997: the four span samples the latest feed stored, exactly as the window saw them (0 = no sample). ElisionAb routes its
    // per-state window from these so both windows count the same frames and the same spans.
    struct LatestFrame {std::uint64_t interval_ns{},eye0_ns{},eye1_ns{},hook_ns{};};
    LatestFrame latest() const noexcept {
        if(!count_) return {};
        const std::size_t slot=(head_+window-1)%window;
        return {interval_[slot],eye0_[slot],eye1_[slot],hook_[slot]};
    }
    // render/game busy fraction of the latest window; negative = unknown (coarse or zero thread clock) -> null.
    void thread_sample(double render_busy,double game_busy) noexcept {render_=render_busy;game_=game_busy;}
    // v993: ExecuteCommandList replay timings for the latest 900 frames. All values are per-frame sums over the phase.
    std::string replay_json() const {
        const auto n=count_;
        bool any=false;
        std::uint64_t restore_true=0,restore_false=0;
        std::uint64_t frames=0,hist[8]{};
        for(std::size_t i=0;i<n;++i) if(replay_valid_[i]) {
            restore_true+=replay_true_[i];restore_false+=replay_false_[i];++frames;
            for(unsigned b=0;b<8;++b) hist[b]+=replay_hist_[b][i];
        }
        auto phase_array=[&](const auto& a,double p,bool lists)->std::string {
            std::ostringstream out;out<<'[';
            for(unsigned i=0;i<phase_count;++i) {
                bool have=false;
                const double v=percentile_of(a[i],replay_valid_,n,p,have);
                if(i) out<<',';
                if(lists) out<<static_cast<std::uint64_t>(v+0.5); else out<<v/1e6;
            }
            out<<']';return out.str();
        };
        std::ostringstream out;
        out<<"{\"ms_p50\":"<<phase_array(replay_ns_,.50,false)
           <<",\"ms_p90\":"<<phase_array(replay_ns_,.90,false)
           <<",\"lists_p50\":"<<phase_array(replay_lists_,.50,true)
           <<",\"total_ms_p50\":"<<percentile_of(replay_total_,replay_valid_,n,.50,any)/1e6
           <<",\"total_ms_p90\":"<<percentile_of(replay_total_,replay_valid_,n,.90,any)/1e6
           // v997: the heavy tail of the serial replay, per frame (the largest single list) and the list histogram.
           <<",\"max_list_ms_p50\":"<<percentile_of(replay_max_,replay_valid_,n,.50,any)/1e6
           <<",\"max_list_ms_p90\":"<<percentile_of(replay_max_,replay_valid_,n,.90,any)/1e6
           <<",\"max_list_ms_max\":"<<percentile_of(replay_max_,replay_valid_,n,1.,any)/1e6
           <<",\"hist\":[";for(unsigned b=0;b<8;++b) out<<(b?",":"")<<hist[b];out<<']'
           <<",\"restore_true\":"<<restore_true
           <<",\"restore_false\":"<<restore_false
           <<",\"frames\":"<<frames<<'}';
        return out.str();
    }
    void reset_replay() noexcept {
        for(auto& a:replay_ns_) a.fill(0);
        for(auto& a:replay_lists_) a.fill(0);
        replay_total_.fill(0);replay_true_.fill(0);replay_false_.fill(0);replay_valid_.fill(false);
        replay_max_.fill(0);for(auto& a:replay_hist_) a.fill(0);
    }
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
        o<<",\"gpu_phase_ms_p50\":";phase_json(o,true);o<<",\"gpu_phase_lists_p50\":";phase_json(o,false);
        o<<",\"mod_gpu_ms_p50\":";mod_json(o);
        o<<",\"frames\":"<<count_<<",\"split_skipped\":"<<skipped_<<'}';
        return o.str();
    }
private:
    // Nearest-rank percentile over the frames that carry a sample. Zero is a real value here (a phase with no lists), so
    // zeros are kept. v997: the ranking itself is percentile_sample(), shared with ElisionAb.
    template<class A> static double percentile_of(const A& a,const std::array<bool,window>& valid,std::size_t count,double p,bool& any) {
        return percentile_sample(a,valid.data(),count,p,any);
    }
    template<class A> static double median_of(const A& a,const std::array<bool,window>& valid,std::size_t count,bool& any) {
        return percentile_of(a,valid,count,.5,any);
    }
    void phase_json(std::ostringstream& o,bool ms_not_lists) const {
        bool any=false;median_of(phase_valid_,phase_valid_,count_,any);
        if(!any) {o<<"null";return;}
        o<<'{';
        for(unsigned i=0;i<phase_count;++i) {bool a=false;
            const double v=ms_not_lists?median_of(phase_ms_[i],phase_valid_,count_,a):median_of(phase_lists_[i],phase_valid_,count_,a);
            o<<(i?",":"")<<'"'<<phase_name(i)<<"\":"<<v;}
        o<<'}';
    }
    void mod_json(std::ostringstream& o) const {
        bool any=false;median_of(mod_valid_,mod_valid_,count_,any);
        if(!any) {o<<"null";return;}
        o<<'{';
        for(unsigned i=0;i<mod_span_count;++i) {bool a=false;o<<'"'<<mod_span_name(i)<<"\":"<<median_of(mod_ms_[i],mod_valid_,count_,a)<<',';}
        bool a=false;o<<"\"sum\":"<<median_of(mod_sum_,mod_valid_,count_,a)<<'}';
    }
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
    std::array<std::array<float,window>,phase_count> phase_ms_{};std::array<std::array<std::uint16_t,window>,phase_count> phase_lists_{};
    std::array<std::array<float,window>,mod_span_count> mod_ms_{};std::array<float,window> mod_sum_{};
    std::array<std::array<std::uint64_t,window>,phase_count> replay_ns_{};
    std::array<std::array<std::uint32_t,window>,phase_count> replay_lists_{};
    std::array<std::uint64_t,window> replay_total_{};
    std::array<std::uint32_t,window> replay_true_{},replay_false_{};
    std::array<std::uint64_t,window> replay_max_{};
    std::array<std::array<std::uint64_t,8>,window> replay_hist_{};
    std::array<bool,window> phase_valid_{},mod_valid_{},replay_valid_{};
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
