#pragma once
#include "game_rig.hpp"

namespace d2vr {
struct NativeMenuState {
    std::uintptr_t root{},manager{},wrapper{},file{},player{};
    std::array<float,2> cached_stage_cursor{};
};
inline bool native_menu_cache_valid(const std::array<float,2>& cursor) {
    std::array<std::uint32_t,2> bits{};std::memcpy(bits.data(),cursor.data(),sizeof(bits));
    // Native wrapper initialization 0x34200 uses 0x1ae8a70 for both dirty axes.
    if(bits==std::array<std::uint32_t,2>{0x7149f2ca,0x7149f2ca}) return true;
    for(auto v:cursor) if(!std::isfinite(v) || std::abs(v)>1e6f) return false;
    return true;
}
// any_batch (v722, world UI only): the wrapper may sit in any batch, not only the newest (the menu that takes input).
template<class Read> std::optional<NativeMenuState> native_menu_state(Read& read,std::uintptr_t base,std::uintptr_t manager,std::uintptr_t wrapper,bool any_batch=false) {
    using namespace rig;
    const auto root=value<std::uintptr_t>(read,base+0x2274270);
    if(!root || !pointer(*root) || manager!=*root+0x18) return {};
    const auto callback=value<std::uintptr_t>(read,manager+0x828);
    if(!callback || !pointer(*callback) || value<std::uintptr_t>(read,*callback)!=base+0x1abe800 ||
       value<std::uintptr_t>(read,*callback+8)!=manager || value<std::uintptr_t>(read,*callback+0x10)!=base+0x32d40) return {};
    const auto batches=list(read,manager+8,1024);
    if(!batches || !batches->packed_count) return {};
    const auto holds=[&](const std::optional<List>& f) {return f && f->packed_count && wrapper>=f->data &&
        !((wrapper-f->data)%0xde8) && (wrapper-f->data)/0xde8<f->packed_count;};
    auto batch=batches->data+(batches->packed_count-1)*32;
    auto files=list(read,batch+0x10,64);
    for(std::uint32_t b=batches->packed_count-1;any_batch && !holds(files) && b-->0;) {batch=batches->data+b*32;files=list(read,batch+0x10,64);}
    if(!holds(files)) return {};
    const auto file=value<std::uintptr_t>(read,wrapper),player=file?value<std::uintptr_t>(read,*file+0x1618):std::nullopt;
    const auto dispatch=file?value<std::uintptr_t>(read,*file+0x1608):std::nullopt;
    const auto member=list(read,wrapper+0x878,16);
    NativeMenuState s{*root,manager,wrapper,file.value_or(0),player.value_or(0),{}};
    if(!file || !pointer(*file) || !player || !pointer(*player) || !dispatch || !pointer(*dispatch) ||
       !member || !member->packed_count || !read(wrapper+0xb08,s.cached_stage_cursor.data(),8)) return {};
    if(!native_menu_cache_valid(s.cached_stage_cursor)) return {};
    const auto after=list(read,batch+0x10,64),owner_after=list(read,manager+8,1024);
    if(!after || !owner_after || after->data!=files->data || after->packed_count!=files->packed_count ||
       owner_after->data!=batches->data || owner_after->packed_count!=batches->packed_count ||
       value<std::uintptr_t>(read,base+0x2274270)!=root || value<std::uintptr_t>(read,wrapper)!=file ||
       value<std::uintptr_t>(read,*file+0x1618)!=player || value<std::uintptr_t>(read,*file+0x1608)!=dispatch ||
       value<std::uintptr_t>(read,manager+0x828)!=callback) return {};
    return s;
}
} // namespace d2vr
