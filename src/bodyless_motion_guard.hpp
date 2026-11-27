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
#include "seh_copy.hpp"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
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
    static constexpr std::uintptr_t set_body_motion_rva=0xc86ad0,link_rva=0xc8fc00,detach_rva=0xc927c0;
    static constexpr std::array<unsigned char,24> set_body_motion_prologue{
        0x44,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x48,0x8d,0x6c,0x24,0xd8,0x48,0x81,0xec,0x28,0x01,0x00};
    static constexpr std::array<unsigned char,24> link_prologue{
        0x4c,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec};
    static constexpr std::array<unsigned char,24> detach_prologue{
        0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x8b};
    static constexpr std::uintptr_t motion_rva=0xc90a90;
    static constexpr std::array<unsigned char,24> motion_prologue{
        0x4c,0x8b,0xdc,0x55,0x57,0x41,0x56,0x41,0x57,0x49,0x8d,0xab,0x78,0xfd,0xff,0xff,0x48,0x81,0xec,0x68,0x03,0x00,0x00,0x44};
    static constexpr bool forbidden_body(std::uint32_t id) noexcept {const auto i=id&0xffffff;return i==0 || i==0xffffff;}
    // v1384 (hang 2026-11-16, after Stilton's mission): an id is serial<<24 | slot, and 0xc86800 looks the body up by the slot alone. A game
    // caller still holding the previous level's 0x3c000003 switched the new level's static trigger 0x01000003 to dynamic and linked the
    // stale id into its motion ring (body +0x74, motion +0x28); destroying the trigger later walked that ring forever (0xc92860).
    // v1386 (third incident, 10:50: the same 0x3c000003 on slot 3 in a freshly started game): 0x3c000003 is no body id at all but a constant
    // found 227 times inside Havok shape/cloth data (... 0x3c020002 0x3c010000 0x3c000003 ...): a caller reads its "id" from memory that
    // holds another object, so it always decodes as slot 3. Wrong = the slot does not hold exactly this id (another body, a free slot,
    // body 0 or the invalid id). Every public engine entry that links a body into a motion ring checks it: 0xc86800 setBodyMotionType,
    // 0xc86ad0 setBodyMotion (queued record 18 at 0x8a1ed, 0xc869e1, 0xc902ca, 0xc9fb38) and 0xc8fc00 (record 4, arkCpntPhysicsSystem
    // 0x76cfa4, 0x4cd8cf, 0x80b149, 0xc9f9bc); all three are void and only ever link the id's own slot. Unreadable: unchanged behaviour.
    template<class Reader> static bool wrong_body(Reader&& read,std::uintptr_t world,std::uint32_t id) noexcept {
        if(forbidden_body(id))return true;
        std::uintptr_t pool{};std::uint32_t stored{};
        if(!read(world+0x28,&pool,8) || !pool || !read(pool+std::uintptr_t{id&0xffffff}*0xb0+0x70,&stored,4))return false;
        return stored!=id;
    }
    // v1386: 0xc927c0(world, body*) detaches a body from its motion by finding its predecessor in the ring (+0x74) - forever when the ring
    // does not lead back to the body's own id (the 09:07 and 10:50 hangs). A wrong-id link always names the slot the engine attached, i.e.
    // the body itself under another serial: such links (in the ring and the motion's first, motion = [world+0x120] + body+0x40 * 0x80) are
    // written back to the body's real id, then the engine detaches as usual. ok: nothing to do; repaired: links fixed; broken: the ring
    // still does not close (the detach is skipped rather than spinning).
    enum class Ring {ok,repaired,broken,unreadable};
    template<class Reader,class Writer> static Ring repair_ring(Reader&& read,Writer&& write,std::uintptr_t world,std::uintptr_t body) noexcept {
        std::uintptr_t pool{},motions{};std::uint32_t own{},motion{};
        if(!read(world+0x28,&pool,8) || !read(world+0x120,&motions,8) || !pool || !motions || !read(body+0x70,&own,4) || !read(body+0x40,&motion,4))
            return Ring::unreadable;
        const auto alias=[own](std::uint32_t v) {return (v&0xffffff)==(own&0xffffff) && v!=own;};
        bool fixed=false;
        if(motion!=0x7fffffff) {
            const auto first_at=motions+std::uintptr_t{motion}*0x80+0x28;std::uint32_t first{};
            if(!read(first_at,&first,4))return Ring::unreadable;
            if(alias(first)) {if(!write(first_at,own))return Ring::unreadable;fixed=true;}
        }
        std::uintptr_t at=body;
        for(int n=0;n<4096;++n) {
            std::uint32_t next{};if(!read(at+0x74,&next,4))return Ring::unreadable;
            if(alias(next)) {if(!write(at+0x74,own))return Ring::unreadable;next=own;fixed=true;}
            if(next==own)return fixed?Ring::repaired:Ring::ok;
            at=pool+std::uintptr_t{next&0xffffff}*0xb0;
        }
        return Ring::broken;
    }
    // v1385 (crash 2026-11-16 10:18 at 0xcaf005, Timepiece raised in a level after its mission): 0xc90a90(world, u32 motion; void, 12 direct
    // callers, e.g. arkCpntPhysicsSystem 0x76ca80) rebuilds a motion from its attached bodies: motion ([world+0x120] + m*0x80) +0x28 first id,
    // body ([world+0x28] + slot*0xb0) +0x74 next, and 0xcaefc0 dereferences every ring body's +0x88. A ring body that is not that id any more
    // or has no +0x88 crashes; a ring that never comes back to its first spins (the 09:07 hang shape). Such a motion is skipped, as the
    // engine itself skips a motion with no first body (0xc90ac8 -> 0xc90dc4). Unreadable: unchanged behaviour.
    template<class Reader> static bool dead_motion(Reader&& read,std::uintptr_t world,std::uint32_t motion) noexcept {
        std::uintptr_t pool{},motions{};std::uint32_t first{};
        if(!read(world+0x28,&pool,8) || !read(world+0x120,&motions,8) || !pool || !motions ||
           !read(motions+std::uintptr_t{motion}*0x80+0x28,&first,4) || (first&0xffffff)==0xffffff)return false;
        std::uint32_t id=first;
        for(int n=0;n<4096;++n) {
            const auto body=pool+std::uintptr_t{id&0xffffff}*0xb0;std::uint32_t stored{},next{};std::uintptr_t props{};
            if(!read(body+0x70,&stored,4) || !read(body+0x74,&next,4) || !read(body+0x88,&props,8))return false;
            if(stored!=id || !props)return true;
            if(next==first)return false;
            id=next;
        }
        return true;
    }

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
        const auto pinned=[&](std::uintptr_t at,const auto& code) {std::array<unsigned char,24> got{};return read(base+at,got.data(),got.size()) && got==code;};
        if(!pinned(motion_rva,motion_prologue) || !pinned(set_body_motion_rva,set_body_motion_prologue) || !pinned(link_rva,link_prologue) ||
           !pinned(detach_rva,detach_prologue))return false;
        void* trampoline{};void* set_trampoline{};void* motion_trampoline{};void* body_motion_trampoline{};void* link_trampoline{};void* detach_trampoline{};
        if(MH_CreateHook(reinterpret_cast<void*>(base+rva),reinterpret_cast<void*>(&hook),&trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+set_motion_rva),reinterpret_cast<void*>(&set_motion_hook),&set_trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+motion_rva),reinterpret_cast<void*>(&motion_hook),&motion_trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+set_body_motion_rva),reinterpret_cast<void*>(&set_body_motion_hook),&body_motion_trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+link_rva),reinterpret_cast<void*>(&link_hook),&link_trampoline)!=MH_OK)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(base+detach_rva),reinterpret_cast<void*>(&detach_hook),&detach_trampoline)!=MH_OK)return false;
        base_=base;original_.store(reinterpret_cast<Original>(trampoline));set_original_.store(reinterpret_cast<SetMotion>(set_trampoline));
        motion_original_.store(reinterpret_cast<RebuildMotion>(motion_trampoline));
        body_motion_original_.store(reinterpret_cast<SetBodyMotion>(body_motion_trampoline));
        link_original_.store(reinterpret_cast<Link>(link_trampoline));detach_original_.store(reinterpret_cast<Detach>(detach_trampoline));
        for(const auto at:{rva,set_motion_rva,motion_rva,set_body_motion_rva,link_rva,detach_rva})
            if(MH_EnableHook(reinterpret_cast<void*>(base+at))!=MH_OK)return false;
        installed_=true;return true;
    }
    static bool installed() noexcept {return installed_.load();}
    static std::uint64_t skips() noexcept {return skips_.load(std::memory_order_relaxed);}
    static std::uint64_t body_skips() noexcept {return body_skips_.load(std::memory_order_relaxed);}
    static std::uint64_t motion_skips() noexcept {return motion_skips_.load(std::memory_order_relaxed);}
    static std::uint64_t link_skips() noexcept {return link_skips_.load(std::memory_order_relaxed);}
    static std::uint64_t ring_repairs() noexcept {return ring_repairs_.load(std::memory_order_relaxed);}
    static std::uint64_t ring_skips() noexcept {return ring_skips_.load(std::memory_order_relaxed);}
    // The first 16 refusals, for the status file: kind (1 motion type, 2 body motion, 3 link, 4 rebuild, 5 ring repaired, 6 ring skipped),
    // the caller's Dishonored2.exe RVA (0 = not in the exe), the id or motion asked for and what its slot held.
    struct Event {std::uint32_t kind{},caller{},asked{},held{};};
    static std::array<Event,16> events() noexcept {
        std::array<Event,16> out{};const auto n=event_count_.load(std::memory_order_acquire);
        for(unsigned i=0;i<16 && i<n;++i) {const auto a=event_log_[2*i].load(std::memory_order_relaxed),b=event_log_[2*i+1].load(std::memory_order_relaxed);
            out[i]={static_cast<std::uint32_t>(a>>32),static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(b>>32),static_cast<std::uint32_t>(b)};}
        return out;
    }
    static unsigned event_total() noexcept {return event_count_.load(std::memory_order_acquire);}

