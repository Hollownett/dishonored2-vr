#pragma once
#include "game_rig.hpp"
#include <cstdint>
#include <optional>

namespace d2vr {
struct NativeTalkOfferReceipt {
    std::uintptr_t sensor{},component{},declaration{},selected_reference{},player{},owner{};
    rig::Identity sensor_identity{},component_identity{};
    std::uint32_t generation{},spawn{};
};

namespace native_talk_offer_detail {
inline constexpr std::uintptr_t sensor_vtable=0x1b403b8;
inline constexpr std::uintptr_t machine_vtable=0x1c73d58;
inline constexpr std::uintptr_t interaction_vtable=0x1c72e98;
inline constexpr std::uintptr_t selector_rva=0x747840;
inline constexpr std::uintptr_t talk_offset=0xe8;
inline constexpr std::uintptr_t busy_talk_offset=0x110;
inline constexpr std::uintptr_t loot_offset=0x98;
inline constexpr std::uintptr_t pickpocket_offset=0xc0;   // v963: the NPC's pocket, a primary offer like Talk/Loot
inline constexpr std::uint32_t no_spawn=0x3fff0010u;

template<class Read>
bool valid(Read& read,std::uintptr_t base,const NativeTalkOfferReceipt& receipt,std::uintptr_t primary_offset=talk_offset) {
    using namespace rig;
    if(!base || !pointer(receipt.sensor) || !pointer(receipt.component) || !pointer(receipt.declaration) ||
       !pointer(receipt.selected_reference) || !pointer(receipt.player) || !pointer(receipt.owner) ||
       receipt.generation==0xffffffffu || !receipt.spawn || receipt.spawn==no_spawn || receipt.spawn==0xffffffffu ||
       (receipt.spawn>>16)>=0x3fffu) return false;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=receipt.player ||
       value<std::uintptr_t>(read,*game+0x3130b0+(receipt.spawn>>16)*8)!=receipt.owner ||
       !typed(read,base,receipt.player,0x1c5e428) ||
       value<std::uint32_t>(read,receipt.owner+0xa8)!=receipt.spawn ||
       !typed(read,base,receipt.sensor,sensor_vtable) ||
       value<std::uintptr_t>(read,receipt.sensor+0x10)!=receipt.player ||
       started(read,receipt.sensor)!=std::optional<Identity>{receipt.sensor_identity}) return false;
    const auto machine=value<std::uintptr_t>(read,receipt.sensor+0x20);
    const auto interaction=machine?value<std::uintptr_t>(read,*machine+0x318):std::nullopt;
    const auto component_vtable=value<std::uintptr_t>(read,receipt.component);
    const auto selector=component_vtable?value<std::uintptr_t>(read,*component_vtable+0xd0):std::nullopt;
    const auto component_identity=started(read,receipt.component);
    const auto declaration=value<std::uintptr_t>(read,receipt.component+8);
    const auto component_owner=value<std::uintptr_t>(read,receipt.component+0x10);
    const auto selected_generation=interaction?value<std::int32_t>(read,*interaction+0x30):std::nullopt;
    const auto selected_component=interaction?value<std::uintptr_t>(read,*interaction+0x38):std::nullopt;
    const auto selected_reference=value<std::uintptr_t>(read,receipt.sensor+0x98);
    const auto selected_reference_generation=value<std::uint32_t>(read,receipt.sensor+0xa0);
    const auto raw_selected_generation=selected_reference?value<std::uint32_t>(read,*selected_reference+0x44):std::nullopt;
    const auto link=selected_reference?value<std::uintptr_t>(read,*selected_reference+0x18):std::nullopt;
    const auto selected_owner=link?value<std::uintptr_t>(read,*link+0x10):std::nullopt;
    const auto primary_offer=value<std::uintptr_t>(read,receipt.sensor+0xa8);
    const auto secondary_offer=value<std::uintptr_t>(read,receipt.sensor+0xb0);
    if(!machine || !typed(read,base,*machine,machine_vtable) || !interaction ||
       !typed(read,base,*interaction,interaction_vtable) ||
       value<std::uintptr_t>(read,*interaction+0x10)!=receipt.player ||
       !component_vtable || !selector || *selector!=base+selector_rva || !component_identity ||
       *component_identity!=receipt.component_identity || component_identity->generation!=static_cast<std::int32_t>(receipt.generation) ||
       !declaration || *declaration!=receipt.declaration || !component_owner || *component_owner!=receipt.owner ||
       !selected_generation || *selected_generation!=static_cast<std::int32_t>(receipt.generation) ||
       !selected_component || *selected_component!=receipt.component || !selected_reference ||
       *selected_reference!=receipt.selected_reference || !selected_reference_generation || !raw_selected_generation ||
       *selected_reference_generation!=*raw_selected_generation ||
       !link || !selected_owner || *selected_owner!=receipt.owner || !primary_offer || !secondary_offer ||
       *primary_offer!=receipt.declaration+primary_offset || *secondary_offer==receipt.declaration+primary_offset ||
       (primary_offset==talk_offset && *secondary_offer==receipt.declaration+busy_talk_offset) ||
       started(read,receipt.sensor)!=std::optional<Identity>{receipt.sensor_identity} ||
       started(read,receipt.component)!=std::optional<Identity>{receipt.component_identity} ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=receipt.player ||
       value<std::uint32_t>(read,receipt.owner+0xa8)!=receipt.spawn ||
       value<std::uintptr_t>(read,receipt.sensor+0x20)!=machine ||
       value<std::uintptr_t>(read,*machine+0x318)!=interaction ||
       value<std::int32_t>(read,*interaction+0x30)!=selected_generation ||
       value<std::uintptr_t>(read,*interaction+0x38)!=selected_component ||
       value<std::uintptr_t>(read,receipt.component)!=component_vtable ||
       value<std::uintptr_t>(read,*component_vtable+0xd0)!=selector ||
       value<std::uintptr_t>(read,receipt.component+8)!=declaration ||
       value<std::uintptr_t>(read,receipt.sensor+0x98)!=selected_reference ||
       value<std::uint32_t>(read,receipt.sensor+0xa0)!=selected_reference_generation ||
       value<std::uint32_t>(read,*selected_reference+0x44)!=raw_selected_generation ||
       value<std::uintptr_t>(read,receipt.sensor+0xa8)!=primary_offer ||
       value<std::uintptr_t>(read,receipt.sensor+0xb0)!=secondary_offer) return false;
    return true;
}
}

