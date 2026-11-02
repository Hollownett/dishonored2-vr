#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace d2vr {
// v841 paired pacing. SteamVR's xrWaitFrame never blocks this Present hook (v835 headset: p50 0.01 ms), so the hook's own cadence
// picks the display slot. GPU-bound near two refresh periods, the slot step flip-flopped 1/2/3 periods (15/71/13% of frames) and
// views located at D + 2 x the reported period missed by up to two periods (the hands are not reprojected: they shook).
// The pacer locks the cadence to k = 2 refresh periods when most frames need more than 1.5 periods (an app above ~2/3 of the
// refresh rate keeps running free: v863 headset ~63 fps with 1/2-period steps, the build the user liked), keeps the wakes on one grid,
// and tries k = 1 again only at probes 10 s apart that back off to 2 min while they fail. miss_lock (knob build/pacer-miss-lock, v844)
// locks instead when more than ~15% of frames miss their slot (a miss: the display step skipped a slot, or the frame's work exceeded
// a period): steady 45 Hz with exact two-period steps, but v863's ~63 fps becomes 45. Times are nanoseconds on one clock (XrTime is
// QPC time on Windows runtimes).
struct FramePacer {
    std::int64_t base{};                 // refresh period: the shortest runtime period seen
    int k{1};
    std::int64_t target{},prev_ret{},prev_display{},probe_at{},probe_end{},probe_gap{10'000'000'000},shifted_at{};
    int score{};bool probing{},active{},miss_lock{};
    // Before xrWaitFrame: the time to sleep until (0: none).
    std::int64_t wake(std::int64_t now) const noexcept {return active && k>1 && target>now && target-now<=k*base?target:0;}
    // The display period poses are predicted with: the locked cadence while pacing, else the runtime's.
    std::int64_t period(std::int64_t runtime_period) const noexcept {return active && base?k*base:runtime_period;}
    // After xrWaitFrame: ret = its return time; idle = this frame's pacing sleep + xrWaitFrame wait.
    void update(std::int64_t ret,std::int64_t idle,std::int64_t display,std::int64_t runtime_period,bool enabled) noexcept {
        if(runtime_period>0 && (!base || runtime_period<base)) base=runtime_period;
        if(!enabled || !base) {const auto b=base;*this=FramePacer{};base=b;return;}
        active=true;
        const bool on_grid=k>1 && target && ret>=target && ret-target<base/2;
        if(prev_ret) {
            const bool miss=miss_lock?ret-prev_ret-idle>base || (prev_display && display-prev_display>k*base+base/2):ret-prev_ret-idle>base*3/2;
            score=std::clamp(score+(miss?(miss_lock?6:1):-1),0,miss_lock?120:60); // miss_lock: rises while more than 1 frame in 7 misses
            if(probing && score>=(miss_lock?24:10)) {k=2;probing=false;probe_gap=std::min<std::int64_t>(probe_gap*2,120'000'000'000);probe_at=ret+probe_gap;}
            else if(probing && ret>=probe_end) {probing=false;probe_gap=10'000'000'000;score=0;}
            else if(!probing && k==1 && score>=(miss_lock?60:45)) {k=2;probe_at=ret+probe_gap;}
            else if(k==2 && ret>=probe_at) {k=1;probing=true;probe_end=ret+1'000'000'000;score=0;}
        }
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
// OpenXR runtime's time is for a frame submitted now); only a steady cadence (FramePacer::miss_lock) makes it exact.
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
