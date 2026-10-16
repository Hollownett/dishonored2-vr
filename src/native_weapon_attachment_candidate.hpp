#pragma once
#include "native_arm_binding_snapshot.hpp"
namespace d2vr {
enum class NativeAttachmentInventoryScope {all_slots,attached_slot};
struct NativeAttachmentCandidate {
    rig::Arm arm{};
    rig::Identity binding_identity{},inventory_identity{};
    std::uintptr_t binding{},entity{},parent{},inventory{},entity_table{},parent_table{},declaration{},declaration_table{};
    std::array<std::uintptr_t,2> slots{},slot_tables{},slot_owner_fields{};
    std::array<UINT64,2> slot_identity_fields{};
    std::int16_t joint{};
    std::uint32_t binding_cached_frame{};
};
// The declaration is the attachment component's declaration. Inventory slots
// remain raw identity tuples; no equipment-object equivalence is asserted.
inline bool same_attachment_candidate(const NativeAttachmentCandidate& a,const NativeAttachmentCandidate& b) {
    return a.arm==b.arm && a.binding==b.binding && a.binding_identity==b.binding_identity &&
        a.entity==b.entity && a.parent==b.parent && a.inventory==b.inventory && a.inventory_identity==b.inventory_identity &&
        a.entity_table==b.entity_table && a.parent_table==b.parent_table && a.declaration==b.declaration &&
        a.declaration_table==b.declaration_table && a.joint==b.joint && a.slots==b.slots &&
        a.slot_tables==b.slot_tables && a.slot_owner_fields==b.slot_owner_fields && a.slot_identity_fields==b.slot_identity_fields;
    // binding_cached_frame is native progress, not an ownership generation.
}
// Bounded pointer/identity checks only. No component enumeration or name walk.
template<class Read> std::optional<NativeAttachmentCandidate> native_attachment_candidate(
    Read& read,std::uintptr_t base,const rig::NativeArmBindingSnapshot& snapshot,std::uintptr_t binding,int stage,
    NativeAttachmentInventoryScope inventory_scope=NativeAttachmentInventoryScope::all_slots) {
    using namespace rig;
    if(stage!=3 || !typed(read,base,binding,0x1c573e8,0x1c574c0)) return {};
    const auto declaration=value<std::uintptr_t>(read,binding+8);
    if(!declaration || !pointer(*declaration)) return {};
    const auto declaration_table=value<std::uintptr_t>(read,*declaration);
    if(!declaration_table || !pointer(*declaration_table)) return {};
    const auto id=started(read,binding);const auto animation=value<std::uintptr_t>(read,binding+0x400);
    const auto parent=value<std::uintptr_t>(read,binding+0x3c8),entity=value<std::uintptr_t>(read,binding+0x10);
    const auto joint=value<std::int16_t>(read,binding+0x3d0);
    if(!id || !animation || !parent || !entity || !pointer(*parent) || !pointer(*entity) ||
       !joint || *joint<0 || *joint>=69 || value<std::uintptr_t>(read,*entity+0x358)!=binding) return {};
    unsigned side=2;
    for(unsigned i=0;i<2;++i) if(snapshot.arms[i].animation==*animation) side=i;
    if(side==2) return {};
    const auto& arm=snapshot.arms[side];
    const auto valid_arm=[&]() {
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);
        const auto descriptor=value<std::uintptr_t>(read,arm.model+0x1fe8);
        return game && typed(read,base,*game,0x1c940a8) && value<std::uintptr_t>(read,*game+0x3130b0)==arm.player &&
            typed(read,base,arm.player,0x1c5e428) && typed(read,base,arm.body,0x1c5a8d8,0x1c5a9b0) &&
            value<std::uintptr_t>(read,arm.body+0x10)==arm.player && started(read,arm.body)==std::optional<Identity>(arm.body_identity) &&
            value<std::uintptr_t>(read,arm.body+(side?0x688:0x378))==arm.animation && typed(read,base,arm.animation,0x1b80408,0x1b804f0) &&
            started(read,arm.animation)==std::optional<Identity>(arm.animation_identity) && value<std::uintptr_t>(read,arm.animation+0x10)==parent &&
            value<std::uintptr_t>(read,arm.animation+0x38)==arm.model && typed(read,base,arm.model,0x1b0d628) &&
            value<std::uintptr_t>(read,*parent+0xf0)==arm.model && descriptor && pointer(*descriptor) &&
            value<std::uintptr_t>(read,*descriptor+0x50)==arm.skeleton && value<std::uint16_t>(read,arm.skeleton+0x78)==69;
    };
    if(!valid_arm()) return {};
    const auto inventory=value<std::uintptr_t>(read,arm.body+0x60);
    if(!inventory || !typed(read,base,*inventory,0x1b9a878) || value<std::uintptr_t>(read,*inventory+0x10)!=arm.player) return {};
    const auto inventory_id=started(read,*inventory);if(!inventory_id) return {};
    NativeAttachmentCandidate candidate{};candidate.arm=arm;candidate.binding=binding;candidate.binding_identity=*id;
    candidate.declaration=*declaration;candidate.declaration_table=*declaration_table;
    candidate.entity=*entity;candidate.parent=*parent;candidate.inventory=*inventory;candidate.inventory_identity=*inventory_id;candidate.joint=*joint;
    const auto entity_table=value<std::uintptr_t>(read,*entity),parent_table=value<std::uintptr_t>(read,*parent);
    const auto frame=value<std::uint32_t>(read,binding+0x84);
    std::array<std::uintptr_t,2> native_slots{};
    const auto read_slots=[&](std::array<std::uintptr_t,2>& output) {
        if(inventory_scope==NativeAttachmentInventoryScope::all_slots)return read(*inventory+0x68,output.data(),sizeof(output));
        if(inventory_scope!=NativeAttachmentInventoryScope::attached_slot)return false;
        return read(*inventory+0x68+8*(1-side),&output[1-side],sizeof(std::uintptr_t));
    };
    if(!entity_table || !parent_table || !frame || !read_slots(native_slots)) return {};
    candidate.entity_table=*entity_table;candidate.parent_table=*parent_table;candidate.binding_cached_frame=*frame;
    candidate.slots={native_slots[1],native_slots[0]}; // Public left/right order, native enum right/left.
    for(unsigned i=0;i<2;++i) if(candidate.slots[i]) {
        if(!pointer(candidate.slots[i])) return {};
        const auto table=value<std::uintptr_t>(read,candidate.slots[i]);
        const auto owner=value<std::uintptr_t>(read,candidate.slots[i]+0x10);
        const auto identity=value<UINT64>(read,candidate.slots[i]+0x18);
        if(!table || !owner || !identity) return {};
        candidate.slot_tables[i]=*table;candidate.slot_owner_fields[i]=*owner;candidate.slot_identity_fields[i]=*identity;
    }
    std::array<std::uintptr_t,2> again{};
    if(!valid_arm() || !typed(read,base,binding,0x1c573e8,0x1c574c0) || started(read,binding)!=id ||
       value<std::uintptr_t>(read,binding+8)!=declaration || value<std::uintptr_t>(read,*declaration)!=declaration_table ||
       value<std::uintptr_t>(read,binding+0x10)!=entity || value<std::uintptr_t>(read,*entity+0x358)!=binding ||
       value<std::uintptr_t>(read,binding+0x3c8)!=parent || value<std::uintptr_t>(read,binding+0x400)!=animation ||
       value<std::int16_t>(read,binding+0x3d0)!=joint || value<std::uintptr_t>(read,arm.body+0x60)!=inventory ||
       started(read,*inventory)!=inventory_id || !read_slots(again) || again!=native_slots) return {};
    if(value<std::uintptr_t>(read,*entity)!=entity_table || value<std::uintptr_t>(read,*parent)!=parent_table ||
       value<std::uintptr_t>(read,*inventory+0x10)!=arm.player || !typed(read,base,*inventory,0x1b9a878)) return {};
    for(unsigned i=0;i<2;++i) if(candidate.slots[i] &&
        (value<std::uintptr_t>(read,candidate.slots[i])!=candidate.slot_tables[i] ||
         value<std::uintptr_t>(read,candidate.slots[i]+0x10)!=candidate.slot_owner_fields[i] ||
         value<UINT64>(read,candidate.slots[i]+0x18)!=candidate.slot_identity_fields[i])) return {};
    return candidate;
}

} // namespace d2vr
