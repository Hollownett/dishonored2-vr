#pragma once
// Native held object (bottle, glass, severed arm) in VR: the holding hand keeps it while its grip is squeezed; letting go
// throws it along the hand's motion when the hand moves fast, otherwise drops it. A pickup made with a short grip click
// (released within click_ms of the carry starting) keeps the object until the next squeeze is let go.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
// The velocity a thrown held object leaves with. The engine's own throw (83de90) has already chosen a speed and arc and
// posts them as the set-velocity event; the hand supplies only the DIRECTION, so a thrown tank keeps the engine's speed
// (a ballistic solve, x0.7) instead of a hand speed that can tunnel a heavy body through a thin floor mesh.
// legacy=true is the old behaviour (build/legacy-throw): hand velocity x1.5, capped at 20 m/s, replacing the engine's.
// ok=false: leave the event untouched (engine velocity blocked or non-finite, hand direction unusable).
struct HeldThrow {std::array<double,3> v{};bool ok{};};
inline HeldThrow held_throw_velocity(const std::array<double,3>& hand,const std::array<double,3>& engine,bool legacy) noexcept {
    HeldThrow out{};
    for(double c:hand) if(!std::isfinite(c)) return out;
    const double hand_speed=std::sqrt(hand[0]*hand[0]+hand[1]*hand[1]+hand[2]*hand[2]);
    if(legacy) {
        const double scale=hand_speed>0?std::min(1.5,20./hand_speed):1.5;
        for(unsigned i=0;i<3;++i) out.v[i]=hand[i]*scale;
        out.ok=true;return out;
    }
    for(double c:engine) if(!std::isfinite(c)) return out;
    const double engine_speed=std::sqrt(engine[0]*engine[0]+engine[1]*engine[1]+engine[2]*engine[2]);
    if(!(hand_speed>1e-3) || !(engine_speed>1e-3)) return out;
    for(unsigned i=0;i<3;++i) out.v[i]=hand[i]/hand_speed*engine_speed;
    out.ok=true;return out;
}
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
// A body on the shoulder (user, headset v627): it stays there whatever the grips do, and is taken off by hand. A squeeze that STARTS in a
// shoulder zone while the body is carried takes hold of it (the grip that lifted it there does not count: it began on the corpse); carrying the
// hand out of the zone with that squeeze held takes the body off: a throwing motion throws it (the game's carry throw), else it is put down
// (the game's drop). Letting go inside the zone leaves it on the shoulder. A take-off the game did not act on can be repeated after retry_ms.
class ShoulderCarry {
public:
    struct Out {bool drop{},toss{};int hand{-1};};
    double throw_speed=1.2;   // m/s (recent peak of the hand's tracking-space speed), as HeldRelease
    std::uint64_t peak_ms=100,retry_ms=1200;
    // in_zone / grip / speed per hand (left, right).
    Out update(bool carrying,const std::array<bool,2>& in_zone,const std::array<float,2>& grip,const std::array<double,2>& speed,std::uint64_t now) {
        Out o{};
        for(unsigned h=0;h<2;++h) {
            auto& s=hand_[h];
            const bool down=grip[h]>=(s.down?.35f:.55f);
            if(!carrying || !down) s.taken=false;
            else if(!s.down && in_zone[h]) {s.taken=s.consumed=true;s.peak_at=0;}   // a squeeze that starts at the shoulder takes hold of the body
            if(!down) s.consumed=false;
            s.down=down;
            if(!s.peak_at || now-s.peak_at>peak_ms || speed[h]>=s.peak) {s.peak=std::isfinite(speed[h])?speed[h]:0;s.peak_at=now;}
            if(!s.taken || in_zone[h] || (sent_ && now-sent_<retry_ms) || o.hand>=0) continue;
            s.taken=false;sent_=now;o.hand=static_cast<int>(h);
            if(s.peak>=throw_speed) o.toss=true;else o.drop=true;
        }
        if(!carrying) sent_=0;
        return o;
    }
    // The squeeze that took hold of the body belongs to the shoulder carry until it is let go (after the take-off too): it is no Use, grab or
    // power for the game (a native Use while carrying would toggle the carry against the take-off's own).
    bool consumes(unsigned h) const noexcept {return h<2 && hand_[h].consumed;}
private:
    struct Hand {bool down{},taken{},consumed{};double peak{};std::uint64_t peak_at{};};
    std::array<Hand,2> hand_{};std::uint64_t sent_{};
};
} // namespace d2vr
