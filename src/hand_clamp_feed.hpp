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

// Without hand geometry the leading probe is one fingertip sphere ahead of the grip, and the wall tip one nearer the palm (a
// separate sphere, so it is swept: without one the rising-diagonal case put the fingers 7-8 cm into walls, Task 3 "New A").
inline ClampProbe default_clamp_tip() {return {{.09,0,0},.035};}
inline ClampProbe default_clamp_wall_tip() {return {{.05,0,0},.035};}

// The hand after the weapon's blade (WeaponCollision, which eases its own push). The clamp is told its own hand (`hand`: after the
// meet and any hold), not the blade's push: both remembering the push made a 4.5 m/s stab recoil 5 cm past the surface and
// bounce. A blade push never moves the hand through a plane the clamp holds (`met`: the met prop's planes are skipped).
struct BladeComposed {Vec3 drawn{},noted{};};
inline BladeComposed compose_blade(const HandClamp& clamp,unsigned side,const std::array<Vec3,3>& basis,Vec3 hand,const CollisionResult& blade,
                                   std::optional<std::uintptr_t> met) {
    BladeComposed r{blade.grip,hand};
    if(finite(blade.shift) && length(blade.shift)>0) r.drawn=clamp.keep_out(side,basis,blade.grip,met);
    return r;
}

// I2 / N5: while the clamp holds a hand, shots and the crosshair leave from the drawn barrel: the controller's aim origin moved by the
// drawn hand's offset from the controller (last frame's: the aim is published before the hands), then clipped at every plane the
// clamp holds, on the segment from the drawn grip (kept out of them) to that origin, short of the surface by the released guard's
// margin. The direction is the drawn barrel's already (headset v361). The clip is what keeps a thin door between the shot and the
// far side: the aim pose sits 5-8 cm ahead of the grip, which the clamp stops only palm radius + skin off the face, and the offset
// is one frame old during a jab (a 6 m/s jab-and-fire left the origin past a 5 cm door).
struct ClampedShotOffset {
    Vec3 offset{},drawn{};std::uint64_t tick{},epoch{};bool valid{};
    std::array<ClampHit,HandClamp::max_held_planes> planes{};std::size_t plane_count{};
};
inline ClampedShotOffset clamped_shot_offset(Vec3 drawn,Vec3 raw,bool clamped,std::uint64_t tick,std::uint64_t epoch,const HandClamp* clamp=nullptr,
                                             unsigned side=0) {
    ClampedShotOffset o{};const Vec3 d=drawn-raw;
    if(!clamped || !finite(d) || !finite(drawn) || !(length(d)<=1)) return o;
    if(clamp) o.plane_count=clamp->held_planes(side,o.planes);
    if(!(length(d)>1e-3) && !o.plane_count) return {};
    o.offset=length(d)>1e-3?d:Vec3{};o.drawn=drawn;o.tick=tick;o.epoch=epoch;o.valid=true;
    return o;
}
inline constexpr std::uint64_t clamped_shot_max_age_ms=100;
inline constexpr double shot_plane_margin=.05;
inline Vec3 before_surface(Vec3 from,Vec3 to,const ClampHit& hit,double margin);
inline Vec3 clamped_shot_origin(Vec3 raw_origin,const ClampedShotOffset& o,std::uint64_t now,std::uint64_t epoch) {
    if(!o.valid || o.epoch!=epoch || now<o.tick || now-o.tick>clamped_shot_max_age_ms || !finite(o.offset) || !finite(raw_origin)) return raw_origin;
    Vec3 origin=raw_origin+o.offset;
    for(std::size_t i=0;i<std::min(o.plane_count,o.planes.size());++i) origin=before_surface(o.drawn,origin,o.planes[i],shot_plane_margin);
    return origin;
}

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

// N1: the hand in a world UI (logic != 2, nothing is cast): the controller kept out of the planes the side held when the pause
// began. Those are infinite half-spaces that nothing ages or refutes during the pause, so they apply only in the same epoch and only
// while that keeps the hand within max_gap of the controller (a hand rested on a table drops below the table-top plane once the
// controller is lowered beside it; a new level's menu never projects onto the old level's planes). Otherwise: the controller.
inline Vec3 paused_hand(const HandClamp& clamp,unsigned side,const WorldTrackedPose& raw,std::uint64_t epoch,double max_gap) {
    const auto held=clamp.side_epoch(side);
    if(!held || *held!=epoch || !finite(raw.position)) return raw.position;
    const Vec3 kept=clamp.keep_out(side,raw.basis,raw.position,std::nullopt);
    return finite(kept) && length(kept-raw.position)<=max_gap?kept:raw.position;
}

