#pragma once
// GitHub issue #6: the grip cancels a held power aim (PowerCancelLatch, power_cancel.hpp) instead of casting it.
//
// arkPlayerStateUsePower (player state 63, vtable D2 0x1c80ed8) runs its release handler when the game's LT is let go
// (D2 0x8566f0, vtable slot 0x1c80f90; DOTO 0x8bf940, the same code but the power-handle field: add rcx,0x58 / 0x68):
//   power = resolve(state+K, 2); if(power is the player's power && ready(power) && power+0xa4 /*aim active*/) power->Cast();
//   end the state.
// The state's exit (D2 0x8564e0) calls StopAim (vt+0x128) instead when the aim is still active. So StopAim run just before the
// original makes the release a cancel: StopAim clears +0xa4 (base StopAim D2 0x61be50 / DOTO 0x671b00: mov word [rcx+0xa4],0;
// Far Reach's own StopAim 0x43f620 ends in it), the original sees no aim and only ends the state. Class-independent: every power
// inherits StopAim at vt+0x128 (docs/doto-research/2026-11-24-R4-grip-mines.md P1, re-read in both executables 2026-11-24).
// The control thread requests the cancel on the grip edge and releases LT at once; the request lives 500 ms.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
typedef enum MH_STATUS {
    MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace d2vr {
inline constexpr std::uintptr_t usepower_release_rva=0x8566f0,usepower_resolve_rva=0x3da910;
// mov [rsp+8],rbx; push rdi; sub rsp,0x20; mov rdi,rcx; mov edx,2 (both games), then add rcx,K (48 83 c1 K) and call resolve (e8 rel32)
inline constexpr std::array<std::byte,18> usepower_release_prologue{{
    std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x08},std::byte{0x57},std::byte{0x48},std::byte{0x83},
    std::byte{0xec},std::byte{0x20},std::byte{0x48},std::byte{0x8b},std::byte{0xf9},std::byte{0xba},std::byte{0x02},std::byte{0x00},
    std::byte{0x00},std::byte{0x00}
}};
// The cast gate inside the first 0x80 bytes: cmp [rbx+0xa4],al; je +0x1e; mov rax,[rbx]; mov rcx,rbx; call [rax+0x120]
inline constexpr std::array<std::byte,20> usepower_cast_gate{{
    std::byte{0x38},std::byte{0x83},std::byte{0xa4},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x74},std::byte{0x1e},
    std::byte{0x48},std::byte{0x8b},std::byte{0x03},std::byte{0x48},std::byte{0x8b},std::byte{0xcb},std::byte{0xff},std::byte{0x90},
    std::byte{0x20},std::byte{0x01},std::byte{0x00},std::byte{0x00}
}};

