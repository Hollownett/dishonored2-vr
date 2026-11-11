#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
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

// v1145 (user, headset v1144: "the moon is round, but still oval when I change my camera view"): the sprite's quad is a constant size in NDC wherever it
// sits, and in the wide rectilinear eye a screen circle off the axis covers 1/cos^2 radially but 1/cos tangentially, so an off-centre moon is squeezed
// toward the centre (0.64 at 50 deg). The scale constant (size,0,0,size*aspect) the pass sends at 0x30ac7f (0x310ad0: float4 xmm2,xmm3,[rsp+20],[rsp+28];
// the centre (ndcX,ndcY,0,aspect) follows at 0x30ac9d) becomes sqrt(S), S the angular disc's image under the eye's projection at that centre:
// S = r^2 F G F, G = n^2 [[1+u^2,uv],[uv,1+v^2]], u=x/fx, v=y/fy, n^2=1+u^2+v^2, r=size/fx, F=diag(fx,fy) - (size,0,0,size*fy/fx) at the centre, so the
// v1141 aspect is its special case. The render frustum is symmetric (canted_fov), so the projection is (fx,fy) about the centre for both eyes.
// Hook: 23 bytes at 0x30ac6d (mulss xmm0,xmm13; store [rsp+28]; store [rsp+20]; call 0x310ad0) -> jmp to a cave that hands (x,y,size,aspect) to shape()
// and calls 0x310ad0 with the result, then returns to 0x30ac84. Live there: xmm10 size, xmm11/xmm12 ndc x/y, xmm13 aspect (disassembly checked).
class SunSpriteShape {
public:
    static constexpr std::uintptr_t hook_rva=0x30ac6d,resume_rva=0x30ac84,setter_rva=0x310ad0;
    static constexpr std::array<unsigned char,23> original{0xf3,0x41,0x0f,0x59,0xc5, 0xf3,0x0f,0x11,0x44,0x24,0x28, 0xf3,0x44,0x0f,0x11,0x4c,0x24,0x20, 0xe8,0x4c,0x5e,0x00,0x00};
    // The 2x2 for a sprite of NDC size `size` centred at NDC (x,y) in a symmetric projection with focal scales fx,fy (b: in x,y,size,aspect; out 4..7).
    static void shape(float* b,float fx,float fy) noexcept {
        const float x=b[0],y=b[1],sz=b[2],asp=b[3];
        b[4]=sz;b[5]=b[6]=0;b[7]=sz*asp;   // the stock constant (v1141 aspect) unless the projection is known
        if(!(fx>0 && fy>0) || !std::isfinite(x+y+sz)) return;
        const double u=x/fx,v=y/fy,n2=std::min(1+u*u+v*v,6.0),r=sz/fx;   // n2 <= 6: past ~66 deg the stretch is held (the eye edge)
        const double s00=r*r*fx*fx*n2*(1+u*u),s01=r*r*fx*fy*n2*u*v,s11=r*r*fy*fy*n2*(1+v*v);
        const double det=std::sqrt(std::max(s00*s11-s01*s01,0.)),t=s00+s11+2*det;
        if(!(t>0) || !std::isfinite(t)) return;
        const double k=1/std::sqrt(t);
        b[4]=static_cast<float>((s00+det)*k);b[5]=b[6]=static_cast<float>(s01*k);b[7]=static_cast<float>((s11+det)*k);
    }
    // Per eye before Setup: the render projection's focal scales (1/tan of the half angles). Installs the hook once.
    static void update(std::uintptr_t image_base,float left,float right,float up,float down) noexcept {
        const double w=std::tan(right)-std::tan(left),h=std::tan(up)-std::tan(down);
        if(w>1e-6 && h>1e-6) {fx_.store(static_cast<float>(2/w));fy_.store(static_cast<float>(2/h));}
        if(state_.load()==0) install(image_base);
    }
    static void disable() noexcept {state_.store(-1);}
    static int state() noexcept {return state_.load();}
    static std::uint64_t calls() noexcept {return calls_.load();}
private:
    static void __cdecl helper(float* b) noexcept {shape(b,fx_.load(std::memory_order_relaxed),fy_.load(std::memory_order_relaxed));calls_.fetch_add(1,std::memory_order_relaxed);}
    static void put64(std::vector<unsigned char>& c,std::uint64_t v) {for(int i=0;i<8;++i) c.push_back(static_cast<unsigned char>(v>>(8*i)));}
    static void install(std::uintptr_t image_base) noexcept {
        if(!image_base) return;
        auto* at=reinterpret_cast<unsigned char*>(image_base+hook_rva);
        std::array<unsigned char,23> now{};SIZE_T n{};
        if(!ReadProcessMemory(GetCurrentProcess(),at,now.data(),now.size(),&n) || n!=now.size() || now!=original) {state_.store(-1);return;}
        const auto blk=reinterpret_cast<std::uint64_t>(block_);
        std::vector<unsigned char> c{0x51,0x52,0x48,0x83,0xec,0x20, 0x48,0xb8};put64(c,blk);                    // push rcx; push rdx; sub rsp,20; mov rax,blk
        for(unsigned char b:{0xf3,0x44,0x0f,0x11,0x18, 0xf3,0x44,0x0f,0x11,0x60,0x04, 0xf3,0x44,0x0f,0x11,0x50,0x08, 0xf3,0x44,0x0f,0x11,0x68,0x0c, 0x48,0x89,0xc1,0x48,0xb8}) c.push_back(b);
        put64(c,reinterpret_cast<std::uint64_t>(&helper));                                                     // [rax]=x,y,size,aspect; mov rcx,rax; mov rax,helper
        for(unsigned char b:{0xff,0xd0, 0x48,0x83,0xc4,0x20, 0x5a,0x59, 0x48,0xb8}) c.push_back(b);put64(c,blk); // call; add rsp,20; pop rdx; pop rcx; mov rax,blk
        for(unsigned char b:{0xf3,0x0f,0x10,0x50,0x10, 0xf3,0x0f,0x10,0x58,0x14, 0xf3,0x0f,0x10,0x40,0x18, 0xf3,0x0f,0x11,0x44,0x24,0x20,
                             0xf3,0x0f,0x10,0x40,0x1c, 0xf3,0x0f,0x11,0x44,0x24,0x28, 0x48,0xb8}) c.push_back(b);   // xmm2,xmm3,[rsp+20],[rsp+28] = out
        put64(c,image_base+setter_rva);for(unsigned char b:{0xff,0xd0,0x48,0xb8}) c.push_back(b);               // call 0x310ad0
        put64(c,image_base+resume_rva);c.push_back(0xff);c.push_back(0xe0);                                    // jmp 0x30ac84
        auto* cave=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if(!cave) {state_.store(-1);return;}
        std::memcpy(cave,c.data(),c.size());FlushInstructionCache(GetCurrentProcess(),cave,c.size());
        std::array<unsigned char,23> jmp{0xff,0x25,0,0,0,0};const auto dest=reinterpret_cast<std::uint64_t>(cave);
        for(int i=0;i<8;++i) jmp[6+i]=static_cast<unsigned char>(dest>>(8*i));
        for(std::size_t i=14;i<jmp.size();++i) jmp[i]=0x90;
        if(!WriteProcessMemory(GetCurrentProcess(),at,jmp.data(),jmp.size(),&n) || n!=jmp.size()) {state_.store(-1);return;}
        FlushInstructionCache(GetCurrentProcess(),at,jmp.size());state_.store(1);
    }
    inline static std::atomic<int> state_{0};inline static std::atomic<float> fx_{0},fy_{0};inline static std::atomic<std::uint64_t> calls_{0};
    alignas(16) inline static float block_[8]{};
};
}
