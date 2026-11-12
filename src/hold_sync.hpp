#pragma once
// v1238 (user v1235: "loose props still not stable in movement"): one session must say whether a held thing or the hand that holds it moves
// against the player while he walks. Per held kind (prop, severed part, corpse bone) and player speed bucket (still < 0.3 m/s, walk < 2.5,
// run): the motor's lag (the led goal to the engine body, NativeGrab) and the drawn hand's distance from the controller while it holds
// (dxgi_probe, after the visible-hand source), each as a millimetre histogram with p50/p90/max; motor cap hits; source blends and snaps of
// the holding hand. Pure counters (atomics): any thread notes, the status thread reads.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace d2vr {
class HoldSync {
public:
    enum Kind : unsigned {prop,severed,corpse,kinds};
    enum Speed : unsigned {still,walk,run,speeds};
    static constexpr unsigned bins=12;
    static constexpr double edge_mm[bins]={1,2,5,10,20,35,50,100,200,350,500,1e9};
    static unsigned bucket(double player_mps) noexcept {return !(player_mps>=.3)?still:player_mps<2.5?walk:run;}
    struct Hist {
        std::array<std::atomic<std::uint64_t>,bins> n{};std::atomic<std::uint64_t> max_um{};
        void add(double m) noexcept {
            if(!std::isfinite(m) || m<0) return;
            const double mm=m*1000.;unsigned i=0;while(i+1<bins && !(mm<=edge_mm[i])) ++i;n[i].fetch_add(1,std::memory_order_relaxed);
            const auto um=static_cast<std::uint64_t>(std::min(mm*1000.,1e12));
            for(auto old=max_um.load(std::memory_order_relaxed);um>old && !max_um.compare_exchange_weak(old,um);) {}
        }
        double quantile_mm(double q) const noexcept {   // the upper edge of the bin holding quantile q (0 with no samples)
            std::uint64_t total=0;for(const auto& v:n) total+=v.load(std::memory_order_relaxed);
            if(!total) return 0;std::uint64_t run=0;
            for(unsigned i=0;i<bins;++i) {run+=n[i].load(std::memory_order_relaxed);if(static_cast<double>(run)>=q*static_cast<double>(total)) return edge_mm[i];}
            return edge_mm[bins-1];
        }
        std::string json() const {
            std::uint64_t total=0;for(const auto& v:n) total+=v.load(std::memory_order_relaxed);
            return "{\"n\":"+std::to_string(total)+",\"p50_mm_le\":"+num(quantile_mm(.5))+",\"p90_mm_le\":"+num(quantile_mm(.9))+",\"max_mm\":"+num(static_cast<double>(max_um.load())/1000.)+'}';
        }
    };
    // NativeGrab, per motor write: kind, the player's speed, the lag the motor saw, whether the write was capped.
    void motor(unsigned kind,double player_mps,double lag_m,bool capped) noexcept {
        if(kind>=kinds) return;
        motor_[kind][bucket(player_mps)].add(lag_m);
        if(capped) caps_[kind].fetch_add(1,std::memory_order_relaxed);
    }
    // dxgi_probe, per displayed frame while a hand holds a motor-held thing: the drawn hand vs the controller (palm pose before the clamp).
    void drawn(unsigned kind,double player_mps,double gap_m,bool blend,bool snap) noexcept {
        if(kind>=kinds) return;
        drawn_[kind][bucket(player_mps)].add(gap_m);
        if(blend) blends_[kind].fetch_add(1,std::memory_order_relaxed);
        if(snap) snaps_[kind].fetch_add(1,std::memory_order_relaxed);
    }
    std::string json() const {
        static constexpr const char* kn[kinds]={"prop","severed","corpse"};
        static constexpr const char* sn[speeds]={"still","walk","run"};
        std::string o="{";
        for(unsigned k=0;k<kinds;++k) {
            o+=std::string(k?",":"")+"\""+kn[k]+"\":{\"motor_lag\":{";
            for(unsigned s=0;s<speeds;++s) o+=std::string(s?",":"")+"\""+sn[s]+"\":"+motor_[k][s].json();
            o+="},\"drawn_gap\":{";
            for(unsigned s=0;s<speeds;++s) o+=std::string(s?",":"")+"\""+sn[s]+"\":"+drawn_[k][s].json();
            o+="},\"caps\":"+std::to_string(caps_[k].load())+",\"blends\":"+std::to_string(blends_[k].load())+",\"snaps\":"+std::to_string(snaps_[k].load())+'}';
        }
        return o+'}';
    }
private:
    static std::string num(double v) {char b[32];std::snprintf(b,sizeof b,"%.3f",v);return b;}
    std::array<std::array<Hist,speeds>,kinds> motor_{},drawn_{};
    std::array<std::atomic<std::uint64_t>,kinds> caps_{},blends_{},snaps_{};
};
inline HoldSync& hold_sync() noexcept {static HoldSync s;return s;}
}