class NativePowerCancel {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    static constexpr std::uintptr_t power_aim_active=0xa4;
    static constexpr std::size_t stop_aim_slot=0x128/sizeof(void*);
    static constexpr std::uint64_t request_ms=500;
    // StopAim on a power whose aim is active, with its vtable and slot inside [lo,hi) (the game image). True when StopAim ran.
    static bool stop_aim(std::uintptr_t power,std::uintptr_t lo,std::uintptr_t hi) noexcept {
        __try {
            if(!power || !*reinterpret_cast<const volatile std::uint8_t*>(power+power_aim_active)) return false;
            const auto vt=*reinterpret_cast<const std::uintptr_t*>(power);
            if(vt<lo || vt>=hi) return false;
            const auto fn=reinterpret_cast<const std::uintptr_t*>(vt)[stop_aim_slot];
            if(fn<lo || fn>=hi) return false;
            reinterpret_cast<void (__fastcall*)(void*)>(fn)(reinterpret_cast<void*>(power));
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {faults_.fetch_add(1,std::memory_order_relaxed);return false;}
    }
    // Control thread, on the latch's cancel edge.
    static void request(std::uint64_t now_ms) noexcept {requested_until_.store(now_ms+request_ms,std::memory_order_release);requests_.fetch_add(1,std::memory_order_relaxed);}
    void install(std::uintptr_t base,std::size_t image_size,Read read,bool route=true) {
        if(!base || !image_size || !read || enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Invalid native power cancel ownership");
        std::array<std::byte,0x80> code{};
        if(!read(base+usepower_release_rva,code.data(),code.size())) throw std::runtime_error("Native power cancel: release handler unreadable");
        if(std::memcmp(code.data(),usepower_release_prologue.data(),usepower_release_prologue.size())!=0)
            throw std::runtime_error("Native power cancel: release handler differs");
        if(code[0x12]!=std::byte{0x48} || code[0x13]!=std::byte{0x83} || code[0x14]!=std::byte{0xc1} || code[0x16]!=std::byte{0xe8})
            throw std::runtime_error("Native power cancel: power handle lookup differs");
        std::int32_t rel{};std::memcpy(&rel,code.data()+0x17,4);
        const auto resolve=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(base+usepower_release_rva+0x1b)+rel);
        if(resolve!=base+usepower_resolve_rva) throw std::runtime_error("Native power cancel: power handle resolver differs");
        bool gate=false;
        for(std::size_t i=0x1b;i+usepower_cast_gate.size()<=code.size() && !gate;++i)
            gate=std::memcmp(code.data()+i,usepower_cast_gate.data(),usepower_cast_gate.size())==0;
        if(!gate) throw std::runtime_error("Native power cancel: cast gate differs");
        base_=base;image_end_=base+image_size;list_offset_=static_cast<std::uint8_t>(code[0x15]);
        resolve_=reinterpret_cast<Resolve>(resolve);route_=route;instance_=this;
        requested_until_=0;requests_=releases_=applied_=no_aim_=expired_=faults_=0;
        const auto target=reinterpret_cast<void*>(base+usepower_release_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native power cancel hook");
        if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native power cancel hook");
        enabled_=true;
    }
    bool disable() noexcept {
        route_=false;
        if(!enabled_) return true;
        const auto s=MH_DisableHook(reinterpret_cast<void*>(base_+usepower_release_rva));
        if(s==MH_OK || s==MH_ERROR_DISABLED) enabled_=false;
        return !enabled_;
    }
    bool installed() const noexcept {return enabled_;}
    std::string status_json() const {
        std::ostringstream o;
        o<<"{\"installed\":"<<(enabled_?"true":"false")<<",\"routing\":"<<(route_.load()?"true":"false")<<",\"list_offset\":"<<static_cast<unsigned>(list_offset_)
         <<",\"requests\":"<<requests_.load()<<",\"releases\":"<<releases_.load()<<",\"applied\":"<<applied_.load()<<",\"no_aim\":"<<no_aim_.load()
         <<",\"expired\":"<<expired_.load()<<",\"faults\":"<<faults_.load()<<'}';
        return o.str();
    }
private:
    using Release=void (__fastcall*)(void*);
    using Resolve=std::uintptr_t (__fastcall*)(const void*,int);
    static void __fastcall detour(void* state) {
        auto& self=*instance_;
        releases_.fetch_add(1,std::memory_order_relaxed);
        const auto until=requested_until_.exchange(0,std::memory_order_acq_rel);
        if(until && self.route_.load(std::memory_order_acquire)) {
            if(GetTickCount64()<=until) {
                std::uintptr_t power=0;
                __try {power=self.resolve_(reinterpret_cast<const std::byte*>(state)+self.list_offset_,2);}
                __except(EXCEPTION_EXECUTE_HANDLER) {faults_.fetch_add(1,std::memory_order_relaxed);power=0;}
                if(stop_aim(power,self.base_,self.image_end_)) applied_.fetch_add(1,std::memory_order_relaxed);
                else no_aim_.fetch_add(1,std::memory_order_relaxed);
            } else expired_.fetch_add(1,std::memory_order_relaxed);
        }
        self.original_(state);
    }
    inline static NativePowerCancel* instance_{};
    inline static std::atomic<std::uint64_t> requested_until_{},requests_{},releases_{},applied_{},no_aim_{},expired_{},faults_{};
    Release original_{};Resolve resolve_{};
    std::uintptr_t base_{},image_end_{};std::uint8_t list_offset_{};bool enabled_{};
    std::atomic<bool> route_{false};
};
} // namespace d2vr
