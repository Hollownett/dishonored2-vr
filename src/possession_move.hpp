#pragma once
#include <cmath>
#include <cstdint>

namespace d2vr {
// D2 possession (plan 2026-11-24-doto-billie-vr Task 15b, R5): the host moves in its own frame while the user looks wherever the head
// points, so the left stick is rotated by the view's yaw relative to that frame. Yaw is positive to the left (tracked_yaw); the
// stick axes are those of head_directed_stick (x right, y forward).
struct PossessionMove {
    // view_yaw - host_yaw wrapped to (-pi, pi]; 0 when either is not finite.
    static double rotation(double view_yaw_rad,double host_yaw_rad) {
        if(!std::isfinite(view_yaw_rad) || !std::isfinite(host_yaw_rad)) return 0;
        const double pi=3.14159265358979323846;
        auto r=std::remainder(view_yaw_rad-host_yaw_rad,2*pi);
        if(r<=-pi) r+=2*pi;
        return r;
    }
    static void orient(float& x,float& y,double view_yaw_rad,double host_yaw_rad) {
        const auto r=rotation(view_yaw_rad,host_yaw_rad);
        if(r==0) return;
        const auto c=std::cos(r),s=std::sin(r);
        const auto right=x*c-y*s,forward=y*c+x*s;
        x=static_cast<float>(right);y=static_cast<float>(forward);
    }
};
// Enter on the observer bit at once; leave only after it has been down for exit_hold_ms, so a flicker does not toggle.
class PossessionGate {
    bool on_{};std::uint64_t down_since_{};std::uint64_t entries_{};
public:
    static constexpr std::uint64_t exit_hold_ms=300;
    bool update(bool bit,std::uint64_t now) {
        if(bit) {if(!on_) ++entries_;on_=true;down_since_=0;}
        else if(on_) {
            if(!down_since_) down_since_=now?now:1;
            if(now>=down_since_ && now-down_since_>=exit_hold_ms) {on_=false;down_since_=0;}
        }
        return on_;
    }
    bool active() const {return on_;}
    std::uint64_t entries() const {return entries_;}
};
} // namespace d2vr
