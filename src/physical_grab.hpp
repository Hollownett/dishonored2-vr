#pragma once
#include "tracked_pose.hpp"
#include "tracked_pose_lifetime.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace d2vr {
inline constexpr std::uint64_t grab_pose_freshness_ms=tracked_pose_freshness_ms;
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
// A velocity motor keeps the native body dynamic, so collision/gravity remain
// native and tracking loss cannot leave a body in a changed motion mode.
class PhysicalGrab {
    bool armed_=false,pressed_=false,held_=false,valid_sample_=false;
    std::uint64_t epoch_{},tick_{};
    WorldTrackedPose previous_{},relative_{};
    Vec3 velocity_{};
public:
    bool pressed() const {return pressed_;}
    // A squeeze the game owned at its press, handed to the physics hand mid-press (a yanked or woken body under it): the
    // grip already held latches on the next valid sample, as if pressed fresh.
    void arm() noexcept {armed_=true;}
    bool held() const {return held_;}
    const Vec3& velocity() const {return velocity_;}
    bool sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick) {
        const bool valid_input=usable && epoch && tick && grab_pose_valid(hand) && std::isfinite(grip) && grip>=0 && grip<=1;
        if(!valid_input || epoch!=epoch_ || tick<tick_ || (tick_ && tick-tick_>grab_pose_freshness_ms)) {
            // A fresh released sample is sufficient to rearm at a new epoch;
            // held grips and unusable/reset samples never acquire on return.
            armed_=valid_input && epoch!=epoch_ && grip<.35f && (!tick_ || tick>=tick_);
            valid_sample_=false;pressed_=false;velocity_={};epoch_=epoch;tick_=tick;previous_=hand;return false;
        }
        const auto dt=double(tick-tick_)*.001;
        const auto displacement=hand.position-previous_.position;
        // Check discontinuities even at duplicate or slow accepted timestamps,
        // before a released grip can convert old movement into a throw.
        if(length(displacement)>.5) {valid_sample_=false;armed_=pressed_=false;velocity_={};tick_=tick;previous_=hand;return false;}
        if(dt>=.001) {
            const double weight=1-std::exp(-dt/.035);
            velocity_=velocity_*(1-weight)+grab_limit(displacement*(1/dt),12)*weight;
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
        tick_=tick;previous_=hand;
    }
    bool bind(const WorldTrackedPose& object) {return bind(object,object.position);}
    bool bind(const WorldTrackedPose& object,Vec3 contact,double maximum_contact=.3) {
        if(!valid_sample_ || !pressed_ || held_ || !grab_pose_valid(object) || !finite(contact) ||
           !std::isfinite(maximum_contact) || maximum_contact<0 || maximum_contact>.55 ||
           length(contact-previous_.position)>maximum_contact || length(contact-object.position)>3) return false;
        relative_.position=grab_local(previous_,object.position-previous_.position);
        for(unsigned i=0;i<3;++i) relative_.basis[i]=grab_local(previous_,object.basis[i]);
        held_=true;return true;
    }
    WorldTrackedPose target() const {
        auto result=previous_;result.position=previous_.position+grab_world(previous_,relative_.position);
        for(unsigned i=0;i<3;++i) result.basis[i]=grab_world(previous_,relative_.basis[i]);
        return result;
    }
    GrabMotor release(bool throw_velocity) {
        held_=false;return {throw_velocity && valid_sample_?velocity_:Vec3{}, {},true};
    }
    GrabMotor release_sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick,bool throw_velocity) {
        // Native grip release can arrive before the next motor callback.
        const bool accepted=sample(hand,grip,usable,epoch,tick);
        return release(throw_velocity && accepted);
    }
    std::optional<GrabMotor> motor(const WorldTrackedPose& object,bool usable,std::uint64_t now=0,std::uint64_t source_tick=0) {
        if(!held_) return {};
        if(!usable || !valid_sample_ || !pressed_ || !grab_pose_valid(object)) return release(usable && valid_sample_ && !pressed_);
        const auto goal=target();const auto error=goal.position-object.position;
        if(length(error)>1.5) return release(valid_sample_ && pressed_); // fast swing outran the motor: throw, do not dead-drop // Wall blockage or an ownership discontinuity.
        const auto spin=grab_rotation_error(object,goal);
        const auto source=source_tick?std::min(source_tick,tick_):tick_;
        const auto age=now>=source?now-source:0;
        const auto feed=now?(now>=source?velocity_*std::exp(-double(age>32?age-32:0)*.001/.035):Vec3{}):velocity_;
        return GrabMotor{grab_limit(error*18+feed,10),grab_limit(spin*12,5),false};
    }
};
} // namespace d2vr
