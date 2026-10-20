#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include "native_trampoline_unwind.hpp"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace d2vr {
class NativeAssassinationReceiptHooks {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Arbitration=bool (__fastcall*)(std::uintptr_t,bool);
    using Update=void (__fastcall*)(std::uintptr_t);
    using Accept=bool (__fastcall*)(std::uintptr_t,const void*);
    using Enter=void (__fastcall*)(std::uintptr_t,const void*);
    using Observe=void (*)(void*,unsigned,unsigned,std::uintptr_t,std::uintptr_t,int,const void*,bool,bool,bool);
    struct Callbacks {void* context{};Observe observe{};};
    inline static constexpr std::array<std::uintptr_t,6> rvas{0x3b1700,0x8636a0,0x860eb0,0x832d20,0x86b050,0x833940};
    inline static constexpr std::array<unsigned,6> prefixes{6,8,6,5,5,11};
    inline static constexpr std::array<std::array<unsigned char,32>,6> prologues{{
        {0x40,0x57,0x48,0x83,0xec,0x40,0x80,0x79,0x38,0x0,0x48,0x8b,0xf9,0xf,0x84,0xce,0x1,0x0,0x0,0x48,0x89,0x5c,0x24,0x50,0x48,0x89,0x6c,0x24,0x58,0x48,0x89,0x74},
        {0x48,0x83,0xec,0x28,0x44,0x8b,0x41,0x3c,0x48,0x83,0xc1,0x30,0x4c,0x8b,0xca,0x41,0x8b,0xc0,0xc1,0xe8,0x6,0x83,0xe0,0x3,0x74,0x1e,0x83,0xf8,0x3,0x74,0x19,0x8b},
        {0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0x68,0xff,0xff,0xff,0x48,0x81,0xec,0x98,0x1,0x0,0x0,0x4c,0x8b,0xea,0x48,0x8b,0xf1,0xe8},
        {0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x60,0x48,0x8b,0xda,0x48,0x8b,0xf9,0xe8,0xdb,0x25,0x3,0x0,0x8b,0x43,0x8,0x89,0x87,0x20,0x1,0x0,0x0,0x8b,0x43},
        {0x88,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x78,0xfe,0xff,0xff,0x48,0x81,0xec,0x88,0x2,0x0,0x0,0x48},
        {0x48,0x83,0xec,0x28,0x48,0x81,0xc1,0xb0,0x0,0x0,0x0,0x4c,0x8b,0xca,0x44,0x8b,0x41,0xc,0x41,0x8b,0xc0,0xc1,0xe8,0x6,0x83,0xe0,0x3,0x74,0x1e,0x83,0xf8,0x3}}};
    inline static constexpr std::array<std::array<unsigned char,12>,6> unwinds{{
        {1,6,2,0,6,0x72,2,0x70},{1,8,1,0,4,0x42,0,0},
        {1,6,4,0,6,0xc0,4,0x60,3,0x30,2,0x50},{1,5,2,0,5,0x34,3,0},
        {1,5,1,0,5,0x50,0,0},{1,11,1,0,4,0x42,0,0}}};
    inline static constexpr std::array<unsigned,6> sizes{487,83,2100,261,2210,86};
    inline static constexpr std::array<std::uint64_t,6> hashes{0x3efd3df6f7fb48b0ull,0x9312bc958039dafeull,0xab221d5d888fac80ull,0x39e856bec7ae4992ull,0xcab7f115e49262fbull,0xf44a324d119e367aull};
    static NativeAssassinationReceiptHooks& process_owner() {static auto* owner=new NativeAssassinationReceiptHooks;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;
        std::array<unsigned char,2210> code{};
        for(unsigned index=0;index<6;++index) {
            if(!read_safe(read,base+rvas[index],code.data(),sizes[index]))return false;
            std::uint64_t hash=14695981039346656037ull;
            for(unsigned offset=0;offset<sizes[index];++offset)hash=(hash^code[offset])*1099511628211ull;
            if(hash!=hashes[index])return false;
        }
        return true;
    }

