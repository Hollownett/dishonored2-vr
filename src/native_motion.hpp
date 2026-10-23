#pragma once
// hknpMotion read for a native body (velocity and the inverse-mass half); shared by native_hand_contact.hpp and mass_probe.hpp.
#include "native_rigid_body.hpp"
namespace d2vr {
struct NativeMotion {Vec3 velocity{};std::uint16_t inverse_mass{};};
// B14 (the hand body's read-back): the same hknpMotion layout, unfiltered (velocity +0x40, spin +0x50 of the motion at record +0x40 in the
// world's motion array +0x120). nullopt only when the memory cannot be read; a non-finite or huge value is returned as read, for the caller to judge.
struct NativeMotionRaw {Vec3 velocity{},spin{};};
template<class Read> std::optional<NativeMotionRaw> native_motion_raw(Read& read,std::uintptr_t world,std::uintptr_t record) {
    using namespace rig;
    const auto index=value<std::uint32_t>(read,record+0x40);
    const auto array=value<std::uintptr_t>(read,world+0x120);
    if(!index || *index>=0x100000 || !array || !pointer(*array)) return {};
    const auto at=*array+static_cast<std::uintptr_t>(*index)*0x80;
    const auto v=value<std::array<float,3>>(read,at+0x40);const auto w=value<std::array<float,3>>(read,at+0x50);
    if(!v || !w) return {};
    return NativeMotionRaw{{(*v)[0],(*v)[1],(*v)[2]},{(*w)[0],(*w)[1],(*w)[2]}};
}
template<class Read> std::optional<NativeMotion> native_motion(Read& read,const NativeRigidBody& body) {
    using namespace rig;
    const auto index=value<std::uint32_t>(read,body.record+0x40);
    const auto array=value<std::uintptr_t>(read,body.world+0x120);
    if(!index || *index>=0x100000 || !array || !pointer(*array)) return {};
    const auto at=*array+static_cast<std::uintptr_t>(*index)*0x80;
    const auto v=value<std::array<float,3>>(read,at+0x40);const auto m=value<std::uint16_t>(read,at+0x26);
    if(!v || !m) return {};
    const Vec3 velocity{(*v)[0],(*v)[1],(*v)[2]};
    if(!finite(velocity) || length(velocity)>100) return {};
    return NativeMotion{velocity,*m};
}
}
