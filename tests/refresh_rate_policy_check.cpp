// Lab for src/refresh_rate_policy.hpp (plan 1.6, knob build/auto-refresh-rate): no OpenXR. A simulated session feeds per-frame work
// at the current refresh rate; a request takes effect 150 ms later, like a runtime event. Checks the chosen rates, the 10 s spacing,
// the sustain times, the hysteresis, the ceiling and floor, loading screens, and runtimes that reject or ignore requests.
#include "../src/refresh_rate_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>

namespace {
void require(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);std::exit(1);}}
struct Change {double at_s;float from,to;};
struct Run {std::vector<Change> changes;float end_hz{};unsigned requests{};bool stopped{};};
// work_ms(t_s): the frame's work at time t; apply=false: the runtime accepts requests but never switches.
Run simulate(const std::vector<float>& offered,float start_hz,float ceiling,double total_s,const std::function<double(double)>& work_ms,
    bool apply=true,bool reject=false,unsigned seed=5) {
    d2vr::RefreshRatePolicy p;p.configure(offered.data(),static_cast<unsigned>(offered.size()),start_hz,ceiling);
    std::mt19937 rng(seed);std::uniform_real_distribution<double> jitter(-0.3,0.3);
    Run r;std::int64_t t=1'000'000'000;const std::int64_t start=t,end=t+static_cast<std::int64_t>(total_s*1e9);
    float rate=start_hz,applying=0;std::int64_t apply_at=-1;
    while(t<end) {
        const double s=static_cast<double>(t-start)/1e9;
        const auto work=static_cast<std::int64_t>((work_ms(s)+jitter(rng))*1e6);
        p.add_work(work);
        if(const float want=p.update(t)) {
            p.requested(want,t,!reject);
            if(!reject && apply) {applying=want;apply_at=t+150'000'000;}
        }
        if(apply_at>=0 && t>=apply_at) {r.changes.push_back({s,rate,applying});rate=applying;p.changed(rate);apply_at=-1;}
        const auto period=static_cast<std::int64_t>(1e9/rate);
        t+=std::max<std::int64_t>(work,period); // the frame takes its work or one refresh period, whichever is longer
    }
    r.end_hz=rate;r.requests=p.requests;r.stopped=p.stopped;
    return r;
}
double min_gap(const Run& r) {double g=1e9;for(std::size_t i=1;i<r.changes.size();++i) g=std::min(g,r.changes[i].at_s-r.changes[i-1].at_s);return g;}
const std::vector<float> quest{72,80,90,120}; // Quest 3 (VDXR and Steam Link offer XR_FB_display_refresh_rate)
} // namespace

