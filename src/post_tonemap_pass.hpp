#pragma once
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace d2vr {
// ARK_RENDERPASS_POST_TONEMAP job callback (0x323E70, registered by 0x34FE00 next to POSTPROC
// 0x327270): the pass after tonemapping that holds the game's resolution upscale. Same job
// context layout as POSTPROC (view at ctx+0x70). The detour marks the job thread with the eye
// of a known per-eye view for the duration of the call; everything else passes through.
class PostTonemapPass {
    using Pass=unsigned long long (__fastcall*)(void*);
    static constexpr std::uintptr_t rva=0x323E70;
    static constexpr std::array<unsigned char,16> prologue{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48};
    inline static Pass original_{};inline static void* target_{};inline static std::atomic<int> depth_{};
    static unsigned long long __fastcall hook(void* ctx) {
        std::uintptr_t view{};SIZE_T n{};
        int eye=-1;
        if(ReadProcessMemory(GetCurrentProcess(),static_cast<char*>(ctx)+0x70,&view,8,&n) && view) {
            const auto l=eye_views[0].load(),r=eye_views[1].load();
            if(l!=r) eye=view==l?0:view==r?1:-1;
        }
        if(eye<0) return original_(ctx);
        ++depth_;pass_eye=eye;pass_context=nullptr;
        unsigned long long result{};
        __try {result=original_(ctx);}
        __finally {
            pass_eye=-1;
            if(const auto end=pass_end.load()) end(eye,pass_context);
            --depth_;
        }
        return result;
    }
public:
    inline static std::array<std::atomic<std::uintptr_t>,2> eye_views{}; // published per eye with the TAA history views
    inline static thread_local int pass_eye=-1;
    inline static thread_local ID3D11DeviceContext* pass_context=nullptr; // set by bind observers inside the pass
    inline static std::atomic<void (*)(int,ID3D11DeviceContext*)> pass_end{};
    static bool install(std::uintptr_t base) noexcept {
        if(target_) return MH_EnableHook(target_)==MH_OK;
        void* target=reinterpret_cast<void*>(base+rva);std::array<unsigned char,16> code{};SIZE_T read{};
        if(!ReadProcessMemory(GetCurrentProcess(),target,code.data(),code.size(),&read) || read!=code.size() || code!=prologue) return false;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return false;}
        target_=target;return true;
    }
    static bool disable() noexcept {
        pass_end=nullptr;
        if(!target_) return true;
        // No drain wait: a pass in flight may itself wait on the render thread calling this, and the
        // detour code stays mapped (the DLL is retained until the game exits), so it finishes safely.
        const auto status=MH_DisableHook(target_);return status==MH_OK || status==MH_ERROR_DISABLED;
    }
    static bool installed() noexcept {return target_!=nullptr;}
};
} // namespace d2vr
