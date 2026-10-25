#pragma once
#include "game_rig.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace d2vr {
struct NativeCameraState {unsigned index;std::uintptr_t vtable;bool combat;};
inline constexpr std::array<NativeCameraState,11> native_action_camera_states{{
    {8,0x1c82f68,false},{9,0x1c86e18,false},{13,0x1c83468,false},
    {33,0x1c3e568,false},{38,0x1c3d878,false},{41,0x1c77c18,false},
    {42,0x1c78d68,false},{55,0x1c3c108,false},{83,0x1c3fc08,false},
    {39,0x1c85d28,true},{44,0x1c81a68,true}}};
// Exact pinned-build player FSM entries, independently read by player_states.py.
// A nearby takedown candidate is not evidence that a finisher is running.
struct NativeActionCamera {
    bool scripted{},combat{};
    int state_index=-1; // diagnostics: the first active scripted/combat FSM index in table order, -1 when none
    bool owns_camera(bool native_camera_mode,bool force_native=false) const noexcept {
        return scripted || force_native || (native_camera_mode && !combat);
    }
};
class NativeActionCameraReader {
    std::uintptr_t player_{},machine_{};
    std::uint64_t next_scan_{};
public:
    template<class Read> NativeActionCamera poll(Read& read,std::uintptr_t base,
        std::uintptr_t player,std::uint64_t now) {
        using namespace rig;
        NativeActionCamera out;
        if(!typed(read,base,player,0x1c5e428)) {machine_=player_=0;return out;}
        if(player!=player_ || !typed(read,base,machine_,0x1c73d58) ||
           value<std::uintptr_t>(read,machine_+0x10)!=player) {
            machine_=0;
            if(player!=player_ || now>=next_scan_) {
                player_=player;next_scan_=now+500;
                if(const auto components=list(read,player+0x268,4096)) {
                    std::vector<std::uintptr_t> addresses(components->packed_count);
                    if(!addresses.empty() && read(components->data,addresses.data(),addresses.size()*sizeof(std::uintptr_t)))
                        for(const auto c:addresses) if(typed(read,base,c,0x1c73d58) &&
                            value<std::uintptr_t>(read,c+0x10)==player) {machine_=c;break;}
                }
            }
        }
        if(!machine_) return out;
        const auto machine=machine_;
        const auto pool=value<std::uintptr_t>(read,machine_+0x90);
        if(!pool || !pointer(*pool)) return out;
        constexpr auto& states=native_action_camera_states;
        struct Receipt {NativeCameraState entry;std::uintptr_t state;std::uint8_t active;};
        std::array<Receipt,states.size()> receipts{};std::size_t receipt_count{};
        for(const auto entry:states) {
            const auto state=value<std::uintptr_t>(read,*pool+0x10+entry.index*8);
            if(!state || !typed(read,base,*state,entry.vtable) ||
               value<std::uint32_t>(read,*state+8)!=entry.index ||
               value<std::uintptr_t>(read,*state+0x10)!=machine_) continue;
            const auto active=value<std::uint8_t>(read,*state+0x20);
            if(!active || *active>1) return {};
            receipts[receipt_count++]={entry,*state,*active};
            if(*active==1) {
                if(entry.combat) out.combat=true;else out.scripted=true;
                if(out.state_index<0) out.state_index=static_cast<int>(entry.index);
            }
        }
        // Ownership must come from one stable native FSM snapshot; a pool or
        // state transition midway through the read must not author just one eye.
        if(!typed(read,base,player,0x1c5e428) || !typed(read,base,machine,0x1c73d58) ||
           value<std::uintptr_t>(read,machine+0x10)!=player || value<std::uintptr_t>(read,machine+0x90)!=pool) return {};
        for(std::size_t i=0;i<receipt_count;++i) {const auto& receipt=receipts[i];if(
            value<std::uintptr_t>(read,*pool+0x10+receipt.entry.index*8)!=receipt.state ||
            !typed(read,base,receipt.state,receipt.entry.vtable) ||
            value<std::uint32_t>(read,receipt.state+8)!=receipt.entry.index ||
            value<std::uintptr_t>(read,receipt.state+0x10)!=machine ||
            value<std::uint8_t>(read,receipt.state+0x20)!=receipt.active) return {};}
        return out;
    }
};
} // namespace d2vr