    bool install(std::uintptr_t base,Read read,Callbacks callbacks) noexcept {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock() || !ready_for_install() || !callbacks.observe || !verify(base,read))return false;
        if(base_ && (base_!=base || read_!=read || callbacks_.context!=callbacks.context ||
           callbacks_.observe!=callbacks.observe))return false;
        if(!base_) {base_=base;read_=read;callbacks_=callbacks;}
        stopping_=true;enabled_=false;stop_complete_=false;
        const auto targets=hooks();
        for(unsigned index=0;index<6;++index)if(!created_[index]) {
            void* original{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),targets[index],&original)!=MH_OK)return rollback();
            originals_[index]=original;created_[index]=true;if(!original)return rollback();
        }
        for(unsigned index=0;index<6;++index)if(!prepare(index))return rollback();
        if(!verify(base,read))return rollback();
        for(unsigned index=0;index<6;++index) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();
            installed_[index]=true;
        }
        observer_failed_=false;stopping_=false;return true;
    }
    void enabled(bool value) noexcept {
        if(!value) {
            enabled_=false;auto previous=dispatch_epoch_.load();
            while(previous!=UINT64_MAX && !dispatch_epoch_.compare_exchange_weak(previous,previous+1)) {}
        } else enabled_=dispatch_epoch_!=UINT64_MAX && !stopping_ && installed_[0] && installed_[1] && installed_[2] && installed_[3] && installed_[4] && installed_[5];
    }
    bool enabled() const noexcept {return enabled_ && !stopping_;}
    bool observer_failed() const noexcept {return observer_failed_;}
    bool created(unsigned index) const noexcept {return index<6 && created_[index];}
    bool installed(unsigned index) const noexcept {return index<6 && installed_[index];}
    void* original(unsigned index) const noexcept {return index<6?originals_[index].load():nullptr;}
    unsigned in_flight() const noexcept {return in_flight_;}
    bool stopped() const noexcept {
        return stop_complete_ && !installed_[0] && !installed_[1] && !installed_[2] && !installed_[3] && !installed_[4] && !installed_[5] && !in_flight_ &&
            !protection_pending_[0] && !protection_pending_[1] && !protection_pending_[2] && !protection_pending_[3] && !protection_pending_[4] && !protection_pending_[5] && !cache_pending_[0] && !cache_pending_[1] && !cache_pending_[2] && !cache_pending_[3] && !cache_pending_[4] && !cache_pending_[5];
    }
    bool ready_for_install() const noexcept {
        if(!stopped())return false;
        for(unsigned index=0;index<6;++index)if(created_[index] && !original(index))return false;
        return true;
    }
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        enabled(false);stopping_=true;if(depth_)return false;
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        return lock.owns_lock() && stop_locked(timeout_ms);
    }
    bool invoke(unsigned kind,std::uintptr_t caller,std::uintptr_t subject,int state,const void* packet) {
        ++in_flight_;++depth_;
        bool result=false;
        __try {result=body(kind,caller,subject,state,packet);}__finally {--depth_;--in_flight_;}
        return result;
    }
