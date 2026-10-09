#pragma once
#include "rig_transform.hpp"
#include "../third_party/dishonored_vr/grab_pose.h"

namespace d2vr::rig {
struct ArmChain {unsigned upper{},elbow{},wrist{};};
struct PosedArm {std::array<Transform,69> joints;bool reach_clamped{};};
using ForearmHelpers=std::array<unsigned,4>;
// An elbow hint is a body-space direction, independent of the native weapon
// animation. Carry the previous bend through a pole singularity instead of
// switching between arbitrary Cartesian axes on adjacent tracking frames.
inline Vec3 anatomical_elbow_pole(Vec3 reach,Vec3 forward,Vec3 outward,Vec3 up,
                                  const Vec3* previous=nullptr) {
    if(!finite(reach) || !finite(forward) || !finite(outward) || !finite(up) || length(reach)<1e-6)
        throw std::invalid_argument("Invalid anatomical elbow frame");
    const auto direction=unit(reach);
    const auto project=[&](Vec3 v) {return v-direction*dot(v,direction);};
    const auto desired=unit(outward-forward-up);
    auto bend=project(desired);
    const auto magnitude=length(bend);
    if(previous && finite(*previous)) {
        const auto carried=project(*previous);
        if(length(carried)>1e-6 && dot(bend,carried)<0) bend=bend*-1;
        if(length(carried)>1e-6 && magnitude<.25) {
            const auto fallback=unit(carried);
            // The hint loses anatomical meaning at this pole. A smoothstep
            // keeps the carried bend authoritative while passing through it.
            const auto t=std::clamp(magnitude/.25,0.,1.);
            const auto weight=t*t*(3-2*t);
            bend=fallback*(1-weight)+bend*weight;
        }
    }
    if(length(bend)<1e-6) {
        bend=project(outward);
        if(length(bend)<1e-6) bend=project(forward);
    }
    if(!finite(bend) || length(bend)<1e-6) throw std::invalid_argument("Degenerate anatomical elbow frame");
    return unit(bend);
}
inline Quaternion align_direction(Vec3 from,Vec3 to) {
    const auto a_length=length(from),b_length=length(to);
    if(!finite(from) || !finite(to) || !std::isfinite(a_length) || !std::isfinite(b_length) || a_length<1e-6 || b_length<1e-6)
        throw std::invalid_argument("Invalid arm segment direction");
    from=from*(1/a_length);to=to*(1/b_length);
    const auto cosine=std::clamp(dot(from,to),-1.,1.);
    if(cosine<-1+1e-9) {
        const Vec3 axis=std::abs(from.x)<=std::abs(from.y) && std::abs(from.x)<=std::abs(from.z)?Vec3{1,0,0}:
            std::abs(from.y)<=std::abs(from.z)?Vec3{0,1,0}:Vec3{0,0,1};
        const auto turn=unit(cross(from,axis));return {turn.x,turn.y,turn.z,0};
    }
    const auto turn=cross(from,to);const double divisor=std::sqrt(2*(1+cosine));
    return normalized({turn.x/divisor,turn.y/divisor,turn.z/divisor,divisor*.5});
}
inline Transform turn_root(const Transform& root,Quaternion turn,Vec3 at) {
    Transform target=root;
    for(unsigned i=0;i<3;++i) {
        const auto direction=rotated(turn,column(root,i));
        target.f[i*4]=static_cast<float>(direction.x);target.f[i*4+1]=static_cast<float>(direction.y);target.f[i*4+2]=static_cast<float>(direction.z);
    }
    target.f[12]=static_cast<float>(at.x);target.f[13]=static_cast<float>(at.y);target.f[14]=static_cast<float>(at.z);
    if(!valid(target)) throw std::invalid_argument("Invalid posed arm root");
    return target;
}
// VRM roll-constraint decomposition: remove the minimum swing of the axis,
// then distribute only its residual roll. Quaternion decoding is upstream.
inline double forearm_roll(const Transform& from,const Transform& to,Vec3 axis) {
    if(!finite(axis) || std::abs(length(axis)-1)>.001) throw std::invalid_argument("Invalid roll axis");
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    const auto delta=rigid_retarget(identity,from,to);
    std::array<float,9> rows{};
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<3;++col) rows[row*3+col]=delta.f[col*4+row];
    const auto raw=dvr::grab::quat_from_rot(rows.data());
    auto q=normalized({raw.x,raw.y,raw.z,raw.w});
    if(q.w<0) q={-q.x,-q.y,-q.z,-q.w};
    const auto swing=align_direction(axis,rotated(q,axis));
    auto roll=normalized(multiplied({-swing.x,-swing.y,-swing.z,swing.w},q));
    if(roll.w<0) roll={-roll.x,-roll.y,-roll.z,-roll.w};
    return 2*std::atan2(dot({roll.x,roll.y,roll.z},axis),roll.w);
}
inline bool in_subtree(const ArmHierarchy& parents,unsigned joint,unsigned ancestor) {
    if(joint>=69 || ancestor>=69) return false;
    for(unsigned depth=0;depth<69;++depth) {
        if(joint==ancestor) return true;
        const auto parent=parents[joint];if(parent<0 || parent>=69) return false;
        joint=static_cast<unsigned>(parent);
    }
    return false;
}
inline Upload calibrated_arm_source(const Upload& native,const ArmHierarchy& parents,ArmChain chain,Vec3 shoulder,double limb_scale) {
    if(!valid_hierarchy(parents) || chain.upper>=69 || chain.wrist>=69 || !finite(shoulder) ||
        !std::isfinite(limb_scale) || limb_scale<.7 || limb_scale>1.45)
        throw std::invalid_argument("Invalid measured avatar arm calibration");
    const auto from=position(native.joints[chain.upper]),wrist=position(native.joints[chain.wrist]);
    auto result=native;
    for(unsigned joint=0;joint<69;++joint) {
        // Keep authored hand geometry its native size while fitting the two
        // limb segments and sleeve helpers to the measured player reach.
        // The root/clavicle also influence the sleeve cut: leaving them in
        // the animated position tears the calibrated upper arm away from it.
        const auto at=in_subtree(parents,joint,chain.wrist)?
            shoulder+(wrist-from)*limb_scale+position(native.joints[joint])-wrist:
            shoulder+(position(native.joints[joint])-from)*limb_scale;
        result.joints[joint].f[12]=static_cast<float>(at.x);
        result.joints[joint].f[13]=static_cast<float>(at.y);
        result.joints[joint].f[14]=static_cast<float>(at.z);
    }
    return result;
}
// Global/model-space transforms. The verified hierarchy selects the upper-arm,
// forearm and wrist subtrees. Finger animation stays relative to the new wrist;
// shoulder/clavicle ancestors remain attached. No game calls or memory writes.
inline PosedArm pose_arm(const Upload& native,const ArmHierarchy& parents,ArmChain chain,
                         Transform tracked_wrist,Vec3 elbow_pole,const ForearmHelpers* helpers=nullptr,bool follow_wrist=false,double maximum_extension=3) {
    if(!valid_hierarchy(parents) || chain.upper>=69 || chain.elbow>=69 || chain.wrist>=69 ||
       parents[chain.elbow]!=static_cast<int>(chain.upper) || parents[chain.wrist]!=static_cast<int>(chain.elbow) ||
       !valid(tracked_wrist)) throw std::invalid_argument("Invalid arm chain or wrist target");
    for(const auto& joint:native.joints) if(!valid(joint)) throw std::invalid_argument("Invalid native arm palette");
    const auto& upper=native.joints[chain.upper];const auto& elbow=native.joints[chain.elbow];const auto& wrist=native.joints[chain.wrist];
    const auto upper_length=length(position(elbow)-position(upper)),lower_length=length(position(wrist)-position(elbow));
    const auto requested=length(position(tracked_wrist)-position(upper));
    // Avatar proportions and native animated shoulder placement differ from
    // the user's reach. Preserve the controller's hand endpoint, extending
    // the sleeve bridge rather than clamping the visible hand off its grip.
    const auto extension=follow_wrist?std::max(1.,requested/(upper_length+lower_length)):1.;
    if(!std::isfinite(maximum_extension) || maximum_extension<1 || maximum_extension>3 || extension>maximum_extension)
        throw std::invalid_argument("Tracked hand lies outside the calibrated avatar interaction envelope");
    const auto solved=solve_arm(position(upper),position(tracked_wrist),elbow_pole,
        upper_length*extension,lower_length*extension);
    const auto upper_target=turn_root(upper,align_direction(position(elbow)-position(upper),solved.elbow-solved.shoulder),solved.shoulder);
    const auto elbow_target=turn_root(elbow,align_direction(position(wrist)-position(elbow),solved.wrist-solved.elbow),solved.elbow);
    tracked_wrist.f[12]=static_cast<float>(solved.wrist.x);tracked_wrist.f[13]=static_cast<float>(solved.wrist.y);tracked_wrist.f[14]=static_cast<float>(solved.wrist.z);
    PosedArm result{native.joints,solved.reach_clamped || extension>1.00001};
    for(unsigned joint=0;joint<69;++joint) {
        if(in_subtree(parents,joint,chain.wrist)) result.joints[joint]=rigid_retarget(native.joints[joint],wrist,tracked_wrist);
        else if(in_subtree(parents,joint,chain.elbow)) result.joints[joint]=rigid_retarget(native.joints[joint],elbow,elbow_target);
        else if(in_subtree(parents,joint,chain.upper)) result.joints[joint]=rigid_retarget(native.joints[joint],upper,upper_target);
    }
    if(helpers) {
        const auto native_axis=position(wrist)-position(elbow);
        const auto axis=unit(solved.wrist-solved.elbow);
        const auto expected_wrist=rigid_retarget(wrist,elbow,elbow_target);
        const auto angle=forearm_roll(expected_wrist,tracked_wrist,axis);
        std::array<bool,69> seen{};
        for(auto joint:*helpers) {
            if(joint>=69 || seen[joint] || joint==chain.wrist || parents[joint]!=static_cast<int>(chain.elbow))
                throw std::invalid_argument("Invalid owned forearm helper");
            seen[joint]=true;
            const double along=dot(position(native.joints[joint])-position(elbow),native_axis)/dot(native_axis,native_axis);
            if(!std::isfinite(along) || along<0 || along>1.05) throw std::invalid_argument("Forearm helper lies outside its segment");
            const double half=angle*std::clamp(along,0.,1.)*.5;
            const Quaternion roll{axis.x*std::sin(half),axis.y*std::sin(half),axis.z*std::sin(half),std::cos(half)};
            const auto at=solved.elbow+rotated(roll,position(result.joints[joint])-solved.elbow);
            result.joints[joint]=turn_root(result.joints[joint],roll,at);
        }
    }
    return result;
}
} // namespace d2vr::rig
