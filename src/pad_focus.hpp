#pragma once
#include "game_rig.hpp"
#include <atomic>

namespace d2vr {
struct NativePadRoot {std::uintptr_t address{},wake_event{};bool native_enabled{};};
template<class Read> std::optional<NativePadRoot> native_pad_root(Read& read,std::uintptr_t base,std::uintptr_t address) {
    if(address!=base+0x3134140 || rig::value<std::uintptr_t>(read,address)!=base+0x1ae06d8) return {};
    const auto enabled=rig::value<unsigned char>(read,address+8),stopping=rig::value<unsigned char>(read,address+0x600);
    const auto count=rig::value<int>(read,address+0x5f8);
    const auto event=rig::value<std::uintptr_t>(read,address+0x5e8);
    if(!enabled || *enabled>1 || !stopping || *stopping || !count || *count<0 || *count>16 || !event || !*event || *event==~std::uintptr_t{}) return {};
    return NativePadRoot{address,*event,*enabled!=0};
}
class PadFocusLease {
    std::atomic<std::uint64_t> tick_{0};
    bool fresh(bool owned,std::uint64_t now) const noexcept {
        const auto tick=tick_.load(std::memory_order_acquire);
        return owned && tick && now>=tick && now-tick<=250;
    }
public:
    bool publish(bool focused,std::uint64_t now) {
        const auto before=tick_.exchange(focused?now:0,std::memory_order_acq_rel);
        return focused && (!before || now<before || now-before>250);
    }
    bool allows(std::uintptr_t caller_rva,bool owned,std::uint64_t now) const noexcept {
        if(!owned || (caller_rva!=0xe1098 && caller_rva!=0xe0146)) return false;
        return fresh(owned,now);
    }
    bool allows_serializer_window(std::uintptr_t caller_rva,bool owned,std::uint64_t now) const noexcept {
        // 0x3a470 processes keyboard/mouse first; this call gates only pads.
        return caller_rva==0x3a4ab && fresh(owned,now);
    }
};
} // namespace d2vr
