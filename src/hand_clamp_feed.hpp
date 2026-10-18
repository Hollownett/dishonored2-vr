#pragma once
// Physical hands phase 1 (Task 5): how the hand publication (dxgi_probe.cpp) feeds HandClamp. Pure; nothing here touches the
// engine. C9 yields, the C14 posting cadence, routing last frame's cast answers, the open-hand probe set (the same spheres for
// leading_probe, wall_leading_probe and ClampInput::hand), and the hold on a re-acquire frame.
#include "hand_clamp.hpp"
#include "hand_contact.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr {

// C9: the clamp yields (draws the controller, sweeps nothing) to a native authored animation on that arm (authored_hands: a
// takedown, a death clip, an authored camera), to a hand carrying a native object (carry_bits) and to a hand holding a prop
// physically: the carried body is moved by physics (deviation 4).
inline bool clamp_yields(unsigned side,unsigned authored_hands,unsigned carry_bits,bool physical_holding) {
    return side>1 || (((authored_hands|carry_bits)>>side)&1u)!=0 || physical_holding;
}

// C14 alternate mode: each hand casts every other frame, the two hands on different frames.
inline bool clamp_posts(bool alternate,std::uint64_t frame,unsigned side) {return !alternate || ((frame+side)&1u)==0;}

// Last frame's answers to what the side posted: slot 0 is the chest re-acquire while the side was released, else the palm.
inline void route_clamp_hits(ClampInput& in,ClampState last_state,const std::optional<ClampHit>& slot0,const std::optional<ClampHit>& tip,
                             const std::optional<ClampHit>& wall_tip,const std::optional<ClampHit>& exclude) {
    in.palm_hit.reset();in.chest_hit.reset();
    if(last_state==ClampState::released) in.chest_hit=slot0;else in.palm_hit=slot0;
    in.tip_hit=tip;in.wall_tip_hit=wall_tip;in.exclude_hit=exclude;
}

// Without hand geometry the leading probe is one fingertip sphere ahead of the grip.
inline ClampProbe default_clamp_tip() {return {{.09,0,0},.035};}

// The open hand's spheres in the grip frame: hand_proxies of the open hand posed at {palm_local, identity}, where palm_local is
// the drawn palm's offset from the grip in grip axes (NativeHandContact::publish poses the same spheres at grip + palm offset).
struct ClampProbeSet {
    std::array<ClampProbe,ClampInput::hand_capacity> hand{};std::size_t count{};
    Vec3 palm_local{};bool valid{};
};
inline ClampProbeSet clamp_probe_set(const HandProxies& open_local,Vec3 palm_local) {
    ClampProbeSet set{};set.palm_local=palm_local;
    for(const auto& sphere:open_local) {
        const ClampProbe probe{sphere.centre,sphere.radius};
        if(set.count<set.hand.size() && clamp_detail::usable(probe)) set.hand[set.count++]=probe;
    }
    set.valid=set.count>0 && finite(palm_local);
    return set;
}
// The palm offset the set was built with drifts (the arm solve's residual): rebuild past this, not every frame, so a probe keeps
// its identity (leading_probe's hysteresis compares probes exactly).
inline constexpr double clamp_probe_refresh=.01;
inline bool clamp_probe_set_stale(const ClampProbeSet& set,Vec3 palm_local) {
    if(!finite(palm_local)) return false;
    return !set.valid || !(length(palm_local-set.palm_local)<=clamp_probe_refresh);
}
// A probe chosen from `from` stays the same sphere of the hand in `to` (same index); one that is not in `from` stays as it is.
inline ClampProbe remap_clamp_probe(const ClampProbeSet& from,const ClampProbeSet& to,ClampProbe current) {
    for(std::size_t i=0;i<from.count;++i)
        if(clamp_detail::same_probe(from.hand[i],current)) return i<to.count?to.hand[i]:current;
    return current;
}

// The re-acquire frame (ClampOutput::reacquired: after a pause, a tracking gap or a reset) draws the target unswept, which can be
// inside a wall (Task 2). The publication holds the hand where it was last drawn instead (kept in the level player frame, C11)
// while that is the same epoch and within max_gap of the target; the clamp then eases from it (note_drawn) once its sweeps answer.
class ClampReacquireHold {
public:
    void note(unsigned side,std::uint64_t epoch,const WorldTrackedPose& root,Vec3 drawn) {
        if(side>1) return;
        auto& k=kept_[side];k.local=clamp_detail::local_point(root,drawn);k.epoch=epoch;k.has=finite(k.local) && clamp_detail::finite_pose(root);
    }
    void clear(unsigned side) {if(side<2) kept_[side]={};}
    std::optional<Vec3> hold(unsigned side,std::uint64_t epoch,const WorldTrackedPose& root,Vec3 target,double max_gap) const {
        if(side>1 || !kept_[side].has || kept_[side].epoch!=epoch || !clamp_detail::finite_pose(root) || !finite(target)) return std::nullopt;
        const Vec3 at=clamp_detail::world_point(root,kept_[side].local);
        if(!finite(at) || !(length(at-target)<=max_gap)) return std::nullopt;
        return at;
    }
private:
    struct Kept {Vec3 local{};std::uint64_t epoch{};bool has{};};
    std::array<Kept,2> kept_{};
};

} // namespace d2vr
