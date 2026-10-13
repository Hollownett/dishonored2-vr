#pragma once
// Native held object (bottle, glass, severed arm) in VR: the holding hand keeps it while its grip is squeezed; letting go
// throws it along the hand's motion when the hand moves fast, otherwise drops it. A pickup made with a short grip click
// (released within click_ms of the carry starting) keeps the object until the next squeeze is let go.
#include <array>
#include <cstdint>

namespace d2vr {
class HeldRelease {
public:
    using V=std::array<double,3>;
    struct Out {bool drop{},toss{};V velocity{};};
    double throw_speed=1.2;        // m/s at release (recent peak) that throws instead of dropping
    std::uint64_t click_ms=350,peak_ms=100;
    // velocity: the hand's world velocity (thrown with it); speed: its tracking-space speed (walking does not throw).
    Out update(bool holding,float grip,const V& velocity,double speed,std::uint64_t now) {
        Out o{};
        if(!holding) {was_=armed_=false;return o;}
        if(!was_) {was_=true;start_=now;armed_=grip>.55f;peak_at_=0;}
        if(!peak_at_ || now-peak_at_>peak_ms || speed>=peak_speed_) {peak_=velocity;peak_speed_=speed;peak_at_=now;}
        if(grip>.55f) {armed_=true;return o;}
        if(grip>=.35f || !armed_) return o;
        armed_=false;
        if(now-start_<click_ms) return o; // click pickup: stays in hand
        if(peak_speed_>=throw_speed) {o.toss=true;o.velocity=peak_;} else o.drop=true;
        return o;
    }
private:
    bool was_{},armed_{};std::uint64_t start_{},peak_at_{};V peak_{};double peak_speed_{};
};
} // namespace d2vr
