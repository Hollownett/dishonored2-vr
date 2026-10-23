#pragma once
// B9: hand servo law (pure). Bonelab's hand is a physical body pulled toward the controller by clamped forces, with braking so it does not
// overshoot, a jerk filter, and an effective mass that includes what it holds. Same law in velocity form: desired velocity = error*gain, limited by
// max_speed and the braking profile sqrt(2*brake*|error|); then plan A's cap_velocity_step (arm axis = the target's forward row, mass hand_kg+held_kg);
// then a low-pass on the commanded acceleration. hand_kg and dt_max come from GrabTuning (no duplicate config).
// X-9: the hand uses UNSCALED caps (strength_scale forced to 1: the corpse scale must never reach the hand) and no mass floor (the 0.5 kg floor would
// double count on top of hand_kg), whatever the passed GrabTuning says.
#include "grab_force_cap.hpp"
#include <algorithm>
#include <cmath>

namespace d2vr {
struct HandServoConfig {double gain=18,max_speed=10,max_spin=20,brake_accel=40,jerk_tau=.02,snap_gap=.35,snap_seconds=.25,hand_radius=.08,held_lever=.3,max_gap=50,max_body_speed=100,snap_closing_mps=.5,tf_min=.05;};
struct HandServoState {Vec3 filtered_accel{};double gap_time{};bool has{};};
struct HandServoOutput {Vec3 linear{},angular{};bool capped{};bool snap{};double gap{};bool valid{};};

namespace hand_servo_detail {
// Finite, unit-length (+-2%), mutually orthogonal (+-0.02) and right handed basis.
inline bool finite_pose(const WorldTrackedPose& p) {
    if(!finite(p.position)) return false;
    for(const Vec3& b:p.basis) if(!finite(b)||std::fabs(length(b)-1)>.02) return false;
    return std::fabs(dot(p.basis[0],p.basis[1]))<=.02&&std::fabs(dot(p.basis[0],p.basis[2]))<=.02&&std::fabs(dot(p.basis[1],p.basis[2]))<=.02&&
           dot(cross(p.basis[0],p.basis[1]),p.basis[2])>0;
}
// Rotation vector (axis*angle) of R = sum_i t_i b_i^T, the rotation taking the body frame to the target frame.
inline Vec3 rotation_error(const WorldTrackedPose& body,const WorldTrackedPose& target) {
    double m[3][3]{};
    for(int i=0;i<3;++i) {
        const Vec3& t=target.basis[static_cast<size_t>(i)];const Vec3& b=body.basis[static_cast<size_t>(i)];
        const double tv[3]{t.x,t.y,t.z},bv[3]{b.x,b.y,b.z};
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) m[r][c]+=tv[r]*bv[c];
    }
    double cosv=std::clamp((m[0][0]+m[1][1]+m[2][2]-1)/2,-1.,1.);
    Vec3 s{m[2][1]-m[1][2],m[0][2]-m[2][0],m[1][0]-m[0][1]};   // 2 sin(angle) * axis
    double sl=length(s);
    if(cosv<-.999999||sl<1e-12) {
        if(cosv>0) return {};
        int k=0;   // near 180 degrees: axis from the largest diagonal of (R+I)/2
        for(int i=1;i<3;++i) if(m[i][i]>m[k][k]) k=i;
        double a[3];
        for(int r=0;r<3;++r) a[r]=(m[r][k]+(r==k?1:0))/2;
        Vec3 axis{a[0],a[1],a[2]};
        double n=length(axis);
        if(!(n>1e-9)) return {};
        axis=axis*(1/n);
        if(sl>1e-12&&dot(axis,s)<0) axis=axis*-1;   // same sign as the shortest-rotation axis just outside this band: no flip at the boundary
        return axis*3.14159265358979323846;
    }
    return s*(std::atan2(sl/2,cosv)/sl);
}
inline Vec3 limited(Vec3 v,double m) {double n=length(v);return n>m?v*(m/n):v;}
}