int main() {
    using P=d2vr::RefreshRatePolicy;
    // best_for: the highest rate with ~10 % headroom, within the ceiling.
    {P p;p.configure(quest.data(),4,90,90);
     require(p.best_for(9'500'000)==90 && p.best_for(11'000'000)==80 && p.best_for(12'000'000)==72 && p.best_for(30'000'000)==72,"best_for picked the wrong rate");
     require(p.best_for(5'000'000)==90,"the ceiling (the player's headset-app rate) was exceeded");
     P q;q.configure(quest.data(),4,90,0);require(q.best_for(6'000'000)==120,"without a ceiling a light scene should reach 120 Hz");
     const float low[]={60,72,90};P f;f.configure(low,3,90,0);require(f.best_for(25'000'000)==72,"the 72 Hz floor was not applied");
     const float only[]={60};P g;g.configure(only,1,60,0);require(g.best_for(25'000'000)==60,"a runtime offering only sub-floor rates must keep its rate");
     const float messy[]={90,72,0,72,-1,80};P m;m.configure(messy,6,90,90);
     require(m.rate_count==3 && m.rates[0]==72 && m.rates[2]==90,"offered rates were not sorted and deduplicated");}
    // Light scene at 90 Hz: nothing to do.
    {const auto r=simulate(quest,90,90,120,[](double){return 9.0;});
     require(r.changes.empty() && r.requests==0,"a scene that fits 90 Hz changed the rate");}
    // The user's heavy area: 14.5 ms (E2). 90 Hz -> 72 Hz about 3 s in, then steady.
    {const auto r=simulate(quest,90,90,120,[](double){return 14.5;});
     std::printf("heavy 14.5 ms: %zu change(s), first at %.2f s %.0f -> %.0f Hz, end %.0f Hz\n",r.changes.size(),r.changes.empty()?-1:r.changes[0].at_s,
         r.changes.empty()?0:r.changes[0].from,r.changes.empty()?0:r.changes[0].to,r.end_hz);
     require(r.changes.size()==1 && r.changes[0].to==72 && r.end_hz==72,"a 14.5 ms scene did not settle at 72 Hz");
     require(r.changes[0].at_s>=3.0 && r.changes[0].at_s<=4.0,"the step down did not wait for ~3 s of sustained over-budget work");}
    // The plan's example: 11 ms fits 80 Hz (12.5 ms period) with headroom but not 90.
    {const auto r=simulate(quest,90,90,120,[](double){return 11.0;});
     require(r.changes.size()==1 && r.end_hz==80,"an 11 ms scene did not settle at 80 Hz");}
    // Hysteresis: 10.3 ms is within 95 % of the 90 Hz period (kept) but not within 90 % (never stepped up to): no change either way.
    {const auto at90=simulate(quest,90,90,120,[](double){return 10.3;}),at80=simulate(quest,80,90,120,[](double){return 10.3;});
     require(at90.changes.empty() && at80.changes.empty(),"work inside the hysteresis band changed the rate");}
    // Heavy, then light: down quickly, up only after 10 s of sustained headroom; no two changes closer than 10 s.
    {const auto r=simulate(quest,90,90,90,[](double s){return s<20?14.5:8.0;});
     for(const auto& c:r.changes) std::printf("  heavy->light: %.2f s %.0f -> %.0f Hz\n",c.at_s,c.from,c.to);
     require(r.changes.size()==2 && r.changes[0].to==72 && r.changes[1].to==90,"heavy then light did not go 90 -> 72 -> 90");
     require(r.changes[1].at_s>=30.0 && r.changes[1].at_s<=31.0,"the step up did not wait for 10 s of sustained headroom");}
    // A scene alternating heavy and light every 2 s (the pacer's flip-flop case): the rate changes at most once per 10 s.
    {const auto r=simulate(quest,90,90,300,[](double s){return static_cast<long long>(s/2)%2?8.5:14.5;});
     std::printf("alternating 14.5/8.5 ms every 2 s, 300 s: %zu changes, min gap %.1f s, end %.0f Hz\n",r.changes.size(),min_gap(r),r.end_hz);
     require(r.changes.size()<=2,"an alternating scene ping-ponged the refresh rate");
     require(r.changes.size()<2 || min_gap(r)>=10.0,"two refresh-rate changes were closer than 10 s");}
    // Long stretches (15 s heavy, 15 s light): the rate follows the scene, never faster than once per 10 s.
    {const auto r=simulate(quest,90,90,300,[](double s){return static_cast<long long>(s/15)%2?8.5:14.5;});
     std::printf("alternating 14.5/8.5 ms every 15 s, 300 s: %zu changes, min gap %.1f s, end %.0f Hz\n",r.changes.size(),min_gap(r),r.end_hz);
     require(r.changes.size()>=10 && r.changes.size()<=20,"the rate did not follow 15 s scene stretches");
     require(min_gap(r)>=10.0,"two refresh-rate changes were closer than 10 s");}
    // Noise around a boundary (work 10.0 +- 1.5 ms, re-drawn every 0.5 s): bounded changes, spacing respected.
    {std::mt19937 rng(9);std::uniform_real_distribution<double> d(-1.5,1.5);std::vector<double> steps(1300);for(auto& v:steps) v=10.0+d(rng);
     const auto r=simulate(quest,90,90,600,[&](double s){return steps[static_cast<std::size_t>(s*2)%steps.size()];});
     std::printf("noisy 10.0+-1.5 ms, 600 s: %zu changes, min gap %.1f s\n",r.changes.size(),r.changes.size()>1?min_gap(r):0.0);
     require(r.changes.size()<=20,"noisy work changed the refresh rate too often");
     require(r.changes.size()<2 || min_gap(r)>=10.0,"two refresh-rate changes were closer than 10 s");}
    // A 6 s loading screen (60 ms frames) at 90 Hz is not the scene's work: no step down.
    {const auto r=simulate(quest,90,90,60,[](double s){return s>10 && s<16?60.0:9.0;});
     require(r.changes.empty(),"a loading screen lowered the refresh rate");}
    // A runtime that accepts requests but never switches: two ignored requests stop the policy.
    {const auto r=simulate(quest,90,90,120,[](double){return 14.5;},false);
     std::printf("ignored requests: %u requests, stopped %d\n",r.requests,r.stopped?1:0);
     require(r.stopped && r.requests==2 && r.changes.empty(),"a runtime ignoring requests was asked forever");}
    // A runtime that rejects 72 Hz: that rate is dropped, the policy settles on the next one (80 Hz) instead of retrying 72.
    {P p;p.configure(quest.data(),4,90,90);
     std::int64_t t=1'000'000'000;float first=0,second=0;
     for(int i=0;i<3000 && !second;++i) {
         t+=14'500'000;p.add_work(14'500'000);
         if(const float want=p.update(t)) {if(!first) {first=want;p.requested(want,t,false);} else {second=want;p.requested(want,t,true);}}
     }
     require(first==72 && second==80 && p.rejected==1,"a rejected rate was retried or no other rate was chosen");}
    // Inactive without rates or a current rate.
    {P p;p.configure(nullptr,0,90,90);p.add_work(30'000'000);require(!p.active() && p.update(5'000'000'000)==0,"an empty policy acted");}
    std::printf("PASS refresh_rate_policy_check\n");
}
