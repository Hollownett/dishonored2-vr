#pragma once
// v997: live same-spot A/B for state elision. v998: FOUR independent 900-frame windows, keyed by the COMBINED state
// (PS elision on/off x wide elision on/off), fed from the same Present-thread frame_split sample. Keeping them separate is what
// makes a mid-session toggle comparable: each window holds only frames that ended in its own knob combination, so an
// off/on/off run (or a PS-on/wide-off vs PS-on/wide-on run) can be read on its own at one spot.
//
// Per frame and state: the serial ExecuteCommandList replay total (ms), the largest single list of that frame, the eye0/eye1/hook
// spans, the frame interval, the number of calls elided that frame (PS + wide), and the number of census barriers that frame.
// Barrier counts matter: a frame whose shadow was invalidated many times cannot elide, so its replay stays heavy even with the
// knobs on.
//
// v998 defect fix: `ecl_hist` is now a per-frame histogram of the TOTAL replay of each frame, with the eight bucket counts
// summing exactly to `frames`. (v997 summed the per-list histogram, so a 900-frame window reported ~100k per bucket; that was
// never a frame count.) A frame with no executed list contributes to bucket 0 like any other zero-ms frame.
//
// Statistics reuse d2vr::percentile_sample (frame_split.hpp), the same nearest-rank code the main window uses. A span sample of
// zero means "no sample for that field this frame" (the same convention FrameSplit::json uses) and is dropped from that
// field's percentile; replay totals and skipped/barrier counts keep zero as a real value.
#include "frame_split.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace d2vr {
class ElisionAb {
public:
    static constexpr std::size_t window=900;
    static constexpr unsigned histogram_buckets=8;
    static constexpr unsigned state_count=4;
    // Window order: bit0 = wide on, bit1 = PS on. Names follow the same order (off/off, off/on, on/off, on/on).
    static unsigned state_index(bool ps_on,bool wide_on) noexcept {return (ps_on?2u:0u)|(wide_on?1u:0u);}
    static const char* state_name(unsigned index) noexcept {
        static const char* const names[state_count]{"ps_off_wide_off","ps_off_wide_on","ps_on_wide_off","ps_on_wide_on"};
        return index<state_count?names[index]:"unknown";
    }
    void feed(bool ps_on,bool wide_on,const ReplayFrame& replay,std::uint64_t interval_ns,std::uint64_t eye0_ns,
              std::uint64_t eye1_ns,std::uint64_t hook_ns,std::uint64_t skipped,std::uint64_t barriers) noexcept {
        State& s=states_[state_index(ps_on,wide_on)];
        const std::size_t at=s.head;
        s.ecl[at]=replay.total_ns;
        s.max_list[at]=replay.max_list_ns;
        s.interval[at]=interval_ns;s.eye0[at]=eye0_ns;s.eye1[at]=eye1_ns;s.hook[at]=hook_ns;
        s.skipped[at]=skipped;s.barriers[at]=barriers;
        s.valid[at]=true;
        s.head=(s.head+1)%window;
        if(s.count<window) ++s.count;
    }
    void note_toggle() noexcept {toggles_.fetch_add(1,std::memory_order_relaxed);}
    void reset() noexcept {for(auto& s:states_) s={};toggles_.store(0,std::memory_order_relaxed);}
    std::uint64_t toggles() const noexcept {return toggles_.load(std::memory_order_relaxed);}
    std::uint64_t frames(bool ps_on,bool wide_on) const noexcept {return states_[state_index(ps_on,wide_on)].count;}
    std::string json() const {
        std::ostringstream out;out<<'{';
        for(unsigned i=0;i<state_count;++i) {
            if(i) out<<',';
            out<<'"'<<state_name(i)<<"\":";state_json(out,states_[i]);
        }
        out<<",\"toggles\":"<<toggles()<<'}';
        return out.str();
    }
private:
    struct State {
        std::array<std::uint64_t,window> ecl{},max_list{},interval{},eye0{},eye1{},hook{},skipped{},barriers{};
        std::array<bool,window> valid{};
        std::size_t head{},count{};
    };
    static double ms(std::uint64_t ns) noexcept {return static_cast<double>(ns)/1e6;}
    // Same thresholds the per-list replay histogram uses (<1, 1-2, 2-4, 4-6, 6-8, 8-12, 12-16, >16 ms), applied to one
    // frame's TOTAL replay. One frame increments exactly one bucket.
    static unsigned bucket(std::uint64_t ns) noexcept {
        if(ns<1'000'000ull) return 0;
        if(ns<2'000'000ull) return 1;
        if(ns<4'000'000ull) return 2;
        if(ns<6'000'000ull) return 3;
        if(ns<8'000'000ull) return 4;
        if(ns<12'000'000ull) return 5;
        if(ns<16'000'000ull) return 6;
        return 7;
    }
    static void state_json(std::ostringstream& out,const State& s) {
        const std::size_t n=s.count;
        // A frame with no interval/eye/hook sample carries 0 there; drop those from the span percentiles (FrameSplit::json does
        // the same). Replay totals and counters keep zero: "no list this frame" and "nothing skipped" are real measurements.
        std::array<bool,window> span{};
        for(std::size_t i=0;i<n;++i) span[i]=s.valid[i] && s.interval[i]!=0;
        std::array<bool,window> eye_span{};
        for(std::size_t i=0;i<n;++i) eye_span[i]=s.valid[i] && s.eye0[i]!=0 && s.eye1[i]!=0;
        std::array<bool,window> hook_span{};
        for(std::size_t i=0;i<n;++i) hook_span[i]=s.valid[i] && s.hook[i]!=0;
        bool any=false;
        const auto pct=[&](const auto& a,const bool* valid,double p) {
            return percentile_sample(a,valid,n,p,any);
        };
        std::uint64_t hist[histogram_buckets]{};
        for(std::size_t i=0;i<n;++i) if(s.valid[i]) ++hist[bucket(s.ecl[i])];
        out<<"{\"frames\":"<<n
           <<",\"ecl_ms_p50\":"<<ms(static_cast<std::uint64_t>(pct(s.ecl,s.valid.data(),.50)))
           <<",\"ecl_ms_p90\":"<<ms(static_cast<std::uint64_t>(pct(s.ecl,s.valid.data(),.90)))
           <<",\"interval_ms_p50\":"<<ms(static_cast<std::uint64_t>(pct(s.interval,span.data(),.50)))
           <<",\"interval_ms_p95\":"<<ms(static_cast<std::uint64_t>(pct(s.interval,span.data(),.95)))
           <<",\"eye0_ms_p50\":"<<ms(static_cast<std::uint64_t>(pct(s.eye0,eye_span.data(),.50)))
           <<",\"eye1_ms_p50\":"<<ms(static_cast<std::uint64_t>(pct(s.eye1,eye_span.data(),.50)))
           <<",\"hook_ms_p50\":"<<ms(static_cast<std::uint64_t>(pct(s.hook,hook_span.data(),.50)))
           <<",\"skipped_per_frame_p50\":"<<static_cast<std::uint64_t>(pct(s.skipped,s.valid.data(),.50))
           <<",\"barriers_p50\":"<<static_cast<std::uint64_t>(pct(s.barriers,s.valid.data(),.50))
           <<",\"barriers_p90\":"<<static_cast<std::uint64_t>(pct(s.barriers,s.valid.data(),.90))
           <<",\"max_list_ms_p90\":"<<ms(static_cast<std::uint64_t>(pct(s.max_list,s.valid.data(),.90)))
           <<",\"ecl_hist\":[";
        for(unsigned b=0;b<histogram_buckets;++b) out<<(b?",":"")<<hist[b];
        out<<"]}";
    }
    std::array<State,state_count> states_{};
    std::atomic<std::uint64_t> toggles_{0};
};
} // namespace d2vr
