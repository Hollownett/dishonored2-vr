#pragma once
#include <windows.h>
#include <MinHook.h>
#include "game_rig.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace d2vr {
// Weapons, powers and hand effects follow the tracked hand.
// 0x53d3f0 (7 args) and 0x53d6a0 (5 args) read ONE arm joint from the animation's
// transform array: *(anim+0x2d8) + remap[state][joint]*64, with the state remap
// table at *(anim+0x2e8) (16-byte entries {short* remap; u32 count}), then compose an
// attachment frame through 0x9b900 (docs: artifacts/re-toolchain/direct-ghidra/
// weapon-attachment-*). Skinning uses our palette (0x25efe0), but these readers
// used the untracked animation, so the sword floated away from the hand.
// While a fresh tracked palette exists for that animation, the joint's slot holds
// the tracked transform for the duration of the original call, then is restored.
class NativeAttachmentPose {
    using Query=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*,void*,void*);
    using Reader=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    static constexpr std::uintptr_t query_rva=0x53d3f0,reader_rva=0x53d6a0;
    static constexpr std::array<unsigned char,16> query_code{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,0xec,0xc0,0x00,0x00,0x00,0x49,0x8b,0xf9};
    static constexpr std::array<unsigned char,16> reader_code{0x48,0x83,0xec,0x28,0x4c,0x8b,0xd9,0x66,0x45,0x85,0xc0,0x78,0x5f,0x48,0x8b,0x41};
    struct Side {std::uintptr_t animation{},first_slot{};std::array<rig::Transform,69> joints{};std::uint64_t tick{};};
    inline static NativeAttachmentPose* instance_{};
    Query query_{};Reader reader_{};Read read_{};std::uintptr_t base_{};
    bool enabled_{};std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    std::mutex mutex_;std::array<Side,2> sides_{};
    std::atomic<std::uint64_t> substitutions_{},calls_{};
    // Locate the slot for (anim,state,joint) exactly as the native readers do.
    bool slot(std::uintptr_t anim,int state,short joint,std::uintptr_t& at) const noexcept {
        std::uintptr_t states{},transforms{},remap{};std::uint32_t count{};std::int16_t index{};
        if(joint<0 || state<0 || state>3 || !read_(anim+0x2e8,&states,8) || !read_(anim+0x2d8,&transforms,8) ||
           !read_(states+static_cast<std::uintptr_t>(state)*16,&remap,8) || !read_(states+static_cast<std::uintptr_t>(state)*16+8,&count,4) ||
           !(count&0xffffff) || static_cast<std::uint32_t>(joint)>=(count&0xffffff) ||
           !read_(remap+static_cast<std::uintptr_t>(joint)*2,&index,2) || index<0) return false;
        at=transforms+static_cast<std::uintptr_t>(index)*64;return true;
    }
    static bool copy_slot(std::uintptr_t at,const void* value) noexcept {
        __try {std::memcpy(reinterpret_cast<void*>(at),value,64);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void restore_slot(std::uintptr_t at,const void* tracked,const void* saved) noexcept {
        __try {if(!std::memcmp(reinterpret_cast<void*>(at),tracked,64)) std::memcpy(reinterpret_cast<void*>(at),saved,64);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    template<class Call> std::uint64_t around(std::uintptr_t anim,int state,short joint,Call call) {
        ++calls_;
        if(!active_.load(std::memory_order_acquire)) return call();
        std::unique_lock<std::mutex> lock(mutex_);
        const auto now=GetTickCount64();
        const Side* side=nullptr;
        for(const auto& s:sides_) if(s.animation && s.animation==anim && now-s.tick<=100) side=&s;
        std::uintptr_t at{};
        if(!side || !slot(anim,state,joint,at) || at<side->first_slot || (at-side->first_slot)%64 || (at-side->first_slot)/64>=69) {
            lock.unlock();return call();
        }
        const auto& tracked=side->joints[(at-side->first_slot)/64];
        std::array<unsigned char,64> saved{};
        if(!read_(at,saved.data(),64)) {lock.unlock();return call();}
        const bool written=copy_slot(at,tracked.f.data());
        if(written) ++substitutions_;
        // Restore only if the slot still holds our value; a concurrent native
        // animation write of a new frame must win.
        struct Restore {std::uintptr_t at;const rig::Transform* tracked;const std::array<unsigned char,64>* saved;bool written;
            ~Restore() {if(written) restore_slot(at,tracked->f.data(),saved->data());}
        } restore{at,&tracked,&saved,written};
        return call();
    }
    static std::uint64_t __fastcall query_hook(std::uintptr_t anim,int state,short joint,void* a,void* b,void* c,void* d) {
        auto& self=*instance_;++self.depth_;
        struct Depth {std::atomic<unsigned>& d;~Depth(){--d;}} guard{self.depth_};
        return self.around(anim,state,joint,[&]{return self.query_(anim,state,joint,a,b,c,d);});
    }
    static std::uint64_t __fastcall reader_hook(std::uintptr_t anim,int state,short joint,void* a,void* b) {
        auto& self=*instance_;++self.depth_;
        struct Depth {std::atomic<unsigned>& d;~Depth(){--d;}} guard{self.depth_};
        return self.around(anim,state,joint,[&]{return self.reader_(anim,state,joint,a,b);});
    }
public:
    // Animation-worker publication of the final tracked palette (model space,
    // same order as the contiguous pose-state-3 block starting at first_slot).
    void publish(unsigned side,std::uintptr_t animation,std::uintptr_t first_slot,const std::array<rig::Transform,69>& joints) {
        if(side>1) return;
        std::lock_guard<std::mutex> lock(mutex_);sides_[side]={animation,first_slot,joints,GetTickCount64()};
    }
    void clear(unsigned side) {if(side>1) return;std::lock_guard<std::mutex> lock(mutex_);sides_[side]={};}
    void install(std::uintptr_t base,Read read) {
        base_=base;read_=read;
        std::array<unsigned char,16> code{};
        if(!read(base+query_rva,code.data(),16) || code!=query_code || !read(base+reader_rva,code.data(),16) || code!=reader_code)
            throw std::runtime_error("Native attachment readers differ from the supported executable");
        instance_=this;{std::lock_guard<std::mutex> lock(mutex_);sides_={};}substitutions_=calls_=0;
        if(!query_ && MH_CreateHook(reinterpret_cast<void*>(base+query_rva),reinterpret_cast<void*>(&query_hook),reinterpret_cast<void**>(&query_))!=MH_OK)
            throw std::runtime_error("Cannot create attachment query hook");
        if(!reader_ && MH_CreateHook(reinterpret_cast<void*>(base+reader_rva),reinterpret_cast<void*>(&reader_hook),reinterpret_cast<void**>(&reader_))!=MH_OK)
            throw std::runtime_error("Cannot create attachment reader hook");
        active_=true;
        if(MH_EnableHook(reinterpret_cast<void*>(base+query_rva))!=MH_OK || MH_EnableHook(reinterpret_cast<void*>(base+reader_rva))!=MH_OK) {
            active_=false;MH_DisableHook(reinterpret_cast<void*>(base+query_rva));MH_DisableHook(reinterpret_cast<void*>(base+reader_rva));
            throw std::runtime_error("Cannot enable attachment hooks");
        }
        enabled_=true;
    }
    bool disable() noexcept {
        active_=false;
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+query_rva))!=MH_OK || MH_DisableHook(reinterpret_cast<void*>(base_+reader_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<unsigned char,16> code{};
        return (!query_ || (read_(base_+query_rva,code.data(),16) && code==query_code)) &&
               (!reader_ || (read_(base_+reader_rva,code.data(),16) && code==reader_code));
    }
    std::uint64_t substitutions() const {return substitutions_.load();}
    std::uint64_t calls() const {return calls_.load();}
};
} // namespace d2vr
