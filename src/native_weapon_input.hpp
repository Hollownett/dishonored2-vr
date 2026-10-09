#pragma once
#include "native_weapon_dry_lease.hpp"
namespace d2vr {
struct NativeWeaponInput {
    enum Reason : unsigned {okay=0,disabled=1,unqualified=2,wrong_caller=3,wrong_span=4,invalid_pose=5,secondary_forbidden=6};
    std::shared_ptr<const NativeWeaponDryLease::Lease> lease;
    const void* source{};unsigned site{},reason{disabled};bool value_changed{};
    explicit operator bool() const {return lease && source && !reason;}
};
// Numeric input only. It retains copied values, never native objects or queries.
inline NativeWeaponInput native_weapon_input(std::shared_ptr<const NativeWeaponDryLease::Lease> lease,
    unsigned site,std::uintptr_t native_source,unsigned count,std::uintptr_t caller,bool requested,bool qualified) {
    NativeWeaponInput result{};result.site=site;
    if(!requested) return result;
    result.lease=lease;
    if(site==NativeWeaponDryLease::secondary) {result.reason=NativeWeaponInput::secondary_forbidden;return result;}
    if((site==NativeWeaponDryLease::primary && caller!=0x53d539) ||
       (site==NativeWeaponDryLease::gpu && caller!=0x541310) ||
       (site!=NativeWeaponDryLease::primary && site!=NativeWeaponDryLease::gpu)) {result.reason=NativeWeaponInput::wrong_caller;return result;}
    if(!qualified || !lease || !lease->equipment || lease->equipment->guards.empty() || lease->candidate.arm.side!=1 ||
       lease->candidate.joint!=59 || !same_attachment_candidate(lease->equipment->attachment,lease->candidate)) {
        result.reason=NativeWeaponInput::unqualified;return result;
    }
    const auto joint=site==NativeWeaponDryLease::primary?59u:0u;
    if(count!=(site==NativeWeaponDryLease::primary?1u:69u) || native_source!=lease->source.source+joint*64 ||
       reinterpret_cast<std::uintptr_t>(lease->posed.joints.data())%16 ||
       reinterpret_cast<std::uintptr_t>(&lease->posed.joints[joint])%16) {result.reason=NativeWeaponInput::wrong_span;return result;}
    for(unsigned i=joint;i<(site==NativeWeaponDryLease::primary?60u:69u);++i) if(!rig::valid(lease->posed.joints[i])) {
        result.reason=NativeWeaponInput::invalid_pose;return result;
    }
    for(unsigned i=joint;i<(site==NativeWeaponDryLease::primary?60u:69u);++i)
        for(unsigned column=0;column<4;++column) for(unsigned row=0;row<3;++row)
            if(std::memcmp(&lease->posed.joints[i].f[column*4+row],&lease->source.joints[i].f[column*4+row],4)) result.value_changed=true;
    result.source=&lease->posed.joints[joint];result.lease=std::move(lease);result.reason=NativeWeaponInput::okay;return result;
}
template<class Original> void native_weapon_input_call(NativeWeaponInput input,const void* fallback,Original&& original) {
    // The by-value lease remains alive through original return or exception.
    original(input?input.source:fallback);
}
} // namespace d2vr