inline HandServoOutput servo_step(HandServoState& st,const WorldTrackedPose& target,const WorldTrackedPose& body,Vec3 body_velocity,Vec3 body_spin,
    double held_kg,double dt,double time_factor,const HandServoConfig& cfg,const GrabTuning& tuning,bool target_blocked) {
    using namespace hand_servo_detail;
    HandServoOutput out;
    Vec3 error=target.position-body.position;
    if(!(dt>0)||!std::isfinite(dt)||!finite_pose(target)||!finite_pose(body)||!finite(body_velocity)||!finite(body_spin)||!std::isfinite(held_kg)||
       !(length(body_velocity)<=cfg.max_body_speed)||!(length(body_spin)<=cfg.max_body_speed)||!(length(error)<=cfg.max_gap)) {
        st.gap_time=0;
        return out;
    }
    GrabTuning t=tuning;   // X-9: unscaled caps, no extra mass floor
    t.strength_scale=1;t.mass_floor_kg=0;
    dt=std::min(dt,t.dt_max);   // also the snap timer's dt: a 0.5 s hitch counts as dt_max toward snap_seconds (fails safe)
    // time_factor: unknown (NaN, <=0) is 1; otherwise [tf_min,1]. The loop runs in real-time units: the read-back velocities are divided by tf
    // (the body moves at the scaled velocity we wrote) and only the commanded output is multiplied by it.
    if(!std::isfinite(time_factor)||time_factor<=0) time_factor=1;
    time_factor=std::clamp(time_factor,cfg.tf_min,1.);
    body_velocity=body_velocity*(1/time_factor);body_spin=body_spin*(1/time_factor);
    double gap=length(error);
    out.valid=true;out.gap=gap;
    // linear: desired velocity
    Vec3 want=limited(error*cfg.gain,cfg.max_speed);
    // Braking profile on the distance left after the jerk filter's lag (the body keeps moving while the filtered deceleration builds up).
    double closing=gap>1e-9?std::max(0.,dot(body_velocity,error)/gap):0.;
    double left=std::max(0.,gap-closing*(cfg.jerk_tau+dt));
    double kg=std::max(held_kg,0.);
    Vec3 axis=target.basis[0];
    double mass=t.hand_kg+std::min(kg,t.mass_ceiling_kg);
    // Braking uses the deceleration the force cap can actually deliver along the error direction (axial/lateral ellipse), never more than brake_accel.
    double decel=cfg.brake_accel;
    if(gap>1e-9) {
        double ca=std::min(1.,std::fabs(dot(error,axis))/gap);
        double inv=ca*ca/(t.axial_n*t.axial_n)+(1-ca*ca)/(t.lateral_n*t.lateral_n);
        decel=std::min(decel,.6/std::sqrt(inv)/mass);   // 0.6: margin for the discrete step and filter lag
    }
    want=limited(want,std::sqrt(2*decel*left));
    // angular: desired spin
    Vec3 rot=rotation_error(body,target);
    double angle=length(rot);
    Vec3 want_w=angle>0?rot*(std::min({angle*cfg.gain,cfg.max_spin,std::sqrt(2*cfg.brake_accel*angle)})/angle):Vec3{};
    // Torque cap inertia: the hand as a solid sphere plus the held body as a point mass at held_lever (arm length) from the hand. The hand alone is
    // light enough that the 30 N m cap is effectively uncapped (spin set by gain/max_spin); a heavy hold lags in rotation.
    double inertia=bone_inertia(t.hand_kg,cfg.hand_radius)+std::min(kg,t.mass_ceiling_kg)*cfg.held_lever*cfg.held_lever;
    CappedStep step=cap_velocity_step(body_velocity,body_spin,want,want_w,axis,kg,inertia,dt,t);
    // jerk filter on the commanded acceleration
    Vec3 accel=(step.linear-body_velocity)*(1/dt);
    double alpha=cfg.jerk_tau>0?dt/(cfg.jerk_tau+dt):1;
    st.filtered_accel=st.has?st.filtered_accel+(accel-st.filtered_accel)*alpha:accel*alpha;
    st.has=true;
    // The filter may delay an acceleration but never carry the velocity past what the capped law asked for (that is the overshoot).
    {
        Vec3 d=step.linear-body_velocity,f=st.filtered_accel*dt;
        double dd=dot(d,d);
        if(!(dd>1e-24)||dot(f,d)>dd) {st.filtered_accel=dd>1e-24?accel:Vec3{};f=st.filtered_accel*dt;}
    }
    Vec3 lin=limited(body_velocity+st.filtered_accel*dt,cfg.max_speed);
    out.linear=lin*time_factor;
    out.angular=limited(step.angular,cfg.max_spin)*time_factor;
    out.capped=step.linear_capped||step.angular_capped;
    // snap request: the gap stays beyond snap_gap for snap_seconds; never while the target is blocked (inside geometry)
    // and not while the hand is still closing on it (a fast sweep has a big steady lag but is not stuck): closing speed >= snap_closing_mps resets the timer.
    if(target_blocked||!(gap>cfg.snap_gap)||closing>=cfg.snap_closing_mps) st.gap_time=0;
    else {
        st.gap_time+=dt;
        out.snap=st.gap_time>=cfg.snap_seconds-1e-9;
    }
    return out;
}
}
