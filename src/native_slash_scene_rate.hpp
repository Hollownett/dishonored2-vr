#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_slash_speed.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

namespace d2vr {
class NativeSlashSceneRate {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=unsigned (*)();
    using Perform=void (__fastcall*)(std::uintptr_t);
    using Primary=std::uint32_t* (__fastcall*)(std::uintptr_t,std::uint32_t*,std::uintptr_t,std::uintptr_t,float);
    using Secondary=std::uint32_t* (__fastcall*)(std::uintptr_t,std::uint32_t*,std::uintptr_t,std::uintptr_t,std::uint64_t,std::uintptr_t,std::uint64_t);
    using Setter=bool (__fastcall*)(std::uintptr_t,std::uint32_t,float);
    inline static constexpr std::uintptr_t slash_vtable_rva=0x1c85d28,attributes_vtable_rva=0x1ca7938,setter_rva=0x7d8fe0;
    inline static constexpr std::size_t perform_trampoline_size=22;
    static std::array<unsigned char,14> expected_perform_tail(std::uintptr_t base) noexcept {
        std::array<unsigned char,14> bytes{0xff,0x25,0,0,0,0};const auto continuation=base+rvas[0]+8;
        std::memcpy(bytes.data()+6,&continuation,sizeof(continuation));return bytes;
    }
    inline static constexpr char attribute_name[]="ARK_ATTRIBUTE_SLASH_SPEED_FACTOR";
    inline static constexpr std::array<std::uintptr_t,3> rvas{0x86cbf0,0x8549c0,0x7d91c0};
    inline static constexpr std::array<std::array<unsigned char,48>,3> prologues{{
        {0x48,0x8b,0xc4,0x56,0x48,0x83,0xec,0x60,0x80,0xb9,0xcc,0,0,0,0,0x48,0x8b,0xf1,0x0f,0x85,0xfa,2,0,0,0x80,0xb9,0xcd,0,0,0,0,0x0f,0x85,0xed,2,0,0,0x48,0x8b,0x89,0xa0,0,0,0,0x48,0x85,0xc9,0x0f},
        {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x4c,0x8b,0x35,0xb9,8,0x15,3,0x48,0x8b,0xf2,0x48,0x8b,0xf9,0x45,0x33,0xff,0x48,0x8d,5,0xa1,0xae,0x13,3,0x48,0x8d,0x54,0x24,0x70,0x4c,0x89,0x7c,0x24},
        {0x4c,0x8b,0xdc,0x49,0x89,0x5b,8,0x49,0x89,0x6b,0x10,0x49,0x89,0x73,0x18,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0x84,0x24,0x80,0,0,0,0x48,0x8b,0xac,0x24,0x90,0,0,0,0x48,0x8b,0xfa,0x49,0x89,0x6b,0xd8,0x49,0x89,0x43,0xd0,0x48}}};
    struct CallerPin {std::uintptr_t rva;std::array<unsigned char,5> bytes;};
    inline static constexpr std::array<CallerPin,8> caller_pins{{
        {0x86cd7d,{0xe8,0x5e,0x7e,0xfe,0xff}},{0x86cdb9,{0xe8,0xf2,0x7c,0xfe,0xff}},
        {0x854b78,{0xe8,0x43,0xfe,0xff,0xff}},{0x854ca3,{0xe8,0x18,0xfd,0xff,0xff}},
        {0x86c7d5,{0xe8,0xe6,0xc9,0xf6,0xff}},{0x86cdf9,{0xe8,0xa2,0xf8,0xff,0xff}},
        {0x86bf1f,{0xe8,0xcc,0x0c,0,0}},{0x86c685,{0xe9,0x66,5,0,0}}}};
    inline static constexpr std::array<std::pair<std::uintptr_t,std::uintptr_t>,2> slot_pins{{
        {slash_vtable_rva+0xc8,0x86c630},{slash_vtable_rva+0xb8,0x86bec0}}};
    inline static constexpr std::array<unsigned char,48> setter_code{
        0x40,0x53,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x0f,
        0x29,0x74,0x24,0x40,0x0f,0x28,0xf2,0x48,0x89,0x4c,0x24,0x28,0xc7,0x44,0x24,0x30,
        0,0,0,0,0x4c,0x8d,0x44,0x24,0x28,0xe8,0x52,0xc8,0xff,0xff,0x48,0x85};
    static NativeSlashSceneRate& process_owner() {static auto* owner=new NativeSlashSceneRate;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        SafeReader reader{read};if(!base || !read || !verify_callers(base,reader))return false;
        std::array<unsigned char,48> bytes{};
        for(unsigned index=0;index<rvas.size();++index)
            if(!reader(base+rvas[index],bytes.data(),bytes.size()) || bytes!=prologues[index])return false;
        return reader(base+setter_rva,bytes.data(),bytes.size()) && bytes==setter_code;
    }
    bool install(std::uintptr_t base,Read read,Logic logic) noexcept {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock() || !logic || !ready_for_install() || (base_ && (base_!=base || read_!=read || logic_!=logic)) || !verify(base,read))return false;
        enabled(false);stopping_=true;stop_complete_=false;base_=base;read_=read;logic_=logic;
        if(!setter_)setter_=reinterpret_cast<Setter>(base+setter_rva);
        const std::array<void*,3> hooks{reinterpret_cast<void*>(&perform_hook),reinterpret_cast<void*>(&primary_hook),reinterpret_cast<void*>(&secondary_hook)};
        for(unsigned index=0;index<rvas.size();++index) {
            if(created_[index].load(std::memory_order_acquire))continue;
            void* trampoline{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),hooks[index],&trampoline)!=MH_OK)return false;
            originals_[index].store(trampoline,std::memory_order_release);created_[index]=true;
            if(!trampoline)return false;
        }
        if(!prepare_perform_unwind())return false;
        for(unsigned index=0;index<rvas.size();++index) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return false;
            installed_[index]=true;
        }
        stopping_=false;return true;
    }
    void enabled(bool value) noexcept {policy_enabled_=false;++epoch_;policy_enabled_=value && installed() && !stopping_;}
    bool enabled() const noexcept {return policy_enabled_.load(std::memory_order_acquire) && !stopping_.load(std::memory_order_acquire);}
    void factor(float value) noexcept {if(std::isfinite(value) && value>=1.f && value<=2.f) {++epoch_;factor_=value;}}
    float factor() const noexcept {return factor_.load(std::memory_order_acquire);}
    bool created(unsigned index) const noexcept {return index<3 && created_[index].load(std::memory_order_acquire);}
    bool installed(unsigned index) const noexcept {return index<3 && installed_[index].load(std::memory_order_acquire);}
    bool installed() const noexcept {return installed(0) && installed(1) && installed(2);}
    bool has_trampolines() const noexcept {for(const auto& original:originals_)if(!original.load(std::memory_order_acquire))return false;return true;}
    void* original(unsigned index) const noexcept {return index<3?originals_[index].load(std::memory_order_acquire):nullptr;}
    bool perform_unwind_registered() const noexcept {return perform_unwind_registered_.load(std::memory_order_acquire);}
    unsigned in_flight() const noexcept {return in_flight_.load(std::memory_order_acquire);}
    bool stopped() const noexcept {return stop_complete_ && !in_flight() && !installed(0) && !installed(1) && !installed(2);}
    bool ready_for_install() const noexcept {
        if(!stopped())return false;
        for(unsigned index=0;index<3;++index)if(created(index) && !originals_[index].load(std::memory_order_acquire))return false;
        return true;
    }
    std::uint64_t accelerations() const noexcept {return accelerations_.load(std::memory_order_relaxed);}
    std::uint64_t setter_failures() const noexcept {return setter_failures_.load(std::memory_order_relaxed);}
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        enabled(false);stopping_=true;if(callback_depth_)return false;
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);if(!lock.owns_lock())return false;
        stop_complete_=false;bool success=true;
        for(unsigned index=0;index<3;++index)if(created(index)) {
            if(!originals_[index].load(std::memory_order_acquire)) {success=false;continue;}
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));
            if(status==MH_OK || status==MH_ERROR_DISABLED)installed_[index]=false;else success=false;
        }
        const auto began=GetTickCount64();
        while(in_flight()) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        if(!success)return false;
        if(!restore_perform_protection())return false;
        SafeReader reader{read_};std::array<unsigned char,48> bytes{};
        for(unsigned index=0;index<3;++index)if(created(index) &&
            (!reader(base_+rvas[index],bytes.data(),bytes.size()) || bytes!=prologues[index]))return false;
        stop_complete_=true;return true;
    }
    void invoke_perform(std::uintptr_t state) {
        Frame* previous=current_frame_;++in_flight_;++callback_depth_;
        __try {perform_body(state);}__finally {finish(previous);}
    }
    std::uint32_t* invoke_primary(std::uintptr_t caller,std::uintptr_t state,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,float start) {
        Frame* previous=current_frame_;std::uint32_t* result{};++in_flight_;++callback_depth_;
        __try {result=primary_body(caller,state,output,scene,context,start);}__finally {finish(previous);}
        return result;
    }
    std::uint32_t* invoke_secondary(std::uintptr_t caller,std::uintptr_t manager,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,std::uint64_t raw_fifth,std::uintptr_t start,std::uint64_t raw_seventh) {
        Frame* previous=current_frame_;std::uint32_t* result{};++in_flight_;++callback_depth_;
        __try {result=secondary_body(caller,manager,output,scene,context,raw_fifth,start,raw_seventh);}__finally {finish(previous);}
        return result;
    }
