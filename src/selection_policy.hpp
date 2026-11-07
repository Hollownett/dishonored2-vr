#pragma once
#include "palm_offset.hpp"
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
namespace d2vr {
inline constexpr double grab_acquire_distance=.25;
inline constexpr double grab_retain_distance=grab_acquire_distance;
// v1088 (user, v1087 headset: "the arm still does not hold the leg ... make it hold like bottles"): a severed limb is the grab's out to where the game's
// Use reaches it from the hand (its hand sweep hit limb bodies up to ~0.4 m off, v1086 ranking log), so a grip on one pulls it into the hand instead
// of the game's floating carry. Under PhysicalGrab::bind's 0.55 m contact cap.
inline constexpr double limb_reach_m=.5;
inline constexpr double limb_seat_move_m=1.2;   // the farthest a limb part's ORIGIN may move in its seat (rig::prop_seat): its origin is its joint, and the turn into the hand swings it
inline constexpr double grab_switch_margin=0;
// Legacy gaze/reach beam only: a palm-wide cylinder along the aim axis.
inline constexpr double grab_beam_radius=.06,grab_beam_range_weight=.25;
// Legacy gaze/reach loot bonus; hand-sphere selection never applies this bias.
inline constexpr double use_loot_bonus=grab_beam_radius;
// Shared hand-distance selection for loot, props and corpse bones. Reach is measured
// to actual bounds, with the same 25 cm gate for every hover candidate.
inline constexpr double palm_axis_push=.015,hover_lead=.8,palm_facing_bias=.6,palm_hysteresis=.86,
    approach_min=.8,approach_max=1.2,approach_per_mps=.2,loot_min_half=.03,default_arm_length=.65;
enum class PalmKind : unsigned char {loot,usable,prop,bone,living};
struct PalmProbe {Vec3 palm{},forward{},direction{},velocity{};double arm_length=default_arm_length;bool valid{};};
// C10: selection reads the RAW grip plus the phase-1 palm fit. basis[1] is the hand's left axis: the palm faces -left on side 0 and +left on side 1.
inline PalmProbe palm_probe(unsigned side,const WorldTrackedPose& raw_grip,const PalmOffset& fit,std::optional<double> arm_length,Vec3 velocity) {
    PalmProbe p;
    const WorldTrackedPose g=apply_palm_offset(raw_grip,fit,1.0);
    p.palm=g.position;p.forward=g.basis[0];
    p.direction=side?g.basis[1]:g.basis[1]*-1.;
    p.velocity=velocity;
    p.arm_length=arm_length && std::isfinite(*arm_length) && *arm_length>.1 ? *arm_length : default_arm_length;
    p.valid=finite(p.palm) && finite(p.forward) && finite(p.direction) && finite(p.velocity);
    return p;
}
// Hand acquisition uses the raw controller grip centre, independent of its orientation or palm fit.
inline PalmProbe hand_sphere_probe(Vec3 centre) {PalmProbe p;p.palm=centre;p.valid=finite(centre);return p;}
inline Vec3 palm_point(const PalmProbe& p) {return p.palm+p.forward*palm_axis_push;}
inline Vec3 hover_centre(const PalmProbe& p,double hover_radius) {return palm_point(p)+p.direction*(hover_lead*hover_radius);}
struct PalmTarget {std::uint64_t id{};PalmKind kind{};Vec3 minimum{},maximum{};double priority=1;Vec3 velocity{};};
struct PalmScore {double cost{},distance{};Vec3 point{};};
inline bool hand_pickup_in_range(const PalmProbe& probe,Vec3 minimum,Vec3 maximum) noexcept {
    if(!probe.valid || !finite(probe.palm) || !finite(probe.forward) || !finite(minimum) || !finite(maximum) ||
       minimum.x>maximum.x || minimum.y>maximum.y || minimum.z>maximum.z) return false;
    const auto origin=palm_point(probe);
    const Vec3 point{std::clamp(origin.x,minimum.x,maximum.x),std::clamp(origin.y,minimum.y,maximum.y),std::clamp(origin.z,minimum.z,maximum.z)};
    return length(point-origin)<=grab_acquire_distance;
}
inline bool palm_target_valid(const PalmTarget& t) {
    return t.id && finite(t.minimum) && finite(t.maximum) && finite(t.velocity) && std::isfinite(t.priority) && t.priority>0 &&
           t.minimum.x<=t.maximum.x && t.minimum.y<=t.maximum.y && t.minimum.z<=t.maximum.z;
}
// Distance to the actual target bounds. Direction, approach speed, kind and a previously hovered
// target must never make a farther object beat a nearer eligible object.
inline std::optional<PalmScore> palm_cost(const PalmProbe& probe,const PalmTarget& target,double hover_radius,double gate) {
    (void)hover_radius;
    if(!probe.valid || !palm_target_valid(target) || !std::isfinite(gate)) return {};
    Vec3 lo=target.minimum,hi=target.maximum;
    const Vec3 origin=palm_point(probe);
    const Vec3 point{std::clamp(origin.x,lo.x,hi.x),std::clamp(origin.y,lo.y,hi.y),std::clamp(origin.z,lo.z,hi.z)};
    const double distance=length(point-origin);
    if(!(distance<=gate)) return {};
    return PalmScore{distance,distance,point};
}
struct PalmPick {std::optional<std::size_t> index;double cost{},runner_up{};bool hysteresis_held{},fallback{};};
// Hover is not a hold: all candidates obey the same acquire gate. Equal distances keep
// the previous target, then use the lower id. A bad probe never acquires a new target.
template<class Targets> PalmPick pick_by_palm(const PalmProbe& probe,const Targets& targets,std::uint64_t current,
        double hover_radius,double acquire=grab_acquire_distance,double retain=grab_retain_distance) {
    PalmPick out;out.runner_up=std::numeric_limits<double>::infinity();
    (void)retain;
    if(!probe.valid) {
        out.fallback=true;
        std::size_t i=0;for(const auto& t:targets) {if(current && t.id==current) {out.index=i;break;}++i;}
        return out;
    }
    std::uint64_t best_id=0;double best=std::numeric_limits<double>::infinity();
    std::size_t i=0;
    for(const auto& t:targets) {
        const std::size_t at=i++;
        const bool held=current && t.id==current;
        const auto s=palm_cost(probe,t,hover_radius,acquire);
        if(!s) continue;
        const double effective=s->distance;
        if(effective<best || (effective==best && (held || (best_id!=current && t.id<best_id)))) {
            if(out.index) out.runner_up=std::min(out.runner_up,best);
            out.index=at;best=effective;best_id=t.id;
        } else out.runner_up=std::min(out.runner_up,effective);
    }
    if(out.index) out.cost=best;
    else out.runner_up=0;
    return out;
}
// What a hand's physical lane picked (NativeGrab::focus_score, plan Task 5): its raw palm cost, the runner-up's cost and the distance from the
// palm point. Read by the grip-alone decision (Task 6) and the grab_focus status block.
struct FocusScore {std::uint64_t id{};PalmKind kind{};double cost{},runner_up{},distance{};std::uintptr_t entity{};};   // entity: the picked body's entity
// Per-hand hover identity and diagnostics.
class PalmSelection {
public:
    // All hover candidates use acquire; retain remains an API compatibility argument.
    PalmPick update(const PalmProbe& probe,const std::vector<PalmTarget>& targets,double hover_radius,std::uint64_t now_ms,
            double acquire=grab_acquire_distance,double retain=grab_retain_distance) {
        PalmPick pick=pick_by_palm(probe,targets,current_,hover_radius,acquire,retain);
        if(pick.fallback) {++fallback_;return pick;}
        const std::uint64_t next=pick.index?targets[*pick.index].id:0;
        if(pick.hysteresis_held) ++held_;
        if(current_ && next && next!=current_) switches_.push_back(now_ms);
        current_=next;
        return pick;
    }
    std::uint64_t current() const {return current_;}
    unsigned switches_10s(std::uint64_t now_ms) const {
        unsigned n=0;for(const auto t:switches_) if(now_ms>=t && now_ms-t<=10000) ++n;return n;
    }
    std::uint64_t hysteresis_held() const {return held_;}
    std::uint64_t scoring_fallback() const {return fallback_;}
    void reset() {current_=0;switches_.clear();}
private:
    std::uint64_t current_{},held_{},fallback_{};
    std::vector<std::uint64_t> switches_;
};
struct NoLoot {bool operator()(std::uintptr_t) const noexcept {return false;}};
struct NativeUseVolumeCandidate {
    std::uintptr_t owner{};
    Vec3 minimum{},maximum{};
    // Must come from native interaction permission/range/obstruction checks.
    // A physics body, overlap, item name or model bounds do not grant eligibility.
    bool native_eligible{};
};
struct NativeUseVolumeFocus {std::uintptr_t owner{};Vec3 point{};double distance{};};
// Pure policy for a future native volume producer. It DOES NOT issue queries or
// publish native selection. Existing native shape construction/retirement must
// be proven before routing candidates through this policy in the live engine.
template<class Candidates>
std::optional<NativeUseVolumeFocus> native_use_volume_focus(const Candidates& candidates,Vec3 hand,std::uintptr_t preferred=0) {
    if(!finite(hand)) return {};
    std::optional<NativeUseVolumeFocus> best,retained;
    for(const auto& c:candidates) {
        if(!c.owner || !c.native_eligible || !finite(c.minimum) || !finite(c.maximum) ||
           c.minimum.x>c.maximum.x || c.minimum.y>c.maximum.y || c.minimum.z>c.maximum.z) continue;
        const Vec3 point{std::clamp(hand.x,c.minimum.x,c.maximum.x),std::clamp(hand.y,c.minimum.y,c.maximum.y),std::clamp(hand.z,c.minimum.z,c.maximum.z)};
        const double distance=length(point-hand);
        const NativeUseVolumeFocus focus{c.owner,point,distance};
        if(c.owner==preferred && distance<=grab_retain_distance) retained=focus;
        if(distance<=grab_acquire_distance && (!best || distance<best->distance)) best=focus;
    }
    if(retained && (!best || best->owner==retained->owner || best->distance+grab_switch_margin>=retained->distance)) return retained;
    return best;
}
// Native stage0 uses 112-byte hit records; fraction at +20 is a sweep
// fraction, which cannot rank initial overlaps by hand proximity. Preserve
// complete records; downstream native permissions and obstruction tests decide
// eligibility.
enum class NativeUseQueryOutcome : unsigned { incomplete,empty,single,multiple,rejected,count };
inline NativeUseQueryOutcome native_use_query_outcome(std::uintptr_t result,std::uintptr_t data,
        int count,unsigned capacity) noexcept {
    // Pinned0x92630 writes AL only (0x92b16/0x92b22), not the whole RAX.
    const auto completion=result&0xffu;
    if(completion==0)return NativeUseQueryOutcome::incomplete;
    if(completion!=1 || count<0 || count>64 || (capacity&0x3fffffff)<static_cast<unsigned>(count) || (count && !data))
        return NativeUseQueryOutcome::rejected;
    return count==0?NativeUseQueryOutcome::empty:count==1?NativeUseQueryOutcome::single:NativeUseQueryOutcome::multiple;
}
using NativeUseHit=std::array<std::byte,112>;
struct GrabTuning;   // grab_force_cap.hpp (it includes this header): the per-kind priority and the hover radius
// The v610 path's inputs as a probe: the hand point (volume_centre) and the beam axis (the aim ray's forward; zero: plain distance).
inline PalmProbe legacy_beam_probe(Vec3 hand,Vec3 axis) {PalmProbe p;p.palm=hand;p.forward=axis;p.valid=finite(hand);return p;}
// Hand mode always orders from the raw grip-centred sphere. The legacy knob and
// palm validity cannot change its origin or introduce an orientation requirement.
// Gaze/reach retains its existing forward beam policy.
struct NativeUseOrderProbe {PalmProbe probe{};bool legacy{},beam_fallback{};};
inline NativeUseOrderProbe native_use_order_probe(const PalmProbe& palm,bool legacy_knob,bool reach,Vec3 volume_centre,Vec3 aim_forward) {
    (void)palm;(void)legacy_knob;
    NativeUseOrderProbe out;out.legacy=reach;
    out.probe=reach?legacy_beam_probe(volume_centre,aim_forward):hand_sphere_probe(volume_centre);
    return out;
}
// Order hand-volume Use hits by distance to their owner's actual bounds (hit point when unknown).
// Hovering with either hand grants no preference or reservation. Native permission and LOS
// checks still decide eligibility after sorting. Full records are only permuted.
// Hand mode supplies hand_sphere_probe; gaze/reach mode retains the old forward beam.
template<class Owner,class Loot,class Bounds,class Tuning=GrabTuning>
bool order_native_use_hits(NativeUseHit* hits,unsigned count,const PalmProbe& probe,std::uintptr_t preferred,std::uintptr_t other_hand,
        Owner owner,Loot loot,Bounds bounds,const Tuning& tuning,bool legacy_beam=false) {
    if(!hits || count>64) return false;
    (void)other_hand; // another hand hovering this object does not reserve it
    struct Known {std::uintptr_t owner{};bool loot{};std::optional<std::pair<Vec3,Vec3>> box;};
    struct Scratch {
        std::array<Vec3,64> point;std::array<double,64> cost;std::array<unsigned char,64> tier;std::array<unsigned,64> order,eligible_hit;
        std::array<std::uintptr_t,64> owners;std::array<PalmTarget,64> targets,eligible;std::array<Known,64> known;std::array<NativeUseHit,64> copy;
    };
    static thread_local Scratch scratch;
    auto& point=scratch.point;auto& cost=scratch.cost;auto& tier=scratch.tier;auto& order=scratch.order;auto& eligible_hit=scratch.eligible_hit;
    auto& owners=scratch.owners;auto& targets=scratch.targets;auto& eligible=scratch.eligible;auto& known=scratch.known;auto& copy=scratch.copy;
    for(unsigned i=0;i<count;++i) {cost[i]=0;tier[i]=0;}
    for(unsigned i=0;i<count;++i) {
        std::array<float,3> xyz{};std::memcpy(xyz.data(),hits[i].data(),12);
        point[i]={xyz[0],xyz[1],xyz[2]};if(!finite(point[i])) return false;
    }
    if(legacy_beam) {
        const Vec3 hand=probe.palm;Vec3 axis=probe.forward;
        if(!finite(hand)) return false;
        const double axis_length=finite(axis)?length(axis):0;const bool beam=axis_length>1e-6;
        if(beam) axis=axis*(1/axis_length);
        for(unsigned i=0;i<count;++i) {
            cost[i]=length(point[i]-hand);if(!std::isfinite(cost[i]) || cost[i]>1000) return false;
            if(beam) {
                const double along=dot(point[i]-hand,axis),across=length(point[i]-hand-axis*along);
                cost[i]=along>=-.05 && across<=grab_beam_radius?across+grab_beam_range_weight*std::max(0.,along):cost[i]+1;
            }
            if(preferred && owner(hits[i])==preferred) cost[i]=std::max(0.,cost[i]-.02);
            if(loot(owner(hits[i]))) cost[i]-=use_loot_bonus;
        }
    } else {
        const Vec3 origin=palm_point(probe);
        if(probe.valid) for(unsigned i=0;i<count;++i) {const double d=length(point[i]-origin);if(!std::isfinite(d) || d>1000) return false;}
        // One target per hit; the owner's kind and bounds are read once per distinct owner.
        unsigned known_count=0;
        const auto valid_box=[](const std::pair<Vec3,Vec3>& b) {
            return finite(b.first) && finite(b.second) && b.first.x<=b.second.x && b.first.y<=b.second.y && b.first.z<=b.second.z;};
        for(unsigned i=0;i<count;++i) {
            const std::uintptr_t o=owner(hits[i]);owners[i]=o;
            const Known unresolved{};const Known* k=&unresolved;   // owner 0: no kind or bounds to read
            if(o) {
                k=nullptr;
                for(unsigned j=0;j<known_count;++j) if(known[j].owner==o) {k=&known[j];break;}
                if(!k) {
                    Known& fresh=known[known_count++];fresh={o,loot(o),{}};
                    if(auto b=bounds(o);b && valid_box(*b)) fresh.box=*b;
                    k=&fresh;
                }
            }
            PalmTarget& t=targets[i];t=PalmTarget{};
            t.id=o?static_cast<std::uint64_t>(o):(std::uint64_t{1}<<63)+i;   // an unresolved owner: a unique id that never matches preferred
            t.kind=k->loot?PalmKind::loot:PalmKind::usable;t.priority=tuning.priority(t.kind);
            t.minimum=k->box?k->box->first:point[i];t.maximum=k->box?k->box->second:point[i];
        }
        // Both hands may hover the same object; hovering reserves nothing.
        unsigned eligible_count=0;
        for(unsigned i=0;i<count;++i) {
            eligible_hit[eligible_count]=i;eligible[eligible_count++]=targets[i];
        }
        struct Span {const PalmTarget* first;const PalmTarget* last;const PalmTarget* begin() const {return first;} const PalmTarget* end() const {return last;}};
        const PalmPick pick=pick_by_palm(probe,Span{eligible.data(),eligible.data()+eligible_count},preferred,tuning.hover_radius_m);
        const std::uint64_t winner=pick.index?eligible[*pick.index].id:0;
        for(unsigned e=0;e<eligible_count;++e) {
            const unsigned i=eligible_hit[e];
            const auto s=probe.valid?palm_cost(probe,targets[i],tuning.hover_radius_m,grab_acquire_distance):std::nullopt;
            if(winner && targets[i].id==winner && (s || !probe.valid)) tier[i]=0;
            else if(!probe.valid) tier[i]=1;   // cost 0: native order
            else if(s) tier[i]=1;
            else tier[i]=2;
            cost[i]=s?s->cost:probe.valid?length(point[i]-origin)+1:0;
        }
    }
    for(unsigned i=0;i<count;++i) order[i]=i;
    std::stable_sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b){return tier[a]!=tier[b]?tier[a]<tier[b]:cost[a]<cost[b];});
    for(unsigned i=0;i<count;++i) copy[i]=hits[order[i]];
    for(unsigned i=0;i<count;++i) hits[i]=copy[i];return true;
}
// Ruling H14 (the prompt equals the take): a hand's remembered prompt target (the 500 ms held grace) that is loot survives that hand's
// own routed stage-0 cycle only while the cycle still selects it. A squeeze takes only the live native selection, so a loot prompt the
// hand's own native scan no longer selects would show a coin the squeeze cannot take. A routing flip to the other hand runs no cycle for
// this hand, so the linger still bridges flips; non-loot targets keep the old grace.
inline bool held_prompt_survives_cycle(std::uintptr_t held_owner,bool held_loot,std::uintptr_t cycle_owner) noexcept {
    return !held_owner || !held_loot || cycle_owner==held_owner;
}
// User decision 2026-10-25 / ruling H16 (hands plan Task 4 fix round 1): loot line of sight is judged FROM THE HAND. The native Use LOS
// (0x3b7b90, docs/native-use-los-3b7b90.md) sweeps from the camera eye to the loot's hit point; for a takeable owner in hand mode it sweeps
// from the CLAMPED palm instead: the drawn hand that the phase-1 hand clamp (or the hand body) kept out of the world, never the raw
// controller, so a controller pushed through a wall cannot take what is behind it. No clamped palm this cycle (no clamp, a yield, a
// re-acquire, stale), not loot, gaze/reach mode, or a palm implausibly far from the hit point: the eye (nullopt). Walls, closed drawers
// and doors between the hand and the item still block: the same sweep, filter and walk, only its start moves.
inline constexpr double loot_hand_los_max_m=1.0;   // the shared interaction volume is 0.45 m from the palm; a farther palm is not this hand's
inline std::optional<Vec3> loot_los_hand_origin(bool takeable,bool hand_mode,const std::optional<Vec3>& clamped_palm,Vec3 point) noexcept {
    if(!takeable || !hand_mode || !clamped_palm || !finite(*clamped_palm) || !finite(point)) return std::nullopt;
    if(length(*clamped_palm-point)>loot_hand_los_max_m) return std::nullopt;
    return *clamped_palm;
}
// Native LOS failure remains a failure. This permits only scanning the next
// object candidate; character obstruction metadata has different semantics.
inline bool continue_native_use_scan(std::uintptr_t native_result,unsigned char native_flag,
        bool owned_ranked,std::optional<bool> native_character) {
    return owned_ranked && (native_result&0xff)!=0 && native_flag==0 && native_character && !*native_character;
}
} // namespace d2vr
