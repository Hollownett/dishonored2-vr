#pragma once
#include "tracked_pose.hpp"

namespace d2vr {
// Readable menus: the panel is placed upright in front of the head when a menu opens
// and then stays put. Head tilts and glances never move it (no roll, no pitch, a 25
// degree dead zone); only a clear turn away makes it glide back in front (about a
// third of a second). summon() lets the user re-place it where they look right now.
class MenuPanel {
    // 1.15 m: close enough to fill the view with the menu and read its text (was 1.6 m).
    double distance_m=1.15;
    TrackedPose pose_{};
    bool placed_{},following_{};
    static Vec3 level_forward(const Quaternion& head) {
        auto f=rotated(normalized(head),{0,0,-1});f.y=0;
        const auto n=std::sqrt(f.x*f.x+f.z*f.z);
        return n<1e-6?Vec3{0,0,-1}:Vec3{f.x/n,0,f.z/n};
    }
public:
    explicit MenuPanel(double distance=1.15) noexcept : distance_m(distance) {}
    void reset() noexcept {placed_=false;following_=false;}
    void summon() noexcept {following_=true;}
    // World-locked use (the settings panel opened during play): placed once in front of the head when first asked, then never moved
    // until reset(); no gliding after the head.
    const TrackedPose& hold(const TrackedPose& head) {
        if(!placed_) return place(head);
        return pose_;
    }
    const TrackedPose& place(const TrackedPose& head) {
        if(!finite(head.position)) throw std::invalid_argument("Invalid menu head position");
        const auto forward=level_forward(head.orientation);
        const double yaw=std::atan2(-forward.x,-forward.z);
        const TrackedPose target{head.position+forward*distance_m,{0,std::sin(yaw*.5),0,std::cos(yaw*.5)}};
        if(!placed_) {pose_=target;placed_=true;following_=false;return pose_;}
        auto to_panel=pose_.position-head.position;to_panel.y=0;
        const auto distance=std::sqrt(to_panel.x*to_panel.x+to_panel.z*to_panel.z);
        if(distance<1e-6 || dot(to_panel,forward)/distance<std::cos(25*3.14159265358979/180) || std::abs(distance-distance_m)>.4) following_=true;
        if(following_) {
            constexpr double rate=.12; // per frame: about a third of a second at 72-90 Hz
            pose_.position=pose_.position+(target.position-pose_.position)*rate;
            const auto& q=pose_.orientation;auto t=target.orientation;
            if(q.x*t.x+q.y*t.y+q.z*t.z+q.w*t.w<0) t={-t.x,-t.y,-t.z,-t.w};
            pose_.orientation=normalized(Quaternion{q.x+(t.x-q.x)*rate,q.y+(t.y-q.y)*rate,q.z+(t.z-q.z)*rate,q.w+(t.w-q.w)*rate});
            const auto left=target.position-pose_.position;
            if(dot(left,left)<.0004) following_=false;
        }
        return pose_;
    }
    static std::array<float,2> extent(std::uint32_t width,std::uint32_t height) {
        if(!width || !height) throw std::invalid_argument("Invalid menu image dimensions");
        const auto aspect=static_cast<float>(width)/height;
        const auto panel_width=std::min(1.8f,1.2f*aspect);
        return {panel_width,panel_width/aspect};
    }
};
} // namespace d2vr
