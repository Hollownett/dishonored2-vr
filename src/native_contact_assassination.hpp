#pragma once
#include "native_sword_contact.hpp"

namespace d2vr {
// Ordinary native kill offer for this exact contact victim. Reads only; RT still owns dispatch.
template<class Read> bool native_contact_assassination_offer(Read& read,const NativePlayerStateLease& owner,std::uint32_t victim_spawn) {
    using namespace rig;
    if(!victim_spawn || victim_spawn==NativeMelee::invalid_id || (victim_spawn>>16)>=0x3fff)return false;
    std::uintptr_t sensor{},victim{},handler{};std::optional<Identity> identity;
    std::optional<List> saved_components;
    for(unsigned pass=0;pass<2;++pass) {
        if(!(native_player_state_lease(read,owner.base,owner.machine)==std::optional<NativePlayerStateLease>{owner}))return false;
        const auto components=list(read,owner.player+0x268,4096);if(!components)return false;
        if(pass && (components->data!=saved_components->data || components->packed_count!=saved_components->packed_count ||
                    components->packed_capacity!=saved_components->packed_capacity))return false;
        std::uintptr_t found{};
        for(unsigned index=0;index<components->packed_count;++index) {
            const auto component=value<std::uintptr_t>(read,components->data+index*8ull);if(!component)return false;
            if(typed(read,owner.base,*component,0x1b3f998)) {if(found)return false;found=*component;}
        }
        if(!found || (pass && found!=sensor))return false;
        sensor=found;
        const auto current_identity=started(read,sensor);
        const auto current_handler=value<std::uintptr_t>(read,sensor+0x1d0);
        const auto current_victim=value<std::uintptr_t>(read,owner.game+0x3130b0+(victim_spawn>>16)*8ull);
        if(!current_identity || !current_handler || !pointer(*current_handler) || !current_victim || !pointer(*current_victim) ||
           (pass && (!(current_identity==identity) || *current_handler!=handler || *current_victim!=victim)))return false;
        identity=current_identity;handler=*current_handler;victim=*current_victim;saved_components=components;
        // kind 2 maps to embedded slot 1 (native 0x86b050 table {1,1,1,3,2}).
        const auto slot=sensor+0x78;
        const auto valid=[&] {
            return typed(read,owner.base,sensor,0x1b3f998) && started(read,sensor)==identity &&
                value<std::uintptr_t>(read,sensor+0x10)==owner.player && value<std::uintptr_t>(read,sensor+0x20)==owner.machine &&
                value<std::uint8_t>(read,sensor+0x38).value_or(0)!=0 && value<std::uint8_t>(read,sensor+0x1dc)==1 &&
                value<std::uint32_t>(read,sensor+0x1d8)==2u && value<std::uintptr_t>(read,sensor+0x1d0)==handler &&
                value<std::uintptr_t>(read,sensor+0x1b8)==handler && typed(read,owner.base,slot,0x1b3f978) &&
                value<std::uintptr_t>(read,slot+8)==sensor && value<std::uint32_t>(read,slot+0x20)==victim_spawn &&
                typed(read,owner.base,victim,NativeMelee::npc_vtable_rva) && value<std::uint32_t>(read,victim+0xa8)==victim_spawn &&
                value<std::uintptr_t>(read,owner.game+0x3130b0+(victim_spawn>>16)*8ull)==victim;
        };
        if(!valid() || !(started(read,sensor)==identity) ||
           !(native_player_state_lease(read,owner.base,owner.machine)==std::optional<NativePlayerStateLease>{owner}))return false;
    }
    return true;
}
}
