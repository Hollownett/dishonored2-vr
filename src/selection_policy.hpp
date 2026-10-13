#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cstring>
namespace d2vr {
inline constexpr double grab_acquire_distance=.45;
inline constexpr double grab_retain_distance=.55;
inline constexpr double grab_switch_margin=.10;
// Hand "beam": a palm-wide cylinder along the hand's forward axis. Items it touches outrank items that
// are merely near the palm; among them the one nearest the axis wins, with a small range penalty.
inline constexpr double grab_beam_radius=.06,grab_beam_range_weight=.25;
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
template<class Owner>
bool order_native_use_hits(NativeUseHit* hits,unsigned count,Vec3 hand,std::uintptr_t preferred,Owner owner,Vec3 axis={}) {
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
