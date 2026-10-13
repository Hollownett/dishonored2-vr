#pragma once
// Faster native sword attack: ARK_ATTRIBUTE_SLASH_SPEED_FACTOR is the play rate the slash state (0x86cbf0) gives its
// attack scene, so the scene's hit window (MELEE_WEAPON_ACTIVE_FRAMES, 200-270 ms) comes sooner (docs/native-melee-damage.md).
// Layout (live 2026-10-12): player state machine +0x330 = arkCpntAttributesPlayer (vtable 0x1ca7938), +0x48 = entries of
// 0x28 bytes {char* name, ..., +0x10 max, +0x20 float, +0x24 current}; the slash state (vtable 0x1c85d28, machine pool
// +0x90) holds its entry index at +0x108. Writes are a compare-exchange of the aligned +0x20 qword: only a value the game
// left at 1.0 is changed, and only our own value is put back.
#include "game_rig.hpp"
#include <cstdint>
#include <cstring>

namespace d2vr {
class NativeSlashSpeed {
public:
    inline static constexpr std::uintptr_t slash_vtable_rva=0x1c85d28,attributes_vtable_rva=0x1ca7938;
    inline static constexpr char name[]="ARK_ATTRIBUTE_SLASH_SPEED_FACTOR";
    // Exchange: bool(uintptr address, uint64 before, uint64 after) - atomic, fault-safe.
    template<class Read,class Exchange> bool apply(Read& read,std::uintptr_t base,std::uintptr_t machine,float factor,Exchange& exchange) {
        const auto at=locate(read,base,machine);if(!at) return false;
        const auto word=rig::value<std::uint64_t>(read,at);if(!word) return false;
        float current{};std::memcpy(&current,reinterpret_cast<const char*>(&*word)+4,4);
        if(current==factor) {entry_=at;factor_=factor;return true;}
        if(current!=1.f) return false; // a game modifier owns it: leave it alone
        if(!exchange(at,*word,with(*word,factor))) return false;
        entry_=at;factor_=factor;return true;
    }
    template<class Read,class Exchange> void restore(Read& read,Exchange& exchange) {
        if(!entry_) return;
        if(const auto word=rig::value<std::uint64_t>(read,entry_)) {
            float current{};std::memcpy(&current,reinterpret_cast<const char*>(&*word)+4,4);
            if(current==factor_) exchange(entry_,*word,with(*word,1.f));
        }
        entry_=0;
    }
    std::uintptr_t entry() const noexcept {return entry_;}
private:
    std::uintptr_t entry_{};float factor_{1.f};
    static std::uint64_t with(std::uint64_t word,float v) noexcept {std::memcpy(reinterpret_cast<char*>(&word)+4,&v,4);return word;}
    // Address of the entry's +0x20 qword (float +0x20, current +0x24), or 0.
    template<class Read> static std::uintptr_t locate(Read& read,std::uintptr_t base,std::uintptr_t machine) {
        using namespace rig;
        const auto attributes=value<std::uintptr_t>(read,machine+0x330),pool=value<std::uintptr_t>(read,machine+0x90);
        if(!attributes || !typed(read,base,*attributes,attributes_vtable_rva) || !pool || !pointer(*pool)) return 0;
        const auto entries=value<std::uintptr_t>(read,*attributes+0x48);if(!entries || !pointer(*entries)) return 0;
        for(unsigned i=0;i<160;++i) {
            const auto state=value<std::uintptr_t>(read,*pool+0x10+i*8ull);
            if(!state || !typed(read,base,*state,slash_vtable_rva)) continue;
            const auto index=value<std::int32_t>(read,*state+0x108);if(!index || *index<0 || *index>4096) return 0;
            const std::uintptr_t entry=*entries+static_cast<std::uintptr_t>(*index)*0x28;
            const auto text=value<std::uintptr_t>(read,entry);char got[sizeof(name)]{};
            if(!text || !read(*text,got,sizeof(got)) || std::memcmp(got,name,sizeof(name))!=0) return 0;
            return entry+0x20;
        }
        return 0;
    }
};
} // namespace d2vr
