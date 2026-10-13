#pragma once
#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace d2vr {
// hud.iggy draws MasterHUD, DamageVignette and world markers in one surface.
// Scaling that surface makes the full-screen damage mask's edges visible.
// Keep its native rect and coordinates until individual draw ownership is known.
// Evidence: artifacts/hud-re/hud-as3-damage.txt, artifacts/marker-re/w2s.txt.
class NativeHudRect {
    using Fn=void (__fastcall*)(void*,int,int,int,int,int,int);
    static constexpr std::uintptr_t rva=0x100CC30;
    static constexpr std::array<unsigned char,10> prologue{0x48,0x85,0xc9,0x0f,0x84,0x8e,0x00,0x00,0x00,0x66};
    inline static Fn original_{};
    inline static void* target_{};
    inline static std::uintptr_t base_{};
    inline static std::atomic<unsigned> depth_{};
    static void __fastcall hook(void* iggy,int w,int h,int rw,int rh,int rx,int ry) {
        ++depth_;++calls;last_w=rw;last_h=rh;
        __try {original_(iggy,w,h,rw,rh,rx,ry);}
        __finally {--depth_;}
    }
public:
    // Retain preference and diagnostics API for existing settings/log callers.
    // Whole-movie scaling and its two compensating coordinate hooks are disabled.
    inline static std::atomic<std::uint64_t> unshrunk_writes{},unshrunk_markers{};
    inline static std::atomic<float> scale{1.f};
    inline static std::atomic<bool> active{false};
    inline static std::atomic<std::uint64_t> calls{},scaled{};
    inline static std::atomic<int> last_w{},last_h{};
    static bool install(std::uintptr_t base) noexcept {
        if(!base || depth_) return false;
        if(target_) {
            if(base!=base_) return false;
            const auto status=MH_EnableHook(target_);
            return status==MH_OK || status==MH_ERROR_ENABLED;
        }
        void* target=reinterpret_cast<void*>(base+rva);
        std::array<unsigned char,10> code{};SIZE_T read{};
        if(!ReadProcessMemory(GetCurrentProcess(),target,code.data(),code.size(),&read) || read!=code.size() || code!=prologue) return false;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
        target_=target;base_=base;
        const auto status=MH_EnableHook(target_);
        return status==MH_OK || status==MH_ERROR_ENABLED;
    }
    static bool markers_corrected() noexcept {return false;}
    static bool disable() noexcept {
        active=false;if(!target_) return true;
        const auto status=MH_DisableHook(target_);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED) return false;
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        return !depth_; // Retain trampoline for a racing original-only call.
    }
};
} // namespace d2vr
