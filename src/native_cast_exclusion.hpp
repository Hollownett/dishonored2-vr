#pragma once
#include "native_rigid_body.hpp"
#include "native_world_trace.hpp"
#include <cstdint>
#include <optional>
namespace d2vr {
// The body the hand clamp's exclusion sweep must not report (physical hands I1): a prop's own body, or, for a dead or unconscious guard
// (the push logic offers him as pushable, native_hand_contact.hpp), his ragdoll limb nearest the hand. native_rigid_body refuses a guard,
// so mapping props alone dropped every sweep with a corpse met. Nothing mapped (another world, an awake guard's keyframed bodies, a stale
// entity): empty, and the caller casts the sweep WITHOUT exclusion (never drops it). info_zero: cast with a zero filter word (a lab switch).
template<class Read> std::optional<NativeCastExclusion> native_cast_exclusion(Read& read,std::uintptr_t base,std::uintptr_t world,std::uintptr_t entity,Vec3 hand,bool info_zero) {
    auto body=native_rigid_body(read,base,entity,true);
    if(!body) body=nearest_ragdoll_body(read,base,entity,hand);
    if(!body || body->world!=world) return {};
    if(info_zero) return NativeCastExclusion{body->record,body->handle,0};
    std::uint32_t info{};
    if(read(body->record+0x6c,&info,sizeof(info)) && info) return NativeCastExclusion{body->record,body->handle,info};
    return {};
}
}
