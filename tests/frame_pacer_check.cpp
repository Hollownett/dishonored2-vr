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
    std::printf("PASS frame_pacer_check\n");
}
