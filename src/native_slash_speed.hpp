#pragma once
#include "game_rig.hpp"
#include <cstdint>
#include <cstring>

namespace d2vr {
struct NativePlayerStateLease {
    std::uintptr_t base{},game{},player{},machine{};
    std::uint32_t spawn{};
    rig::Identity identity{};
    bool operator==(const NativePlayerStateLease& other) const noexcept {
        return base==other.base && game==other.game && player==other.player && machine==other.machine &&
            spawn==other.spawn && identity==other.identity;
    }
};
template<class Read> std::optional<NativePlayerStateLease> native_player_state_lease(Read& read,std::uintptr_t base,std::uintptr_t machine) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    const auto player=game?value<std::uintptr_t>(read,*game+0x3130b0):std::nullopt;
    const auto spawn=player?value<std::uint32_t>(read,*player+0xa8):std::nullopt;
    const auto identity=started(read,machine);
    if(!game || !typed(read,base,*game,0x1c940a8) || !player || !typed(read,base,*player,0x1c5e428) ||
       !spawn || !*spawn || !identity || !typed(read,base,machine,0x1c73d58) || value<std::uintptr_t>(read,machine+0x10)!=player) return {};
    const auto components=list(read,*player+0x268,4096);if(!components)return {};
    for(unsigned index=0;index<components->packed_count;++index)
        if(value<std::uintptr_t>(read,components->data+index*8ull)==machine)
            return NativePlayerStateLease{base,*game,*player,machine,*spawn,*identity};
    return {};
}
} // namespace d2vr
