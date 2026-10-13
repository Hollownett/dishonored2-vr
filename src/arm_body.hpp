#pragma once
#include "arm_palette.hpp"
// Boneworks/VRIK-style body policy for the rendered first-person arms. Pure,
// deterministic, time-based. World is Z-up; yaw is about +Z from +X toward +Y.
namespace d2vr::rig {
constexpr double kPi=3.14159265358979323846;
inline double wrap_pi(double a) {a=std::fmod(a+kPi,2*kPi);if(a<0) a+=2*kPi;return a-kPi;}
inline double smooth01(double t) {t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
inline double yaw_of(Vec3 forward) {return std::atan2(forward.y,forward.x);}
inline Vec3 yaw_forward(double yaw) {return {std::cos(yaw),std::sin(yaw),0};}
inline Vec3 yaw_left(double yaw) {return {-std::sin(yaw),std::cos(yaw),0};}
// Chest yaw: stays put while the head is within the dead zone, is dragged along
// at its edge, and drifts toward the average hand direction when both hands are
// forward (VRIK chest-follows-head with hand weighting). Exponential smoothing
// keeps it frame-rate independent.
struct ChestYaw {
    double yaw{};bool valid{};
    static constexpr double dead_zone=40*kPi/180,hand_weight=.5,tau=.3,max_dt=.1,max_offset=75*kPi/180;
    double step(double head_yaw,const double* hand_yaw,double dt) {
        if(!valid) {yaw=head_yaw;valid=true;return yaw;}
        dt=std::clamp(dt,0.,max_dt);
        double target=yaw;
        const double off=wrap_pi(head_yaw-yaw);
        if(std::abs(off)>dead_zone) target=head_yaw-(off>0?dead_zone:-dead_zone);
        else if(hand_yaw) {
            // Hands pull the chest but never farther than the dead zone from the head.
            target=yaw+hand_weight*wrap_pi(*hand_yaw-yaw);
            target=head_yaw+std::clamp(wrap_pi(target-head_yaw),-dead_zone,dead_zone);
        }
        yaw=wrap_pi(yaw+wrap_pi(target-yaw)*(1-std::exp(-dt/tau)));
        const double rel=wrap_pi(yaw-head_yaw); // hard bound for large dt jumps
        yaw=wrap_pi(head_yaw+std::clamp(rel,-max_offset,max_offset));
        return yaw;
    }
};
// Hand direction for the chest: only when both hands are comfortably in front.
inline bool both_hands_yaw(Vec3 head,Vec3 left,Vec3 right,Vec3 head_forward,double units,double& out) {
    const auto l=left-head,r=right-head;
    if(dot(l,head_forward)<.15*units || dot(r,head_forward)<.15*units) return false;
    const auto mid=(l+r)*.5;if(std::hypot(mid.x,mid.y)<.25*units) return false;
    out=std::atan2(mid.y,mid.x);return true;
}
// Clavicle elevation/protraction: past a comfortable reach the shoulder point
// travels toward the hand, never more than cap.
inline Vec3 shoulder_reach_offset(Vec3 reach,double arm_length,double cap) {
    const double d=length(reach);
    if(!finite(reach) || d<1e-9 || arm_length<1e-6) return {};
    return reach*(cap*smooth01((d/arm_length-.72)/.28)/d);
}
// Rotation of the clavicle about its own origin toward the new shoulder,
// bounded; the residual is translated by the caller.
inline Quaternion clavicle_swing(Vec3 clavicle,Vec3 native_upper,Vec3 posed_upper,double max_angle=35*kPi/180) {
    const auto a=native_upper-clavicle,b=posed_upper-clavicle;
    if(length(a)<1e-6 || length(b)<1e-6) return {0,0,0,1};
    const auto q=align_direction(a,b);
    const double angle=2*std::acos(std::clamp(q.w,-1.,1.));
    if(angle<1e-9) return {0,0,0,1};
    const double use=std::min(angle,max_angle),s=std::sin(use*.5)/std::sin(angle*.5);
    return normalized({q.x*s,q.y*s,q.z*s,std::cos(use*.5)});
}
// Boneworks elbows: down and slightly out; rise for high/behind hands; palm-up
// roll turns the elbow inward. Per-step change about the reach axis is rate
// limited (continuity); dt<=0 disables the limit.
inline Vec3 boneworks_elbow_pole(Vec3 reach,Vec3 forward,Vec3 outward,Vec3 up,double arm_length,double palm_up,
                                 const Vec3* previous=nullptr,double dt=0) {
    if(!finite(reach) || length(reach)<1e-6 || arm_length<1e-6) throw std::invalid_argument("Invalid anatomical elbow frame");
    const double raise=std::max(smooth01((dot(reach,up)/arm_length-.25)/.6),smooth01(-dot(reach,forward)/arm_length/.4));
    palm_up=std::clamp(palm_up,-1.,1.);
    const auto hint=outward*(.45+.5*raise-.6*palm_up)-up*(1-1.3*raise)-forward*.15;
    // Anatomical target without the hemisphere lock: carrying the previous side
    // kept a once-flipped elbow inverted indefinitely. Continuity comes from turning
    // the carried bend toward the target about the reach axis (rate limited; held
    // while the hint is degenerate along the reach).
    auto pole=elbow_pole_from_hint(reach,hint,forward,outward);
    if(previous && finite(*previous)) {
        const auto dir=unit(reach);const auto p=*previous-dir*dot(*previous,dir);
        if(length(p)>1e-6) {
            const auto a=unit(p),h=unit(hint);
            double ang=std::atan2(dot(cross(a,pole),dir),dot(a,pole))*smooth01(length(h-dir*dot(h,dir))/.25);
            if(dt>0) ang=std::clamp(ang,-6.*std::min(dt,.1),6.*std::min(dt,.1));
            pole=unit(a*std::cos(ang)+cross(dir,a)*std::sin(ang));
        }
    }
    return pole;
}
} // namespace d2vr::rig
