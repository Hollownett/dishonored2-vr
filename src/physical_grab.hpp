#pragma once
#include "tracked_pose.hpp"
#include "tracked_pose_lifetime.hpp"
#include "dwell_timer.hpp"
#include "grab_force_cap.hpp"
#include "release_reason.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace d2vr {
inline constexpr std::uint64_t grab_pose_freshness_ms=tracked_pose_freshness_ms;
// Samples closer than this share a clock quantum; their motion waits for the next spaced sample instead of being dropped.
inline constexpr std::uint64_t grab_velocity_min_ms=8;
inline bool grab_pose_valid(const WorldTrackedPose& pose) {
    if(!finite(pose.position)) return false;
    for(unsigned i=0;i<3;++i) {
        if(!finite(pose.basis[i]) || std::abs(length(pose.basis[i])-1)>.02) return false;
        for(unsigned j=i+1;j<3;++j) if(std::abs(dot(pose.basis[i],pose.basis[j]))>.02) return false;
    }
    return dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])>.98;
}
inline Vec3 grab_local(const WorldTrackedPose& hand,Vec3 v) {
    return {dot(v,hand.basis[0]),dot(v,hand.basis[1]),dot(v,hand.basis[2])};
}
inline Vec3 grab_world(const WorldTrackedPose& hand,Vec3 v) {
    return hand.basis[0]*v.x+hand.basis[1]*v.y+hand.basis[2]*v.z;
}
inline Vec3 grab_limit(Vec3 v,double limit) {
    const auto n=length(v);return n>limit?v*(limit/n):v;
}
inline Vec3 grab_rotation_error(const WorldTrackedPose& object,const WorldTrackedPose& goal) {
    // Quaternion extraction preserves a correction at a half turn, where
    // summing crossed basis vectors incorrectly gives zero angular velocity.
    double r[3][3]{};
    for(unsigned i=0;i<3;++i) {
        const double a[3]{goal.basis[i].x,goal.basis[i].y,goal.basis[i].z};
        const double b[3]{object.basis[i].x,object.basis[i].y,object.basis[i].z};
        for(unsigned row=0;row<3;++row) for(unsigned col=0;col<3;++col) r[row][col]+=a[row]*b[col];
    }
    double q[4]{};const auto trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0) {
        const auto s=2*std::sqrt(1+trace);q[3]=s*.25;
        q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s;
    } else {
        unsigned i=0;if(r[1][1]>r[i][i]) i=1;if(r[2][2]>r[i][i]) i=2;
        const unsigned j=(i+1)%3,k=(i+2)%3;
        const auto s=2*std::sqrt(std::max(0.,1+r[i][i]-r[j][j]-r[k][k]));
        if(s<1e-9) return {};
        q[i]=s*.25;q[j]=(r[i][j]+r[j][i])/s;q[k]=(r[i][k]+r[k][i])/s;q[3]=(r[k][j]-r[j][k])/s;
    }
    if(q[3]<0) for(auto& v:q) v=-v;
    const Vec3 axis{q[0],q[1],q[2]};const auto n=length(axis);
    return n<1e-9?axis*2:axis*(2*std::atan2(n,q[3])/n);
}
struct GrabMotor {Vec3 linear{},angular{};bool release{};};
// Input for PhysicalGrab::corpse_motor. corpse=false is a prop: the force cap only, with motor()'s 1.5 m release and throw.
struct CorpseMotorInput {Vec3 body_velocity{},body_spin{};bool velocity_known{},corpse{true};
    bool state_changed{};   // caller saw the entity change state this tick; wins over an open fist (the release reason is state_changed)
    double object_kg{},inertia_kgm2{},share{1.},dt{};const GrabTuning* tuning{};};
