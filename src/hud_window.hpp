#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>

namespace d2vr {
// Dishonored 1 style "HUD window": a world-up (yaw-only) panel ~1.3 m ahead and ~7 deg below (Dishonored VR keeps hints near eye level)
// eye level. It stays put in the world; when the head yaw leaves it by more than recentre_deg,
// or the head moves more than move_m from where it was placed, it glides to the new heading.
// Pure math (no OpenXR); tests/hud_window_check.cpp.
struct HudWindowConfig {
    double distance=1.3,pitch_deg=-7,recentre_deg=35,move_m=.4,speed=4,settle_deg=2,settle_m=.03;
};
// FOV-independent sizing: the relaid HUD content (the central `scale*margin` of the 16:9 band, since
// children are scaled about the stage centre) is cropped out and sized to subtend target_deg
// horizontally at `distance`, whatever the eye FOV or render resolution.
struct HudWindowCrop {int x,y,w,h;double width_m,height_m;};
inline HudWindowCrop hud_window_crop(unsigned tex_w,unsigned tex_h,double scale,double distance=1.3,double target_deg=38,double margin=1.1) {
    const double f=std::clamp(scale*margin,.25,1.);
    const unsigned band=std::clamp<unsigned>(static_cast<unsigned>(std::lround(tex_w*9./16.)),1u,tex_h);
    const int w=std::max(1,static_cast<int>(std::lround(tex_w*f))),h=std::max(1,std::min(static_cast<int>(band),static_cast<int>(std::lround(band*f))));
    const double wm=2*distance*std::tan(target_deg*.5*0.017453292519943295);
    return {static_cast<int>((tex_w-w)/2),static_cast<int>((tex_h-h)/2),w,h,wm,wm*h/w};
}
// Angular width of the HUD screen: a fraction of the headset's real horizontal FOV, capped for reading
// comfort; the HUD size setting (55-100%) scales it to 82-100%. Falls back to 60 deg for an invalid FOV.
inline double hud_screen_degrees(double hfov_deg,int hud_percent) {
    const double base=hfov_deg>20 && hfov_deg<180?std::min(60.,.58*hfov_deg):60.;
    return base*(.6+.4*std::clamp(hud_percent,55,100)/100.);
}
class HudWindow {
    Vec3 origin_{};double yaw_{};bool valid_{},following_{};
    static double wrap(double a) {return std::remainder(a,6.283185307179586);}
    static double heading(const TrackedPose& head,double fallback) {
        const auto f=rotated(normalized(head.orientation),{0,0,-1});
        return std::hypot(f.x,f.z)<.2?fallback:std::atan2(-f.x,-f.z); // looking nearly straight up/down: keep heading
    }
public:
    void reset() noexcept {valid_=following_=false;}
    bool following() const noexcept {return following_;}
    // Returns the panel pose; the panel normal (+Z) faces the placement origin and up is world up.
    TrackedPose update(const TrackedPose& head,double dt,const HudWindowConfig& c={}) {
        if(!finite(head.position) || !std::isfinite(dt)) throw std::invalid_argument("Invalid HUD window input");
        dt=std::clamp(dt,0.,.25);
        const double hy=heading(head,yaw_);
        if(!valid_) {origin_=head.position;yaw_=hy;valid_=true;following_=false;}
        const double away=std::fabs(wrap(hy-yaw_))*57.29577951308232;
        const double moved=std::hypot(head.position.x-origin_.x,head.position.z-origin_.z);
        if(!following_ && (away>c.recentre_deg || moved>c.move_m)) following_=true;
        if(following_) {
            const double k=1-std::exp(-c.speed*dt);
            yaw_=wrap(yaw_+wrap(hy-yaw_)*k);
            origin_=origin_+(head.position-origin_)*k;
            if(std::fabs(wrap(hy-yaw_))*57.29577951308232<c.settle_deg &&
               std::hypot(head.position.x-origin_.x,head.position.z-origin_.z)<c.settle_m) following_=false;
        }
        const double p=c.pitch_deg*.017453292519943295;
        const Vec3 forward{-std::sin(yaw_),0,-std::cos(yaw_)};
        return {{origin_.x+forward.x*std::cos(p)*c.distance,origin_.y+std::sin(p)*c.distance,origin_.z+forward.z*std::cos(p)*c.distance},
                {0,std::sin(yaw_*.5),0,std::cos(yaw_*.5)}};
    }
};
} // namespace d2vr
