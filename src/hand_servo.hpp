#pragma once
// B9: hand servo law (pure). Bonelab's hand is a physical body pulled toward the controller by clamped forces, with braking so it does not
// overshoot, a jerk filter, and an effective mass that includes what it holds. Same law in velocity form: desired velocity = error*gain, limited by
// max_speed and the braking profile sqrt(2*brake*|error|); then plan A's cap_velocity_step (arm axis = the target's forward row, mass hand_kg+held_kg);
// then a low-pass on the commanded acceleration. hand_kg and dt_max come from GrabTuning (no duplicate config).
// X-9: the hand uses UNSCALED caps (strength_scale forced to 1: the corpse scale must never reach the hand) and no mass floor (the 0.5 kg floor would
// double count on top of hand_kg), whatever the passed GrabTuning says.
// The hand's lag behind a fast-moving controller (about 0.65 m at 6 m/s) is by design: the visible hand's closeness to the raw controller is owned by B15/B16.
// held_lever: per-call (default kDefaultHeldLever, 0.3 m = arm length): the held body is a point mass that far from the hand for the torque cap's inertia.
// B16 can pass the held body's real extent later; a non-finite or negative value falls back to the default.
// Snap timing is tick quantised: the snap lands within one tick of snap_seconds.
#include "grab_force_cap.hpp"
#include <algorithm>
#include <cmath>

namespace d2vr {
constexpr double kDefaultHeldLever=.3;
struct HandServoConfig {double gain=18,max_speed=10,max_spin=20,brake_accel=40,jerk_tau=.02,snap_gap=.35,snap_seconds=.25,hand_radius=.08,max_gap=50,max_body_speed=100,snap_closing_mps=.5,tf_min=.05;};
struct HandServoState {Vec3 filtered_accel{};double gap_time{};bool has{};Vec3 prev_ff{},tgt_prev{},tgt_vel{};double tgt_acc{},tgt_dt{};unsigned tgt_still{};bool tgt_has{},tgt_vel_ok{};};   // prev_ff/tgt_*: Fix B's stop handling
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
    double held_kg,double dt,double time_factor,const HandServoConfig& cfg,const GrabTuning& tuning,bool target_blocked,double held_lever=kDefaultHeldLever,Vec3 feedforward={}) {
    using namespace hand_servo_detail;
    HandServoOutput out;
    Vec3 error=target.position-body.position;
    if(!(dt>0)||!std::isfinite(dt)||!finite_pose(target)||!finite_pose(body)||!finite(body_velocity)||!finite(body_spin)||!std::isfinite(held_kg)||
       !(length(body_velocity)<=cfg.max_body_speed)||!(length(body_spin)<=cfg.max_body_speed)||!(length(error)<=cfg.max_gap)) {
        st.gap_time=0;st.prev_ff={};st.tgt_has=false;
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
    // Fix B (running): the target rides on the player, so it moves at the player's velocity with no error at all. The desired velocity is that
    // feedforward plus the existing error law; the law itself runs on the velocity relative to it (the body at the player's speed is not
    // "closing" on a target that moves with it), and the speed limit becomes max_speed+|feedforward|. Zero (or non-finite) feedforward is the old law, bit for bit.
    // Stop handling (the estimator lags a stop by 0.1-0.2 s, and a body at full speed with no gap would coast on the decel cap): the feedforward is
    // gated by the target's own observed velocity (the component of it along the feedforward, 0..1 of it: a stopped target carries none), and when the
    // feedforward falls, that same velocity is dropped from the body at once, symmetric to adding it (the reference is the larger of the old and new).
    // No pose is smoothed: the target and the written pose are untouched.
    if(!finite(feedforward)||length(feedforward)>cfg.max_body_speed) feedforward={};
    if(!st.tgt_has) {st.tgt_prev=target.position;st.tgt_has=true;st.tgt_acc=0;st.tgt_vel={};st.tgt_vel_ok=false;st.tgt_dt=0;st.tgt_still=0;}
    else {
        st.tgt_acc+=dt;   // the dispatcher ticks faster than the target updates: the velocity is over the interval between target changes
        const Vec3 d=target.position-st.tgt_prev;
        if(d.x!=0||d.y!=0||d.z!=0) {st.tgt_vel=d*(1/st.tgt_acc);st.tgt_prev=target.position;st.tgt_dt=std::max(std::min(st.tgt_acc,.05),st.tgt_dt*.9);   // the longest recent update interval (decaying): a jittery frame is not a stop
            st.tgt_acc=0;st.tgt_still=0;st.tgt_vel_ok=true;}
        else if(++st.tgt_still>=2&&st.tgt_acc>=1.5*st.tgt_dt) {st.tgt_vel={};st.tgt_vel_ok=true;}   // 2+ unchanged ticks and 1.5 intervals: the target stopped (one repeated frame or a tracking hitch is not a stop)
    }
    if(feedforward.x!=0||feedforward.y!=0||feedforward.z!=0) {
        const double f2=dot(feedforward,feedforward);
        feedforward=st.tgt_vel_ok?feedforward*std::clamp(dot(st.tgt_vel,feedforward)/f2,0.,1.):Vec3{};
    }
    const Vec3 ff_ref=length(st.prev_ff)>length(feedforward)?st.prev_ff:feedforward;
    st.prev_ff=feedforward;
    const bool ff=ff_ref.x!=0||ff_ref.y!=0||ff_ref.z!=0;
    if(ff) body_velocity=body_velocity-ff_ref;
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
    // Torque cap inertia: the hand as a solid sphere plus the held body as a point mass at held_lever from the hand. The hand alone is light enough
    // that the 30 N m cap is effectively uncapped (spin set by gain/max_spin); a heavy hold lags in rotation.
    if(!std::isfinite(held_lever)||held_lever<0) held_lever=kDefaultHeldLever;
    double inertia=bone_inertia(t.hand_kg,cfg.hand_radius)+std::min(kg,t.mass_ceiling_kg)*held_lever*held_lever;
    // Angular braking uses the deceleration the torque cap can deliver for this inertia (0.6 margin, like the linear side), on the angle left after
    // one tick of the current closing spin.
    double adecel=std::min(cfg.brake_accel,.6*t.torque_cap()/inertia);
    double aclosing=angle>1e-9?std::max(0.,dot(body_spin,rot)/angle):0.;
    double aleft=std::max(0.,angle-aclosing*dt);
    Vec3 want_w=angle>0?rot*(std::min({angle*cfg.gain,cfg.max_spin,std::sqrt(2*adecel*aleft)})/angle):Vec3{};
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
    if(ff) lin=lin+feedforward;   // the relative velocity was limited to max_speed above: the written one to max_speed+|feedforward|
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