// What Task 10 needs per write: the command, the cap fraction (share of the commanded velocity change removed), the capped flag,
// the implied force, and the release reason (none while held).
// write=false: nothing to write this frame (invalid dt); the hold continues. A non-finite input or result releases with
// ReleaseReason::fault (a zero-velocity release; the caller writes nothing). cap_fraction is the share of the LINEAR velocity
// change removed by the cap (angular share only when the linear change is zero).
struct CorpseMotor {GrabMotor motor;double lag_m{};bool capped{};double cap_fraction{};double implied_force_n{};ReleaseReason reason{ReleaseReason::none};bool write{true};};
// The grasp point kept for the drawn hand to blend out after a release (A-12): weight falls 1 -> 0 over anchor_blend_s.
struct ReleasedAnchor {Vec3 position{};double weight{};std::uint64_t released_at{};};
// A velocity motor keeps the native body dynamic, so collision/gravity remain
// native and tracking loss cannot leave a body in a changed motion mode.
class PhysicalGrab {
    bool armed_=false,pressed_=false,held_=false,valid_sample_=false;
    std::uint64_t epoch_{},tick_{},velocity_tick_{};
    WorldTrackedPose previous_{},relative_{};
    // Velocity has its own baseline so same-quantum samples keep their motion while previous_/tick_ track the newest pose.
    Vec3 velocity_{},velocity_origin_{};
    // Hand-to-limb contact point in the object's own frame (snatch grab: the bind stores it, the first command is zero).
    Vec3 contact_local_{};
    DwellTimer lag_timer_{lag_release_s};
    ReleaseReason reason_=ReleaseReason::none;
    bool forced_release_=false,has_released_=false;
    std::uint64_t release_tick_{};
    Vec3 last_anchor_{};
    double recovery_s_=0;   // stale-frame recovery window left (s); 0 = normal play. Reset on bind and release.
    void note_release(ReleaseReason reason,bool forced) {
        recovery_s_=0;reason_=reason;forced_release_=forced;release_tick_=tick_;has_released_=true;
    }
public:
    // Corpse lag rule (Bonelab's hand lags a blocked body, then lets go): the grasp point trailing its target by more than
    // lag_release_m for lag_release_s releases; a forced release blocks a regrab for regrab_cooldown_s (a flapping grip).
    static constexpr double lag_release_m=.5,lag_release_s=.25,regrab_cooldown_s=.3,anchor_blend_s=.25;
    bool pressed() const {return pressed_;}
    // A squeeze the game owned at its press, handed to the physics hand mid-press (a yanked or woken body under it): the
    // grip already held latches on the next valid sample, as if pressed fresh.
    void arm() noexcept {armed_=true;}
    bool held() const {return held_;}
    const Vec3& velocity() const {return velocity_;}
    double recovery_s() const {return recovery_s_;}
    bool sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick) {
        const bool valid_input=usable && epoch && tick && grab_pose_valid(hand) && std::isfinite(grip) && grip>=0 && grip<=1;
        if(!valid_input || epoch!=epoch_ || tick<tick_ || (tick_ && tick-tick_>grab_pose_freshness_ms)) {
            // A fresh released sample is sufficient to rearm at a new epoch;
            // held grips and unusable/reset samples never acquire on return.
            armed_=valid_input && epoch!=epoch_ && grip<.35f && (!tick_ || tick>=tick_);
            valid_sample_=false;pressed_=false;velocity_={};epoch_=epoch;tick_=tick;previous_=hand;velocity_origin_=hand.position;velocity_tick_=tick;return false;
        }
        const auto displacement=hand.position-previous_.position;
        // Check discontinuities even at duplicate or slow accepted timestamps,
        // before a released grip can convert old movement into a throw.
        if(!(length(displacement)<=.5)) {valid_sample_=false;armed_=pressed_=false;velocity_={};tick_=tick;previous_=hand;velocity_origin_=hand.position;velocity_tick_=tick;return false;}
        // Samples inside one clock quantum leave velocity (and its baseline) alone; their motion enters the next spaced sample.
        if(tick-velocity_tick_>=grab_velocity_min_ms) {
            const auto dt=double(tick-velocity_tick_)*.001;
            const double weight=1-std::exp(-dt/.035);
            velocity_=velocity_*(1-weight)+grab_limit((hand.position-velocity_origin_)*(1/dt),12)*weight;
            velocity_origin_=hand.position;velocity_tick_=tick;
        }
        if(grip<.35f) {armed_=true;pressed_=false;}
        else if(armed_ && grip>=.45f) pressed_=true;
        valid_sample_=true;tick_=tick;previous_=hand;return true;
    }
    // While the press waits for its owner (a native interactable's query, e.g. a corpse's carry, plus the obstruction check:
    // ~250 ms in sim v400), keep the sample fresh without touching the press latch. Otherwise the wait outlasts the freshness
    // window, the next sample resets, and a grip already held can never press.
    void keep_fresh(const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick) {
        if(!valid_sample_ || epoch!=epoch_ || tick<tick_ || tick-tick_>grab_pose_freshness_ms || !grab_pose_valid(hand) ||
           length(hand.position-previous_.position)>.5) return;
        tick_=tick;previous_=hand;velocity_origin_=hand.position;velocity_tick_=tick;
    }
    bool bind(const WorldTrackedPose& object) {return bind(object,object.position);}
    bool bind(const WorldTrackedPose& object,Vec3 contact,double maximum_contact=.3) {
        // A hand clock that went backwards (stale rebase) keeps the cooldown running until it passes the release tick again.
        if(forced_release_ && (tick_<release_tick_ || double(tick_-release_tick_)*.001<regrab_cooldown_s)) return false;
        if(!valid_sample_ || !pressed_ || held_ || !grab_pose_valid(object) || !finite(contact) ||
           !std::isfinite(maximum_contact) || maximum_contact<0 || maximum_contact>.55 ||
           length(contact-previous_.position)>maximum_contact || length(contact-object.position)>3) return false;
        relative_.position=grab_local(previous_,object.position-previous_.position);
        for(unsigned i=0;i<3;++i) relative_.basis[i]=grab_local(previous_,object.basis[i]);
        contact_local_=grab_local(object,contact-object.position);
        recovery_s_=0;lag_timer_.reset();has_released_=false;forced_release_=false;reason_=ReleaseReason::none;last_anchor_=contact;
        held_=true;return true;
    }
    // The grasped point: object origin plus the stored contact offset in the object's current frame (valid while held()).
    Vec3 anchor(const WorldTrackedPose& object) const {return object.position+grab_world(object,contact_local_);}
    ReleaseReason release_reason() const {return reason_;}
    // The last grasp point for the drawn hand to blend out over anchor_blend_s after a release; `tick` is the hand sample clock (ms).
    std::optional<ReleasedAnchor> released_anchor(std::uint64_t tick) const {
        if(!has_released_ || held_ || tick<release_tick_) return {};
        const auto age=double(tick-release_tick_)*.001;
        if(age>=anchor_blend_s) return {};
        return ReleasedAnchor{last_anchor_,1-age/anchor_blend_s,release_tick_};
    }
    WorldTrackedPose target() const {
        auto result=previous_;result.position=previous_.position+grab_world(previous_,relative_.position);
        for(unsigned i=0;i<3;++i) result.basis[i]=grab_world(previous_,relative_.basis[i]);
        return result;
    }
    // `reason`: open_fist only for a grip-open, lag for a blocked-carry release, lifecycle for a handoff or tracking loss (default).
    // Bookkeeping only happens when a hold actually ends, so releasing an idle lane keeps the cooldown, reason and anchor.
    GrabMotor release(bool throw_velocity,ReleaseReason reason=ReleaseReason::lifecycle) {
        if(held_) note_release(reason,false);
        held_=false;return {throw_velocity && valid_sample_?velocity_:Vec3{}, {},true};
    }
    GrabMotor release_sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick,bool throw_velocity,ReleaseReason reason=ReleaseReason::lifecycle) {
        // Native grip release can arrive before the next motor callback.
        const bool accepted=sample(hand,grip,usable,epoch,tick);
        return release(throw_velocity && accepted,reason);
    }
    std::optional<GrabMotor> motor(const WorldTrackedPose& object,bool usable,std::uint64_t now=0,std::uint64_t source_tick=0) {
        if(!held_) return {};
        if(usable && valid_sample_ && grab_pose_valid(object)) last_anchor_=anchor(object);
        if(!usable || !valid_sample_ || !pressed_ || !grab_pose_valid(object)) {const bool open=usable && valid_sample_ && !pressed_;return release(open,open?ReleaseReason::open_fist:ReleaseReason::lifecycle);}
        const auto goal=target();const auto error=goal.position-object.position;
        if(length(error)>1.5) return release(valid_sample_ && pressed_,ReleaseReason::lag); // fast swing outran the motor: throw, do not dead-drop // Wall blockage or an ownership discontinuity.
        const auto spin=grab_rotation_error(object,goal);
        const auto source=source_tick?std::min(source_tick,tick_):tick_;
        const auto age=now>=source?now-source:0;
        const auto feed=now?(now>=source?velocity_*std::exp(-double(age>32?age-32:0)*.001/.035):Vec3{}):velocity_;
        return GrabMotor{grab_limit(error*18+feed,10),grab_limit(spin*12,5),false};
    }
    std::optional<CorpseMotor> corpse_motor(const WorldTrackedPose& object,bool usable,std::uint64_t now,std::uint64_t source_tick,const CorpseMotorInput& in) {
        if(!held_) return {};
        const bool hand_ok=usable && valid_sample_ && grab_pose_valid(object);
        if(hand_ok) last_anchor_=anchor(object);
        auto finish=[&](ReleaseReason reason,bool forced,bool throw_velocity,double lag) {
            // A corpse release writes no velocity; a prop release is today's release(throw).
            CorpseMotor out;out.lag_m=lag;out.reason=reason;
            if(in.corpse) {held_=false;out.motor=GrabMotor{{},{},true};}
            else out.motor=release(throw_velocity,reason);   // props keep their old behaviour: no cooldown
            if(in.corpse) note_release(reason,forced);
            return out;
        };
        if(in.state_changed) return finish(ReleaseReason::state_changed,false,false,0);
        if(!hand_ok || !pressed_) {
            const bool open=usable && valid_sample_ && !pressed_;
            return finish(open?ReleaseReason::open_fist:ReleaseReason::lifecycle,false,open,0);
        }
        const GrabTuning fallback;const auto& tuning=in.tuning?*in.tuning:fallback;
        // Non-finite inputs never reach the engine: release as a fault (no velocity).
        if(!finite(in.body_velocity) || !finite(in.body_spin) || !std::isfinite(in.share) || !std::isfinite(in.object_kg) || !std::isfinite(in.inertia_kgm2))
            return finish(ReleaseReason::fault,false,false,0);
        // One validated, clamped dt feeds both the lag timer and the cap; an unusable dt writes nothing this frame.
        if(!(in.dt>0) || !std::isfinite(in.dt)) {CorpseMotor idle;idle.write=false;return idle;}
        const double dt=std::min(in.dt,tuning.dt_max);
        const auto goal=target();const auto error=goal.position-object.position;
        const auto lag=length(anchor(goal)-anchor(object));
        if(in.corpse) {if(lag_timer_.update(lag>lag_release_m,dt)) return finish(ReleaseReason::lag,true,false,lag);}
        else if(length(error)>1.5) return finish(ReleaseReason::lag,false,pressed_,lag);
        const auto spin=grab_rotation_error(object,goal);
        const auto source=source_tick?std::min(source_tick,tick_):tick_;
        const auto age=now>=source?now-source:0;
        const auto feed=now?(now>=source?velocity_*std::exp(-double(age>32?age-32:0)*.001/.035):Vec3{}):velocity_;
        // Stale frame (a game-thread hitch: raw dt beyond dt_max): the hand kept moving while no motor ran, so `error` holds the whole stall.
        // Closing it at the normal gain would command a jump (0.4 m x 18 = 7 m/s) and whip the chain; scale the error term by dt_max/dt so the
        // catch-up speed a stall can ask for stays what a normal frame could (smooth catch-up, as the hand lags and then closes in Bonelab).
        const double catch_up=in.dt>tuning.dt_max?tuning.dt_max/in.dt:1.;
        // The stale frame also opens a recovery window: the NORMAL frames that follow still see the leftover lag (a 0.5 s stall leaves ~0.3 m) and
        // would close it at the full gain (~6 m/s), whipping the chain. While recovering, the error-closure term alone is capped at catch_up_mps
        // (the hand's own feed velocity is not); recovery ends when the lag is under 5 cm or the window runs out. Outside recovery the law is unchanged.
        if(in.dt>tuning.dt_max) recovery_s_=tuning.catch_up_window_s;
        else if(recovery_s_>0) {recovery_s_-=in.dt;if(recovery_s_<=0 || lag<.05) recovery_s_=0;}
        auto closure=error*(18*catch_up);
        if(recovery_s_>0) {const double c=length(closure);if(c>tuning.catch_up_mps) closure=closure*(tuning.catch_up_mps/c);}
        const auto want_v=grab_limit(closure+feed,10),want_w=grab_limit(spin*12,5);
        // Unknown body velocity: step from rest (the caller counts the fallback).
        const Vec3 v0=in.velocity_known?in.body_velocity:Vec3{},w0=in.velocity_known?in.body_spin:Vec3{};
        const auto step=cap_velocity_step(v0,w0,want_v,want_w,previous_.basis[0],in.object_kg,in.inertia_kgm2,dt,tuning,in.share);
        if(!finite(step.linear) || !finite(step.angular)) return finish(ReleaseReason::fault,false,false,lag);
        const auto asked=length(want_v-v0)>1e-12?length(want_v-v0):length(want_w-w0),
            given=length(want_v-v0)>1e-12?length(step.linear-v0):length(step.angular-w0);
        CorpseMotor out;
        out.motor=GrabMotor{step.linear,step.angular,false};out.lag_m=lag;
        out.capped=step.linear_capped||step.angular_capped;
        out.cap_fraction=asked>1e-12?std::clamp(1-given/asked,0.,1.):0;
        out.implied_force_n=step.implied_force_n;
        return out;
    }
};
} // namespace d2vr