private:
    friend struct NativeSlashSceneRateFixture;
    NativeSlashSceneRate()=default;
    NativeSlashSceneRate(const NativeSlashSceneRate&)=delete;
    NativeSlashSceneRate& operator=(const NativeSlashSceneRate&)=delete;
    struct SafeReader {
        Read read{};
        bool operator()(std::uintptr_t address,void* output,std::size_t size) const noexcept {return guarded_read(read,address,output,size);}
    };
    struct Qualification {
        NativePlayerStateLease lease;std::uintptr_t state{},pool{},attributes{},entries{},entry{};float rate{};
    };
    struct Frame {
        NativeSlashSceneRate* owner;std::uintptr_t state;std::uint64_t epoch;float factor;std::optional<Qualification> before;
        std::uint32_t primary{},secondary{};bool invalid{};
    };
    void finish(Frame* previous) noexcept {current_frame_=previous;--callback_depth_;--in_flight_;}
    void perform_body(std::uintptr_t state) {
        const auto original=reinterpret_cast<Perform>(originals_[0].load(std::memory_order_acquire));if(!original)return;
        Frame frame{this,state,epoch_.load(std::memory_order_acquire),factor(),enabled()?qualify(state):std::nullopt};
        current_frame_=&frame;original(state);
        if(!eligible(frame) || !frame.primary)return;
        SafeReader reader{read_};
        if(rig::value<std::uint32_t>(reader,state+0x100)!=frame.primary)return;
        apply(frame,frame.primary,0x100);
        if(frame.secondary)apply(frame,frame.secondary,0xc8);
    }
    std::uint32_t* primary_body(std::uintptr_t caller,std::uintptr_t state,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,float start) {
        const auto original=reinterpret_cast<Primary>(originals_[1].load(std::memory_order_acquire));if(!original)return output;
        Frame* frame=current_frame_;
        const bool candidate=frame && frame->owner==this && frame->state==state &&
            (caller==base_+0x854b7d || caller==base_+0x854ca8) && eligible(*frame);
        auto* result=original(state,output,scene,context,start);
        if(candidate && current_frame_==frame && eligible(*frame) && result==output)capture(*frame,result,false);
        return result;
    }
    std::uint32_t* secondary_body(std::uintptr_t caller,std::uintptr_t manager,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,std::uint64_t raw_fifth,std::uintptr_t start,std::uint64_t raw_seventh) {
        const auto original=reinterpret_cast<Secondary>(originals_[2].load(std::memory_order_acquire));if(!original)return output;
        Frame* frame=current_frame_;
        const bool candidate=frame && frame->owner==this && frame->before && caller==base_+0x86c7da &&
            manager==frame->before->lease.game+0x1f5338 && eligible(*frame);
        auto* result=original(manager,output,scene,context,raw_fifth,start,raw_seventh);
        if(candidate && current_frame_==frame && eligible(*frame) && result==output)capture(*frame,result,true);
        return result;
    }
    static bool cpp_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return read && read(address,output,size);}catch(...) {return false;}}
    static bool guarded_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static unsigned cpp_logic(Logic logic) noexcept {try {return logic?logic():0;}catch(...) {return 0;}}
    bool gameplay() const noexcept {__try {return cpp_logic(logic_)==2;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    struct PerformMetadata {RUNTIME_FUNCTION function;std::array<unsigned char,8> unwind;};
    static bool trampoline_read(std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::array<unsigned char,14> owned_perform_tail(std::uintptr_t base) noexcept {
        std::array<unsigned char,14> bytes{0x49,0xbb};const auto continuation=base+rvas[0]+8;
        std::memcpy(bytes.data()+2,&continuation,sizeof(continuation));bytes[10]=0x41;bytes[11]=0xff;bytes[12]=0xe3;bytes[13]=0x90;return bytes;
    }
    bool restore_perform_protection() noexcept {
        const auto trampoline=originals_[0].load(std::memory_order_acquire);
        if(perform_protection_pending_) {
            DWORD ignored{};
            if(!VirtualProtect(static_cast<unsigned char*>(trampoline)+8,14,perform_protection_,&ignored))return false;
            perform_protection_pending_=false;
        }
        if(perform_cache_flush_pending_) {
            if(!FlushInstructionCache(GetCurrentProcess(),trampoline,perform_trampoline_size))return false;
            perform_cache_flush_pending_=false;
        }
        return true;
    }
    bool verify_perform_unwind() const noexcept {
        const auto trampoline=reinterpret_cast<std::uintptr_t>(originals_[0].load(std::memory_order_acquire));
        const auto metadata_base=reinterpret_cast<std::uintptr_t>(perform_metadata_);
        if(!trampoline || !metadata_base || metadata_base>=trampoline || trampoline-metadata_base>0xffffffffull-perform_trampoline_size ||
            perform_protection_pending_ || perform_cache_flush_pending_)return false;
        std::array<unsigned char,perform_trampoline_size> code{};PerformMetadata metadata{};
        if(!trampoline_read(trampoline,code.data(),code.size()) || !trampoline_read(metadata_base,&metadata,sizeof(metadata)) ||
            std::memcmp(code.data(),prologues[0].data(),8))return false;
        const auto tail=owned_perform_tail(base_);
        if(std::memcmp(code.data()+8,tail.data(),tail.size()) || metadata.function.BeginAddress!=trampoline-metadata_base ||
            metadata.function.EndAddress!=metadata.function.BeginAddress+perform_trampoline_size ||
            metadata.function.UnwindData!=offsetof(PerformMetadata,unwind) || metadata.unwind!=std::array<unsigned char,8>{1,8,2,0,8,0xb2,4,0x60})return false;
        DWORD64 lookup_base{};
        return RtlLookupFunctionEntry(trampoline+8,&lookup_base,nullptr)==&perform_metadata_->function && lookup_base==metadata_base;
    }
    bool prepare_perform_unwind() noexcept {
        if(semantic_fixture_)return true;
        if(!restore_perform_protection())return false;
        if(perform_unwind_registered_)return verify_perform_unwind();
        const auto trampoline=reinterpret_cast<std::uintptr_t>(originals_[0].load(std::memory_order_acquire));
        if(!trampoline)return false;
        std::array<unsigned char,perform_trampoline_size> current{};
        if(!trampoline_read(trampoline,current.data(),current.size()) ||
            std::memcmp(current.data(),prologues[0].data(),8))return false;
        const auto replacement=owned_perform_tail(base_);
        const auto expected=perform_relay_patched_?replacement:expected_perform_tail(base_);
        if(std::memcmp(current.data()+8,expected.data(),expected.size()))return false;
        PerformMetadata* metadata{};
        const auto aligned=trampoline&~std::uintptr_t{0xffff};
        for(unsigned slot=1;slot<=4096 && aligned>static_cast<std::uintptr_t>(slot)*0x10000;++slot) {
            const auto address=aligned-static_cast<std::uintptr_t>(slot)*0x10000;
            if(trampoline-address>0xffffffffull-perform_trampoline_size)break;
            metadata=static_cast<PerformMetadata*>(VirtualAlloc(reinterpret_cast<void*>(address),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            if(metadata)break;
        }
        if(!metadata)return false;
        const auto metadata_base=reinterpret_cast<std::uintptr_t>(metadata);
        if(metadata_base>=trampoline || trampoline-metadata_base>0xffffffffull-perform_trampoline_size) {VirtualFree(metadata,0,MEM_RELEASE);return false;}
        metadata->function.BeginAddress=static_cast<DWORD>(trampoline-metadata_base);
        metadata->function.EndAddress=metadata->function.BeginAddress+static_cast<DWORD>(perform_trampoline_size);
        metadata->function.UnwindData=static_cast<DWORD>(offsetof(PerformMetadata,unwind));
        metadata->unwind={1,8,2,0,8,0xb2,4,0x60};
        DWORD previous{};
        if(!VirtualProtect(reinterpret_cast<void*>(trampoline+8),replacement.size(),PAGE_EXECUTE_READWRITE,&previous)) {VirtualFree(metadata,0,MEM_RELEASE);return false;}
        perform_protection_=previous;
        std::memcpy(reinterpret_cast<void*>(trampoline+8),replacement.data(),replacement.size());perform_relay_patched_=true;
        DWORD ignored{};const bool protected_again=VirtualProtect(reinterpret_cast<void*>(trampoline+8),replacement.size(),previous,&ignored)!=FALSE;
        perform_protection_pending_=!protected_again;
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(trampoline),perform_trampoline_size)!=FALSE;
        perform_cache_flush_pending_=!flushed;
        if(!protected_again || !flushed) {VirtualFree(metadata,0,MEM_RELEASE);return false;}
        if(!RtlAddFunctionTable(&metadata->function,1,metadata_base)) {VirtualFree(metadata,0,MEM_RELEASE);return false;}
        perform_metadata_=metadata;perform_unwind_registered_=true;return verify_perform_unwind();
    }
    static bool verify_callers(std::uintptr_t base,SafeReader& reader) noexcept {
        for(const auto& pin:caller_pins) {std::array<unsigned char,5> bytes{};if(!reader(base+pin.rva,bytes.data(),bytes.size()) || bytes!=pin.bytes)return false;}
        for(const auto& pin:slot_pins)if(rig::value<std::uintptr_t>(reader,base+pin.first)!=base+pin.second)return false;
        return true;
    }
    std::optional<Qualification> qualify(std::uintptr_t state) const noexcept {
        SafeReader reader{read_};if(!gameplay() || !rig::typed(reader,base_,state,slash_vtable_rva) || rig::value<std::int32_t>(reader,state+8)!=39)return {};
        const auto machine=rig::value<std::uintptr_t>(reader,state+0x10);if(!machine)return {};
        const auto lease=native_player_state_lease(reader,base_,*machine);if(!lease)return {};
        const auto pool=rig::value<std::uintptr_t>(reader,*machine+0x90),attributes=rig::value<std::uintptr_t>(reader,*machine+0x330);
        if(!pool || !rig::pointer(*pool) || rig::value<std::uintptr_t>(reader,*pool+0x10+39*8)!=state ||
            !attributes || !rig::typed(reader,base_,*attributes,attributes_vtable_rva))return {};
        const auto entries=rig::list(reader,*attributes+0x48,4096);const auto index=rig::value<std::int32_t>(reader,state+0x108);
        if(!entries || !index || *index<0 || static_cast<unsigned>(*index)>=entries->packed_count || !verify_callers(base_,reader))return {};
        const auto entry=entries->data+static_cast<std::uintptr_t>(*index)*0x28;
        const auto name=rig::value<std::uintptr_t>(reader,entry);char text[sizeof(attribute_name)]{};
        const auto rate=rig::value<float>(reader,entry+0x24);
        if(!name || !rig::pointer(*name) || !reader(*name,text,sizeof(text)) || std::memcmp(text,attribute_name,sizeof(text)) || !rate)return {};
        const auto final_lease=native_player_state_lease(reader,base_,*machine);
        if(!final_lease || !(final_lease==lease) || rig::value<std::uintptr_t>(reader,*machine+0x90)!=pool ||
            rig::value<std::uintptr_t>(reader,*machine+0x330)!=attributes || rig::value<std::uintptr_t>(reader,*pool+0x10+39*8)!=state)return {};
        return Qualification{*lease,state,*pool,*attributes,entries->data,entry,*rate};
    }
    static bool same(const Qualification& before,const Qualification& after) noexcept {
        return before.lease==after.lease && before.state==after.state && before.pool==after.pool && before.attributes==after.attributes && before.entries==after.entries && before.entry==after.entry;
    }
    bool eligible(const Frame& frame) const noexcept {
        return !frame.invalid && frame.before && std::isfinite(frame.before->rate) && frame.before->rate==1.f && frame.factor>1.f && enabled() && epoch_.load(std::memory_order_acquire)==frame.epoch && gameplay();
    }
    void capture(Frame& frame,std::uint32_t* output,bool secondary) noexcept {
        SafeReader reader{read_};const auto identifier=rig::value<std::uint32_t>(reader,reinterpret_cast<std::uintptr_t>(output));
        const auto current=qualify(frame.state);
        if(!identifier || !*identifier || !current || !same(*frame.before,*current))return;
        auto& captured=secondary?frame.secondary:frame.primary;
        if(captured)frame.invalid=true;else captured=*identifier;
    }
    void apply(Frame& frame,std::uint32_t identifier,std::uintptr_t offset) {
        if(!eligible(frame))return;
        const auto current=qualify(frame.state);SafeReader reader{read_};
        if(!current || !same(*frame.before,*current) || !std::isfinite(current->rate) || current->rate!=1.f ||
            rig::value<std::uint32_t>(reader,frame.state+offset)!=identifier || !eligible(frame))return;
        if(setter_ && setter_(current->lease.game+0x1f5338,identifier,frame.factor))++accelerations_;else ++setter_failures_;
    }
    static void __fastcall perform_hook(std::uintptr_t state) {process_owner().invoke_perform(state);}
    static std::uint32_t* __fastcall primary_hook(std::uintptr_t state,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,float start) {
        return process_owner().invoke_primary(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),state,output,scene,context,start);
    }
    static std::uint32_t* __fastcall secondary_hook(std::uintptr_t manager,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,std::uint64_t raw_fifth,std::uintptr_t start,std::uint64_t raw_seventh) {
        return process_owner().invoke_secondary(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),manager,output,scene,context,raw_fifth,start,raw_seventh);
    }
    std::mutex lifecycle_;
    std::uintptr_t base_{};Read read_{};Logic logic_{};Setter setter_{};
    PerformMetadata* perform_metadata_{};
    bool perform_relay_patched_{};
    DWORD perform_protection_{};bool perform_protection_pending_{},perform_cache_flush_pending_{},semantic_fixture_{};
    std::atomic<bool> perform_unwind_registered_{};
    std::array<std::atomic<void*>,3> originals_{};
    std::array<std::atomic<bool>,3> created_{},installed_{};
    std::atomic<bool> policy_enabled_{},stopping_{true},stop_complete_{true};
    std::atomic<float> factor_{1.7f};
    std::atomic<unsigned> in_flight_{};
    std::atomic<std::uint64_t> epoch_{},accelerations_{},setter_failures_{};
    inline static thread_local unsigned callback_depth_{};
    inline static thread_local Frame* current_frame_{};
};
}