// The shot origin while a side is released (G3 snap-back: it is drawn at the controller, which can be past a door). Last frame's
// chest re-acquire sweep (the clamp's slot 0 while released) and an eye -> hand sweep (the exclusion slot, which a released side
// never posts: no meet while released; the tip slot was re-read as the tip's answer on the frame after a re-arm, N7) say what lies between the body and the hand. The origin is the point on the chest -> barrel segment just short of the
// surface the chest sweep met (by `margin`); the raw origin when it met nothing; the same on the eye -> barrel segment when there
// is no usable chest answer (none within max_age, or the chest started inside geometry); the eye when neither is known.
inline constexpr double released_shot_margin=shot_plane_margin;
inline constexpr std::uint64_t released_shot_max_age_ms=100;
inline constexpr double eye_sweep_radius=.02;
// The point on from -> to just before the plane of `hit` (point, normal toward free space), backed off by margin along the
// segment; `to` when it is not behind that plane; `from` when from is not in front of it.
inline Vec3 before_surface(Vec3 from,Vec3 to,const ClampHit& hit,double margin) {
    const double d0=dot(from-hit.point,hit.normal),d1=dot(to-hit.point,hit.normal),span=length(to-from);
    if(!std::isfinite(d0) || !std::isfinite(d1) || !(span>0)) return from;
    if(d1>=0) return to;
    if(d0<=0) return from;
    const double s=std::max(0.,d0/(d0-d1)-margin/span);
    return from+(to-from)*s;
}
class ReleasedShotGuard {
public:
    struct Answer {Vec3 from{};std::optional<ClampHit> hit;std::uint64_t tick{};bool valid{};};
    // Explicit completion path: the caller has matched request ID, producer
    // and epoch. Its actual submitted origin owns this answer, even if a
    // newer sweep has already been posted. No call means no completion;
    // a call with no hit is a genuine miss. Keep the completion timestamp
    // unchanged so replay cannot extend a clear answer's lifetime.
    void read_receipt(unsigned side,bool eye,Vec3 actual_from,const std::optional<ClampHit>& hit,std::uint64_t completed_tick) {
        if(side>1 || !finite(actual_from) || !completed_tick) return;
        auto& k=sides_[side];
        if(!k.released) return;
        auto& answer=eye?k.eye:k.chest;
        if(answer.valid && completed_tick<=answer.tick) return;
        answer={actual_from,hit,completed_tick,true};
    }
    // Before this frame's update: the answers to what was posted for this side last frame (slot 0, the exclusion slot).
    void read(unsigned side,const std::optional<ClampHit>& slot0,const std::optional<ClampHit>& eye_slot,std::uint64_t tick) {
        if(side>1) return;auto& k=sides_[side];
        if(k.chest_posted) k.chest={k.chest_posted->from,slot0,tick,true};
        if(k.eye_posted) k.eye={k.eye_posted->from,eye_slot,tick,true};
        k.chest_posted.reset();k.eye_posted.reset();
    }
    // After the update: whether the side is released (drawn by the clamp at the controller), the chest sweep it posted, and the
    // eye sweep to post on the exclusion slot (returned; only while released and posting).
    std::optional<ClampSweep> posted(unsigned side,bool clamp_drew,const ClampOutput& out,bool post,Vec3 eye,Vec3 hand,std::uint64_t tick,std::uint64_t epoch) {
        if(side>1) return std::nullopt;auto& k=sides_[side];
        k.released=clamp_drew && out.valid && out.state==ClampState::released;k.tick=tick;k.epoch=epoch;k.eye_at=eye;
        if(!k.released) {k.chest={};k.eye={};return std::nullopt;}
        if(out.palm_sweep.valid) k.chest_posted=out.palm_sweep;
        if(!post || !finite(eye) || !finite(hand) || !(length(hand-eye)>=1e-3)) return std::nullopt;
        k.eye_posted=ClampSweep{eye,hand,eye_sweep_radius,true};
        return k.eye_posted;
    }
    void clear(unsigned side) {if(side<2) sides_[side]={};}
    // This side was released at the last publication (same epoch, within max_age): the guarded origin applies.
    bool applies(unsigned side,std::uint64_t now,std::uint64_t epoch) const {
        if(side>1) return false;const auto& k=sides_[side];
        return k.released && k.epoch==epoch && now>=k.tick && now-k.tick<=released_shot_max_age_ms;
    }
    Vec3 origin(unsigned side,Vec3 raw_origin,std::uint64_t now) const {
        if(side>1) return raw_origin;const auto& k=sides_[side];
        const auto fresh=[&](const Answer& a) {return a.valid && now>=a.tick && now-a.tick<=released_shot_max_age_ms;};
        for(const Answer* a:{&k.chest,&k.eye}) {
            if(!fresh(*a) || (a->hit && a->hit->initial_overlap)) continue;
            if(!a->hit) return raw_origin;
            return before_surface(a->from,raw_origin,*a->hit,released_shot_margin);
        }
        return finite(k.eye_at)?k.eye_at:raw_origin;
    }
private:
    struct Side {bool released{};std::uint64_t tick{},epoch{};Vec3 eye_at{};Answer chest,eye;std::optional<ClampSweep> chest_posted,eye_posted;};
    std::array<Side,2> sides_{};
};

// The shot (and crosshair) origin for one side: the released guard while the side is released, else the clamped offset (I2).
inline Vec3 guarded_shot_origin(const ReleasedShotGuard& released,const ClampedShotOffset& clamped,unsigned side,Vec3 raw_origin,std::uint64_t now,
                                std::uint64_t epoch) {
    return released.applies(side,now,epoch)?released.origin(side,raw_origin,now):clamped_shot_origin(raw_origin,clamped,now,epoch);
}

// N6: a load (the loading screen or a death reload: game logic 0 or bit 15, where the mod already suspends the avatar fit) forgets the
// clamp's planes, the held hand and the shot state, even within the same epoch (a death reload can keep the player and pose epoch).
inline bool clamp_forgets_on(unsigned logic) {return logic==0 || logic==(1u<<15);}

} // namespace d2vr
