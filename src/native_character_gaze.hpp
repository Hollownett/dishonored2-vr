#pragma once
#include "native_grab.hpp"
#include "native_sword_contact.hpp"
#include "native_carry_snapshot.hpp"
#include "native_offered_choke.hpp"

namespace d2vr {
inline bool native_character_choke_owner(const std::optional<NativeOfferedChoke::Receipt>& offered,
        const NativePlayerCamera& camera,const rig::NativeCarrySnapshot& carry) noexcept {
    return offered && offered->owner.player==camera.player && offered->owner.machine==carry.machine &&
        offered->owner.game==carry.game && offered->owner.identity==carry.machine_identity;
}
inline bool native_character_gaze_lanes(const std::array<NativeGrab::DiagnosticLane,2>& lanes,
        const NativePlayerCamera& owner,std::uint64_t epoch,std::uint64_t now) noexcept {
    if(!epoch || !now || owner.authored_view() || !lanes[0].lifecycle_revision ||
       lanes[0].lifecycle_revision!=lanes[1].lifecycle_revision)return false;
    for(const auto& lane:lanes) {
        const auto& published=lane.publication_owner;
        if(lane.epoch!=epoch || lane.player!=owner.player || lane.camera!=owner.camera ||
           published.player!=owner.player || published.camera!=owner.camera || published.body!=owner.body ||
           published.controller!=owner.controller || published.physics!=owner.physics || published.heading_delegate!=owner.heading_delegate ||
           published.camera_identity!=owner.camera_identity ||
           published.physics_identity!=owner.physics_identity || published.authored_view() ||
           !lane.serial || !lane.publication_tick || !lane.source_tick ||
           now<lane.publication_tick || now-lane.publication_tick>grab_pose_freshness_ms ||
           now<lane.source_tick || now-lane.source_tick>grab_pose_freshness_ms || !lane.usable ||
           lane.occupied || lane.held || lane.pending_release || lane.deferred ||
           !std::isfinite(lane.raw_grip) || lane.raw_grip<0 || lane.raw_grip>=.35f ||
           (lane.role!=RightGripOwner::Role::release && lane.role!=RightGripOwner::Role::ready))return false;
    }
    return true;
}
template<class Read,class Diagnostics,class Idle,class Clock>
bool native_character_gaze_idle(Read& read,std::uintptr_t base,const NativePlayerCamera& owner,
        const rig::NativeCarrySnapshot& previous,std::uint64_t epoch,std::uint64_t now,
        Diagnostics&& diagnostics,Idle&& idle,Clock&& clock) {
    if(!idle() || previous.player!=owner.player || previous.body!=owner.body)return false;
    const auto before=diagnostics(now);
    const auto before_tick=clock();
    if(before_tick<now || !before || !native_character_gaze_lanes(*before,owner,epoch,before_tick))return false;
    const auto carry=rig::native_carry_current(read,base,previous);
    if(!carry || carry->carrying())return false;
    const auto spawn=rig::value<std::uint32_t>(read,owner.player+0xa8);
    if(!spawn || !*spawn)return false;
    const NativePlayerStateLease state{base,carry->game,carry->player,carry->machine,*spawn,carry->machine_identity};
    const auto scene=native_contact_scene(read,state);
    if(!scene)return false;
    const auto after_carry=rig::native_carry_current(read,base,*carry);
    const auto after_scene=native_contact_scene(read,state);
    const auto after=diagnostics(before_tick);
    const auto after_tick=clock();
    if(after_tick<before_tick || !after_carry || *after_carry!=*carry || !after_scene || after_scene->pool!=scene->pool || after_scene->states!=scene->states || !after ||
       !native_character_gaze_lanes(*after,owner,epoch,after_tick) || !idle())return false;
    for(unsigned side=0;side<2;++side)
        if((*before)[side].lifecycle_revision!=(*after)[side].lifecycle_revision ||
           (*after)[side].serial<(*before)[side].serial ||
           (*after)[side].publication_tick<(*before)[side].publication_tick ||
           (*after)[side].source_tick<(*before)[side].source_tick)return false;
    return true;
}
}
