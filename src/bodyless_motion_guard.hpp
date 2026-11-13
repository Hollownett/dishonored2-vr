#pragma once
// v1296: skip an engine motion-type switch on a physics system that has no live Havok body (game crash, 2026-11-13).
//
// 0x76ce50 = arkCpntPhysicsSystemEvolved vtable +0x168 (this, parent, u16 bone, r9, u16). With bone == the "whole system"
// sentinel (u16 at 0x231da40, 0xfffe in the file) it scans the body ids at [[this+0x100]+0x28] (count this+0x138) for the
// first live one (id & 0xffffff != 0xffffff). With none it still goes on when (this+0x1c & 0xc) >= 8: index -1 picks body
// id 0 (Havok's fixed world body, no motion), setBodyMotionType 0xc86800 -> 0xcaefc0 reads motion data at 0 and the game
// dies at Dishonored2.exe+0xcaf005 (two crashes on 2026-11-13: an arkSeveredLimb woken during a dialogue). It also writes
// a byte at record index -1. That path can only crash, so the hook returns without calling the engine on it.
// v1297: two more crashes on v1296 went through this hook (a 17-bone ragdoll; its slots did not read 0xffffff) and still
// asked Havok for body 0. So the shared Havok entry setBodyMotionType 0xc86800 (world, u32 id, u8 type, u8 activation; 8 direct
// callers, void) is guarded too: body 0 is hknp's reserved fixed world body (no motion, +0x88 null) and 0xffffff is the invalid
// id, neither may ever change motion type.
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace d2vr {
class BodylessMotionGuard {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    static constexpr std::uintptr_t rva=0x76ce50,sentinel_rva=0x231da40;
    static constexpr std::array<unsigned char,64> prologue{
        0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x45,0x33,0xff,0x66,0x44,0x3b,
        0x05,0xd3,0x0b,0xbb,0x01,0x4d,0x8b,0xe1,0x41,0x0f,0xb7,0xf8,0x4c,0x8b,0xea,0x48,0x8b,0xf1,0x75,0x47,0x8b,0x91,0x38,0x01,
        0x00,0x00,0x4c,0x8b,0x81,0x00,0x01,0x00,0x00,0x41,0x8b,0xff,0x85,0xd2,0x7e,0x30};
    // The flag test the crash path passes (0x76cec3: mov eax,[rsi+1c] ... and al,0c; cmp al,08).
    static constexpr std::uintptr_t flag_rva=0x76cec3;
    static constexpr std::array<unsigned char,19> flag_code{
        0x8b,0x46,0x1c,0x0f,0xb7,0x5d,0x60,0x4c,0x89,0xb4,0x24,0x98,0x00,0x00,0x00,0x24,0x0c,0x3c,0x08};

    // Pure decision over a reader: true = the engine would pick body index -1 and crash.
    static constexpr std::uintptr_t set_motion_rva=0xc86800;
    static constexpr std::array<unsigned char,40> set_motion_prologue{
        0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc9,0x48,0x81,0xec,0xf0,0x00,0x00,0x00,0x48,0x8b,
        0x79,0x28,0x44,0x8b,0xd2,0x41,0x8b,0xd8,0x41,0x81,0xe2,0xff,0xff,0xff,0x00,0x45};
    static constexpr bool forbidden_body(std::uint32_t id) noexcept {const auto i=id&0xffffff;return i==0 || i==0xffffff;}

    template<class Reader> static bool doomed(Reader&& read,std::uintptr_t system,std::uint16_t bone,std::uint16_t sentinel) noexcept {
        if(bone!=sentinel || !system)return false;
        std::uint32_t flags{};if(!read(system+0x1c,&flags,4) || (flags&0xc)<8)return false;
        std::int32_t count{};std::uintptr_t table{},ids{};
        if(!read(system+0x138,&count,4) || !read(system+0x100,&table,8))return false;
        if(count<=0)return true;
        if(!table || !read(table+0x28,&ids,8) || !ids)return false;   // unreadable: leave it to the engine (unchanged behaviour)
        for(std::int32_t i=0;i<count && i<0x8000;++i) {
            std::uint32_t id{};if(!read(ids+4u*static_cast<std::uint32_t>(i),&id,4))return false;
            if((id&0xffffff)!=0xffffff)return false;
        }
        return true;
    }

    static bool install(std::uintptr_t base,Read read) noexcept {
        if(original_.load() || !base || !read)return original_.load()!=nullptr;
        std::array<unsigned char,prologue.size()> entry{};std::array<unsigned char,flag_code.size()> flag{};
        if(!read(base+rva,entry.data(),entry.size()) || entry!=prologue ||
           !read(base+flag_rva,flag.data(),flag.size()) || flag!=flag_code)return false;
        std::array<unsigned char,set_motion_prologue.size()> set_entry{};
        if(!read(base+set_motion_rva,set_entry.data(),set_entry.size()) || set_entry!=set_motion_prologue)return false;
        void* trampoline{};void* set_trampoline{};
        if(MH_CreateHook(reinterpret_cast<void*>(base+rva),reinterpret_cast<void*>(&hook),&trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+set_motion_rva),reinterpret_cast<void*>(&set_motion_hook),&set_trampoline)!=MH_OK)return false;
        base_=base;original_.store(reinterpret_cast<Original>(trampoline));set_original_.store(reinterpret_cast<SetMotion>(set_trampoline));
        if(MH_EnableHook(reinterpret_cast<void*>(base+rva))!=MH_OK || MH_EnableHook(reinterpret_cast<void*>(base+set_motion_rva))!=MH_OK)return false;
        installed_=true;return true;
    }
    static bool installed() noexcept {return installed_.load();}
    static std::uint64_t skips() noexcept {return skips_.load(std::memory_order_relaxed);}
    static std::uint64_t body_skips() noexcept {return body_skips_.load(std::memory_order_relaxed);}

private:
    using Original=std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uint16_t,std::uintptr_t,std::uint16_t);
    inline static std::atomic<Original> original_{};
    inline static std::atomic<bool> installed_{};
    inline static std::atomic<std::uint64_t> skips_{};
    using SetMotion=void (__fastcall*)(std::uintptr_t,std::uint32_t,std::uint8_t,std::uint8_t);
    inline static std::atomic<SetMotion> set_original_{};
    inline static std::atomic<std::uint64_t> body_skips_{};
    static void __fastcall set_motion_hook(std::uintptr_t world,std::uint32_t id,std::uint8_t type,std::uint8_t activation) {
        if(forbidden_body(id)) {body_skips_.fetch_add(1,std::memory_order_relaxed);return;}
        set_original_.load()(world,id,type,activation);
    }
    inline static std::uintptr_t base_{};
    static bool seh_read(std::uintptr_t address,void* out,std::size_t size) noexcept {
        __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::uintptr_t __fastcall hook(std::uintptr_t system,std::uintptr_t parent,std::uint16_t bone,std::uintptr_t r9,std::uint16_t extra) {
        std::uint16_t sentinel{};
        if(seh_read(base_+sentinel_rva,&sentinel,2) && doomed(&seh_read,system,bone,sentinel)) {
            skips_.fetch_add(1,std::memory_order_relaxed);return 0;
        }
        return original_.load()(system,parent,bone,r9,extra);
    }
};
}
