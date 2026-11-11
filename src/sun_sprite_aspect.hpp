#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <windows.h>
namespace d2vr {
// v1141 (user, 2026-11-11: "still that distortion of the moon in the tutorial"; sim tutorial_p: the moon drawn 45 x 81 px in a square eye,
// 81/45 = 1.8 ~ 16:9). The atmosphere pass draws the moon as the engine's sun sprite (0x30a800, atm/worldFog SunSpriteSize) and keeps it
// round with the aspect width/height of a render target ([rsi+0x1c]/[rsi+0x20] at 0x30a8c6), which stays the flat 16:9 one in VR. The three
// instructions that turn that size into a float aspect (0x30a8f2: cvtdq2ps xmm0,xmm0; cvtdq2ps xmm13,xmm13; divss xmm13,xmm0; 12 bytes) become
// mov eax,<aspect bits>; movd xmm13,eax; nop; nop with the eye's own aspect (tan extent width / height, the same for both canted eyes).
// eax and xmm0 are dead there (the next call takes rcx/rdx/r8 and clobbers both). Knob build/no-sun-sprite-aspect leaves the code alone.
class SunSpriteAspect {
public:
    static constexpr std::uintptr_t rva=0x30a8f2;
    static constexpr std::array<unsigned char,12> original{0x0f,0x5b,0xc0, 0x45,0x0f,0x5b,0xed, 0xf3,0x44,0x0f,0x5e,0xe8};
    static std::array<unsigned char,12> patched(float aspect) noexcept {
        std::array<unsigned char,12> b{0xb8,0,0,0,0, 0x66,0x44,0x0f,0x6e,0xe8, 0x90,0x90};
        std::memcpy(b.data()+1,&aspect,4);return b;
    }
    // Eye aspect from the eye's half-angle tangents (OpenXR fov: left/down negative).
    static float eye_aspect(float left,float right,float up,float down) noexcept {
        const double w=std::tan(right)-std::tan(left),h=std::tan(up)-std::tan(down);
        return w>1e-6 && h>1e-6 && std::isfinite(w/h)?static_cast<float>(w/h):0.f;
    }
    static void disable() noexcept {state_.store(-1);}
    static int state() noexcept {return state_.load();}   // 0 untouched, 1 patched, -1 refused/disabled
    static float applied() noexcept {float f;const auto b=bits_.load();std::memcpy(&f,&b,4);return f;}
    // Called per eye before Setup. Writes code only when the aspect changes (once at start, again after a FOV/resolution change).
    static void update(std::uintptr_t image_base,float aspect) noexcept {
        if(state_.load()<0 || !image_base || !(aspect>.2f && aspect<5.f)) return;
        std::uint32_t want;std::memcpy(&want,&aspect,4);
        if(state_.load()==1 && std::fabs(applied()-aspect)<1e-4f) return;
        auto* at=reinterpret_cast<unsigned char*>(image_base+rva);
        std::array<unsigned char,12> now{};SIZE_T n{};
        if(!ReadProcessMemory(GetCurrentProcess(),at,now.data(),now.size(),&n) || n!=now.size()) {state_.store(-1);return;}
        if(state_.load()==0 && now!=original) {state_.store(-1);return;}   // an unknown build or another patch: refuse
        if(state_.load()==1 && (now[0]!=0xb8 || std::memcmp(now.data()+5,patched(1.f).data()+5,7)!=0)) {state_.store(-1);return;}
        const auto b=patched(aspect);
        if(!WriteProcessMemory(GetCurrentProcess(),at,b.data(),b.size(),&n) || n!=b.size()) {state_.store(-1);return;}
        FlushInstructionCache(GetCurrentProcess(),at,b.size());
        bits_.store(want);state_.store(1);
    }
private:
    inline static std::atomic<int> state_{0};
    inline static std::atomic<std::uint32_t> bits_{0};
};
}
