#pragma once
// v1283 (plan docs/plans/2026-11-13-v1283-weapons-perf-mode-plan.md, phase A; user, v1282 headset: "FPS drops when I hold a weapon", the session's
// dt 8.3 -> 16.7 ms with any published weapon, 25-58 ms with two crossbows): measure the near-draw path instead of guessing.
//   A1: per branch of ObjectMotion::prepare's near path the TSC ticks and calls (held mask match, held shadow place, one-view place, paired place,
//       placed_previous, target_dual, census/severed, RigidLocal, hand_lock), near draws and how many of them a side placed, and per mutex of the
//       weapon/held path the contended acquisitions (try_lock missed: another thread held it).
//   A2: a time series, one row per 0.5 s of gameplay into artifacts/weapon-perf.csv (pace p50/p95, mod CPU p50/p95, renders, near draws and placed
//       per render, us per render per branch, lock misses, per hand what is published and how, the player's speed). 4000 rows per file; the older
//       file is kept as weapon-perf.previous.csv (xr-frame-timing.csv keeps only the last ~1500 frames).
// Diagnostics only (build/diagnostics, d2vr::diag): off = one relaxed atomic load per timed branch, no rdtsc, no counter, no file.
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace d2vr::weapon_perf {
inline std::atomic<bool> on{false};
inline bool enabled() noexcept {return on.load(std::memory_order_relaxed);}
enum Branch : unsigned {b_held_mask,b_held_shadow,b_one_view,b_paired,b_placed_previous,b_target_dual,b_census,b_rigid_local,b_hand_lock,branches};
inline constexpr const char* branch_names[branches]={"held_mask","held_shadow","one_view","paired","placed_previous","target_dual","census","rigid_local","hand_lock"};
enum Lock : unsigned {l_held,l_binder,l_rigid,l_severed,l_writelog,locks};
inline constexpr const char* lock_names[locks]={"held","binder","rigid_local","severed","write_log"};
struct Totals {std::array<std::uint64_t,branches> ticks{},calls{};std::uint64_t nears{},matched{};std::array<std::uint64_t,locks> misses{};};
namespace detail {
inline std::array<std::atomic<std::uint64_t>,branches> ticks{},calls{};
inline std::atomic<std::uint64_t> nears{},matched{};
inline std::array<std::atomic<std::uint64_t>,locks> misses{};
struct Local {std::array<std::uint64_t,branches> ticks{},calls{};std::uint64_t nears{},matched{};unsigned n{};};
inline thread_local Local tl{};
inline void flush() noexcept {   // the draw threads' sums, every 64 events (a quiet thread keeps at most 63 unreported)
    for(unsigned b=0;b<branches;++b) {
        if(tl.ticks[b]) {ticks[b].fetch_add(tl.ticks[b],std::memory_order_relaxed);tl.ticks[b]=0;}
        if(tl.calls[b]) {calls[b].fetch_add(tl.calls[b],std::memory_order_relaxed);tl.calls[b]=0;}
    }
    if(tl.nears) {nears.fetch_add(tl.nears,std::memory_order_relaxed);tl.nears=0;}
    if(tl.matched) {matched.fetch_add(tl.matched,std::memory_order_relaxed);tl.matched=0;}
}
inline void bump() noexcept {if(!(++tl.n&63u)) flush();}
} // namespace detail
inline void add(Branch b,std::uint64_t ticks) noexcept {detail::tl.ticks[b]+=ticks;++detail::tl.calls[b];detail::bump();}
// (windows.h defines near and far as empty macros: nothing here is named so.) A near draw (clip w under Place::near_w) reached the placement paths / a published side placed one.
inline void near_draw() noexcept {if(!enabled()) return;++detail::tl.nears;detail::bump();}
inline void placed_draw() noexcept {if(!enabled()) return;++detail::tl.matched;detail::bump();}
// Times one branch while diagnostics are on (0 = off: no rdtsc at the end either).
class Timer {
public:
    explicit Timer(Branch b) noexcept:t0_(enabled()?__rdtsc():0),b_(b) {}
    ~Timer() {if(t0_) add(b_,__rdtsc()-t0_);}
    Timer(const Timer&)=delete;Timer& operator=(const Timer&)=delete;
private:
    std::uint64_t t0_;Branch b_;
};
// A lock whose contention is counted: try first; a miss (another thread holds it) is counted while diagnostics are on, then it waits as before.
template<class M> class Counted {
public:
    Counted(M& m,Lock k) noexcept:m_(m) {if(!m_.try_lock()) {if(enabled()) detail::misses[k].fetch_add(1,std::memory_order_relaxed);m_.lock();}}
    ~Counted() {m_.unlock();}
    Counted(const Counted&)=delete;Counted& operator=(const Counted&)=delete;
private:
    M& m_;
};
inline Totals totals() noexcept {
    Totals t;
    for(unsigned b=0;b<branches;++b) {t.ticks[b]=detail::ticks[b].load(std::memory_order_relaxed);t.calls[b]=detail::calls[b].load(std::memory_order_relaxed);}
    t.nears=detail::nears.load(std::memory_order_relaxed);t.matched=detail::matched.load(std::memory_order_relaxed);
    for(unsigned k=0;k<locks;++k) t.misses[k]=detail::misses[k].load(std::memory_order_relaxed);
    return t;
}
// TSC ticks -> ns, measured against steady_clock since the first call (0 until 50 ms have passed).
inline double ns_per_tick() noexcept {
    static const auto t0=std::chrono::steady_clock::now();static const std::uint64_t c0=__rdtsc();
    const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count();const std::uint64_t dc=__rdtsc()-c0;
    return ns>5e7 && dc?ns/static_cast<double>(dc):0.;
}
// Status weapon_draw.near_cost: per render (renders = Place publishes) the near draws, placed, and per branch microseconds and calls; lock misses.
inline std::string json(std::uint64_t renders) {
    const Totals t=totals();const double r=renders?static_cast<double>(renders):1.,npt=ns_per_tick();
    char b[160];std::snprintf(b,sizeof b,"{\"on\":%s,\"renders\":%llu,\"draws_per_render\":%.2f,\"placed_per_render\":%.2f,\"us_per_render\":{",enabled()?"true":"false",
        static_cast<unsigned long long>(renders),static_cast<double>(t.nears)/r,static_cast<double>(t.matched)/r);
    std::string o=b;
    for(unsigned k=0;k<branches;++k) {std::snprintf(b,sizeof b,"%s\"%s\":%.3f",k?",":"",branch_names[k],static_cast<double>(t.ticks[k])*npt/1000./r);o+=b;}
    o+="},\"calls_per_render\":{";
    for(unsigned k=0;k<branches;++k) {std::snprintf(b,sizeof b,"%s\"%s\":%.2f",k?",":"",branch_names[k],static_cast<double>(t.calls[k])/r);o+=b;}
    o+="},\"lock_misses\":{";
    for(unsigned k=0;k<locks;++k) o+=std::string(k?",":"")+"\""+lock_names[k]+"\":"+std::to_string(t.misses[k]);
    return o+"}}";
}

// A2. One thread (the Present thread) feeds frames and rolls rows; nothing here is shared.
struct HandRow {int kind{-1},source{-1},bound{},skip{-1};};   // Place kind published (-1 none), binder source, bound, last skip reason this window
class Series {
public:
    static constexpr double window_s=.5;static constexpr unsigned max_rows=4000;
    void frame(double pace_ms,double hook_ms) {if(pace_.size()<4096) {pace_.push_back(static_cast<float>(pace_ms));hook_.push_back(static_cast<float>(hook_ms));}}
    bool due(double now_s) const noexcept {return start_<0 || now_s-start_>=window_s;}
    void roll(double now_s,std::uint64_t tick,std::uint64_t renders,const std::array<HandRow,2>& hands,double player_mps) {
        const Totals t=totals();
        if(start_<0) {start_=now_s;first_=now_s;last_=t;renders_=renders;pace_.clear();hook_.clear();return;}
        const double r=renders>renders_?static_cast<double>(renders-renders_):0.,npt=ns_per_tick();
        char b[256];
        std::snprintf(b,sizeof b,"%.2f,%llu,%.3f,%.3f,%.3f,%.3f,%u,%.0f,%.2f,%.2f",now_s-first_,static_cast<unsigned long long>(tick),q(pace_,.5),q(pace_,.95),q(hook_,.5),q(hook_,.95),
            static_cast<unsigned>(pace_.size()),r,r>0?static_cast<double>(t.nears-last_.nears)/r:0.,r>0?static_cast<double>(t.matched-last_.matched)/r:0.);
        std::string row=b;
        for(unsigned k=0;k<branches;++k) {std::snprintf(b,sizeof b,",%.3f",r>0?static_cast<double>(t.ticks[k]-last_.ticks[k])*npt/1000./r:0.);row+=b;}
        for(unsigned k=0;k<locks;++k) row+=","+std::to_string(t.misses[k]-last_.misses[k]);
        for(const auto& h:hands) {std::snprintf(b,sizeof b,",%d,%d,%d,%d",h.kind,h.source,h.bound,h.skip);row+=b;}
        std::snprintf(b,sizeof b,",%.2f\n",player_mps);row+=b;
        if(pending_.size()<max_rows) pending_.push_back(std::move(row));
        start_=now_s;last_=t;renders_=renders;pace_.clear();hook_.clear();
    }
    // Appends the rows rolled since the last call (an empty path: diagnostics off, nothing kept). A file at max_rows becomes .previous.
    void write(const std::filesystem::path& path) {
        if(path.empty()) {pending_.clear();return;}
        if(pending_.empty()) return;
        std::error_code ec;
        if(!header_ || rows_>=max_rows) {
            if(header_) {auto old=path;old.replace_extension(L".previous.csv");std::filesystem::remove(old,ec);std::filesystem::rename(path,old,ec);}
            std::ofstream o(path,std::ios::trunc);if(!o) {pending_.clear();return;}
            o<<"t_s,tick,pace_p50_ms,pace_p95_ms,mod_cpu_p50_ms,mod_cpu_p95_ms,frames,renders,near_per_render,placed_per_render";
            for(const char* n:branch_names) o<<",us_"<<n;
            for(const char* n:lock_names) o<<",miss_"<<n;
            for(const char* h:{"l","r"}) o<<",kind_"<<h<<",source_"<<h<<",bound_"<<h<<",skip_"<<h;
            o<<",player_mps\n";header_=true;rows_=0;
        }
        std::ofstream o(path,std::ios::app);if(!o) {pending_.clear();return;}
        for(const auto& r:pending_) {o<<r;++rows_;}
        pending_.clear();
    }
private:
    static double q(std::vector<float> v,double p) {
        if(v.empty()) return 0.;
        const auto at=static_cast<std::size_t>(p*static_cast<double>(v.size()-1)+.5);
        std::nth_element(v.begin(),v.begin()+static_cast<std::ptrdiff_t>(at),v.end());return v[at];
    }
    std::vector<float> pace_,hook_;std::vector<std::string> pending_;
    double start_{-1},first_{};Totals last_{};std::uint64_t renders_{};
    bool header_{};unsigned rows_{};
};
inline Series& series() {static Series s;return s;}
} // namespace d2vr::weapon_perf
