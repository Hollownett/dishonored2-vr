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
struct FramePacer {
    std::int64_t base{};                 // refresh period: the shortest runtime period seen
    int k{1};
    std::int64_t target{},prev_ret{},prev_display{},shifted_at{};
    int score{};bool active{},miss_lock{};
    // Work samples (ns) of the last 32 frames. Work, not the interval, is what a scene costs: while pacing, the interval is the
    // locked two periods and says nothing about how much headroom there is.
    static constexpr unsigned work_window=32;
    std::array<std::int64_t,work_window> work_{};unsigned work_next_{},work_count_{};
    // Enter/leave thresholds in tenths of a period, integers so the comparison cannot round: enter at >1.4, leave at <1.1.
    static constexpr std::int64_t tenths=10,lock_tenths=14,unlock_tenths=11;
    // Before xrWaitFrame: the time to sleep until (0: none).
    std::int64_t wake(std::int64_t now) const noexcept {return active && k>1 && target>now && target-now<=k*base?target:0;}
    // The display period poses are predicted with: the locked cadence while pacing, else the runtime's.
    std::int64_t period(std::int64_t runtime_period) const noexcept {return active && base?k*base:runtime_period;}
    void add_work(std::int64_t ns) noexcept {work_[work_next_]=ns;work_next_=(work_next_+1)%work_window;if(work_count_<work_window) ++work_count_;}
    // Median of the samples collected so far (0 when none). A copy of at most 32 numbers: once per frame.
    std::int64_t work_median() const noexcept {
        if(!work_count_) return 0;
        std::array<std::int64_t,work_window> copy=work_;
        std::nth_element(copy.begin(),copy.begin()+work_count_/2,copy.begin()+work_count_);
        return copy[work_count_/2];
    }
    unsigned locks{},unlocks{},paced_frames{},frames{}; // status: how often the cadence moved, and how much of the session it paced
    // After xrWaitFrame: ret = its return time; idle = this frame's pacing sleep + xrWaitFrame wait.
    void update(std::int64_t ret,std::int64_t idle,std::int64_t display,std::int64_t runtime_period,bool enabled) noexcept {
        if(runtime_period>0 && (!base || runtime_period<base)) base=runtime_period;
        if(!enabled || !base) {const auto b=base;*this=FramePacer{};base=b;return;}
        active=true;++frames;
        const bool on_grid=k>1 && target && ret>=target && ret-target<base/2;
        if(prev_ret) {
            const auto work=ret-prev_ret-idle;
            // Only a gameplay frame's work describes the scene: at most one skipped slot (two periods). A menu, a loading screen
            // or a hitch runs far past that, and on 2026-11-04 such frames (30-60 ms) filled the window and locked the cadence to
            // half rate during play. Reject them and restart the estimate, so the cadence follows the scene, not the last menu.
            if(work>0 && work<=base*2) add_work(work);
            else work_count_=work_next_=0;
        }
        if(miss_lock) { // v844: the miss-rate lock, opt-in
            if(prev_ret) {
                const bool miss=ret-prev_ret-idle>base || (prev_display && display-prev_display>k*base+base/2);
                score=std::clamp(score+(miss?6:-1),0,120);
                if(k==1 && score>=60) {k=2;++locks;}
                else if(k==2 && score<=12) {k=1;++unlocks;score=0;}
            }
        } else if(work_count_>=work_window/2) { // half a window is enough to judge a scene change (16 frames = 0.35 s at 45 Hz)
            const auto median=work_median();
            if(k==1 && median*tenths>base*lock_tenths) {k=2;++locks;}
            else if(k==2 && median*tenths<base*unlock_tenths) {k=1;++unlocks;}
        }
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
