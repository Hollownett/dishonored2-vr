#pragma once
// F2: force-capped velocity step for mod-driven grabbed bodies, and the build/grab-tuning.cfg loader.
// Technique (in our words, after Bonelab's force-limited hand): the hand may only pull with a bounded force, strongest
// along the forearm and weaker sideways, and the held body adds to the hand's effective mass. We can only write velocities,
// so the same law limits how much the body's velocity may change per motor tick: dv_max = F_max * dt / m_effective.
// All caps are multiplied by strength_scale (forgiving default 3: one hand lifts and swings a whole body).
// Defaults tuned in tools/corpse_drag_lab.cpp (Task 9): lift_fraction .5 -> .25 (the one-hand swing and the heavy hitch both pass only in a
// narrow window around it, a property of the lab model); tow is ON by default since Task 10 wired CorpseMotor.capped into limb_saturated.
// The lab model is optimistic (point masses, a stiff written velocity, no rotation, no Havok solver) and whether this FEELS right is a headset question
// (Tier C), not something the lab proves; catch_up_mps 1.0 ramping at catch_up_ramp_mps2 (see the lab) holds a 0.5 s hitch to 3.9 m/s (0.8 m/s drag) and 5.9 m/s (1.2 m/s drag) in the lab, against 11.9 and 16.6 with only the per-frame dt_max/dt scaling. The headset session retunes them in build/grab-tuning.cfg.
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace d2vr {
// Plan H1 (v603 Windows test: "make corpses 2 times easier to move"): the shipped strength_scale doubled, 3 -> 6. strength_scale multiplies the axial, lateral and
// torque caps together (and the tow's pelvis pull, which uses the same cap law), so every force limit doubles and the lab's hand lead at the cap halves.
// What stays put on purpose: the 10 m/s / 5 rad/s motor limits, the catch-up ramp (hitch / fling protection), the 4 m/s tow limit, the lag release, RunawayDetector,
// EntityLockout and the fault releases. The 3.0 below is the pre-H1 value: build/legacy-corpse-strength restores it.
constexpr double kLegacyCorpseStrengthScale=3.,kCorpseStrengthScale=2*kLegacyCorpseStrengthScale;
struct GrabTuning {double axial_n=300,lateral_n=90,torque_nm=30,strength_scale=kCorpseStrengthScale,hand_kg=2,mass_floor_kg=.5,mass_ceiling_kg=60,
    lift_fraction=.25,dt_max=1./30,
    stale_dt_s=.1,        // a frame is STALE (a game-thread hitch; enters recovery) only when its raw dt exceeds this; sustained low fps (dt 0.04) is not a hitch
    catch_up_mps=1.,      // recovery: the error-closure speed (not the hand's own feed) starts capped here ...
    catch_up_ramp_mps2=4.;   // ... and the cap rises by this per second, so recovery ends without a step (when the cap reaches the normal limit: 10 m/s linear, 4 m/s tow) or the lag is under 5 cm
    // The angular closure is capped at half the linear cap (the normal limits are 5 rad/s and 10 m/s), so it also ramps up to its normal limit with no jump.
    bool tow=true,use_measured_mass=false;   // tow ON: NativeGrab feeds plan_tow with limb_saturated from CorpseMotor.capped (Task 10); the lab judges this shipped default
    double axial_cap() const {return axial_n*strength_scale;} double lateral_cap() const {return lateral_n*strength_scale;} double torque_cap() const {return torque_nm*strength_scale;}};
enum class TuningSource {defaults,file};
struct TuningLoad {GrabTuning value;TuningSource source{TuningSource::defaults};std::string error;};

namespace grab_cap_detail {
inline bool parse_number(const std::string& text,double& out) {
    if(text.empty()) return false;
    char* end=nullptr;
    out=std::strtod(text.c_str(),&end);
    return end==text.c_str()+text.size() && std::isfinite(out);
}
inline bool in_range(double v,double lo,double hi) {return v>=lo && v<=hi;}
}

// File format: one "key value" per line; blank lines and lines starting with '#' are ignored. Any problem (unknown key,
// missing or non-finite value, out-of-bounds value, file over 4096 bytes) returns all defaults plus a message.
inline TuningLoad load_grab_tuning(const std::filesystem::path& file) noexcept {
    TuningLoad result;
    try {
        std::ifstream in(file,std::ios::binary);
        if(!in) return result;   // missing file is normal
        std::string text(4097,'\0');
        in.read(&text[0],4097);
        text.resize(static_cast<size_t>(in.gcount()));
        auto fail=[&](const std::string& why) {result.value=GrabTuning{};result.source=TuningSource::defaults;result.error=why;return result;};
        if(text.size()>4096) return fail("grab-tuning.cfg larger than 4096 bytes");
        GrabTuning t;
        size_t pos=0;int line_no=0;
        while(pos<=text.size()) {
            size_t eol=text.find('\n',pos);
            if(eol==std::string::npos) eol=text.size();
            std::string line=text.substr(pos,eol-pos);
            pos=eol+1;++line_no;
            size_t a=line.find_first_not_of(" \t\r");
            if(a==std::string::npos || line[a]=='#') continue;
            size_t b=line.find_first_of(" \t",a);
            std::string key=line.substr(a,b==std::string::npos?std::string::npos:b-a);
            std::string value;
            if(b!=std::string::npos) {
                size_t c=line.find_first_not_of(" \t",b);
                if(c!=std::string::npos) {size_t e=line.find_last_not_of(" \t\r");value=line.substr(c,e-c+1);}
            }
            std::string where=" (line "+std::to_string(line_no)+")";
            if(value.empty()) return fail("key without value: "+key+where);
            double v=0;
            if(!grab_cap_detail::parse_number(value,v)) return fail("bad number for "+key+where);
            auto ranged=[&](double& field,double lo,double hi) {
                if(!grab_cap_detail::in_range(v,lo,hi)) return false;
                field=v;return true;
            };
            bool ok=true;
            if(key=="axial_n") ok=ranged(t.axial_n,50,600);
            else if(key=="lateral_n") ok=ranged(t.lateral_n,20,300);
            else if(key=="torque_nm") ok=ranged(t.torque_nm,5,60);
            else if(key=="strength_scale") ok=ranged(t.strength_scale,.5,10);
            else if(key=="lift_fraction") ok=ranged(t.lift_fraction,0,1);
            else if(key=="mass_floor_kg") ok=ranged(t.mass_floor_kg,.1,5);
            else if(key=="mass_ceiling_kg") ok=ranged(t.mass_ceiling_kg,10,200);
            else if(key=="hand_kg") ok=ranged(t.hand_kg,.1,20);
            else if(key=="dt_max") ok=ranged(t.dt_max,.005,.1);
            else if(key=="stale_dt_s") ok=ranged(t.stale_dt_s,.034,1);
            else if(key=="catch_up_mps") ok=ranged(t.catch_up_mps,.2,10);
            else if(key=="catch_up_ramp_mps2") ok=ranged(t.catch_up_ramp_mps2,.5,100);
            else if(key=="tow") {ok=v==0||v==1;if(ok) t.tow=v==1;}
            else if(key=="use_measured_mass") {ok=v==0||v==1;if(ok) t.use_measured_mass=v==1;}
            else return fail("unknown key: "+key+where);
            if(!ok) return fail("value out of bounds for "+key+where);
        }
        if(t.stale_dt_s<t.dt_max) return fail("stale_dt_s must be >= dt_max");
        result.value=t;
        result.source=TuningSource::file;
    } catch(...) {
        result=TuningLoad{};
        result.error="grab-tuning.cfg could not be read";
    }
    return result;
}

// hand_kg + share * clamp(object_kg, floor, ceiling). A non-finite object mass counts as the ceiling (moves least).
inline double effective_mass(double object_kg,const GrabTuning& t,double share=1.) {
    double kg=std::isnan(object_kg)?t.mass_ceiling_kg:std::clamp(object_kg,t.mass_floor_kg,t.mass_ceiling_kg);
    return t.hand_kg+share*kg;
}
// Solid-sphere style estimate 0.4 m r^2, never below 1e-4 so the angular step stays finite.
inline double bone_inertia(double mass_kg,double radius_m) {
    double i=0.4*mass_kg*radius_m*radius_m;
    return std::isfinite(i)?std::max(i,1e-4):1e-4;
}

struct CappedStep {Vec3 linear{},angular{};bool linear_capped{},angular_capped{};double implied_force_n{},implied_torque_nm{};};

namespace grab_cap_detail {
// Limits |v| to max_len; returns true when it had to shrink.
inline bool limit(Vec3& v,double max_len) {
    double len=length(v);
    if(len<=max_len) return false;
    v=v*(max_len/len);
    return true;
}
}

inline CappedStep cap_velocity_step(Vec3 v_object,Vec3 w_object,Vec3 v_motor,Vec3 w_motor,Vec3 arm_axis,
    double object_kg,double inertia_kgm2,double dt,const GrabTuning& tuning,double share=1.) {
    CappedStep out;
    out.linear=v_object;out.angular=w_object;
    if(!(dt>0) || !std::isfinite(dt) || !finite(v_object) || !finite(w_object) || !finite(v_motor) || !finite(w_motor))
        return out;
    dt=std::min(dt,tuning.dt_max);
    double m=effective_mass(object_kg,tuning,share);
    double inertia=std::isfinite(inertia_kgm2)?std::max(inertia_kgm2,1e-4):1e-4;
    Vec3 delta=v_motor-v_object;
    Vec3 axial{},lateral=delta;
    if(finite(arm_axis)) {
        double n=length(arm_axis);
        if(std::fabs(n-1)<=.01) {
            Vec3 axis=arm_axis*(1/n);
            axial=axis*dot(delta,axis);
            lateral=delta-axial;
        }
    }
    bool ca=grab_cap_detail::limit(axial,tuning.axial_cap()*dt/m);
    bool cl=grab_cap_detail::limit(lateral,tuning.lateral_cap()*dt/m);
    Vec3 dv=axial+lateral;
    Vec3 dw=w_motor-w_object;
    out.angular_capped=grab_cap_detail::limit(dw,tuning.torque_cap()*dt/inertia);
    out.linear_capped=ca||cl;
    out.linear=v_object+dv;
    out.angular=w_object+dw;
    out.implied_force_n=m*length(dv)/dt;
    out.implied_torque_nm=inertia*length(dw)/dt;
    return out;
}
}
