#pragma once
// Takedowns and scripted scenes: the game's camera animates (spin, pitch, roll) but the headset owns the
// view's rotation. The player's heading is held, and only a large native turn re-aims it.
// build/legacy-authored-camera restores the old behaviour (the native camera's rotation).
#include "game_camera.hpp"
#include "tracked_pose.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace d2vr {
class AuthoredHeading {
    static constexpr double pi=3.14159265358979323846;
    static constexpr double vertical_limit=.97;
    bool in_{};
    std::uint64_t epoch_{};
    double held_{};
    std::uint64_t snaps_{};
    static double wrap(double a) noexcept {return std::remainder(a,2*pi);}
public:
    static constexpr double snap_rad=.5236; // 30 degrees
    // The native camera's level yaw, or nullopt if it is unusable. A (nearly) vertical forward falls back to the
    // left axis, which stays horizontal while the camera pitches.
    static std::optional<double> native_yaw(const GameCameraPose& n,bool* vertical=nullptr) noexcept {
        for(float f:n.basis) if(!std::isfinite(f)) return {};
        const double fx=n.basis[0],fy=n.basis[1],fz=n.basis[2],lx=n.basis[3],ly=n.basis[4];
        const bool v=std::fabs(fz)>vertical_limit;
        if(vertical) *vertical=v;
        if(v) {
            if(std::hypot(lx,ly)<.2) return {};
            return wrap(std::atan2(ly,lx)-pi/2);
        }
        if(std::hypot(fx,fy)<.05) return {};
        return std::atan2(fy,fx);
    }
    std::optional<double> update(const GameCameraPose& native,bool authored,std::uint64_t epoch) noexcept {
        if(!authored) {in_=false;return {};}
        bool vertical=false;
        const auto yaw=native_yaw(native,&vertical);
        if(!in_ || epoch!=epoch_) {
            if(!yaw) {in_=false;return {};}
            in_=true;epoch_=epoch;held_=*yaw;return held_;
        }
        if(yaw && !vertical && std::fabs(wrap(*yaw-held_))>=snap_rad) {held_=*yaw;++snaps_;}
        return held_;
    }
    void reset() noexcept {in_=false;snaps_=0;}
    std::uint64_t snaps() const {return snaps_;}
};

// The native centre position plus the physical eye offset, turned by the headset into the held heading's frame;
// physical_offset optionally preserves bounded gameplay translation; orientation is the headset's, in that level frame. `origin` is the yaw-only recenter reference (default none).
inline GameCameraPose authored_head_view(const GameCameraPose& native_centre,double heading_yaw,
    const TrackedPose& centre,const TrackedPose& eye,double units_per_metre=1,
    const Quaternion& origin={0,0,0,1},const Vec3& physical_offset={}) {
    if(!std::isfinite(heading_yaw) || !finite(centre.position) || !finite(eye.position) ||
       !std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000)
        throw std::invalid_argument("Invalid authored head view input");
    const Vec3 offset=eye.position-centre.position;
    if(length(offset)>.15) throw std::invalid_argument("Eye offset is stale or mismatched");
    // Already bounded/crouch-compensated by gameplay. Validate separately from pupil/IPD.
    // Crouch lift is at most .9 m, so the gameplay upper bound is .3 + .9 m.
    if(!finite(physical_offset) || std::hypot(physical_offset.x,physical_offset.z)>.450001 ||
       physical_offset.y<-.700001 || physical_offset.y>1.200001)
        throw std::invalid_argument("Physical head offset is outside the gameplay volume");
    for(float f:native_centre.position) if(!std::isfinite(f)) throw std::invalid_argument("Invalid native centre");
    const double c=std::cos(heading_yaw),s=std::sin(heading_yaw);
    GameCameraPose level=native_centre;
    level.basis={static_cast<float>(c),static_cast<float>(s),0.f, static_cast<float>(-s),static_cast<float>(c),0.f, 0.f,0.f,1.f};
    const auto world=to_game_pose({physical_offset+offset,eye.orientation},{{0,0,0},origin},level,units_per_metre);
    GameCameraPose out=native_centre;
    for(int i=0;i<3;++i) {
        out.position[i]=static_cast<float>(i==0?world.position.x:i==1?world.position.y:world.position.z); // already anchored at the native centre
        out.basis[i*3]=static_cast<float>(world.basis[i].x);
        out.basis[i*3+1]=static_cast<float>(world.basis[i].y);
        out.basis[i*3+2]=static_cast<float>(world.basis[i].z);
    }
    for(float f:out.position) if(!std::isfinite(f)) throw std::invalid_argument("Authored head view overflow");
    return out;
}
} // namespace d2vr
