#pragma once
// Corpse prep 5.4: pelvis tow planner (pure). When a limb is dragged, the pelvis is towed after it so the body follows, the way a
// held limb drags a ragdoll in Bonelab: the pull is capped by the pelvis mass (same force law as the limb motor), stays on the
// floor plane, and switches off while the limb motor is itself saturated (the hand already cannot move the limb, so towing the
// pelvis would only fight it). Behind GrabTuning::tow (default off until the headset comparison). Task 10 owns the write.
// pelvis_kg comes from the class table: ragdoll_class_kg[0] (grab_mass.hpp), ruling A-25.
#include "grab_force_cap.hpp"
#include <algorithm>
#include <cmath>

namespace d2vr {
// hand_velocity: the holding hand's world velocity (PhysicalGrab::velocity, the walk included). Its part away from the pelvis is fed forward,
// so a body dragged by a walking or sprinting player keeps up instead of trailing until the lag rule drops it (user, headset v627; corpse_drag_lab walk_*).
struct TowInput {Vec3 target{},pelvis{},pelvis_velocity{},hand_velocity{};double bind_reach{},pelvis_kg{},dt{},recovery_s{};bool limb_saturated{};const GrabTuning* tuning{};};
// write: apply `linear` (absolute pelvis velocity, z = 0) with zero angular velocity. bind_reach: store back as the lane's tow reach.
struct TowOutput {bool write{};Vec3 linear{};double bind_reach{},recovery_s{};bool disabled_saturated{};};
inline constexpr double tow_max_mps=8;   // a sprint (~7 m/s) with the body in tow; was 4 (a walk only)

inline TowOutput plan_tow(const TowInput& in) {
    TowOutput out;
    out.bind_reach=std::isfinite(in.bind_reach)?in.bind_reach:0;
    if(!in.tuning || !in.tuning->tow) return out;
    out.recovery_s=std::isfinite(in.recovery_s)&&in.recovery_s>0?in.recovery_s:0;   // carried by the caller's lane, like bind_reach
    if(!finite(in.target) || !finite(in.pelvis) || !finite(in.pelvis_velocity) || !std::isfinite(in.pelvis_kg) || !std::isfinite(in.dt) || !(in.dt>0)) return out;
    Vec3 pull=in.target-in.pelvis;pull.z=0;
    const double reach=length(pull);
    if(!std::isfinite(reach)) return out;
    // Stale-frame recovery (same rule as PhysicalGrab::corpse_motor): a stale dt starts a ramped cap on the closing speed;
    // it ends when the ramp reaches the normal limit or the pelvis is back within 5 cm of its reach.
    // The cap is catch_up_mps + catch_up_ramp_mps2 * t up to the normal tow limit (tow_max_mps); only a hitch (dt > stale_dt_s) enters it.
    if(out.recovery_s>0) {   // an active ramp keeps ramping through further stale frames (never re-armed); only an idle one arms
        out.recovery_s+=in.dt;if(in.tuning->catch_up_mps+in.tuning->catch_up_ramp_mps2*out.recovery_s>=tow_max_mps) out.recovery_s=0;
    } else if(in.dt>in.tuning->stale_dt_s) out.recovery_s=1e-9;
    if(!(in.bind_reach>0) || !std::isfinite(in.bind_reach)) {out.bind_reach=std::clamp(reach,.4,1.4);return out;}
    // The hand's speed away from the pelvis (along the pull, on the floor plane); a hand walking off inside the 5 cm band tows too, so the
    // feed does not stop and start the pelvis at the band's edge.
    Vec3 hand=finite(in.hand_velocity)?in.hand_velocity:Vec3{};hand.z=0;
    const double feed=reach>1e-9?std::max(0.,dot(hand,pull*(1/reach))):0.;
    if(reach<=in.bind_reach+.05 && !(reach>in.bind_reach && feed>.3)) {
        out.recovery_s=0;
        // The towed body coasting into the hand that stopped (it slid ~0.9 m past it in corpse_drag_lab walk_3ms): the hand holds it back,
        // its approach brought down to the hand's own speed away, within the same force cap.
        Vec3 current=in.pelvis_velocity;current.z=0;
        const Vec3 dir=reach>1e-9?pull*(1/reach):Vec3{};
        const double excess=dot(current,dir)-feed;
        if(reach<in.bind_reach-.05 && !in.limb_saturated && excess>.2) {
            const auto step=cap_velocity_step(current,{},current-dir*excess,{},dir,in.pelvis_kg,bone_inertia(in.pelvis_kg,.15),in.dt,*in.tuning);
            Vec3 v=step.linear;v.z=0;
            if(finite(v)) {out.write=true;out.linear=v;}
        }
        return out;
    }
    // A saturated limb (its own capped motor cannot reach its velocity) stops the catch-up, which would only fight it, but not the hand's walk:
    // the heavy body is exactly the one the tow must bring along (sim v632: the cap bound on 43 of 126 writes of a walking drag, each one
    // stood the tow down, and the body trailed until the lag rule dropped it).
    if(in.limb_saturated && feed<=.3) {out.disabled_saturated=true;return out;}
    // A stale frame (raw dt beyond dt_max) scales the catch-up by dt_max/dt, the same rule as PhysicalGrab::corpse_motor.
    const double catch_up=in.dt>in.tuning->dt_max?in.tuning->dt_max/in.dt:1.;
    double speed=std::min((in.limb_saturated?0.:std::max(0.,reach-in.bind_reach)*6*catch_up)+feed,tow_max_mps);
    if(out.recovery_s>0) speed=std::min(speed,in.tuning->catch_up_mps+in.tuning->catch_up_ramp_mps2*out.recovery_s);
    const Vec3 dir=pull*(1/reach);
    const Vec3 wanted=dir*speed;
    Vec3 current=in.pelvis_velocity;current.z=0;
    const auto step=cap_velocity_step(current,{},wanted,{},dir,in.pelvis_kg,bone_inertia(in.pelvis_kg,.15),in.dt,*in.tuning);
    Vec3 v=step.linear;v.z=0;
    if(!finite(v)) return out;
    out.write=true;out.linear=v;
    return out;
}
}
