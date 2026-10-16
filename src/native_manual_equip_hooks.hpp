#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace d2vr {
class NativeManualEquipHooks {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    struct alignas(16) Params {std::array<unsigned char,48> bytes{};};
    using Processor=void (__fastcall*)(std::uintptr_t,const void*);
    using Wheel=void (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
    using ProcessorHost=bool (*)(void*,std::uintptr_t,std::uintptr_t,Params&);
    using WheelBefore=bool (*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
    using WheelAfter=bool (*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,bool,bool);
    struct Callbacks {void* context{};ProcessorHost processor{};WheelBefore wheel_before{};WheelAfter wheel_after{};};
    inline static constexpr std::array<std::uintptr_t,2> rvas{0x8363f0,0x7fb350};
    inline static constexpr std::array<unsigned,2> prefixes{5,6};
    inline static constexpr std::array<std::array<unsigned char,32>,2> prologues{{
        {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x60,0x48,0x83,0x7a,0x08,0,0x48,0x8b,0x41,0x10},
        {0x48,0x8b,0xc4,0x56,0x41,0x56,0x48,0x83,0xec,0x68,0x80,0xb9,0x71,0x01,0,0,0,0x4d,0x8b,0xf0,0x48,0x8b,0xf1,0x0f,0x84,0x23,0x01,0,0,0x48,0x89,0x58}}};
    inline static constexpr std::array<std::array<unsigned char,8>,2> unwinds{{
        {1,5,2,0,5,0x34,3,0},{1,6,2,0,6,0xe0,4,0x60}}};
    inline static constexpr std::uintptr_t wheel_return_rva=0x4f57e6;
    inline static constexpr std::array<unsigned char,5> wheel_call{0xe8,0x6a,0x5b,0x30,0};
    static NativeManualEquipHooks& process_owner() {static auto* owner=new NativeManualEquipHooks;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;
        std::array<unsigned char,32> code{};
        for(unsigned index=0;index<2;++index)
            if(!read_safe(read,base+rvas[index],code.data(),code.size()) || code!=prologues[index])return false;
        std::array<unsigned char,5> caller{};
        return read_safe(read,base+wheel_return_rva-5,caller.data(),caller.size()) && caller==wheel_call;
    }
    bool install(std::uintptr_t base,Read read,Callbacks callbacks) noexcept {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock() || !ready_for_install() || !callbacks.processor || !callbacks.wheel_before || !callbacks.wheel_after || !verify(base,read))return false;
        if(base_ && (base_!=base || read_!=read || callbacks_.context!=callbacks.context ||
           callbacks_.processor!=callbacks.processor || callbacks_.wheel_before!=callbacks.wheel_before || callbacks_.wheel_after!=callbacks.wheel_after))return false;
        if(!base_) {base_=base;read_=read;callbacks_=callbacks;}
        stopping_=true;enabled_=false;stop_complete_=false;
        const auto targets=hooks();
        for(unsigned index=0;index<2;++index)if(!created_[index]) {
            void* original{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),targets[index],&original)!=MH_OK)return rollback();
            originals_[index]=original;created_[index]=true;if(!original)return rollback();
        }
        for(unsigned index=0;index<2;++index)if(!prepare(index))return rollback();
        if(!verify(base,read))return rollback();
        for(unsigned index=0;index<2;++index) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();
            installed_[index]=true;
        }
        stopping_=false;return true;
    }
    void enabled(bool value) noexcept {
        if(!value) {
            enabled_=false;auto previous=dispatch_epoch_.load();
            while(previous!=UINT64_MAX && !dispatch_epoch_.compare_exchange_weak(previous,previous+1)) {}
        } else enabled_=dispatch_epoch_!=UINT64_MAX && !stopping_ && installed_[0] && installed_[1];
    }
    bool enabled() const noexcept {return enabled_ && !stopping_;}
    bool created(unsigned index) const noexcept {return index<2 && created_[index];}
    bool installed(unsigned index) const noexcept {return index<2 && installed_[index];}
    void* original(unsigned index) const noexcept {return index<2?originals_[index].load():nullptr;}
    unsigned in_flight() const noexcept {return in_flight_;}
    bool stopped() const noexcept {
        return stop_complete_ && !installed_[0] && !installed_[1] && !in_flight_ &&
            !protection_pending_[0] && !protection_pending_[1] && !cache_pending_[0] && !cache_pending_[1];
    }
    bool ready_for_install() const noexcept {
        if(!stopped())return false;
        for(unsigned index=0;index<2;++index)if(created_[index] && !original(index))return false;
        return true;
    }
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        enabled(false);stopping_=true;if(depth_)return false;
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        return lock.owns_lock() && stop_locked(timeout_ms);
    }
    void invoke_processor(std::uintptr_t caller,std::uintptr_t state,const void* params) {
        ++in_flight_;++depth_;
        __try {processor_body(caller,state,params);}__finally {--depth_;--in_flight_;}
    }
    void invoke_wheel(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) {
        ++in_flight_;++depth_;
        __try {wheel_body(caller,controller,item,ammo);}__finally {--depth_;--in_flight_;}
    }