private:
    friend struct NativeAssassinationReceiptHooksFixture;
    NativeAssassinationReceiptHooks()=default;
    struct Metadata {
        std::array<RUNTIME_FUNCTION,2> functions{};
        std::array<unsigned char,12> unwind{};
        std::array<unsigned char,4> relay{1,0,0,0};
    };
    static bool cpp_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        try {return read && read(address,output,size);}catch(...) {return false;}
    }
    static bool read_safe(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool local_read(std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool body(unsigned kind,std::uintptr_t caller,std::uintptr_t subject,int state,const void* packet) {
        const auto native=original(kind);if(!native)return false;
        const auto epoch=dispatch_epoch_.load();
        const bool admitted=enabled();
        if(admitted)observe(0,kind,caller,subject,state,packet,false,true,false);
        bool completed=false,result=false;
        __try {
            if(kind==0)reinterpret_cast<Update>(native)(subject);
            else if(kind==1 || kind==5)result=reinterpret_cast<Accept>(native)(subject,packet);
            else if(kind==4)result=reinterpret_cast<Arbitration>(native)(subject,state!=0);
            else reinterpret_cast<Enter>(native)(subject,packet);
            completed=true;
        } __finally {
            if(admitted)observe(1,kind,caller,subject,state,packet,completed,enabled() && dispatch_epoch_==epoch,result);
        }
        return result;
    }
    void cpp_observe(unsigned phase,unsigned kind,std::uintptr_t caller,std::uintptr_t subject,int state,const void* packet,bool completed,bool current,bool result) noexcept {
        try {callbacks_.observe(callbacks_.context,phase,kind,caller,subject,state,packet,completed,current,result);}catch(...) {observer_failed_=true;enabled(false);}
    }
    void observe(unsigned phase,unsigned kind,std::uintptr_t caller,std::uintptr_t subject,int state,const void* packet,bool completed,bool current,bool result) noexcept {
        __try {cpp_observe(phase,kind,caller,subject,state,packet,completed,current,result);}__except(EXCEPTION_EXECUTE_HANDLER) {observer_failed_=true;enabled(false);}
    }
    static void __fastcall update_hook(std::uintptr_t sensor) {
        process_owner().invoke(0,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),sensor,0,nullptr);
    }
    static bool __fastcall accept13_hook(std::uintptr_t state,const void* packet) {
        return process_owner().invoke(1,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,13,packet);
    }
    static bool __fastcall accept19_hook(std::uintptr_t state,const void* packet) {
        return process_owner().invoke(5,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,19,packet);
    }
    static void __fastcall assassination_hook(std::uintptr_t state,const void* packet) {
        process_owner().invoke(2,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,13,packet);
    }
    static void __fastcall drop_hook(std::uintptr_t state,const void* packet) {
        process_owner().invoke(3,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,19,packet);
    }
    static bool __fastcall arbitration_hook(std::uintptr_t slash,bool special) {
        return process_owner().invoke(4,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),slash,special?1:0,nullptr);
    }
    static std::array<void*,6> hooks() noexcept {
        return {reinterpret_cast<void*>(&update_hook),reinterpret_cast<void*>(&accept13_hook),
            reinterpret_cast<void*>(&assassination_hook),reinterpret_cast<void*>(&drop_hook),reinterpret_cast<void*>(&arbitration_hook),reinterpret_cast<void*>(&accept19_hook)};
    }
    static std::array<unsigned char,14> tail(std::uintptr_t target,bool owned) noexcept {
        std::array<unsigned char,14> bytes{};
        if(owned) {bytes[0]=0x49;bytes[1]=0xbb;std::memcpy(bytes.data()+2,&target,8);bytes[10]=0x41;bytes[11]=0xff;bytes[12]=0xe3;bytes[13]=0x90;}
        else {bytes[0]=0xff;bytes[1]=0x25;std::memcpy(bytes.data()+6,&target,8);}return bytes;
    }
    bool protect(unsigned index) noexcept {
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));DWORD ignored{};
        if(protection_pending_[index]) {
            if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefixes[index]),14,protection_[index],&ignored))return false;
            protection_pending_[index]=false;
        }
        if(cache_pending_[index]) {
            if(!FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(trampoline),prefixes[index]+14))return false;
            cache_pending_[index]=false;
        }
        return true;
    }
    bool prepare(unsigned index) noexcept {
        if(!protect(index))return false;
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));const auto prefix=prefixes[index];
        std::array<unsigned char,40> bytes{};
        const auto replacement=tail(base_+rvas[index]+prefix,true),expected=tail(base_+rvas[index]+prefix,patched_[index]),
            relay=tail(reinterpret_cast<std::uintptr_t>(hooks()[index]),false);
        if(!trampoline || !local_read(trampoline,bytes.data(),prefix+28) || std::memcmp(bytes.data(),prologues[index].data(),prefix) ||
           std::memcmp(bytes.data()+prefix,expected.data(),14) || std::memcmp(bytes.data()+prefix+14,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            metadata=static_cast<Metadata*>(alloc_unwind_above(trampoline));if(!metadata)return false;
            metadata_[index]=metadata;*metadata=Metadata{};
            const auto rva=reinterpret_cast<std::uintptr_t>(metadata)-trampoline;
            metadata->functions={RUNTIME_FUNCTION{0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},
                RUNTIME_FUNCTION{prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}};
            metadata->unwind=unwinds[index];
        }
        const auto address=trampoline,block=reinterpret_cast<std::uintptr_t>(metadata);
        if(block<=address || block-address>UINT32_MAX-4096)return false;
        const auto rva=block-address;
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{
            {0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},
            {prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}}};
        if(std::memcmp(metadata->functions.data(),expected_functions.data(),sizeof(expected_functions)) ||
           metadata->unwind!=unwinds[index] || metadata->relay!=std::array<unsigned char,4>{1,0,0,0})return false;
        if(!patched_[index]) {
            DWORD previous{};
            if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefix),14,PAGE_EXECUTE_READWRITE,&previous))return false;
            protection_[index]=previous;std::memcpy(reinterpret_cast<void*>(trampoline+prefix),replacement.data(),14);
            patched_[index]=true;protection_pending_[index]=true;cache_pending_[index]=true;if(!protect(index))return false;
        }
        if(!registered_[index]) {
            for(unsigned offset=0;offset<prefix+28;++offset) {DWORD64 lookup{};if(RtlLookupFunctionEntry(trampoline+offset,&lookup,nullptr))return false;}
            if(!RtlAddFunctionTable(metadata->functions.data(),2,address))return false;registered_[index]=true;
        }
        if(!local_read(trampoline,bytes.data(),prefix+28) || std::memcmp(bytes.data(),prologues[index].data(),prefix) ||
           std::memcmp(bytes.data()+prefix,replacement.data(),14) || std::memcmp(bytes.data()+prefix+14,relay.data(),14))return false;
        for(unsigned offset=0;offset<prefix+28;++offset) {
            DWORD64 lookup{};const auto expected_function=offset<prefix+14?0u:1u;
            if(RtlLookupFunctionEntry(trampoline+offset,&lookup,nullptr)!=&metadata->functions[expected_function] || lookup!=address)return false;
        }
        return true;
    }
    bool stop_locked(std::uint64_t timeout_ms) noexcept {
        stop_complete_=false;const auto began=GetTickCount64();
        for(const unsigned index:{5u,4u,3u,2u,1u,0u}) {
            if(created_[index] && !original(index))return false;
            if(created_[index]) {
                const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));
                if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[index]=false;
            }
        }
        while(in_flight_) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        for(unsigned index=0;index<6;++index)if(created_[index] && !protect(index))return false;
        if(base_ && !verify(base_,read_))return false;
        stop_complete_=true;return true;
    }
    bool rollback() noexcept {enabled_=false;stopping_=true;stop_locked(5000);return false;}
    std::mutex lifecycle_;
    std::uintptr_t base_{};Read read_{};Callbacks callbacks_{};
    std::array<std::atomic<void*>,6> originals_{};
    std::array<std::atomic<bool>,6> created_{},installed_{},protection_pending_{},cache_pending_{};
    std::array<Metadata*,6> metadata_{};std::array<bool,6> patched_{},registered_{};std::array<DWORD,6> protection_{};
    std::atomic<bool> enabled_{},stopping_{true},stop_complete_{true},observer_failed_{};std::atomic<unsigned> in_flight_{};
    std::atomic<std::uint64_t> dispatch_epoch_{1};
    inline static thread_local unsigned depth_{};
};
}
