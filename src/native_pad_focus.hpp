#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <stdexcept>
#include "pad_focus.hpp"

namespace d2vr {
// Only the native joystick worker/serializer may use a focused XR lease.
// This never changes window focus, the native enabled field or OS input.
class NativePadFocus {
    using Enabled=unsigned char (__fastcall*)(void*);
    using WindowActive=unsigned char (__fastcall*)();
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    inline static NativePadFocus* instance_{};
    static constexpr std::uintptr_t getter_rva=0xd1f70;
    static constexpr std::uintptr_t window_rva=0xe3cf0;
    Enabled original_{};Read read_{};std::uintptr_t base_{};
    WindowActive original_window_{};
    PadFocusLease lease_;
    bool enabled_{};
    bool window_enabled_{};
    std::atomic<bool> active_{false};
    std::atomic<UINT> depth_{0};
    std::atomic<UINT64> calls_{0},overrides_{0},wakeups_{0};
    std::atomic<UINT64> poll_calls_{0},serializer_calls_{0},window_calls_{0},window_overrides_{0};
    static unsigned char __fastcall enabled(void* root) {
        auto& self=*instance_;self.depth_.fetch_add(1,std::memory_order_acq_rel);++self.calls_;
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(caller-self.base_==0xe1098) ++self.poll_calls_;
        if(caller-self.base_==0xe0146) ++self.serializer_calls_;
        unsigned char result{};
        __try {
            result=self.original_(root);
            if(!result && self.active_) {
                const auto owner=native_pad_root(self.read_,self.base_,reinterpret_cast<std::uintptr_t>(root));
                if(self.lease_.allows(caller-self.base_,owner.has_value(),GetTickCount64())) {result=1;++self.overrides_;}
            }
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
        return result;
    }
    static unsigned char __fastcall window_active() {
        auto& self=*instance_;self.depth_.fetch_add(1,std::memory_order_acq_rel);
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        unsigned char result{};
        __try {
            result=self.original_window_();
            if(caller-self.base_==0x3a4ab) ++self.window_calls_;
            if(!result && self.active_ && caller-self.base_==0x3a4ab) {
                const auto owner=native_pad_root(self.read_,self.base_,self.base_+0x3134140);
                if(self.lease_.allows_serializer_window(caller-self.base_,owner.has_value(),GetTickCount64())) {
                    result=1;++self.window_overrides_;
                }
            }
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
        return result;
    }
    bool code_matches() const {
        std::array<unsigned char,16> code{};
        const std::array<unsigned char,16> expected{0x0f,0xb6,0x41,0x08,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
        return read_(base_+getter_rva,code.data(),code.size()) && code==expected;
    }
    bool window_code_matches() const {
        std::array<unsigned char,24> code{};
        const std::array<unsigned char,24> expected{0x80,0x3d,0x5a,0x42,0x05,0x03,0x00,0x74,0x0c,
            0x80,0x3d,0x52,0x42,0x05,0x03,0x00,0x74,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
        return read_(base_+window_rva,code.data(),code.size()) && code==expected;
    }
public:
    void install(std::uintptr_t base,Read read) {
        if(enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Native joystick focus gate already owned");
        base_=base;read_=read;
        if(!code_matches() || !window_code_matches() || !native_pad_root(read_,base_,base_+0x3134140))
            throw std::runtime_error("Native joystick owner/getter differs from the pinned build");
        for(auto caller:{std::uintptr_t{0xe1098},std::uintptr_t{0xe0146}}) {
            std::array<unsigned char,5> call{};std::int32_t relative{};
            if(!read_(base_+caller-5,call.data(),call.size()) || call[0]!=0xe8)
                throw std::runtime_error("Native joystick call site differs");
            std::memcpy(&relative,call.data()+1,4);
            if(base_+caller+relative!=base_+getter_rva) throw std::runtime_error("Native joystick caller target differs");
        }
        std::array<unsigned char,5> window_call{};std::int32_t window_relative{};
        if(!read_(base_+0x3a4a6,window_call.data(),window_call.size()) || window_call[0]!=0xe8)
            throw std::runtime_error("Native pad serialization window call differs");
        std::memcpy(&window_relative,window_call.data()+1,4);
        if(base_+0x3a4ab+window_relative!=base_+window_rva)
            throw std::runtime_error("Native pad serialization window target differs");
        instance_=this;calls_=0;overrides_=0;wakeups_=0;lease_.publish(false,0);
        poll_calls_=0;serializer_calls_=0;window_calls_=0;window_overrides_=0;
        if(!original_ && MH_CreateHook(reinterpret_cast<void*>(base+getter_rva),reinterpret_cast<void*>(&enabled),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native joystick focus gate");
        if(!original_window_ && MH_CreateHook(reinterpret_cast<void*>(base+window_rva),reinterpret_cast<void*>(&window_active),reinterpret_cast<void**>(&original_window_))!=MH_OK)
            throw std::runtime_error("Cannot create native pad serialization window gate");
        active_=true;
        if(MH_EnableHook(reinterpret_cast<void*>(base+getter_rva))!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native joystick focus gate");}
        enabled_=true;
        if(MH_EnableHook(reinterpret_cast<void*>(base+window_rva))!=MH_OK) {
            disable();throw std::runtime_error("Cannot enable native pad serialization window gate");
        }
        window_enabled_=true;
    }
    void publish(bool focused,std::uint64_t now) {
        if(!active_) return;
        if(lease_.publish(focused,now)) {
            const auto owner=native_pad_root(read_,base_,base_+0x3134140);
            DWORD flags{};
            if(!owner || !GetHandleInformation(reinterpret_cast<HANDLE>(owner->wake_event),&flags) ||
               !SetEvent(reinterpret_cast<HANDLE>(owner->wake_event)))
                throw std::runtime_error("Cannot wake the owned native joystick worker");
            ++wakeups_;
        }
    }
    bool disable() noexcept {
        active_=false;lease_.publish(false,0);
        bool clean=true;
        if(window_enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+window_rva))!=MH_OK) clean=false;
            else window_enabled_=false;
        }
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+getter_rva))!=MH_OK) clean=false;
            else enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        return clean && !depth_ && (!original_ || code_matches()) && (!original_window_ || window_code_matches());
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 overrides() const {return overrides_.load();}
    UINT64 wakeups() const {return wakeups_.load();}
    UINT64 poll_calls() const {return poll_calls_.load();}
    UINT64 serializer_calls() const {return serializer_calls_.load();}
    UINT64 window_calls() const {return window_calls_.load();}
    UINT64 window_overrides() const {return window_overrides_.load();}
};
} // namespace d2vr
