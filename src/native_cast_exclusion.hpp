#pragma once
#include "native_rigid_body.hpp"
#include "native_world_trace.hpp"
#include <cstdint>
#include <optional>
namespace d2vr {
// The body the hand clamp's exclusion sweep must not report (physical hands I1). A prop (bone < 0): its own body. A dead or unconscious guard:
// EXACTLY the body the contact service met, by the bone and handle it published (any of the 17: a torso push is excluded as a torso), read again
// here and accepted only while it is still that handle, dynamic, owned by this entity (ragdoll_owned) and not carried (ragdoll_unclaimed).
// native_rigid_body refuses a guard, and a limb merely nearest the hand is not the body that was met. Nothing mapped (another world, an awake
// guard, a reassigned body, no bone published): empty, and the caller casts the sweep WITHOUT exclusion (never drops it).
// info_zero: cast with a zero filter word (a lab switch).
template<class Read> std::optional<NativeCastExclusion> native_cast_exclusion(Read& read,std::uintptr_t base,std::uintptr_t world,std::uintptr_t entity,int bone,std::uint32_t handle,bool info_zero) {
    std::optional<NativeRigidBody> body;
    if(bone<0) body=native_rigid_body(read,base,entity,true);
    else {
        body=native_ragdoll_body(read,base,entity,bone);
        if(body && (body->handle!=handle || !ragdoll_pushable_state(read,entity) || !ragdoll_pushable_body(read,*body))) body.reset();
    }
    if(!body || body->world!=world) return {};
    if(info_zero) return NativeCastExclusion{body->record,body->handle,0};
    std::uint32_t info{};
    if(read(body->record+0x6c,&info,sizeof(info)) && info) return NativeCastExclusion{body->record,body->handle,info};
    return {};
}
// One game-thread scope's mapped exclusions, keyed by (entity, side, bone, handle): the two hands meet different bodies of one corpse, and a
// failed mapping is remembered too. map() runs once per key.
class NativeExclusionCache {
public:
    struct Entry {std::uintptr_t entity{};unsigned side{};int bone=-1;std::uint32_t handle{};std::optional<NativeCastExclusion> mapped;};
    template<class Map> std::optional<NativeCastExclusion> get(std::uintptr_t entity,unsigned side,int bone,std::uint32_t handle,Map&& map) {
        for(unsigned i=0;i<count_;++i) if(entries_[i].entity==entity && entries_[i].side==side && entries_[i].bone==bone && entries_[i].handle==handle) return entries_[i].mapped;
        const auto mapped=map();
        if(count_<entries_.size()) entries_[count_++]={entity,side,bone,handle,mapped};
        return mapped;
    }
private:
    std::array<Entry,8> entries_{};unsigned count_{};
};
}
