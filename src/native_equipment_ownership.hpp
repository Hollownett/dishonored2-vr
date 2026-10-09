#pragma once
#include "native_weapon_attachment_candidate.hpp"
#include <algorithm>

namespace d2vr {
struct NativeEquipmentReadGuard {
    std::uintptr_t address{};
    std::vector<unsigned char> bytes;
};
struct NativeEquipmentOwnership {
    NativeAttachmentCandidate attachment;
    std::uintptr_t game{},item{},item_declaration{},equipment{},equipment_declaration{};
    rig::Identity equipment_identity{};
    std::uint32_t child_id{},parent_id{},player_id{};
    std::vector<NativeEquipmentReadGuard> guards;
};
// These are ownership bytes only; palette data and native frame progress are
// excluded. Discovery enumerates bounded lists outside native callbacks.
template<class Read> bool native_equipment_current(Read& read,const NativeEquipmentOwnership& lease) {
    if(lease.guards.empty() || lease.guards.size()>512) return false;
    std::size_t total{};
    std::array<unsigned char,256> current{};
    for(const auto& guard:lease.guards) {
        total+=guard.bytes.size();
        if(guard.address<0x10000 || guard.bytes.empty() || guard.bytes.size()>32768 || total>131072 ||
           guard.address>=0x0000800000000000-guard.bytes.size()) return false;
    }
    // Fixed saved ranges only; two passes catch a generation changed after an
    // earlier range was read. No callback-time lists, allocation or discovery.
    for(unsigned pass=0;pass<2;++pass) for(const auto& guard:lease.guards)
        for(std::size_t offset=0;offset<guard.bytes.size();offset+=current.size()) {
            const auto bytes=std::min(current.size(),guard.bytes.size()-offset);
            if(!read(guard.address+offset,current.data(),bytes) ||
               std::memcmp(current.data(),guard.bytes.data()+offset,bytes)) return false;
        }
    return true;
}
// Exact-build Sword acquisition only. The native slot is an arkInventoryItem
// list element, never an arkCpntEquipment pointer. No writer eligibility here.
template<class Read> std::optional<NativeEquipmentOwnership> native_sword_equipment_ownership(
    Read& read,std::uintptr_t base,const rig::NativeArmBindingSnapshot& snapshot,const NativeAttachmentCandidate& expected) {
    using namespace rig;
    if(expected.arm.side!=1 || expected.joint!=59 || !expected.slots[1]) return {};
    NativeEquipmentOwnership result{};result.attachment=expected;
    std::size_t total{};
    auto guarded=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(!size || size>32768 || at<0x10000 || at>=0x0000800000000000-size ||
           result.guards.size()>=512 || total+size>131072 || !read(at,out,size)) return false;
        if(at==expected.binding+0x84 && size==4) return true; // Native progress, not generation.
        const auto* bytes=static_cast<const unsigned char*>(out);
        result.guards.push_back({at,{bytes,bytes+size}});total+=size;return true;
    };
    const auto attachment=native_attachment_candidate(guarded,base,snapshot,expected.binding,3);
    if(!attachment || !same_attachment_candidate(*attachment,expected)) return {};
    const auto game=value<std::uintptr_t>(guarded,base+0x39a5290);
    if(!game) return {};result.game=*game;
    const auto registered=[&](std::uintptr_t entity,std::uint32_t id) {
        return id!=0x3fff0010 && (id>>16)<0x3fff &&
            value<std::uintptr_t>(guarded,*game+0x3130b0+(id>>16)*8)==entity &&
            value<std::uint32_t>(guarded,entity+0xa8)==id;
    };
    const auto player_id=value<std::uint32_t>(guarded,expected.arm.player+0xa8);
    const auto child_id=value<std::uint32_t>(guarded,expected.entity+0xa8);
    const auto parent_id=value<std::uint32_t>(guarded,expected.parent+0xa8);
    if(!player_id || !child_id || !parent_id || !registered(expected.arm.player,*player_id) ||
       !registered(expected.entity,*child_id) || !registered(expected.parent,*parent_id) ||
       value<std::uint32_t>(guarded,expected.entity+0xac)!=player_id ||
       value<std::uint32_t>(guarded,expected.arm.body+0x680)!=parent_id ||
       !typed(guarded,base,expected.entity,0x1ce1e98)) return {};
    result.player_id=*player_id;result.child_id=*child_id;result.parent_id=*parent_id;
    const auto player_components=list(guarded,expected.arm.player+0x268,4096);
    if(!player_components || !player_components->packed_count) return {};
    std::vector<std::uintptr_t> player_entries(player_components->packed_count);
    if(!guarded(player_components->data,player_entries.data(),player_entries.size()*8) ||
       std::count(player_entries.begin(),player_entries.end(),expected.arm.body)!=1 ||
       std::count(player_entries.begin(),player_entries.end(),expected.inventory)!=1) return {};
    const auto items=list(guarded,expected.inventory+0x38,4096);
    const auto item=expected.slots[1];
    if(!items || !items->packed_count || item<items->data || (item-items->data)%0x38 ||
       (item-items->data)/0x38>=items->packed_count) return {};
    std::array<unsigned char,0x38> raw_item{};
    if(!guarded(item,raw_item.data(),raw_item.size())) return {};
    const auto item_decl=value<std::uintptr_t>(guarded,item);
    if(!item_decl || !typed(guarded,base,*item_decl,0x1b9ea48) ||
       value<std::uint32_t>(guarded,item+8)!=child_id) return {};
    result.item=item;result.item_declaration=*item_decl;
    const auto components=list(guarded,expected.entity+0x268,4096);
    if(!components || !components->packed_count) return {};
    std::vector<std::uintptr_t> entries(components->packed_count);
    if(!guarded(components->data,entries.data(),entries.size()*8) ||
       std::count(entries.begin(),entries.end(),expected.binding)!=1) return {};
    for(auto at:entries) if(typed(guarded,base,at,0x1b9d508)) {
        if(result.equipment) return {};result.equipment=at;
    }
    if(!result.equipment) return {};
    const auto equipment_id=started(guarded,result.equipment);
    const auto decl=value<std::uintptr_t>(guarded,result.equipment+8);
    if(!equipment_id || !decl || !typed(guarded,base,*decl,0x1b9d3c8) ||
       value<std::uintptr_t>(guarded,result.equipment+0x10)!=expected.entity ||
       value<std::uintptr_t>(guarded,result.equipment+0x38)!=expected.arm.body ||
       value<std::int32_t>(guarded,*decl+0x68)!=0) return {};
    result.equipment_identity=*equipment_id;result.equipment_declaration=*decl;
    if(!native_equipment_current(read,result)) return {};
    return result;
}
// Main/present discovery entry. Never call from native hooks.
template<class Read> std::optional<NativeEquipmentOwnership> native_sword_equipment_ownership(
    Read& read,std::uintptr_t base,const rig::NativeArmBindingSnapshot& snapshot) {
    using namespace rig;
    if(!native_arm_pair_owned(snapshot.arms)) return {};
    const auto inventory=value<std::uintptr_t>(read,snapshot.arms[1].body+0x60);
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!inventory || !game) return {};
    const auto item=value<std::uintptr_t>(read,*inventory+0x68);
    if(!item || !pointer(*item)) return {};
    const auto child_id=value<std::uint32_t>(read,*item+8);
    if(!child_id || *child_id==0x3fff0010 || (*child_id>>16)>=0x3fff) return {};
    const auto child=value<std::uintptr_t>(read,*game+0x3130b0+(*child_id>>16)*8);
    if(!child || !pointer(*child)) return {};
    const auto binding=value<std::uintptr_t>(read,*child+0x358);
    if(!binding) return {};
    const auto candidate=native_attachment_candidate(read,base,snapshot,*binding,3);
    if(!candidate || candidate->entity!=*child || candidate->slots[1]!=*item) return {};
    return native_sword_equipment_ownership(read,base,snapshot,*candidate);
}
} // namespace d2vr
