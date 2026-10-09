#pragma once
#include "finger_pose.hpp"

namespace d2vr::rig {
// The native animation palette uploaded for the right arm while the sword is
// held already carries the authored sword-grip finger pose: every finger joint
// as a LOCAL transform relative to its parent. Capturing those locals lets the
// empty tracked hand close on the game's own fist instead of the procedural
// curl, and lets one authored right-hand grip serve the left hand after a
// lateral reflection.
struct FingerGrip {
    std::array<Transform,69> local{};
    std::array<bool,69> mask{};
    bool valid{};
};
// Local-frame reflection R*M*R with R=diag(1,1,-1): the 3x3 conjugates to a
// proper rotation (det(R)^2) and the translation/affine row mirrors on the same
// axis, so the mirrored local stays a valid rigid transform of the other hand.
inline Transform mirror_grip_local(const Transform& local,int axis) {
    Transform result=local;
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col) {
        const double sign=(row==static_cast<unsigned>(axis)?-1.:1.)*
            (col==static_cast<unsigned>(axis)?-1.:1.);
        result.f[col*4+row]=static_cast<float>(local.f[col*4+row]*sign);
    }
    return result;
}
// Every joint in the finger subtrees -- the three named chain joints of each
// digit and all their descendants, exactly the subtree rule procedural_curl
// rotates -- is stored relative to its parent, the way finger_pose.hpp builds a
// native local. A missing chain joint, an invalid hierarchy or a non-finite
// palette entry yields valid=false rather than a partial grip.
inline FingerGrip capture_finger_grip(const std::array<Transform,69>& native_model_palette,
    const ArmHierarchy& parents,const FingerChains& chains) {
    FingerGrip grip{};
    if(!valid_hierarchy(parents)) return grip;
    std::array<unsigned char,69> role{};
    for(const auto& chain:chains) for(auto joint:chain) {
        if(joint>=69) return FingerGrip{};
        role[joint]=1;
    }
    for(unsigned joint=0;joint<69;++joint) if(role[joint]) for(unsigned sub=0;sub<69;++sub)
        if(!role[sub] && in_subtree(parents,sub,joint)) role[sub]=2;
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    for(unsigned joint=0;joint<69;++joint) {
        if(!role[joint]) continue;
        const auto parent=parents[joint];
        if(!valid(native_model_palette[joint]) ||
           (parent>=0 && !valid(native_model_palette[static_cast<unsigned>(parent)]))) return FingerGrip{};
        grip.local[joint]=parent<0?native_model_palette[joint]:
            rigid_retarget(native_model_palette[joint],native_model_palette[static_cast<unsigned>(parent)],identity);
        grip.mask[joint]=true;
    }
    for(const auto& chain:chains) for(auto joint:chain) if(!grip.mask[joint]) return FingerGrip{};
    grip.valid=true;
    return grip;
}
// Map the captured grip onto the other hand: the 15 chain joints finger by
// finger and chain position by chain position, then the descendants paired in
// ascending native order. The two hands are cooked from one mirrored skeleton,
// so their joint arrays keep the same relative order: that ordering maps
// parents to parents and preserves sibling order. The source hierarchy is not
// part of this API (the grip carries locals only); a rig whose two hands are
// NOT declaration-order isomorphic would need the source hierarchy passed in.
inline FingerGrip mirror_finger_grip(const FingerGrip& source,const FingerChains& from_chains,
    const FingerChains& to_chains,const ArmHierarchy& to_parents,int axis=2) {
    FingerGrip result{};
    if(!source.valid || axis<0 || axis>2) return result;
    std::array<bool,69> from_chain{},to_chain{};
    for(const auto& chain:from_chains) for(auto joint:chain) {
        if(joint>=69) return FingerGrip{};
        from_chain[joint]=true;
    }
    for(const auto& chain:to_chains) for(auto joint:chain) {
        if(joint>=69) return FingerGrip{};
        to_chain[joint]=true;
    }
    std::array<bool,69> wanted{};
    for(const auto& chain:to_chains) for(auto joint:chain) for(unsigned sub=0;sub<69;++sub)
        if(!to_chain[sub] && in_subtree(to_parents,sub,joint)) wanted[sub]=true;
    std::array<unsigned,69> pending{};unsigned count=0,targets=0;
    for(unsigned joint=0;joint<69;++joint) if(source.mask[joint] && !from_chain[joint]) pending[count++]=joint;
    for(unsigned joint=0;joint<69;++joint) if(wanted[joint]) ++targets;
    if(count!=targets) return FingerGrip{};
    for(unsigned finger=0;finger<5;++finger) for(unsigned segment=0;segment<3;++segment) {
        const auto from=from_chains[finger][segment],to=to_chains[finger][segment];
        if(!source.mask[from]) return FingerGrip{};
        result.local[to]=mirror_grip_local(source.local[from],axis);
        result.mask[to]=true;
    }
    unsigned next=0;
    for(unsigned joint=0;joint<69;++joint) if(wanted[joint]) {
        result.local[joint]=mirror_grip_local(source.local[pending[next++]],axis);
        result.mask[joint]=true;
    }
    result.valid=true;
    return result;
}
// Expand the grip locals onto the CURRENT posed parents (parent before child;
// native declaration order is not assumed), then blend the posed open hand into
// that grip per digit. The five masked subtrees are disjoint under the wrist,
// so ranking their roots by native index recovers thumb..pinky exactly as the
// named chains (0=thumb, 1=index, 2=middle, 3=ring, 4=pinky). Zero amounts and
// every unmasked joint return the input untouched; the blend is the existing
// rigid screw interpolation, so segment lengths survive every amount.
inline std::array<Transform,69> apply_finger_grip(const std::array<Transform,69>& posed,const ArmHierarchy& parents,
    const FingerGrip& grip,const std::array<float,5>& amounts) {
    for(auto amount:amounts) if(!std::isfinite(amount) || amount<0 || amount>1)
        throw std::invalid_argument("Invalid native finger grip amount");
    if(!grip.valid) return posed;
    if(!valid_hierarchy(parents)) throw std::invalid_argument("Invalid native finger grip hierarchy");
    if(std::all_of(amounts.begin(),amounts.end(),[](float amount) {return amount==0;})) return posed;
    std::array<unsigned,5> roots{};unsigned root_count=0;
    for(unsigned joint=0;joint<69;++joint) if(grip.mask[joint] && (parents[joint]<0 || !grip.mask[parents[joint]])) {
        if(root_count>=5) throw std::invalid_argument("Native finger grip has more than five digits");
        roots[root_count++]=joint;
    }
    if(root_count!=5) throw std::invalid_argument("Native finger grip does not hold five digits");
    std::array<float,69> digit_amounts{};
    for(unsigned joint=0;joint<69;++joint) if(grip.mask[joint]) {
        int root=static_cast<int>(joint);
        while(parents[root]>=0 && grip.mask[parents[root]]) root=parents[root];
        unsigned rank=0;
        for(unsigned other=0;other<5;++other) if(roots[other]<static_cast<unsigned>(root)) ++rank;
        if(rank>=5) throw std::invalid_argument("Native finger grip root is not a digit");
        digit_amounts[joint]=amounts[rank];
    }
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    auto expanded=posed;std::array<bool,69> done{};
    for(unsigned pass=0;pass<69;++pass) {
        bool remaining=false,progress=false;
        for(unsigned joint=0;joint<69;++joint) {
            if(!grip.mask[joint] || done[joint]) continue;
            remaining=true;const auto parent=parents[joint];
            if(parent>=0 && grip.mask[parent] && !done[parent]) continue;
            expanded[joint]=parent<0?grip.local[joint]:
                rigid_retarget(grip.local[joint],identity,expanded[static_cast<unsigned>(parent)]);
            done[joint]=true;progress=true;
        }
        if(!remaining) break;
        if(!progress) throw std::invalid_argument("Native finger grip chain cannot be reconstructed");
    }
    return blend_finger_palette(posed,posed,expanded,parents,grip.mask,digit_amounts);
}
} // namespace d2vr::rig
