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
struct TowInput {Vec3 target{},pelvis{},pelvis_velocity{};double bind_reach{},pelvis_kg{},dt{};bool limb_saturated{};const GrabTuning* tuning{};};
// write: apply `linear` (absolute pelvis velocity, z = 0) with zero angular velocity. bind_reach: store back as the lane's tow reach.
struct TowOutput {bool write{};Vec3 linear{};double bind_reach{};bool disabled_saturated{};};

inline TowOutput plan_tow(const TowInput& in) {
    TowOutput out;
    out.bind_reach=std::isfinite(in.bind_reach)?in.bind_reach:0;
    if(!in.tuning || !in.tuning->tow) return out;
    if(!finite(in.target) || !finite(in.pelvis) || !finite(in.pelvis_velocity) || !std::isfinite(in.pelvis_kg) || !std::isfinite(in.dt) || !(in.dt>0)) return out;
    Vec3 pull=in.target-in.pelvis;pull.z=0;
    const double reach=length(pull);
    if(!std::isfinite(reach)) return out;
    if(!(in.bind_reach>0) || !std::isfinite(in.bind_reach)) {out.bind_reach=std::clamp(reach,.4,1.4);return out;}
    if(reach<=in.bind_reach+.05) return out;
    if(in.limb_saturated) {out.disabled_saturated=true;return out;}
    // A stale frame (raw dt beyond dt_max) scales the catch-up by dt_max/dt, the same rule as PhysicalGrab::corpse_motor.
    const double catch_up=in.dt>in.tuning->dt_max?in.tuning->dt_max/in.dt:1.;
    const double speed=std::min((reach-in.bind_reach)*6*catch_up,4.);
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
