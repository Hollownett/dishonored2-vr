#pragma once
#include "game_rig.hpp"
namespace d2vr {
struct NativeLeanOwner {
    std::uintptr_t game{},player{},machine{},controller{},body{};
    std::uint32_t player_id{};
    rig::Identity machine_identity{},controller_identity{},body_identity{};
};
struct NativeLeanSnapshot {
    NativeLeanOwner owner;
    std::uintptr_t pool{},state{},declaration{},input_device{};
    std::uint32_t action_index{};
    bool active{},gamepad{};
    int toggle_option{};
};
// Call only when acquiring/refreshing an owner. Snapshot polling below never
// scans component lists, names, registries or state arrays.
template<class Read> std::optional<NativeLeanOwner> native_lean_owner(
        Read& read,std::uintptr_t base,std::uintptr_t player,std::uintptr_t machine) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    const auto id=value<std::uint32_t>(read,player+0xa8);
    if(!game || !id || !typed(read,base,*game,0x1c940a8) ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=player || !typed(read,base,player,0x1c5e428) ||
       !typed(read,base,machine,0x1c73d58) || value<std::uintptr_t>(read,machine+0x10)!=player) return {};
    const auto identity=started(read,machine);
    const auto controller=value<std::uintptr_t>(read,machine+0x2f8),body=value<std::uintptr_t>(read,machine+0x300);
    if(!identity || !controller || !body || !typed(read,base,*controller,0x1c72918) ||
       !typed(read,base,*body,0x1c5a8d8,0x1c5a9b0) ||
       value<std::uintptr_t>(read,*controller+0x10)!=player || value<std::uintptr_t>(read,*body+0x10)!=player) return {};
    const auto ci=started(read,*controller),bi=started(read,*body);
    if(!ci || !bi) return {};
    NativeLeanOwner result{*game,player,machine,*controller,*body,*id,*identity,*ci,*bi};
    if(value<std::uintptr_t>(read,base+0x39a5290)!=game || value<std::uintptr_t>(read,*game+0x3130b0)!=player ||
       value<std::uint32_t>(read,player+0xa8)!=id || started(read,machine)!=identity ||
       value<std::uintptr_t>(read,machine+0x10)!=player || value<std::uintptr_t>(read,machine+0x2f8)!=controller ||
       value<std::uintptr_t>(read,machine+0x300)!=body || started(read,*controller)!=ci || started(read,*body)!=bi ||
       value<std::uintptr_t>(read,*controller+0x10)!=player || value<std::uintptr_t>(read,*body+0x10)!=player ||
       !typed(read,base,*controller,0x1c72918) || !typed(read,base,*body,0x1c5a8d8,0x1c5a9b0) ||
       !typed(read,base,player,0x1c5e428) || !typed(read,base,machine,0x1c73d58)) return {};
    return result;
}
template<class Read> std::optional<NativeLeanOwner> find_native_lean_owner(Read& read,std::uintptr_t base) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return {};
    const auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428)) return {};
    const auto components=list(read,*player+0x268,128);if(!components) return {};
    std::vector<std::uintptr_t> entries(components->packed_count);
    if(!entries.empty() && !read(components->data,entries.data(),entries.size()*8)) return {};
    std::uintptr_t machine{};
    for(auto at:entries) if(typed(read,base,at,0x1c73d58) && value<std::uintptr_t>(read,at+0x10)==player) {
        if(machine) return {};machine=at;
    }
    const auto final_list=list(read,*player+0x268,128);
    if(!machine || !final_list || final_list->data!=components->data || final_list->packed_count!=components->packed_count || final_list->packed_capacity!=components->packed_capacity) return {};
    std::vector<std::uintptr_t> again(entries.size());
    if(!again.empty() && !read(components->data,again.data(),again.size()*8)) return {};
    if(entries!=again) return {};
    return native_lean_owner(read,base,*player,machine);
}
// Exact sources: 802ea0/802a20 state pool +90, bounded index<5a;
// 85d400 Lean vtable,853ce0 owner/index,853d40/8541a0 active flag;
// 85d350 runtime action index;39d50 input device;138f3e0 toggle cvar.
template<class Read> std::optional<NativeLeanSnapshot> native_lean_snapshot(
        Read& read,std::uintptr_t base,const NativeLeanOwner& owner) {
    using namespace rig;
    const auto current=native_lean_owner(read,base,owner.player,owner.machine);
    const auto same=[&](const NativeLeanOwner& a) {
        return a.game==owner.game && a.player_id==owner.player_id && a.controller==owner.controller && a.body==owner.body &&
            a.machine_identity==owner.machine_identity && a.controller_identity==owner.controller_identity && a.body_identity==owner.body_identity;
    };
    if(!current || !same(*current)) return {};
    const auto pool=value<std::uintptr_t>(read,owner.machine+0x90);if(!pool || !pointer(*pool)) return {};
    const auto state=value<std::uintptr_t>(read,*pool+0x10+18*8);
    if(!state || !typed(read,base,*state,0x1c82b28) || value<int>(read,*state+8)!=18 ||
       value<std::uintptr_t>(read,*state+0x10)!=owner.machine) return {};
    const auto declaration=value<std::uintptr_t>(read,*state+0x18);
    const auto active=value<unsigned char>(read,*state+0x20);
    const auto action=value<std::uint32_t>(read,*state+0x30);
    const auto device=value<std::uintptr_t>(read,base+0x2274340);
    const auto option=value<int>(read,base+0x3995628);
    if(!declaration || !typed(read,base,*declaration,0x1c829f8) || value<int>(read,*declaration+0x60)!=18 || !active || *active>1 || !action || *action>=64 ||
       !device || !pointer(*device) || !option || *option<0 || *option>1 ||
       value<std::uintptr_t>(read,base+0x3995630)!=base+0x1c82bf8) return {};
    const auto gamepad=value<unsigned char>(read,*device+0xa544);if(!gamepad || *gamepad>1) return {};
    const auto again=native_lean_owner(read,base,owner.player,owner.machine);
    if(!again || !same(*again) || value<std::uintptr_t>(read,owner.machine+0x90)!=pool ||
       value<std::uintptr_t>(read,*pool+0x10+18*8)!=state || !typed(read,base,*state,0x1c82b28) ||
       value<int>(read,*state+8)!=18 || value<std::uintptr_t>(read,*state+0x10)!=owner.machine ||
       value<std::uintptr_t>(read,*state+0x18)!=declaration || value<unsigned char>(read,*state+0x20)!=active ||
       value<std::uint32_t>(read,*state+0x30)!=action || value<std::uintptr_t>(read,base+0x2274340)!=device ||
       value<unsigned char>(read,*device+0xa544)!=gamepad || value<int>(read,base+0x3995628)!=option ||
       value<std::uintptr_t>(read,base+0x3995630)!=base+0x1c82bf8 || !typed(read,base,*declaration,0x1c829f8) || value<int>(read,*declaration+0x60)!=18) return {};
    return NativeLeanSnapshot{owner,*pool,*state,*declaration,*device,*action,*active!=0,*gamepad!=0,*option};
}
} // namespace d2vr
