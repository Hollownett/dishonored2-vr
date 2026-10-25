#pragma once
#include "palm_offset.hpp"
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>
namespace d2vr {
inline constexpr double grab_acquire_distance=.45;
inline constexpr double grab_retain_distance=.55;
inline constexpr double grab_switch_margin=.10;
// Hand "beam": a palm-wide cylinder along the hand's forward axis. Items it touches outrank items that
// are merely near the palm; among them the one nearest the axis wins, with a small range penalty.
inline constexpr double grab_beam_radius=.06,grab_beam_range_weight=.25;
// Loot (pickups, carryables) within a palm width of the nearest hit wins: coins on a cash machine were ranked behind the
// machine's surface under the palm, which the grip then used (sim v467).
inline constexpr double use_loot_bonus=grab_beam_radius;
// Bonelab palm cost (research 1.1, Hand.UpdateHovering): facing x distance x approach x priority, 0.86 hysteresis on the current pick.
// ONE implementation shared by loot, props and corpse bones (plan Task 2, ruling H5: later tasks call PalmSelection; per-kind priority is a field on the target).
// Ruling H10: eligibility is the 0.45 m acquire distance from the palm point (not the hover sphere); hover_centre is only the scoring centre
// and the facing term handles the back of the hand. The 0.55 m retain gate keeps a held pick.
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
inline Vec3 palm_point(const PalmProbe& p) {return p.palm+p.forward*palm_axis_push;}
inline Vec3 hover_centre(const PalmProbe& p,double hover_radius) {return palm_point(p)+p.direction*(hover_lead*hover_radius);}
struct PalmTarget {std::uint64_t id{};PalmKind kind{};Vec3 minimum{},maximum{};double priority=1;Vec3 velocity{};};
struct PalmScore {double cost{},distance{};Vec3 point{};};
inline bool palm_target_valid(const PalmTarget& t) {
    return t.id && finite(t.minimum) && finite(t.maximum) && finite(t.velocity) && std::isfinite(t.priority) && t.priority>0 &&
           t.minimum.x<=t.maximum.x && t.minimum.y<=t.maximum.y && t.minimum.z<=t.maximum.z;
}
// Raw cost (no hysteresis) of one target, or nothing when it is farther than `gate` from the palm point (or the inputs are bad). No allocation.
inline std::optional<PalmScore> palm_cost(const PalmProbe& probe,const PalmTarget& target,double hover_radius,double gate) {
    (void)hover_radius;   // scoring centre only (ruling H10); distance and direction below are from the palm point, as the tests pin
    if(!probe.valid || !palm_target_valid(target) || !std::isfinite(gate)) return {};
    Vec3 lo=target.minimum,hi=target.maximum;
    if(target.kind==PalmKind::loot) {   // pad small pickups about their centre, for scoring only
        const Vec3 c=(lo+hi)*.5,h=(hi-lo)*.5;
        const Vec3 e{std::max(h.x,loot_min_half),std::max(h.y,loot_min_half),std::max(h.z,loot_min_half)};
        lo=c-e;hi=c+e;
    }
    const Vec3 origin=palm_point(probe);
    const Vec3 point{std::clamp(origin.x,lo.x,hi.x),std::clamp(origin.y,lo.y,hi.y),std::clamp(origin.z,lo.z,hi.z)};
    const double distance=length(point-origin);
    if(!(distance<=gate)) return {};
    Vec3 to_target=probe.direction;   // a palm on the item faces it
    double facing_dot=1;
    if(distance>1e-9) {to_target=(point-origin)*(1/distance);facing_dot=dot(probe.direction,to_target);}
    const double facing=(palm_facing_bias-facing_dot)*(palm_facing_bias-facing_dot)+1;
    const double dn=distance/(probe.arm_length*.25);
    const double close=dot(probe.velocity-target.velocity,to_target);
    const double approach=std::clamp(1-approach_per_mps*close,approach_min,approach_max);
    const double cost=facing*(dn*dn+1)*approach*target.priority;
    if(!std::isfinite(cost)) return {};
    return PalmScore{cost,distance,point};
}
struct PalmPick {std::optional<std::size_t> index;double cost{},runner_up{};bool hysteresis_held{},fallback{};};
// current==0: nothing held. A new target must be within `acquire` of the palm point; the current one stays eligible out to `retain` and its cost
// is multiplied by palm_hysteresis. Ties go to the lower id. A non-finite probe keeps `current` (if it is in the list) and sets fallback.
template<class Targets> PalmPick pick_by_palm(const PalmProbe& probe,const Targets& targets,std::uint64_t current,
        double hover_radius,double acquire=grab_acquire_distance,double retain=grab_retain_distance) {
    PalmPick out;out.runner_up=std::numeric_limits<double>::infinity();
    if(!probe.valid) {
        out.fallback=true;
        std::size_t i=0;for(const auto& t:targets) {if(current && t.id==current) {out.index=i;break;}++i;}
        return out;
    }
    std::uint64_t best_id=0;double best=std::numeric_limits<double>::infinity(),best_raw=best;
    double lowest_raw=best;   // best raw cost among everything eligible: tells whether hysteresis decided
    std::size_t i=0;
    for(const auto& t:targets) {
        const std::size_t at=i++;
        const bool held=current && t.id==current;
        const auto s=palm_cost(probe,t,hover_radius,held?std::max(retain,acquire):acquire);
        if(!s) continue;
        const double effective=held?s->cost*palm_hysteresis:s->cost;
        lowest_raw=std::min(lowest_raw,s->cost);
        if(effective<best || (effective==best && t.id<best_id)) {
            if(out.index) out.runner_up=std::min(out.runner_up,best);
            out.index=at;best=effective;best_raw=s->cost;best_id=t.id;
        } else out.runner_up=std::min(out.runner_up,effective);
    }
    if(out.index) {out.cost=best;out.hysteresis_held=current && best_id==current && lowest_raw<best_raw;}
    else out.runner_up=0;
    return out;
}
// Per-hand owner of the hysteresis state plus the counters.
class PalmSelection {
public:
    PalmPick update(const PalmProbe& probe,const std::vector<PalmTarget>& targets,double hover_radius,std::uint64_t now_ms) {
        PalmPick pick=pick_by_palm(probe,targets,current_,hover_radius);
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
// eligibility. The preferred body receives only a 2cm hysteresis allowance.
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
template<class Owner,class Loot=NoLoot>
bool order_native_use_hits(NativeUseHit* hits,unsigned count,Vec3 hand,std::uintptr_t preferred,Owner owner,Vec3 axis={},Loot loot={}) {
    if(!hits || count>64 || !finite(hand)) return false;
    const double axis_length=finite(axis)?length(axis):0;const bool beam=axis_length>1e-6;
    if(beam) axis=axis*(1/axis_length);
    std::array<double,64> distance{};std::array<unsigned,64> order{};
    for(unsigned i=0;i<count;++i) {
        std::array<float,3> xyz{};std::memcpy(xyz.data(),hits[i].data(),12);
        const Vec3 point{xyz[0],xyz[1],xyz[2]};if(!finite(point)) return false;
        distance[i]=length(point-hand);if(!std::isfinite(distance[i]) || distance[i]>1000) return false;
        if(beam) {
            const double along=dot(point-hand,axis),across=length(point-hand-axis*along);
            distance[i]=along>=-.05 && across<=grab_beam_radius?across+grab_beam_range_weight*std::max(0.,along):distance[i]+1;
        }
        if(preferred && owner(hits[i])==preferred) distance[i]=std::max(0.,distance[i]-.02);
        if(loot(owner(hits[i]))) distance[i]-=use_loot_bonus;
        order[i]=i;
    }
    std::stable_sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b){return distance[a]<distance[b];});
    std::array<NativeUseHit,64> copy{};for(unsigned i=0;i<count;++i) copy[i]=hits[order[i]];
    for(unsigned i=0;i<count;++i) hits[i]=copy[i];return true;
}
// Native LOS failure remains a failure. This permits only scanning the next
// object candidate; character obstruction metadata has different semantics.
inline bool continue_native_use_scan(std::uintptr_t native_result,unsigned char native_flag,
        bool owned_ranked,std::optional<bool> native_character) {
    return owned_ranked && (native_result&0xff)!=0 && native_flag==0 && native_character && !*native_character;
}
} // namespace d2vr
