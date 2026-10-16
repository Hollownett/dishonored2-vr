#pragma once
#include "physical_grab.hpp"

namespace d2vr {
inline WorldTrackedPose grip_compose(const WorldTrackedPose& parent,const WorldTrackedPose& local) {
    WorldTrackedPose result{};result.position=parent.position+grab_world(parent,local.position);
    for(unsigned axis=0;axis<3;++axis)result.basis[axis]=grab_world(parent,local.basis[axis]);
    return result;
}
inline WorldTrackedPose grip_inverse(const WorldTrackedPose& pose) {
    WorldTrackedPose result{};
    result.basis={Vec3{pose.basis[0].x,pose.basis[1].x,pose.basis[2].x},
        Vec3{pose.basis[0].y,pose.basis[1].y,pose.basis[2].y},Vec3{pose.basis[0].z,pose.basis[1].z,pose.basis[2].z}};
    result.position=grab_local(pose,pose.position)*-1.;return result;
}
// Generations are supplied by a native body/geometry lease owner, never inferred from age or a body handle alone.
struct GripBodyId {
    std::uintptr_t world{},body_owner{},shape_owner{};
    std::uint32_t body_handle{0xffffffffu},shape_key{0xffffffffu};
    std::uint64_t body_generation{},geometry_generation{};
    bool valid() const {return world && body_owner && shape_owner && (body_handle&0xffffffu)!=0xffffffu && body_generation && geometry_generation;}
};
inline bool operator==(const GripBodyId& a,const GripBodyId& b) {
    return a.world==b.world && a.body_owner==b.body_owner && a.shape_owner==b.shape_owner && a.body_handle==b.body_handle &&
        a.shape_key==b.shape_key && a.body_generation==b.body_generation && a.geometry_generation==b.geometry_generation;
}
inline bool operator!=(const GripBodyId& a,const GripBodyId& b) {return !(a==b);}
inline bool grip_same_body(const GripBodyId& a,const GripBodyId& b) {
    return a.world==b.world && a.body_owner==b.body_owner && a.body_handle==b.body_handle && a.body_generation==b.body_generation;
}

// Pure frame math. Capturing this anchor does not establish native contact permission or lifetime.
class GripAnchor {
    GripBodyId body_{};
    WorldTrackedPose body_from_hand_{};
    Vec3 body_contact_{},hand_contact_{},body_normal_{};
    GripAnchor()=default;
public:
    static std::optional<GripAnchor> capture(const GripBodyId& body,const WorldTrackedPose& actual_body,
            const WorldTrackedPose& actual_hand,Vec3 contact,Vec3 normal) {
        if(!body.valid() || !grab_pose_valid(actual_body) || !grab_pose_valid(actual_hand) || !finite(contact) || !finite(normal) ||
           std::abs(length(normal)-1.)>.01 || length(contact-actual_hand.position)>.35)return {};
        GripAnchor result;result.body_=body;
        result.body_from_hand_=grip_compose(grip_inverse(actual_body),actual_hand);
        result.body_contact_=grab_local(actual_body,contact-actual_body.position);
        result.hand_contact_=grab_local(actual_hand,contact-actual_hand.position);
        result.body_normal_=grab_local(actual_body,normal);
        if(!grab_pose_valid(result.body_from_hand_) || !finite(result.body_contact_) || !finite(result.hand_contact_) || !finite(result.body_normal_))return {};
        return result;
    }
    const GripBodyId& body() const {return body_;}
    const WorldTrackedPose& body_from_hand() const {return body_from_hand_;}
    Vec3 contact_in_body() const {return body_contact_;}
    Vec3 contact_in_hand() const {return hand_contact_;}
    Vec3 normal_in_body() const {return body_normal_;}
    bool same(const GripAnchor& other) const {
        const auto equal=[](Vec3 a,Vec3 b){return a.x==b.x && a.y==b.y && a.z==b.z;};
        if(body_!=other.body_ || !equal(body_contact_,other.body_contact_) || !equal(hand_contact_,other.hand_contact_) ||
           !equal(body_normal_,other.body_normal_) || !equal(body_from_hand_.position,other.body_from_hand_.position))return false;
        for(unsigned axis=0;axis<3;++axis)if(!equal(body_from_hand_.basis[axis],other.body_from_hand_.basis[axis]))return false;
        return true;
    }
    std::optional<WorldTrackedPose> hand_target(const GripBodyId& body,const WorldTrackedPose& actual_body) const {
        if(body!=body_ || !grab_pose_valid(actual_body))return {};
        const auto result=grip_compose(actual_body,body_from_hand_);return grab_pose_valid(result)?std::optional<WorldTrackedPose>{result}:std::nullopt;
    }
    std::optional<WorldTrackedPose> body_target(const WorldTrackedPose& raw_hand) const {
        if(!grab_pose_valid(raw_hand))return {};
        const auto result=grip_compose(raw_hand,grip_inverse(body_from_hand_));return grab_pose_valid(result)?std::optional<WorldTrackedPose>{result}:std::nullopt;
    }
    Vec3 body_contact(const WorldTrackedPose& actual_body) const {return actual_body.position+grab_world(actual_body,body_contact_);}
    Vec3 hand_contact(const WorldTrackedPose& hand) const {return hand.position+grab_world(hand,hand_contact_);}
    Vec3 normal(const WorldTrackedPose& actual_body) const {return grab_world(actual_body,body_normal_);}
};

struct GripBodyMotion {
    GripBodyId id{};
    WorldTrackedPose pose{};
    Vec3 centre_of_mass{},linear_velocity{},angular_velocity{};
    double inverse_mass{};
    Vec3 inverse_inertia{}; // principal inverse inertia about centre_of_mass
    std::array<Vec3,3> inertia_axes{{{1,0,0},{0,1,0},{0,0,1}}}; // principal axes in body pose basis
    bool valid() const {
        return id.valid() && grab_pose_valid(pose) && finite(centre_of_mass) && finite(linear_velocity) && finite(angular_velocity) &&
            std::isfinite(inverse_mass) && inverse_mass>=0 && finite(inverse_inertia) &&
            inverse_inertia.x>=0 && inverse_inertia.y>=0 && inverse_inertia.z>=0 && grab_pose_valid({{},inertia_axes}) &&
            ((inverse_mass==0 && length(inverse_inertia)==0) || (inverse_mass>0 && inverse_inertia.x>0 && inverse_inertia.y>0 && inverse_inertia.z>0));
    }
};
inline Vec3 grip_inverse_inertia(const GripBodyMotion& body,Vec3 torque) {
    const auto local=grab_local(body.pose,torque);
    return grab_world(body.pose,body.inertia_axes[0]*(dot(body.inertia_axes[0],local)*body.inverse_inertia.x)+
        body.inertia_axes[1]*(dot(body.inertia_axes[1],local)*body.inverse_inertia.y)+
        body.inertia_axes[2]*(dot(body.inertia_axes[2],local)*body.inverse_inertia.z));
}
struct GripTrackingMotion {WorldTrackedPose pose{};Vec3 linear_velocity{},angular_velocity{};};
struct GripDriveSettings {
    double position_stiffness=400.,position_damping=30.,rotation_stiffness=12.,rotation_damping=2.;
    double maximum_force=80.,maximum_torque=8.;
    double maximum_acceleration=60.,maximum_angular_acceleration=120.;
    bool valid() const {
        for(const auto value:{position_stiffness,position_damping,rotation_stiffness,rotation_damping,maximum_force,maximum_torque,
                maximum_acceleration,maximum_angular_acceleration})
            if(!std::isfinite(value) || value<0)return false;
        return maximum_force>0 && maximum_torque>0 && maximum_acceleration>0 && maximum_angular_acceleration>0;
    }
};
struct GripDrive {
    GripBodyId body{};
    Vec3 contact{},target_contact{},force{},torque{},hand_reaction_origin{},hand_reaction_force{},hand_reaction_torque{};
};
// Same surface anchor for a prop, a ragdoll part, or a static support. No entity names, radii, bone lists or carry offsets.
inline std::optional<GripDrive> grip_drive(const GripAnchor& anchor,const GripBodyMotion& body,const GripTrackingMotion& raw,
        const GripDriveSettings& settings={}) {
    if(!body.valid() || body.id!=anchor.body() || !grab_pose_valid(raw.pose) || !finite(raw.linear_velocity) ||
       !finite(raw.angular_velocity) || !settings.valid())return {};
    const auto target_body=anchor.body_target(raw.pose),actual_hand=anchor.hand_target(body.id,body.pose);if(!target_body || !actual_hand)return {};
    GripDrive result{};result.body=body.id;result.contact=anchor.body_contact(body.pose);result.target_contact=anchor.hand_contact(raw.pose);
    const auto body_arm=result.contact-body.centre_of_mass,hand_arm=result.target_contact-raw.pose.position;
    const auto velocity=body.linear_velocity+cross(body.angular_velocity,body_arm);
    const auto target_velocity=raw.linear_velocity+cross(raw.angular_velocity,hand_arm);
    const auto force_limit=body.inverse_mass>0?std::min(settings.maximum_force,settings.maximum_acceleration/body.inverse_mass):settings.maximum_force;
    result.force=grab_limit((result.target_contact-result.contact)*settings.position_stiffness+
        (target_velocity-velocity)*settings.position_damping,force_limit);
    auto couple=grab_limit(grab_rotation_error(body.pose,*target_body)*settings.rotation_stiffness+
        (raw.angular_velocity-body.angular_velocity)*settings.rotation_damping,settings.maximum_torque);
    // Limit the couple; the off-centre contact force retains its physical lever arm on both bodies.
    result.torque=couple+cross(body_arm,result.force);
    const auto angular_acceleration=length(grip_inverse_inertia(body,result.torque));
    if(!std::isfinite(angular_acceleration))return {};
    if(angular_acceleration>settings.maximum_angular_acceleration) {
        const auto scale=settings.maximum_angular_acceleration/angular_acceleration;
        result.force=result.force*scale;couple=couple*scale;result.torque=result.torque*scale;
    }
    result.hand_reaction_origin=actual_hand->position;
    result.hand_reaction_force=result.force*-1.;
    result.hand_reaction_torque=couple*-1.+cross(result.contact-result.hand_reaction_origin,result.hand_reaction_force);
    if(!finite(result.force) || !finite(result.torque) || !finite(result.hand_reaction_torque))return {};
    return result;
}
struct GripBodyLimits {
    double maximum_acceleration=60.,maximum_angular_acceleration=120.;
    bool valid() const {return std::isfinite(maximum_acceleration) && maximum_acceleration>0 &&
        std::isfinite(maximum_angular_acceleration) && maximum_angular_acceleration>0;}
};
struct GripVelocityStep {Vec3 linear{},angular{};double wrench_scale=1.;};
// Adapter math only: caller retains each drive's own surface lease and supplies verified body motion and dt.
// Apply wrench_scale to each hand reaction too; the combined body safety limit must preserve reaction balance.
template<std::size_t Count> inline std::optional<GripVelocityStep> grip_velocity_step(const GripBodyMotion& body,
        const std::array<std::optional<GripDrive>,Count>& drives,double dt,const GripBodyLimits& limits={}) {
    if(!body.valid() || !std::isfinite(dt) || dt<=0 || dt>.05 || !limits.valid())return {};
    Vec3 force{},torque{};
    for(const auto& drive:drives)if(drive) {
        if(!drive->body.valid() || !grip_same_body(drive->body,body.id) || !finite(drive->force) || !finite(drive->torque))return {};
        force=force+drive->force;torque=torque+drive->torque;
    }
    if(!finite(force) || !finite(torque))return {};
    if(body.inverse_mass==0)return GripVelocityStep{body.linear_velocity,body.angular_velocity};
    const auto acceleration=grip_inverse_inertia(body,torque);
    const auto linear_acceleration=length(force)*body.inverse_mass,angular_acceleration=length(acceleration);
    if(!std::isfinite(linear_acceleration) || !std::isfinite(angular_acceleration))return {};
    double scale=1.;
    if(linear_acceleration>limits.maximum_acceleration)scale=limits.maximum_acceleration/linear_acceleration;
    if(angular_acceleration>limits.maximum_angular_acceleration)scale=std::min(scale,limits.maximum_angular_acceleration/angular_acceleration);
    GripVelocityStep result{body.linear_velocity+force*(body.inverse_mass*dt*scale),body.angular_velocity+acceleration*(dt*scale),scale};
    if(!finite(result.linear) || !finite(result.angular))return {};
    return result;
}
inline std::optional<GripVelocityStep> grip_velocity_step(const GripBodyMotion& body,const GripDrive& drive,double dt,const GripBodyLimits& limits={}) {
    return grip_velocity_step(body,std::array<std::optional<GripDrive>,1>{drive},dt,limits);
}
}
