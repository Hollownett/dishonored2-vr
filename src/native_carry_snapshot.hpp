#pragma once
#include "game_rig.hpp"

namespace d2vr::rig {
// Native HoldObject/CarryCorpse are separate from inventory equip slots.
// Exact Steam17576867 state and declaration RTTI, confirmed in paused bottle carry.
struct NativeCarrySnapshot {
    std::uintptr_t game{},player{},body{},machine{},pool{},component_slot{};
    List components{};Identity machine_identity{},body_identity{};
    std::array<std::uintptr_t,2> states{},declarations{};
    std::array<bool,2> active{};
    bool carrying()const noexcept{return active[0] || active[1];}
    bool valid_owner()const noexcept{return game && player && body && machine && pool && components.packed_count && components.packed_count<=4096 &&
        component_slot>=components.data && component_slot-components.data<static_cast<std::uintptr_t>(components.packed_count)*8 &&
        (component_slot-components.data)%8==0 && states[0] && states[1] && declarations[0] && declarations[1];}
};
inline bool operator==(const NativeCarrySnapshot& a,const NativeCarrySnapshot& b) {
    return a.game==b.game && a.player==b.player && a.body==b.body && a.machine==b.machine && a.pool==b.pool &&
        a.component_slot==b.component_slot && a.components.data==b.components.data && a.components.packed_count==b.components.packed_count &&
        a.components.packed_capacity==b.components.packed_capacity && a.machine_identity==b.machine_identity && a.body_identity==b.body_identity &&
        a.states==b.states && a.declarations==b.declarations && a.active==b.active;
}
inline bool operator!=(const NativeCarrySnapshot& a,const NativeCarrySnapshot& b){return !(a==b);}
// Fixed callback-time revalidation; never scans a heap list or follows a carried object's unverified fields.
template<class Read> std::optional<NativeCarrySnapshot> native_carry_current(Read& read,std::uintptr_t base,const NativeCarrySnapshot& old) {
    if(!old.valid_owner())return {};
    const auto root=[&] {
        const auto components=list(read,old.player+0x268,4096);
        return value<std::uintptr_t>(read,base+0x39a5290)==old.game && typed(read,base,old.game,0x1c940a8) &&
            value<std::uintptr_t>(read,old.game+0x3130b0)==old.player && typed(read,base,old.player,0x1c5e428) &&
            typed(read,base,old.body,0x1c5a8d8,0x1c5a9b0) && value<std::uintptr_t>(read,old.body+0x10)==old.player &&
            started(read,old.body)==old.body_identity && typed(read,base,old.machine,0x1c73d58) &&
            value<std::uintptr_t>(read,old.machine+0x10)==old.player && started(read,old.machine)==old.machine_identity &&
            value<std::uintptr_t>(read,old.machine+0x300)==old.body && value<std::uintptr_t>(read,old.machine+0x90)==old.pool &&
            components && components->data==old.components.data && components->packed_count==old.components.packed_count &&
            components->packed_capacity==old.components.packed_capacity && value<std::uintptr_t>(read,old.component_slot)==old.machine;
    };
    constexpr std::array<int,2> indices{49,45};
    constexpr std::array<std::uintptr_t,2> state_tables{0x1c7a998,0x1c77748},decl_tables{0x1c7a868,0x1c77618};
    auto result=old;std::array<bool,2> first{};
    for(unsigned pass=0;pass<2;++pass){
        if(!root())return {};
        for(unsigned side=0;side<2;++side){const auto at=old.states[side],decl=old.declarations[side];
            const auto active=value<unsigned char>(read,at+0x20);
            if(value<std::uintptr_t>(read,old.pool+0x10+static_cast<unsigned>(indices[side])*8)!=at || !typed(read,base,at,state_tables[side]) ||
               value<int>(read,at+8)!=indices[side] || value<std::uintptr_t>(read,at+0x10)!=old.machine ||
               value<std::uintptr_t>(read,at+0x18)!=decl || !typed(read,base,decl,decl_tables[side]) ||
               value<int>(read,decl+0x60)!=indices[side] || !active || *active>1)return {};
            if(!pass)first[side]=*active!=0;else if(first[side]!=(*active!=0))return {};
        }
    }
    if(!root())return {};result.active=first;return result;
}
// Discovery is allowed only when acquiring a new complete native hand snapshot.
template<class Read> std::optional<NativeCarrySnapshot> native_carry_snapshot(Read& read,std::uintptr_t base,std::uintptr_t player,std::uintptr_t body) {
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);const auto components=list(read,player+0x268,4096);
    const auto body_id=started(read,body);
    if(!game || !components || !components->packed_count || !body_id)return {};
    std::vector<std::uintptr_t> entries(components->packed_count);
    if(!read(components->data,entries.data(),entries.size()*8))return {};
    NativeCarrySnapshot result{};result.game=*game;result.player=player;result.body=body;result.body_identity=*body_id;result.components=*components;
    for(unsigned i=0;i<entries.size();++i)if(typed(read,base,entries[i],0x1c73d58) && value<std::uintptr_t>(read,entries[i]+0x10)==player){
        if(result.machine)return {};result.machine=entries[i];result.component_slot=components->data+i*8;
    }
    if(!result.machine)return {};const auto identity=started(read,result.machine);const auto pool=value<std::uintptr_t>(read,result.machine+0x90);
    if(!identity || !pool || !pointer(*pool))return {};result.machine_identity=*identity;result.pool=*pool;
    constexpr std::array<unsigned,2> indices{49,45};
    for(unsigned i=0;i<2;++i){const auto state=value<std::uintptr_t>(read,*pool+0x10+indices[i]*8);if(!state || !pointer(*state))return {};result.states[i]=*state;
        const auto decl=value<std::uintptr_t>(read,*state+0x18);if(!decl || !pointer(*decl))return {};result.declarations[i]=*decl;}
    const auto current=native_carry_current(read,base,result);if(!current)return {};
    std::vector<std::uintptr_t> again(entries.size());if(!read(components->data,again.data(),again.size()*8) || again!=entries)return {};
    return current;
}
} // namespace d2vr::rig
