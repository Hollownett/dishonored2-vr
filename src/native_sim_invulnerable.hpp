#pragma once
// Simulated sessions only (build/sim-invulnerable, written by tools/vr_selftest.py for combat scenarios): the player ignores
// damage, so tests can fight alerted guards and bloodflies (engine god/buddha cvars are not honoured; sim v386 died to an
// NpcFatality within three swings). Hooks idEntity::TakeDamage 0x879270 (bytes pinned by NativeMelee::take_code). For the
// player it does what the engine does for an entity without a health component: construct the result (0x780470) and return it.
#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "native_melee.hpp"
#include <atomic>
#include <cstdint>

namespace d2vr {
class NativeSimInvulnerable {
public:
    inline static constexpr std::uintptr_t result_ctor_rva=0x780470;
    static bool install(std::uintptr_t base) noexcept {
        if(target_) {const bool ok=MH_EnableHook(target_)==MH_OK;NativeMelee::take_hooked=ok;return ok;}
        std::array<unsigned char,48> got{};SIZE_T n{};
        if(!base || !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+NativeMelee::take_rva),got.data(),got.size(),&n) ||
           n!=got.size() || got!=NativeMelee::take_code) return false;
        base_=base;void* t=reinterpret_cast<void*>(base+NativeMelee::take_rva);
        if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
        if(MH_EnableHook(t)!=MH_OK) {MH_RemoveHook(t);return false;}
        target_=t;NativeMelee::take_hooked=true;return true;
    }
    static void disable() noexcept {if(target_) MH_DisableHook(target_);NativeMelee::take_hooked=false;}
    inline static std::atomic<std::uint64_t> blocked{};
private:
    using Take=void* (__fastcall*)(std::uintptr_t,void*,void*);
    inline static Take original_{};inline static void* target_{};inline static std::uintptr_t base_{};
    static void* __fastcall hook(std::uintptr_t victim,void* result,void* params) {
        std::uintptr_t player{};
        __try {
            const auto game=*reinterpret_cast<const std::uintptr_t*>(base_+NativeMelee::game_rva);
            player=game?*reinterpret_cast<const std::uintptr_t*>(game+NativeMelee::entities_offset):0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {player=0;}
        if(!player || victim!=player || !result) return original_(victim,result,params);
        reinterpret_cast<void (__fastcall*)(void*)>(base_+result_ctor_rva)(result);
        ++blocked;return result;
    }
};
} // namespace d2vr
