#pragma once
// Arm reach (physical hands phase 1: G2, C4). body_ik's soft two-bone reach rule, the largest shoulder-to-wrist
// distance that rule still reaches exactly, and a translation-only clamp that keeps a grip target inside it, so the
// hand clamp's sweep and the arm IK aim at the same, reachable hand. Pure math: nothing here touches the engine.
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>

namespace d2vr {

// Soft IK near full reach (body_ik pose_arm): the elbow straightens along an exponential instead of popping (2-bone IK
// has infinite d(elbow)/d(reach) at full length); the wrist stays exact through a bone stretch of at most 6%, beyond
// which it stops short along the reach. target_distance is where the wrist is solved; extension scales both bones.
struct SoftReach {double target_distance{},extension{1};};
inline SoftReach soft_reach(double d,double full) {
    const double soft=.08*full;
    SoftReach result{d,1};
    if(d>full-soft) {
        const double softened=full-soft*std::exp(-(d-(full-soft))/soft);
        result.extension=std::min(1.06,d/softened);
        result.target_distance=softened*result.extension;
    }
    return result;
}
// Largest shoulder-to-wrist distance soft_reach still reaches exactly (the stretch cap not yet engaged), by bisection.
// The rule's d/softened grows monotonically past full-soft, so the exact set is one interval. 0 for an invalid arm.
inline double max_exact_reach(double full) {
    if(!std::isfinite(full) || !(full>0)) return 0;
    const auto exact=[full](double d) {return std::abs(soft_reach(d,full).target_distance-d)<=1e-12*d;};
    double lo=full*.92,hi=full*2; // lo: the soft band starts here, every shorter reach is exact; hi: far past the cap
    for(int i=0;i<200 && hi-lo>1e-15*full;++i) {
        const double mid=.5*(lo+hi);
        (exact(mid)?lo:hi)=mid;
    }
    return lo;
}

// max_exact_reach for one arm, bisected again only when the arm length changes (it changes on an arm solve, not per frame).
class ExactReachCache {
    double full_{},reach_{};bool have_{};unsigned computed_{};
public:
    double get(double full) {
        if(!have_ || !(full==full_)) {reach_=max_exact_reach(full);full_=full;have_=true;++computed_;}
        return reach_;
    }
    unsigned computed() const noexcept {return computed_;}
};

// One arm's reach in the world: the shoulder the last arm solve used, the farthest wrist distance it reaches exactly,
// and the wrist in the controller grip frame (wrist = grip.position + R(grip)·wrist_local, R's columns = grip.basis).
struct ArmReach {Vec3 shoulder{};double max_wrist{};Vec3 wrist_local{};bool valid{};};

// Moves the grip, translation only, so that its wrist lies within max_wrist of the shoulder, along shoulder->wrist.
// A grip already in reach, an invalid reach or a non-finite input is returned unchanged with clamped=false.
inline WorldTrackedPose reach_clamp(const WorldTrackedPose& grip,const ArmReach& reach,bool* clamped=nullptr) {
    if(clamped) *clamped=false;
    if(!reach.valid || !finite(reach.shoulder) || !finite(reach.wrist_local) || !std::isfinite(reach.max_wrist) || !(reach.max_wrist>0) ||
       !finite(grip.position) || !finite(grip.basis[0]) || !finite(grip.basis[1]) || !finite(grip.basis[2])) return grip;
    const auto wrist=grip.position+grip.basis[0]*reach.wrist_local.x+grip.basis[1]*reach.wrist_local.y+grip.basis[2]*reach.wrist_local.z;
    const auto arm=wrist-reach.shoulder;
    const double distance=length(arm);
    if(!(distance>reach.max_wrist)) return grip;
    auto result=grip;
    result.position=grip.position+arm*(reach.max_wrist/distance-1);
    if(clamped) *clamped=true;
    return result;
}

} // namespace d2vr