template<class Read>
std::optional<NativeTalkOfferReceipt> native_talk_offer(Read& read,std::uintptr_t base,
    std::uintptr_t sensor,std::uintptr_t player,std::uintptr_t component,std::uint32_t generation,
    std::uintptr_t owner,std::uint32_t spawn,std::uintptr_t primary_offset=native_talk_offer_detail::talk_offset) {
    using namespace rig;
    const auto sensor_identity=started(read,sensor),component_identity=started(read,component);
    const auto declaration=value<std::uintptr_t>(read,component+8);
    const auto selected_reference=value<std::uintptr_t>(read,sensor+0x98);
    if(!sensor_identity || !component_identity || !declaration || !selected_reference) return {};
    NativeTalkOfferReceipt receipt{sensor,component,*declaration,*selected_reference,player,owner,
        *sensor_identity,*component_identity,generation,spawn};
    if(!native_talk_offer_detail::valid(read,base,receipt,primary_offset)) return {};
    return receipt;
}

template<class Read>
bool native_talk_offer_current(Read& read,std::uintptr_t base,const NativeTalkOfferReceipt& receipt,std::uintptr_t primary_offset=native_talk_offer_detail::talk_offset) {
    return native_talk_offer_detail::valid(read,base,receipt,primary_offset);
}
}