private:
    using Original=std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uint16_t,std::uintptr_t,std::uint16_t);
    inline static std::atomic<Original> original_{};
    inline static std::atomic<bool> installed_{};
    inline static std::atomic<std::uint64_t> skips_{};
    using SetMotion=void (__fastcall*)(std::uintptr_t,std::uint32_t,std::uint8_t,std::uint8_t);
    inline static std::atomic<SetMotion> set_original_{};
    inline static std::atomic<std::uint64_t> body_skips_{};
    inline static std::array<std::atomic<std::uint64_t>,32> event_log_{};
    inline static std::atomic<unsigned> event_count_{};
    static void note(std::uint32_t kind,void* return_address,std::uint32_t asked,std::uintptr_t world,std::uint32_t held_slot_of) noexcept {
        std::uint32_t held=0xffffffff;std::uintptr_t pool{};
        if(held_slot_of!=0xffffffff && seh_read(world+0x28,&pool,8) && pool) seh_read(pool+std::uintptr_t{held_slot_of&0xffffff}*0xb0+0x70,&held,4);
        const auto at=reinterpret_cast<std::uintptr_t>(return_address);
        const std::uint32_t caller=base_ && at>=base_ && at-base_<0x10000000?static_cast<std::uint32_t>(at-base_):0;
        const auto i=event_count_.load(std::memory_order_relaxed);
        if(i>=16)return;
        event_log_[2*i].store((std::uint64_t{kind}<<32)|caller,std::memory_order_relaxed);
        event_log_[2*i+1].store((std::uint64_t{asked}<<32)|held,std::memory_order_relaxed);
        event_count_.store(i+1,std::memory_order_release);   // ponytail: racy slot claim between threads, worst case one entry overwritten
    }
    static void __fastcall set_motion_hook(std::uintptr_t world,std::uint32_t id,std::uint8_t type,std::uint8_t activation) {
        if(wrong_body(&seh_read,world,id)) {body_skips_.fetch_add(1,std::memory_order_relaxed);note(1,_ReturnAddress(),id,world,id);return;}
        set_original_.load()(world,id,type,activation);
    }
    using SetBodyMotion=void (__fastcall*)(std::uintptr_t,std::uint32_t,std::uint32_t,std::uint32_t);
    inline static std::atomic<SetBodyMotion> body_motion_original_{};
    inline static std::atomic<std::uint64_t> link_skips_{},ring_repairs_{},ring_skips_{};
    static void __fastcall set_body_motion_hook(std::uintptr_t world,std::uint32_t id,std::uint32_t motion,std::uint32_t mode) {
        if(wrong_body(&seh_read,world,id)) {link_skips_.fetch_add(1,std::memory_order_relaxed);note(2,_ReturnAddress(),id,world,id);return;}
        body_motion_original_.load()(world,id,motion,mode);
    }
    using Link=void (__fastcall*)(std::uintptr_t,std::uint32_t,void*,std::int32_t,std::int32_t);
    inline static std::atomic<Link> link_original_{};
    static void __fastcall link_hook(std::uintptr_t world,std::uint32_t id,void* data,std::int32_t a,std::int32_t b) {
        if(wrong_body(&seh_read,world,id)) {link_skips_.fetch_add(1,std::memory_order_relaxed);note(3,_ReturnAddress(),id,world,id);return;}
        link_original_.load()(world,id,data,a,b);
    }
    using Detach=void (__fastcall*)(std::uintptr_t,std::uintptr_t);
    inline static std::atomic<Detach> detach_original_{};
    static bool seh_write(std::uintptr_t address,std::uint32_t value) noexcept {
        __try {*reinterpret_cast<volatile std::uint32_t*>(address)=value;return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void __fastcall detach_hook(std::uintptr_t world,std::uintptr_t body) {
        const auto ring=repair_ring(&seh_read,&seh_write,world,body);
        std::uint32_t own{};seh_read(body+0x70,&own,4);
        if(ring==Ring::repaired) {ring_repairs_.fetch_add(1,std::memory_order_relaxed);note(5,_ReturnAddress(),own,world,0xffffffff);}
        if(ring==Ring::broken) {ring_skips_.fetch_add(1,std::memory_order_relaxed);note(6,_ReturnAddress(),own,world,0xffffffff);return;}
        detach_original_.load()(world,body);
    }
    using RebuildMotion=void (__fastcall*)(std::uintptr_t,std::uint32_t);
    inline static std::atomic<RebuildMotion> motion_original_{};
    inline static std::atomic<std::uint64_t> motion_skips_{};
    static void __fastcall motion_hook(std::uintptr_t world,std::uint32_t motion) {
        if(dead_motion(&seh_read,world,motion)) {motion_skips_.fetch_add(1,std::memory_order_relaxed);note(4,_ReturnAddress(),motion,world,0xffffffff);return;}
        motion_original_.load()(world,motion);
    }
    inline static std::uintptr_t base_{};
    // 2026-11-27: these hooks sit on Havok entry points whose engine callers keep live floats in the volatile vector registers across the
    // call (whole-program optimised code). Routing this read through the CRT memcpy (seh_memcpy) clobbered xmm0-5 and the engine crashed a
    // few seconds into play (DOTO +0x526047, bisected to this one line); a plain std::memcpy is not covered by clang-cl's __try. So it
    // reads with seh_integer_copy: an opaque call (the __try catches a fault: a level freed under the hook) that uses no vector register.
    static bool seh_read(std::uintptr_t address,void* out,std::size_t size) noexcept {
        __try {d2vr::seh_integer_copy(out,reinterpret_cast<const void*>(address),size);return true;}
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
