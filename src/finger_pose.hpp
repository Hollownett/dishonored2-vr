#pragma once
#include "arm_palette.hpp"
#include "../third_party/dishonored_vr/grab_pose.h"

namespace d2vr::rig {
struct FingerBinding {
    std::array<Transform,69> closed;
    std::array<bool,69> mask{};
    std::array<Transform,69> open{};
    ArmHierarchy parents{};
    bool native_reference{};
    std::array<unsigned char,69> digit{};
    std::array<bool,69> bone{};
};
using FingerChains=std::array<std::array<unsigned,3>,5>;
using FingerPalmHelpers=std::array<unsigned,2>;
inline FingerBinding finger_binding(const std::array<Transform,69>& bind,const ArmHierarchy& parents,const FingerChains& chains,
    FingerPalmHelpers palm_helpers={69,69}) {
    if(!valid_hierarchy(parents)) throw std::invalid_argument("Invalid finger bind hierarchy");
    for(const auto& t:bind) if(!valid(t)) throw std::invalid_argument("Invalid finger bind pose");
    for(const auto& chain:chains) if(chain[0]>=69 || chain[1]>=69 || chain[2]>=69 ||
        parents[chain[1]]!=static_cast<int>(chain[0]) || parents[chain[2]]!=static_cast<int>(chain[1]))
        throw std::invalid_argument("Invalid named finger chain");
    const auto wrist=parents[chains[0][0]];
    if(wrist<0) throw std::invalid_argument("Finger root has no wrist");
    std::array<bool,69> seen{};
    for(unsigned finger=0;finger<5;++finger) {
        for(auto joint:chains[finger]) {
            if(seen[joint]) throw std::invalid_argument("Duplicate finger chain joint");
            seen[joint]=true;
        }
        if(finger && (parents[chains[finger][0]]<0 || parents[parents[chains[finger][0]]]!=wrist))
            throw std::invalid_argument("Finger metacarpal does not belong to the named wrist");
    }
    const auto width=position(bind[chains[1][0]])-position(bind[chains[4][0]]);
    if(length(width)<.02 || length(width)>.15) throw std::invalid_argument("Invalid finger palm width");
    FingerBinding binding{};binding.closed=bind;binding.open=bind;binding.parents=parents;binding.digit.fill(5);
    binding.bone=seen;
    for(unsigned finger=0;finger<5;++finger) {
        // Include the native metacarpal and its knuckle helpers: mixed skin
        // weights cross these branches, so rotating only phalanges tears skin.
        const auto root=finger?static_cast<unsigned>(parents[chains[finger][0]]):chains[finger][0];
        for(unsigned joint=0;joint<69;++joint) if(in_subtree(parents,joint,root)) {
            if(binding.mask[joint]) throw std::invalid_argument("Native finger branches overlap");
            binding.mask[joint]=true;binding.digit[joint]=static_cast<unsigned char>(finger);
        }
    }
    for(auto joint:palm_helpers) if(joint!=69) {
        if(joint>=69 || parents[joint]!=wrist || binding.mask[joint]) throw std::invalid_argument("Invalid named palm helper");
        for(unsigned child=0;child<69;++child) if(in_subtree(parents,child,joint)) {binding.mask[child]=true;binding.digit[child]=0;}
    }
    return binding;
}
// Capture before any mod retargeting, from the same named native skeleton and
// inverse-bind identity. The caller establishes authored open/fist provenance.
inline FingerBinding native_finger_binding(const std::array<Transform,69>& open,const std::array<Transform,69>& closed,
    const ArmHierarchy& parents,const FingerChains& chains,FingerPalmHelpers palm_helpers={69,69}) {
    auto binding=finger_binding(open,parents,chains,palm_helpers);
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    unsigned flexed{};
    for(unsigned joint=0;joint<69;++joint) {
        if(!valid(closed[joint])) throw std::invalid_argument("Invalid native closed reference");
        if(!binding.mask[joint]) continue;
        const auto parent=parents[joint];
        const auto a=rigid_retarget(open[joint],open[parent],identity);
        const auto b=rigid_retarget(closed[joint],closed[parent],identity);
        bool bone=false;
        for(const auto& chain:chains) for(auto segment:chain) if(segment==joint) bone=true;
        // Bone lengths/offsets must match; authored muscle helpers can deform.
        if(length(position(a)-position(b))>(bone?.002:.06)) throw std::invalid_argument("Native finger reference geometry differs");
        if(bone && dot(column(a,0),column(b,0))+dot(column(a,1),column(b,1))+dot(column(a,2),column(b,2))<2.98) ++flexed;
    }
    if(flexed<4) throw std::invalid_argument("Native closed reference does not contain finger flexion");
    binding.closed=closed;binding.native_reference=true;
    return binding;
}
// The upstream screw interpolation is reused verbatim. This adapter changes
// only affine-row/column packing and the explicitly verified native hierarchy.
inline Transform blend_finger_local(const Transform& open,const Transform& closed,float amount) {
    if(!std::isfinite(amount) || amount<0 || amount>1 || !valid(open) || !valid(closed))
        throw std::invalid_argument("Invalid calibrated finger pose");
    const auto a=affine_rows(open),b=affine_rows(closed);std::array<float,12> rows{};
    if(!dvr::grab::screw_blend_3x4(a.data(),b.data(),amount,rows.data()))
        throw std::invalid_argument("Finger screw interpolation rejected a reference pose");
    auto result=open;
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col) result.f[col*4+row]=rows[row*4+col];
    if(!valid(result)) throw std::invalid_argument("Finger interpolation is not rigid");
    return result;
}
inline std::array<Transform,69> blend_finger_palette(const std::array<Transform,69>& current,
    const std::array<Transform,69>& open,const std::array<Transform,69>& closed,
    const ArmHierarchy& parents,const std::array<bool,69>& fingers,const std::array<float,69>& amounts) {
    if(!valid_hierarchy(parents)) throw std::invalid_argument("Finger hierarchy is invalid");
    for(unsigned joint=0;joint<69;++joint)
        if(!valid(current[joint]) || !valid(open[joint]) || !valid(closed[joint]))
            throw std::invalid_argument("Finger palette reference is invalid");
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=identity.f[15]=1;
    auto result=current;std::array<bool,69> done{};
    // Native declaration order is not assumed to put parents before children.
    for(unsigned pass=0;pass<69;++pass) {
        bool remaining=false,progress=false;
        for(unsigned joint=0;joint<69;++joint) {
            if(!fingers[joint] || done[joint]) continue;
            remaining=true;const auto parent=parents[joint];
            if(parent>=0 && fingers[parent] && !done[parent]) continue;
            const auto a=parent<0?open[joint]:rigid_retarget(open[joint],open[parent],identity);
            const auto b=parent<0?closed[joint]:rigid_retarget(closed[joint],closed[parent],identity);
            const auto local=blend_finger_local(a,b,amounts[joint]);
            result[joint]=parent<0?local:rigid_retarget(local,identity,result[parent]);
            done[joint]=true;progress=true;
        }
        if(!remaining) return result;
        if(!progress) throw std::invalid_argument("Finger chain cannot be reconstructed");
    }
    for(unsigned joint=0;joint<69;++joint) if(fingers[joint] && !done[joint])
        throw std::invalid_argument("Finger chain exceeds native bounds");
    return result;
}
inline std::array<Transform,69> curl_fingers(const std::array<Transform,69>& current,const ArmHierarchy& parents,
    const FingerBinding& binding,const std::array<float,5>& curls) {
    for(auto amount:curls) if(!std::isfinite(amount) || amount<0 || amount>1) throw std::invalid_argument("Invalid controller finger curl");
    // Bind geometry is a mapping, never evidence of a natural fist.
    if(!binding.native_reference) return current;
    if(parents!=binding.parents) throw std::invalid_argument("Native finger reference hierarchy changed");
    if(std::all_of(curls.begin(),curls.end(),[](float amount) {return amount==0;})) return current;
    std::array<float,69> amounts{};
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    for(unsigned joint=0;joint<69;++joint) if(binding.mask[joint]) {
        if(binding.digit[joint]>=5) throw std::invalid_argument("Invalid native finger assignment");
        amounts[joint]=curls[binding.digit[joint]];
        const auto parent=parents[joint];
        const auto a=rigid_retarget(current[joint],current[parent],identity);
        const auto b=rigid_retarget(binding.closed[joint],binding.closed[parent],identity);
        if(length(position(a)-position(b))>(binding.bone[joint]?.003:.12))
            throw std::invalid_argument("Current authored hand geometry differs from the native reference");
    }
    // Preserve authored idle/partial poses instead of snapping to a captured
    // empty open hand whenever the player's squeeze first crosses zero.
    return blend_finger_palette(current,current,binding.closed,parents,binding.mask,amounts);
}
inline std::array<Transform,69> curl_fingers(const std::array<Transform,69>& current,const ArmHierarchy& parents,
    const FingerBinding& binding,float amount) {
    return curl_fingers(current,parents,binding,std::array<float,5>{amount,amount,amount,amount,amount});
}
// Rotate a joint and its whole subtree about a model-space pivot.
inline void rotate_subtree(std::array<Transform,69>& joints,const ArmHierarchy& parents,unsigned root,Quaternion turn,Vec3 pivot) {
    for(unsigned joint=0;joint<69;++joint) if(in_subtree(parents,joint,root))
        joints[joint]=turn_root(joints[joint],turn,pivot+rotated(turn,position(joints[joint])-pivot));
}
inline Quaternion axis_angle(Vec3 axis,double radians) {
    const auto s=std::sin(radians*.5);return normalized({axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5)});
}
// Procedural fist from controller squeeze/trigger. Each finger hinges about one
// axis perpendicular to its proximal phalanx and the palm normal, so a native
// pose that already grips something (sword hilt) is only curled further where
// it is still more open than the requested amount. Knuckle helper joints take
// half of each hinge angle so skin around the knuckles bends instead of tearing.
// curls: thumb, index, middle, ring, pinky in [0,1].
struct HandJoints {unsigned underwrist{69},upperwrist{69};};
inline std::array<Transform,69> procedural_curl(std::array<Transform,69> joints,const ArmHierarchy& parents,
    const FingerChains& chains,HandJoints palm,unsigned wrist,const std::array<float,5>& curls) {
    for(auto amount:curls) if(!std::isfinite(amount) || amount<0 || amount>1) throw std::invalid_argument("Invalid controller finger curl");
    if(wrist>=69 || !valid_hierarchy(parents)) throw std::invalid_argument("Invalid procedural hand");
    // Palm-inward normal: named under/upper wrist helpers, else knuckle geometry.
    const auto knuckle=[&](unsigned finger) {return position(joints[chains[finger][0]]);};
    const auto across=knuckle(1)-knuckle(4),along=(knuckle(1)+knuckle(4))*.5-position(joints[wrist]);
    Vec3 palm_in=palm.underwrist<69 && palm.upperwrist<69?position(joints[palm.underwrist])-position(joints[palm.upperwrist]):cross(along,across);
    if(length(palm_in)<1e-6) return joints;
    palm_in=unit(palm_in);
    constexpr double degree=3.14159265358979323846/180;
    constexpr std::array<std::array<double,3>,5> full{{{15,40,55},{75,100,70},{75,100,70},{75,100,70},{75,100,70}}};
    std::array<bool,69> chain_joint{};
    for(const auto& chain:chains) for(auto joint:chain) chain_joint[joint]=true;
    const auto next_joint=[&](unsigned joint) {
        unsigned best=69;double distance=-1;
        for(unsigned child=0;child<69;++child) if(parents[child]==static_cast<int>(joint)) {
            const auto d=length(position(joints[child])-position(joints[joint]));
            if(chain_joint[child]) return child;
            if(d>distance) {distance=d;best=child;}
        }
        return best;
    };
    for(unsigned finger=0;finger<5;++finger) {
        if(curls[finger]<=0) continue;
        const auto& chain=chains[finger];
        // Thumb hinges toward the middle knuckle; fingers toward the palm.
        const auto base=finger?chain[0]:chain[1];
        const auto tip=next_joint(base);if(tip>=69) continue;
        const auto direction=position(joints[tip])-position(joints[base]);
        auto toward=finger?palm_in:knuckle(2)-position(joints[base]);
        toward=toward-unit(direction)*dot(toward,unit(direction));
        const auto hinge=cross(direction,toward);
        if(length(direction)<1e-6 || length(toward)<1e-6 || length(hinge)<1e-9) continue;
        const auto axis=unit(hinge);
        for(unsigned segment=0;segment<3;++segment) {
            const auto joint=chain[segment];const auto parent=parents[joint];const auto child=next_joint(joint);
            if(parent<0 || child>=69) break;
            const auto pivot=position(joints[joint]);
            auto incoming=pivot-position(joints[parent]),outgoing=position(joints[child])-pivot;
            incoming=incoming-axis*dot(incoming,axis);outgoing=outgoing-axis*dot(outgoing,axis);
            double existing=0;
            // The thumb's wrist-to-CMC line is not a bone; its small turn is additive.
            if(finger || segment) if(length(incoming)>1e-6 && length(outgoing)>1e-6)
                existing=std::atan2(dot(cross(incoming,outgoing),axis),dot(incoming,outgoing));
            const double delta=curls[finger]*full[finger][segment]*degree-existing;
            if(delta<=0) continue;
            rotate_subtree(joints,parents,joint,axis_angle(axis,delta),pivot);
            if(static_cast<unsigned>(parent)==wrist) continue;
            const auto half=axis_angle(axis,delta*.5);
            for(unsigned helper=0;helper<69;++helper)
                if(parents[helper]==parent && helper!=joint && !chain_joint[helper]) rotate_subtree(joints,parents,helper,half,pivot);
        }
    }
    return joints;
}
} // namespace d2vr::rig
