#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace d2vr {
// Paired pacing. SteamVR's xrWaitFrame never blocks this Present hook (v835 headset: p50 0.01 ms), so the hook's own cadence
// picks the display slot. GPU-bound near two refresh periods, the slot step flip-flopped 1/2/3 periods (15/71/13% of frames) and
// views located at D + 2 x the reported period missed by up to two periods (the hands are not reprojected: they shook).
//
// v984: the lock is decided by the frame's own work, not by a miss score with a growing probe backoff. v844-v983 counted misses
// and retried the full rate only every 10 s, doubling to 2 min while the retries failed; one heavy stretch therefore held the app
// at half rate for a minute or more after the scene had recovered (2026-11-03 headset: 22.2 ms holds of 20-60 s, the user's
// "fps drops to 45 and stays"). Now:
//   - every frame measures its work: present-to-present minus the time spent inside xrWaitFrame and the pacing sleep;
//   - the median work of the last 32 frames picks the cadence (k=1 free, k=2 half rate);
//   - k=2 is entered only when that median exceeds 1.4 periods (below ~2/3 of the refresh rate; locking costs no fps there,
//     because such frames miss every slot anyway) and left as soon as it falls under 1.1 periods (~82% of the refresh rate);
//   - the cadence is therefore back to the full rate within ~0.4 s of the scene recovering, with no probe timer to expire.
// The wakes still sit on one grid while pacing, and the grid shift rule (a runtime that ignores the wake time) is unchanged.
// miss_lock (knob build/pacer-miss-lock, v844) keeps the old miss-rate lock: a steady half rate when more than ~15% of frames
// miss their slot, which v863 showed is too eager (its ~63 fps became 45).
//
// Plan 1.6 (docs/PERF-PLAN-90HZ-2026-11-19.md): each cadence change is held for at least min_hold_ns. A scene whose work hovers
// around the thresholds locked and unlocked 37 times in a 6-minute headset session (E2), and every switch is a visible judder; the
// thresholds are unchanged, only the earliest time the next switch may happen moves. 2 s is several 32-frame windows (0.35-0.7 s
// each), so a real scene change still switches within ~2 s, while a switch cannot repeat faster than that.
struct FramePacer {
    static constexpr std::int64_t default_min_hold_ns=2'000'000'000;
    std::int64_t min_hold_ns{default_min_hold_ns}; // the lab sets 0 to reproduce the pre-hold behaviour
    // v1419: the hold before a LOCK (half rate) that follows a change; -1 = min_hold_ns. Spacewarp sets it short (user, headset v1418: "the
    // interpolation kicks in a little late"): the generated frames come back as soon as the full rate is lost, and only leaving half rate
    // waits min_hold_ns, so the cadence still cannot flip-flop faster than that pair of holds.
    std::int64_t lock_hold_ns{-1};
    // v1420 (user, headset v1419: "when a heavy moment starts I still see low fps for some seconds; it should start right away"):
    // fast_lock (spacewarp) locks as soon as two of the last three frames missed their slot (work above one period), without waiting for
    // the percentile of the window; and a frame longer than two periods is kept (clamped) instead of restarting the window. The v985 restart
    // keeps a menu from locking the plain cadence in gameplay; with spacewarp, half rate in a heavy moment is the point, and in gameplay
    // after a menu half rate costs nothing (the frames in between are generated).
    bool fast_lock{};
    std::int64_t base{};                 // refresh period: the shortest runtime period seen
    int k{1};
    std::int64_t target{},prev_ret{},prev_display{},shifted_at{},changed_at{}; // changed_at: ret of the last cadence change (0: none)
    int score{};bool active{},miss_lock{};
    // Work samples (ns) of the last 32 frames. Work, not the interval, is what a scene costs: while pacing, the interval is the
    // locked two periods and says nothing about how much headroom there is.
    static constexpr unsigned work_window=32;
    std::array<std::int64_t,work_window> work_{};unsigned work_next_{},work_count_{};
    // Enter/leave thresholds in tenths of a period, integers so the comparison cannot round: enter at >1.4, leave at <1.1.
    // Spacewarp (docs/SPACEWARP-PLAN-2026-11-19.md section 10) sets its own: half rate as soon as the full rate cannot be held, because
    // the frames in between are then generated instead of missed; 0/0 holds half rate.
    static constexpr std::int64_t tenths=10,default_lock_tenths=14,default_unlock_tenths=11;
    std::int64_t lock_tenths{default_lock_tenths},unlock_tenths{default_unlock_tenths};
    // Before xrWaitFrame: the time to sleep until (0: none).
    std::int64_t wake(std::int64_t now) const noexcept {return active && k>1 && target>now && target-now<=k*base?target:0;}
    // The display period poses are predicted with: the locked cadence while pacing, else the runtime's.
    std::int64_t period(std::int64_t runtime_period) const noexcept {return active && base?k*base:runtime_period;}
    void add_work(std::int64_t ns) noexcept {work_[work_next_]=ns;work_next_=(work_next_+1)%work_window;if(work_count_<work_window) ++work_count_;}
    // Median of the samples collected so far (0 when none). A copy of at most 32 numbers: once per frame.
    std::int64_t work_median() const noexcept {return work_percentile(50);}
    std::int64_t work_percentile(unsigned percent) const noexcept {
        if(!work_count_) return 0;
        std::array<std::int64_t,work_window> copy=work_;
        const unsigned at=std::min(work_count_-1,work_count_*std::min(percent,100u)/100u);
        std::nth_element(copy.begin(),copy.begin()+at,copy.begin()+work_count_);
        return copy[at];
    }
    // Which percentile of the work window the lock/unlock rule reads: the median by default; spacewarp reads the 80th (the frames
    // that miss their slot are the slow ones, and a half-rate lock there costs no real frames).
    unsigned decide_percent{50};
    unsigned locks{},unlocks{},paced_frames{},frames{}; // status: how often the cadence moved, and how much of the session it paced
    unsigned held_frames{}; // status: frames whose rule asked for a change the hold deferred
    // The display refresh rate changed (refresh_rate_policy.hpp): the shortest period seen no longer applies. Restart the period at the
    // new rate's (0: the next runtime period), the work window and the cadence; the status counters stay. A runtime period more than
    // 5 % shorter than the new rate's is a frame predicted before the switch and is ignored from then on.
    std::int64_t min_period{};
    void rebase(std::int64_t period=0) noexcept {
        base=period>0?period:0;min_period=period>0?period-period/20:0;k=1;
        target=prev_ret=prev_display=shifted_at=changed_at=0;score=0;work_next_=work_count_=0;
    }
    // After xrWaitFrame: ret = its return time; idle = this frame's pacing sleep + xrWaitFrame wait.
    void update(std::int64_t ret,std::int64_t idle,std::int64_t display,std::int64_t runtime_period,bool enabled) noexcept {
        if(runtime_period>0 && runtime_period>min_period && (!base || runtime_period<base)) base=runtime_period;
        if(!enabled || !base) {const auto b=base;const auto hold=min_hold_ns,lock_hold=lock_hold_ns;const auto floor=min_period;const auto lt=lock_tenths,ut=unlock_tenths;const auto dp=decide_percent;const bool fl=fast_lock;
            *this=FramePacer{};base=b;min_hold_ns=hold;lock_hold_ns=lock_hold;min_period=floor;lock_tenths=lt;unlock_tenths=ut;decide_percent=dp;fast_lock=fl;return;}
        active=true;++frames;
        const bool held_unlock=changed_at && ret-changed_at<min_hold_ns;
        const bool held_lock=changed_at && ret-changed_at<(lock_hold_ns>=0?lock_hold_ns:min_hold_ns);
        const int k_before=k;
        const bool on_grid=k>1 && target && ret>=target && ret-target<base/2;
        if(prev_ret) {
            const auto work=ret-prev_ret-idle;
            // Only a gameplay frame's work describes the scene: at most one skipped slot (two periods). A menu, a loading screen
            // or a hitch runs far past that, and on 2026-11-04 such frames (30-60 ms) filled the window and locked the cadence to
            // half rate during play. Reject them and restart the estimate, so the cadence follows the scene, not the last menu.
            if(work>0 && work<=base*2) add_work(work);
            else if(fast_lock && work>0) add_work(base*2);  // v1420: a slow frame is the heavy moment itself
            else work_count_=work_next_=0;
        }
        bool want_lock=false,want_unlock=false;
        if(miss_lock) { // v844: the miss-rate lock, opt-in
            if(prev_ret) {
                const bool miss=ret-prev_ret-idle>base || (prev_display && display-prev_display>k*base+base/2);
                score=std::clamp(score+(miss?6:-1),0,120);
                want_lock=k==1 && score>=60;want_unlock=k==2 && score<=12;
            }
        } else if(work_count_>=work_window/2) { // half a window is enough to judge a scene change (16 frames = 0.35 s at 45 Hz)
            const auto median=work_percentile(decide_percent);
            want_lock=k==1 && median*tenths>base*lock_tenths;
            want_unlock=k==2 && median*tenths<base*unlock_tenths;
        }
        if(fast_lock && !miss_lock && k==1 && !want_lock && work_count_>=3) {  // v1420: two of the last three frames missed their slot
            unsigned missed=0;
            for(unsigned i=1;i<=3;++i) missed+=work_[(work_next_+work_window-i)%work_window]>base;
            want_lock=missed>=2;
        }
        if((want_lock && held_lock) || (want_unlock && held_unlock)) ++held_frames;
        else if(want_lock) {k=2;++locks;}
        else if(want_unlock) {k=1;++unlocks;if(miss_lock) score=0;}
        if(k!=k_before) changed_at=ret;
        if(k>1) ++paced_frames;
        // Next wake k periods after this one on the same grid; an overrun restarts the grid. A step other than k while on the grid
        // means the wakes sit at the runtime's slot boundary: move the grid half a period, at most once a second (a runtime whose
        // predicted times ignore the wake time must not stretch every frame).
        target=on_grid?target+k*base:ret+k*base;
        if(on_grid && prev_display && ret-shifted_at>1'000'000'000 && (display-prev_display>k*base+base/4 || display-prev_display<k*base-base/4)) {target+=base/2;shifted_at=ret;}
        prev_ret=ret;prev_display=display;
    }
};
// v890: how far ahead the image of this locate is displayed (it is submitted `hooks` Presents later). Unlocked, D + 2 x period missed
// that slot on 26% of frames, p99 22 ms (v863 headset log: ~63 fps on 90 Hz, steps of 1 or 2 periods at random); the median of the
// last 31 measured two-Present display steps misses on 20%, p99 11 ms. Nobody can know the slot two Presents ahead at such a rate (the
// OpenXR runtime's time is for a frame submitted now); only a steady cadence (FramePacer::k = 2) makes it exact.
struct DisplayHorizon {
    std::array<std::int64_t,33> d{};unsigned n{};
    void add(std::int64_t display) noexcept {d[n++%d.size()]=display;}
    // The median `hooks`-Present step of the ring (oldest at n % size), or `fallback` until it is full or for a non-increasing time.
    std::int64_t ahead(unsigned hooks,std::int64_t fallback) const noexcept {
        if(n<d.size() || hooks<1 || hooks>2) return fallback;
        std::array<std::int64_t,32> steps{};unsigned count=0;
        for(unsigned i=hooks;i<d.size();++i) steps[count++]=d[(n+i)%d.size()]-d[(n+i-hooks)%d.size()];
        std::nth_element(steps.begin(),steps.begin()+count/2,steps.begin()+count);
        return steps[count/2]>0?steps[count/2]:fallback;
    }
};
} // namespace d2vr