private:
    friend struct NativeManualEquipHooksFixture;
    NativeManualEquipHooks()=default;
    struct Metadata {
        std::array<RUNTIME_FUNCTION,2> functions{};
        std::array<unsigned char,8> unwind{};
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
    void processor_body(std::uintptr_t caller,std::uintptr_t state,const void* params) {
        const auto native=reinterpret_cast<Processor>(original(0));if(!native)return;
        Params owned{},before{};const auto epoch=dispatch_epoch_.load();
        if(enabled() && read_safe(read_,reinterpret_cast<std::uintptr_t>(params),&owned,sizeof(owned))) {
            before=owned;
            if(!processor_host(caller,state,owned) || !enabled() || dispatch_epoch_!=epoch) {owned=before;enabled(false);}
            native(state,&owned);
        } else native(state,params);
    }
    void wheel_body(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) {
        const auto native=reinterpret_cast<Wheel>(original(1));if(!native)return;
        const auto epoch=dispatch_epoch_.load();
        const bool admitted=enabled() && caller==base_+wheel_return_rva;
        if(admitted && !wheel_before(caller,controller,item,ammo))enabled(false);
        bool completed=false;
        __try {native(controller,item,ammo);completed=true;}
        __finally {if(admitted && !wheel_after(caller,controller,item,ammo,completed,enabled() && dispatch_epoch_==epoch))enabled(false);}
    }
    bool cpp_processor(std::uintptr_t caller,std::uintptr_t state,Params& params) noexcept {
        try {return callbacks_.processor(callbacks_.context,caller,state,params);}catch(...) {return false;}
    }
    bool processor_host(std::uintptr_t caller,std::uintptr_t state,Params& params) noexcept {
        __try {return cpp_processor(caller,state,params);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool cpp_before(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) noexcept {
        try {return callbacks_.wheel_before(callbacks_.context,caller,controller,item,ammo);}catch(...) {return false;}
    }
    bool wheel_before(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) noexcept {
        __try {return cpp_before(caller,controller,item,ammo);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool cpp_after(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo,bool completed,bool current) noexcept {
        try {return callbacks_.wheel_after(callbacks_.context,caller,controller,item,ammo,completed,current);}catch(...) {return false;}
    }
    bool wheel_after(std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo,bool completed,bool current) noexcept {
        __try {return cpp_after(caller,controller,item,ammo,completed,current);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void __fastcall processor_hook(std::uintptr_t state,const void* params) {
        process_owner().invoke_processor(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,params);
    }
    static void __fastcall wheel_hook(std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) {
        process_owner().invoke_wheel(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),controller,item,ammo);
    }
    static std::array<void*,2> hooks() noexcept {return {reinterpret_cast<void*>(&processor_hook),reinterpret_cast<void*>(&wheel_hook)};}
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
        std::array<unsigned char,34> bytes{};
        const auto replacement=tail(base_+rvas[index]+prefix,true),expected=tail(base_+rvas[index]+prefix,patched_[index]),
            relay=tail(reinterpret_cast<std::uintptr_t>(hooks()[index]),false);
        if(!trampoline || !local_read(trampoline,bytes.data(),prefix+28) || std::memcmp(bytes.data(),prologues[index].data(),prefix) ||
           std::memcmp(bytes.data()+prefix,expected.data(),14) || std::memcmp(bytes.data()+prefix+14,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            const auto aligned=trampoline&~std::uintptr_t{0xffff};
            for(unsigned slot=1;slot<=4096 && aligned>slot*0x10000ull;++slot) {
                metadata=static_cast<Metadata*>(VirtualAlloc(reinterpret_cast<void*>(aligned-slot*0x10000ull),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(metadata)break;
            }
            if(!metadata)return false;
            metadata_[index]=metadata;*metadata=Metadata{};
            const auto address=reinterpret_cast<std::uintptr_t>(metadata);
            if(address>=trampoline || trampoline-address>UINT32_MAX-prefix-28)return false;
            const auto begin=static_cast<DWORD>(trampoline-address);
            metadata->functions={RUNTIME_FUNCTION{begin,begin+prefix+14,static_cast<DWORD>(offsetof(Metadata,unwind))},
                RUNTIME_FUNCTION{begin+prefix+14,begin+prefix+28,static_cast<DWORD>(offsetof(Metadata,relay))}};
            metadata->unwind=unwinds[index];
        }
        const auto address=reinterpret_cast<std::uintptr_t>(metadata),begin=trampoline-address;
        if(address>=trampoline || begin>UINT32_MAX-prefix-28)return false;
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{
            {static_cast<DWORD>(begin),static_cast<DWORD>(begin+prefix+14),static_cast<DWORD>(offsetof(Metadata,unwind))},
            {static_cast<DWORD>(begin+prefix+14),static_cast<DWORD>(begin+prefix+28),static_cast<DWORD>(offsetof(Metadata,relay))}}};
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
        for(const unsigned index:{1u,0u}) {
            if(created_[index] && !original(index))return false;
            if(created_[index]) {
                const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));
                if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[index]=false;
            }
        }
        while(in_flight_) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        for(unsigned index=0;index<2;++index)if(created_[index] && !protect(index))return false;
        if(base_ && !verify(base_,read_))return false;
        stop_complete_=true;return true;
    }
    bool rollback() noexcept {enabled_=false;stopping_=true;stop_locked(5000);return false;}
    std::mutex lifecycle_;
    std::uintptr_t base_{};Read read_{};Callbacks callbacks_{};
    std::array<std::atomic<void*>,2> originals_{};
    std::array<std::atomic<bool>,2> created_{},installed_{},protection_pending_{},cache_pending_{};
    std::array<Metadata*,2> metadata_{};std::array<bool,2> patched_{},registered_{};std::array<DWORD,2> protection_{};
    std::atomic<bool> enabled_{},stopping_{true},stop_complete_{true};std::atomic<unsigned> in_flight_{};
    std::atomic<std::uint64_t> dispatch_epoch_{1};
    inline static thread_local unsigned depth_{};
};
}
