// Lab for src/frame_pacer.hpp: a SteamVR-like runtime whose xrWaitFrame returns the first free refresh slot at least `lead` after the
// call, blocking only an app that runs ahead of the refresh rate; the hook's frame work varies. Checks the display step is steady when GPU-bound near two periods (v835),
// that light frames keep the full rate, that an app above ~2/3 of the refresh rate runs free by default (v863), and that miss_lock
// (knob build/pacer-miss-lock) locks an app missing slots often (v863: 37%) to a steady half rate.
#include "../src/frame_pacer.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {
struct Steps {double s1{},s2{},s3{};int k{};std::int64_t period{};double miss_period{},miss_median{};}; // miss_*: the image two hooks later not in the located slot
Steps run(double work_ms,double jitter_ms,std::int64_t phase0,bool enabled,bool miss_lock=false,unsigned seed=7) {
    constexpr std::int64_t base=11'111'111,lead=52'300'000,warm=3'000'000'000;
    d2vr::FramePacer pacer;pacer.miss_lock=miss_lock;std::mt19937 rng(seed);
    std::uniform_real_distribution<double> work(work_ms-jitter_ms,work_ms+jitter_ms),wake_late(0.,300'000.);
    std::int64_t t=1'000'000'000,prev_display=0;int n[4]{},total=0;
    d2vr::DisplayHorizon horizon;std::int64_t pred_period[3]{},pred_median[3]{};int misses_period=0,misses_median=0,predicted=0;std::uint64_t hook=0;
    while(t<301'000'000'000) {
        std::int64_t idle=0;
        if(const auto w=pacer.wake(t)) {idle=w-t+static_cast<std::int64_t>(wake_late(rng));t+=idle;}
        auto display=(t+lead-phase0+base-1)/base*base+phase0;
        if(prev_display && display<prev_display+base) {display=prev_display+base;idle+=display-lead-t;t=display-lead;} // the runtime blocks
        pacer.update(t,idle,display,base,enabled);
        horizon.add(display);
        if(hook>=2 && t>warm) {const auto c=(hook+1)%3;misses_period+=std::llabs(display-pred_period[c])>base/2;misses_median+=std::llabs(display-pred_median[c])>base/2;++predicted;}
        pred_period[hook%3]=display+2*pacer.period(base);pred_median[hook%3]=display+horizon.ahead(2,2*pacer.period(base));++hook;
        if(prev_display && t>warm) {const auto step=(display-prev_display+base/2)/base;++n[std::min<std::int64_t>(step,3)];++total;}
        prev_display=display;t+=static_cast<std::int64_t>(work(rng)*1e6);
    }
    return {double(n[1])/total,double(n[2])/total,double(n[3])/total,pacer.k,pacer.period(base),double(misses_period)/predicted,double(misses_median)/predicted};
}
void require(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);std::exit(1);}}
// v984: the cadence is chosen from the frame's own work, so a heavy stretch must not strand the app at half rate once the scene
// recovers. `heavy_ms` for `heavy_s` seconds, then `light_ms`: returns how long after the switch k returned to 1 (negative: never),
// the k at the end, and the share of frames paced at half rate after the switch.
struct Recovery {double k1_delay_s{-1};int k_end{};double paced_after{};unsigned locks{},unlocks{};};
Recovery run_recovery(double heavy_ms,double light_ms,double heavy_s,double total_s,unsigned seed=11) {
    constexpr std::int64_t base=11'111'111,lead=52'300'000,start=1'000'000'000;
    d2vr::FramePacer pacer;std::mt19937 rng(seed);
    std::uniform_real_distribution<double> jitter(-1.,1.),wake_late(0.,300'000.);
    std::int64_t t=start,prev_display=0,light_since=-1;unsigned after=0,paced_after=0;
    Recovery r;
    const auto switch_at=start+static_cast<std::int64_t>(heavy_s*1e9),end=start+static_cast<std::int64_t>(total_s*1e9);
    while(t<end) {
        std::int64_t idle=0;
        if(const auto w=pacer.wake(t)) {idle=w-t+static_cast<std::int64_t>(wake_late(rng));t+=idle;}
        auto display=(t+lead+base-1)/base*base;
        if(prev_display && display<prev_display+base) {display=prev_display+base;idle+=display-lead-t;t=display-lead;}
        pacer.update(t,idle,display,base,true);
        if(t>=switch_at) {
            ++after;if(pacer.k>1) ++paced_after;
            if(pacer.k==1) {
                if(light_since<0) light_since=t;
                if(r.k1_delay_s<0 && t-light_since>=1'000'000'000) r.k1_delay_s=static_cast<double>(light_since-switch_at)/1e9;
            } else light_since=-1;
        }
        prev_display=display;
        const double work=(t<switch_at?heavy_ms:light_ms)+jitter(rng)*0.2;
        t+=static_cast<std::int64_t>(work*1e6);
    }
    r.k_end=pacer.k;r.paced_after=after?static_cast<double>(paced_after)/after:0;
    r.locks=pacer.locks;r.unlocks=pacer.unlocks;
    return r;
}
// Plan 1.6: a scene whose work alternates between `heavy_ms` and `light_ms` every `half_s` seconds (the user's 2026-11-19 session hovered
// around the thresholds and switched 37 times in 6 min). Returns the number of cadence changes, the shortest gap between two changes,
// and the frames the hold deferred.
struct FlipFlop {unsigned changes{};double min_gap_s{1e9};unsigned held{};};
FlipFlop run_flipflop(double heavy_ms,double light_ms,double half_s,double total_s,std::int64_t hold_ns,unsigned seed=13) {
    constexpr std::int64_t base=11'111'111,lead=52'300'000,start=1'000'000'000;
    d2vr::FramePacer pacer;pacer.min_hold_ns=hold_ns;std::mt19937 rng(seed);
    std::uniform_real_distribution<double> jitter(-1.,1.),wake_late(0.,300'000.);
    std::int64_t t=start,prev_display=0,last_change=-1;int k_prev=1;FlipFlop r;
    const auto end=start+static_cast<std::int64_t>(total_s*1e9),half=static_cast<std::int64_t>(half_s*1e9);
    while(t<end) {
        std::int64_t idle=0;
        if(const auto w=pacer.wake(t)) {idle=w-t+static_cast<std::int64_t>(wake_late(rng));t+=idle;}
        auto display=(t+lead+base-1)/base*base;
        if(prev_display && display<prev_display+base) {display=prev_display+base;idle+=display-lead-t;t=display-lead;}
        pacer.update(t,idle,display,base,true);
        if(pacer.k!=k_prev) {
            ++r.changes;
            if(last_change>=0) r.min_gap_s=std::min(r.min_gap_s,static_cast<double>(t-last_change)/1e9);
            last_change=t;k_prev=pacer.k;
        }
        prev_display=display;
        const bool heavy=((t-start)/half)%2==0;
        t+=static_cast<std::int64_t>(((heavy?heavy_ms:light_ms)+jitter(rng)*0.2)*1e6);
    }
    r.held=pacer.held_frames;
    return r;
}
} // namespace

int main() {
    const auto free=run(18,3,3'700'000,false);
    std::printf("free-running 18+-3 ms: steps 1/2/3 = %.2f/%.2f/%.2f\n",free.s1,free.s2,free.s3);
    for(std::int64_t phase=0;phase<11'111'111;phase+=1'111'111) {
        const auto r=run(18,3,phase,true);
        std::printf("paced 18+-3 ms phase %5.2f ms: steps 1/2/3 = %.3f/%.3f/%.3f\n",phase/1e6,r.s1,r.s2,r.s3);
        require(r.s2>=.97,"GPU-bound near two periods did not lock to a steady two-period step");
        require(r.k==2 && r.period==22'222'222,"the locked cadence is not the prediction period");
        require(r.miss_median<=.01,"locked cadence: the median display step missed the image slot");
    }
    const auto light=run(8,1,3'700'000,true);
    require(light.s1>=.999 && light.k==1 && light.period==11'111'111,"light frames lost the full rate");
    const auto v863=run(13.6,3,3'700'000,true); // v863 headset: interval p50 13.6 ms, 37% of frames on 22.2 ms; the user liked it free-running
    std::printf("paced 13.6+-3 ms: steps 1/2/3 = %.3f/%.3f/%.3f k %d\n",v863.s1,v863.s2,v863.s3,v863.k);
    require(v863.k==1 && v863.s1>.5,"an app above 2/3 of the refresh rate was locked to half rate by default");
    std::printf("free 13.6+-3 ms: image slot missed by D + 2 x period %.3f, by D + median step %.3f\n",v863.miss_period,v863.miss_median);
    // This runtime model (slot = first after call + fixed lead) makes ~27 ms two-hook gaps a coin flip between slots: neither rule helps
    // (printed only). The v863 SteamVR log (lead 47-78 ms) is where the median wins: 26% -> 20% of images off their slot, p99 22 -> 11 ms.
    {   // The helper itself: the fallback until the ring is full, the exact step of a steady 1-2-1-2 cadence (two runtime periods miss
        // every slot there), and the fallback for a display time that does not increase.
        d2vr::DisplayHorizon h;std::int64_t d=0;
        for(int i=0;i<32;++i) {d+=i%2?22'222'222:11'111'111;h.add(d);}
        require(h.ahead(2,1)==1,"the horizon left its fallback before the ring was full");
        h.add(d+11'111'111);
        require(h.ahead(2,1)==33'333'333 && h.ahead(1,1)==22'222'222,"a steady 1-2 cadence did not give its measured step");
        d2vr::DisplayHorizon flat;for(int i=0;i<40;++i) flat.add(5);
        require(flat.ahead(2,7)==7,"a display time that does not increase was used");
    }
    const auto near=run(13.6,3,3'700'000,true,true);
    std::printf("miss_lock 13.6+-3 ms: steps 1/2/3 = %.3f/%.3f/%.3f k %d\n",near.s1,near.s2,near.s3,near.k);
    require(near.k==2 && near.s2>=.97,"miss_lock: frequent slot misses did not lock to a steady half rate");
    for(const bool lock:{false,true}) {
        const auto fast=run(9.5,1,3'700'000,true,lock);
        require(fast.k==1 && fast.s1>=.99,"an app that makes its slots lost the full rate");
    }
    d2vr::FramePacer off;off.update(5,0,100,11'111'111,false);
    require(off.wake(0)==0 && off.period(22'222'222)==22'222'222,"disabled pacing changed timing");
    // v984 regression: 6 s of 18 ms frames lock the cadence to half rate (correct: they miss every slot either way), but 8 s of
    // 10.5 ms frames afterwards must bring the full rate back within a second and keep it. v844-v983 held k=2 for 10-120 s here.
    {const auto r=run_recovery(18,10.5,6,20);
     std::printf("v984 recovery 18 -> 10.5 ms: k=1 after %.2f s, k end %d, paced after %.2f, locks %u unlocks %u\n",
         r.k1_delay_s,r.k_end,r.paced_after,r.locks,r.unlocks);
     require(r.k1_delay_s>=0 && r.k1_delay_s<=1.0,"a recovered scene stayed locked at half rate (the v983 20-60 s 45 fps holds)");
     require(r.k_end==1,"the cadence did not stay at the full rate after the scene recovered");
     require(r.paced_after<0.25,"more than a quarter of the recovered scene was paced at half rate");}
    // The user's normal case (engine work just under one period) must never lock: 10.5 ms of work in an 11.1 ms period.
    {const auto r=run_recovery(10.5,10.5,4,12);
     std::printf("v984 steady 10.5 ms: k end %d, paced %.2f, locks %u\n",r.k_end,r.paced_after,r.locks);
     require(r.k_end==1 && r.paced_after<0.02,"an app whose frames fit the period was locked to half rate");}
    // v985: a menu or loading stretch is not the scene's work. 45 ms frames must not lock the cadence, and play afterwards keeps
    // the full rate (2026-11-04 headset: menu frames filled the work window and held the cadence at half rate in gameplay).
    {const auto r=run_recovery(45,10.5,4,14);
     std::printf("v985 menu 45 ms then 10.5 ms: k end %d, paced after %.2f, locks %u\n",r.k_end,r.paced_after,r.locks);
     require(r.k_end==1 && r.paced_after<0.02,"menu or loading frames locked the cadence to half rate");}
    // Plan 1.6: minimum hold. Work alternating 17.5 / 10 ms every 0.6 s crosses both thresholds (lock > 15.6 ms, unlock < 12.2 ms).
    // Without the hold (the pre-1.6 rule) the cadence follows every swing; with it no two changes are closer than 2 s.
    {const auto old_rule=run_flipflop(17.5,10,0.6,60,0);
     const auto held=run_flipflop(17.5,10,0.6,60,d2vr::FramePacer::default_min_hold_ns);
     std::printf("1.6 flip-flop 17.5/10 ms every 0.6 s, 60 s: no hold %u changes (min gap %.2f s); hold 2 s %u changes (min gap %.2f s, %u frames held)\n",
         old_rule.changes,old_rule.min_gap_s,held.changes,held.min_gap_s,held.held);
     require(old_rule.changes>=60 && old_rule.min_gap_s<1.0,"the lab no longer reproduces the flip-flop the hold is for");
     require(held.min_gap_s>=2.0,"two cadence changes were closer than the minimum hold");
     require(held.changes<=30 && held.changes>=10,"the hold did not bound the changes (or stopped the cadence from following the scene)");
     require(held.held>0,"the hold never deferred a change");}
    // A real scene change still switches within about the hold: 18 ms frames lock within ~2.5 s of the start even right after an unlock.
    {d2vr::FramePacer pacer;pacer.update(1'000'000'000,0,1'050'000'000,11'111'111,true);
     pacer.changed_at=1'000'000'000;pacer.k=1; // as if the cadence had just unlocked
     std::int64_t t=1'000'000'000;double locked_after=-1;
     for(int i=0;i<400 && locked_after<0;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);if(pacer.k==2) locked_after=(t-1'000'000'000)/1e9;}
     std::printf("1.6 lock after a fresh unlock: %.2f s\n",locked_after);
     require(locked_after>=2.0 && locked_after<=2.5,"a sustained heavy scene did not lock within the hold plus one frame window");}
    // v1419 (spacewarp: "kicks in a little late"): with a short lock hold the same scene locks within ~0.5 s of a fresh unlock, while an
    // unlock right after a lock still waits the full hold.
    {d2vr::FramePacer pacer;pacer.lock_hold_ns=500'000'000;pacer.update(1'000'000'000,0,1'050'000'000,11'111'111,true);
     pacer.changed_at=1'000'000'000;pacer.k=1;
     std::int64_t t=1'000'000'000;double locked_after=-1;
     for(int i=0;i<400 && locked_after<0;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);if(pacer.k==2) locked_after=(t-1'000'000'000)/1e9;}
     std::printf("v1419 lock after a fresh unlock with a 0.5 s lock hold: %.2f s\n",locked_after);
     require(locked_after>=0.5 && locked_after<=0.8,"the short lock hold did not let a heavy scene lock within ~0.5 s");
     const auto locked_at=t;double unlocked_after=-1;
     for(int i=0;i<1000 && unlocked_after<0;++i) {t+=8'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);if(pacer.k==1) unlocked_after=(t-locked_at)/1e9;}
     std::printf("v1419 unlock after that lock: %.2f s (min hold %.1f s)\n",unlocked_after,pacer.min_hold_ns/1e9);
     require(unlocked_after>=pacer.min_hold_ns/1e9,"the short lock hold also shortened the unlock hold");}
    // rebase(): a new refresh rate restarts the period and the cadence but keeps the status counters.
    {d2vr::FramePacer pacer;std::int64_t t=1'000'000'000;
     for(int i=0;i<200;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);}
     require(pacer.k==2 && pacer.locks==1,"setup: 18 ms frames did not lock at 90 Hz");
     pacer.rebase(13'888'889);
     require(pacer.k==1 && pacer.base==13'888'889 && pacer.locks==1 && pacer.work_median()==0,"rebase did not restart the cadence");
     for(int i=0;i<3;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);} // frames predicted before the switch
     require(pacer.base==13'888'889,"a stale 90 Hz period after the switch to 72 Hz became the base again");
     for(int i=0;i<200;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,13'888'889,true);}
     require(pacer.base==13'888'889 && pacer.k==1,"after rebase to 72 Hz, 18 ms frames (1.3 periods) must run free");
     pacer.rebase(11'111'111);
     for(int i=0;i<200;++i) {t+=18'000'000;pacer.update(t,0,t+50'000'000,11'111'111,true);}
     require(pacer.base==11'111'111 && pacer.k==2,"back at 90 Hz, 18 ms frames must lock again");
     d2vr::FramePacer fresh;fresh.rebase();require(fresh.base==0 && fresh.min_period==0,"rebase() without a period must take the next runtime period");}
    std::printf("PASS frame_pacer_check\n");
}
