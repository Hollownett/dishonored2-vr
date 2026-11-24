#pragma once
// A short black fade over an engine teleport of the player (Displace swap, Foresight exit; plan 2026-11-24-doto-billie-vr Task 12).
// edge() opens a window [start,end); alpha ramps 0->1 over its first 30 %, holds 1 until 70 %, ramps 1->0 to the end. A new edge inside
// a window extends the end to max(end, now+duration) and keeps the ramp-in where it is (no restart, never above 1). Pure: the caller
// passes a millisecond clock. Control thread writes, render thread reads: guard with the caller's mutex or use from one thread.
#include <algorithm>
#include <cstdint>

namespace d2vr {
// Which engine teleports fade the view, from two consecutive power snapshots: the rising edge of the swapper player state (the Displace swap)
// = 120 ms; Foresight's spirit state (3 or 4) ending in 5 or 0 (the engine puts the player back at the origin and yaw) = 220 ms. 0 = none.
struct TeleportSample {bool valid{};bool swapper{};int theeye{-1};};
inline int teleport_fade_duration_ms(const TeleportSample& prev,const TeleportSample& cur) noexcept {
    if(!prev.valid || !cur.valid) return 0;
    int ms=0;
    if(!prev.swapper && cur.swapper) ms=120;
    if((prev.theeye==3 || prev.theeye==4) && (cur.theeye==5 || cur.theeye==0)) ms=220;
    return ms;
}
class TeleportFade {
    std::uint64_t start_{},end_{},fades_{};
    double in_len_{},out_len_{};
    bool open_{};
public:
    void edge(std::uint64_t now_ms,int duration_ms) noexcept {
        if(duration_ms<=0) return;
        const std::uint64_t d=static_cast<std::uint64_t>(duration_ms);
        ++fades_;
        if(open_ && now_ms>=start_ && now_ms<end_) { // inside a window: extend the end, the ramp-in stays where it is
            end_=std::max(end_,now_ms+d);out_len_=std::max(out_len_,.3*static_cast<double>(d));
            return;
        }
        start_=now_ms;end_=now_ms+d;in_len_=out_len_=.3*static_cast<double>(d);open_=true;
    }
    float alpha(std::uint64_t now_ms) const noexcept {
        if(!open_ || now_ms<=start_ || now_ms>=end_) return 0.f;
        const double up=in_len_>0?static_cast<double>(now_ms-start_)/in_len_:1.0,down=out_len_>0?static_cast<double>(end_-now_ms)/out_len_:1.0;
        return static_cast<float>(std::min({1.0,up,down}));
    }
    std::uint64_t fades() const noexcept {return fades_;}
};
} // namespace d2vr
