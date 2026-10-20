#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_trampoline_unwind.hpp"
#include "native_slash_speed.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace d2vr {
class NativeChokeDistanceScope {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=unsigned (*)();
    using Entry=void (__fastcall*)(std::uintptr_t);
    using Distance=float (__fastcall*)(std::uintptr_t);
    struct DistanceResult {float distance{};std::uint32_t padding{};std::uintptr_t declaration{};};
    inline static constexpr std::array<std::uintptr_t,2> rvas{0x3a6980,0x3a66f0};
    inline static constexpr std::uintptr_t return_rva=0x3a6ab3,sensor_vtable_rva=0x1b3e158,decl_vtable_rva=0x1b3e018;
    inline static constexpr std::array<std::size_t,6> offsets{0x70,0x7c,0x80,0x84,0x88,0x8c};
    inline static constexpr std::array<float,6> shipped{3.f,1.7f,-40.f,40.f,-50.f,50.f};
    inline static constexpr std::array<std::array<unsigned char,48>,2> prologues{{
        {0x40,0x53,0x48,0x83,0xec,0x70,0x80,0x79,0x38,0,0x48,0x8b,0xd9,0x0f,0x84,0x0c,3,0,0,0x48,0x63,0x41,0x58,0x48,0x89,0x7c,0x24,0x60,0x83,0xf8,0xff,0x7e,0x28,0x48,0x8b,0x51,0x20,0x4c,0x8d,0x0c,0x80,0x4c,0x8b,0x82,0x30,3,0,0},
        {0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xf1,0x8b,0x49,0x50,0x0f,0x29,0x74,0x24,0x30,0xe8,0x51,0x5e,0x33,0,0x48,0x8b,0x7e,0x10,0x48,0x8d,0x54,0x24,0x20,0x48,0x8b,0xc8,0x48,0x8b,0xd8,0xe8,0x0d}}};
    inline static constexpr std::array<unsigned char,38> caller_code{
        0x48,0x8b,0x73,8,0x48,0x8b,0xcb,0x0f,0x29,0x74,0x24,0x30,0x0f,0x29,0x7c,0x24,0x20,
        0xe8,0x3d,0xfc,0xff,0xff,0x8b,0x4b,0x50,0x0f,0x28,0xf8,0xe8,0xa2,0x5a,0x33,0,0xf3,0x0f,0x10,0x46,0x70};
    struct CodePin {std::uintptr_t rva;unsigned size;std::array<unsigned char,8> bytes;};
    inline static constexpr std::array<CodePin,7> consumer_pins{{
        {0x94fd3e,5,{0xe8,0x3d,0x6c,0xa5,0xff}},{0x3a6abe,5,{0xf3,0x0f,0x10,0x46,0x70}},
        {0x3a6b18,5,{0xf3,0x0f,0x10,0x46,0x7c}},{0x3a6c57,8,{0xf3,0x0f,0x10,0x86,0x80,0,0,0}},
        {0x3a6c64,7,{0x0f,0x2f,0x8e,0x84,0,0,0}},{0x3a6caf,8,{0xf3,0x0f,0x10,0x86,0x88,0,0,0}},
        {0x3a6cbc,7,{0x0f,0x2f,0x8e,0x8c,0,0,0}}}};
    static NativeChokeDistanceScope& process_owner() {static auto* owner=new NativeChokeDistanceScope;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;SafeReader reader{read};std::array<unsigned char,48> code{};
        for(unsigned index=0;index<2;++index)if(!reader(base+rvas[index],code.data(),code.size()) || code!=prologues[index])return false;
        return verify_consumers(base,reader);
    }
    bool install(std::uintptr_t base,Read read,Logic logic) noexcept {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock() || !logic || !ready_for_install() || (base_ && (base_!=base || read_!=read || logic_!=logic)) || !verify(base,read))return false;
        enabled(false);stopping_=true;stop_complete_=false;unwind_ready_=false;base_=base;read_=read;logic_=logic;
        if(!prepare_shim())return rollback();
        const std::array<void*,2> hooks{reinterpret_cast<void*>(&entry_hook),reinterpret_cast<void*>(shim_)};
        for(unsigned index=0;index<2;++index)if(!created_[index]) {
            void* trampoline{};const auto status=MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),hooks[index],&trampoline);
            if(status!=MH_OK)return rollback();
            originals_[index].store(trampoline,std::memory_order_release);created_[index]=true;
            if(!trampoline)return rollback();
        }
        if(!prepare_trampoline(0) || !prepare_trampoline(1) || !prepare_shim() || !verify(base,read))return rollback();
        unwind_ready_=!semantic_fixture_;
        for(unsigned index=0;index<2;++index) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();
            installed_[index]=true;
        }
        stopping_=false;return true;
    }
    void enabled(bool value) noexcept {policy_enabled_=false;++epoch_;policy_enabled_=value && installed() && !stopping_;}
    bool enabled() const noexcept {return policy_enabled_ && !stopping_;}
    void factor(float value) noexcept {if(std::isfinite(value) && value>=1.f && value<=2.f) {++epoch_;factor_=value;}}
    float factor() const noexcept {return factor_;}
    bool installed(unsigned index) const noexcept {return index<2 && installed_[index];}
    bool installed() const noexcept {return installed(0) && installed(1);}
    bool created(unsigned index) const noexcept {return index<2 && created_[index];}
    void* original(unsigned index) const noexcept {return index<2?originals_[index].load(std::memory_order_acquire):nullptr;}
    unsigned in_flight() const noexcept {return in_flight_;}
    std::uint64_t substitutions() const noexcept {return substitutions_;}
    bool unwind_registered() const noexcept {return unwind_ready_.load(std::memory_order_acquire);}
    bool stopped() const noexcept {return stop_complete_ && !installed(0) && !installed(1) && !in_flight();}
    bool ready_for_install() const noexcept {
        if(!stopped())return false;
        for(unsigned index=0;index<2;++index)if(created(index) && !original(index))return false;
        return true;
    }
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        enabled(false);stopping_=true;if(callback_depth_)return false;
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);if(!lock.owns_lock())return false;
        return stop_locked(timeout_ms);
    }
    void invoke_entry(std::uintptr_t sensor) {
        Frame frame{};frame.previous=current_frame_;frame.owner=this;frame.sensor=sensor;
        ++in_flight_;++callback_depth_;current_frame_=&frame;
        __try {entry_body(frame);}__finally {current_frame_=frame.previous;--callback_depth_;--in_flight_;}
    }
    void invoke_distance(std::uintptr_t sensor,std::uintptr_t caller,std::uintptr_t incoming,DistanceResult* output,std::uintptr_t native_rbx) {
        ++in_flight_;++callback_depth_;
        __try {distance_body(sensor,caller,incoming,output,native_rbx);}__finally {--callback_depth_;--in_flight_;}
    }
private:
    friend struct NativeChokeDistanceScopeFixture;
    NativeChokeDistanceScope()=default;
    NativeChokeDistanceScope(const NativeChokeDistanceScope&)=delete;
    NativeChokeDistanceScope& operator=(const NativeChokeDistanceScope&)=delete;
    struct SafeReader {
        Read read{};
        bool operator()(std::uintptr_t at,void* output,std::size_t size) const noexcept {return guarded_read(read,at,output,size);}
    };
    struct Qualification {NativePlayerStateLease lease{};rig::Identity identity{};std::uintptr_t declaration{};std::array<unsigned char,0x98> source{};};
    struct Frame {
        Frame* previous{};NativeChokeDistanceScope* owner{};std::uintptr_t sensor{};std::uint64_t epoch{};float factor{};
        Qualification qualification{};alignas(16) std::array<unsigned char,0x98> view{};bool prepared{};
    };
    struct Metadata {std::array<RUNTIME_FUNCTION,2> functions{};std::array<unsigned char,8> unwind{};std::array<unsigned char,4> relay{1,0,0,0};};
    struct ShimMetadata {RUNTIME_FUNCTION function{};std::array<unsigned char,12> unwind{1,9,3,0,9,0x64,9,0,4,0xa2,0,0};};
    static bool cpp_read(Read read,std::uintptr_t at,void* output,std::size_t size) noexcept {try {return read && read(at,output,size);}catch(...) {return false;}}
    static bool guarded_read(Read read,std::uintptr_t at,void* output,std::size_t size) noexcept {
        __try {return cpp_read(read,at,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool local_read(std::uintptr_t at,void* output,std::size_t size) noexcept {
        __try {std::memcpy(output,reinterpret_cast<const void*>(at),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static unsigned cpp_logic(Logic logic) noexcept {try {return logic?logic():0;}catch(...) {return 0;}}
    bool gameplay() const noexcept {__try {return cpp_logic(logic_)==2;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    static bool verify_consumers(std::uintptr_t base,SafeReader& reader) noexcept {
        std::array<unsigned char,caller_code.size()> code{};
        for(const auto& pin:consumer_pins) {std::array<unsigned char,8> bytes{};if(!reader(base+pin.rva,bytes.data(),pin.size) || std::memcmp(bytes.data(),pin.bytes.data(),pin.size))return false;}
        return reader(base+0x3a6a9d,code.data(),code.size()) && code==caller_code &&
            rig::value<std::uintptr_t>(reader,base+0x1d0b738)==base+0x94fd10;
    }
    std::optional<Qualification> qualify(std::uintptr_t sensor) const noexcept {
        SafeReader reader{read_};if(!gameplay() || !rig::typed(reader,base_,sensor,sensor_vtable_rva) || rig::value<unsigned char>(reader,sensor+0x38).value_or(0)==0)return {};
        const auto identity=rig::started(reader,sensor);
        const auto machine=rig::value<std::uintptr_t>(reader,sensor+0x20);
        if(!identity || !machine)return {};
        const auto lease=native_player_state_lease(reader,base_,*machine);
        if(!lease || rig::value<std::uintptr_t>(reader,sensor+0x10)!=lease->player)return {};
        const auto components=rig::list(reader,lease->player+0x268,4096);if(!components)return {};
        unsigned sensors{},machines{};
        for(unsigned index=0;index<components->packed_count;++index) {
            const auto component=rig::value<std::uintptr_t>(reader,components->data+index*8ull);
            if(component==sensor)++sensors;if(component==*machine)++machines;
        }
        const auto declaration=rig::value<std::uintptr_t>(reader,sensor+8);
        if(sensors!=1 || machines!=1 || !declaration || !rig::typed(reader,base_,*declaration,decl_vtable_rva) || !verify_consumers(base_,reader))return {};
        Qualification result{*lease,*identity,*declaration,{}};
        if(!reader(*declaration,result.source.data(),result.source.size()))return {};
        for(unsigned index=0;index<6;++index) {float value{};std::memcpy(&value,result.source.data()+offsets[index],4);if(value!=shipped[index])return {};}
        if(!(native_player_state_lease(reader,base_,*machine)==lease) || rig::started(reader,sensor)!=identity ||
           rig::value<std::uintptr_t>(reader,sensor+8)!=declaration || rig::value<std::uintptr_t>(reader,sensor+0x20)!=machine)return {};
        return result;
    }
    static bool same(const Qualification& first,const Qualification& second) noexcept {
        return first.lease==second.lease && first.identity==second.identity && first.declaration==second.declaration && first.source==second.source;
    }
    void entry_body(Frame& frame) {
        const auto entry=reinterpret_cast<Entry>(original(0));if(!entry)return;
        frame.epoch=epoch_;frame.factor=factor();
        const auto before=enabled()?qualify(frame.sensor):std::nullopt;
        if(before && enabled() && frame.epoch==epoch_ && frame.factor==factor()) {
            frame.qualification=*before;frame.view=before->source;
            for(unsigned index=0;index<6;++index) {const float value=shipped[index]*frame.factor;std::memcpy(frame.view.data()+offsets[index],&value,4);}
            frame.prepared=true;
        }
        entry(frame.sensor);
    }
    void distance_body(std::uintptr_t sensor,std::uintptr_t caller,std::uintptr_t incoming,DistanceResult* output,std::uintptr_t native_rbx) {
        const auto distance=reinterpret_cast<Distance>(original(1));
        output->declaration=incoming;output->distance=distance?distance(sensor):0.f;
        Frame* frame=current_frame_;
        if(!distance || caller!=base_+return_rva || native_rbx!=sensor || !frame || frame->owner!=this || frame->sensor!=sensor ||
           !frame->prepared || incoming!=frame->qualification.declaration || !enabled() || frame->epoch!=epoch_ || frame->factor!=factor())return;
        const auto after=qualify(sensor);
        if(!after || current_frame_!=frame || !same(frame->qualification,*after) || !enabled() || frame->epoch!=epoch_ || frame->factor!=factor())return;
        output->declaration=reinterpret_cast<std::uintptr_t>(frame->view.data());++substitutions_;
    }
    static void __fastcall entry_hook(std::uintptr_t sensor) {process_owner().invoke_entry(sensor);}
    static void __fastcall dispatch(std::uintptr_t sensor,std::uintptr_t caller,std::uintptr_t incoming,DistanceResult* output,std::uintptr_t native_rbx) {
        process_owner().invoke_distance(sensor,caller,incoming,output,native_rbx);
    }
    static std::array<unsigned char,55> shim_bytes() noexcept {
        std::array<unsigned char,55> code{0x48,0x83,0xec,0x58,0x48,0x89,0x74,0x24,0x48,0x48,0x8b,0x54,0x24,0x58,
            0x4c,0x8b,0xc6,0x4c,0x8d,0x4c,0x24,0x30,0x48,0x89,0x5c,0x24,0x20,0x48,0xb8,0,0,0,0,0,0,0,0,
            0xff,0xd0,0xf3,0x0f,0x10,0x44,0x24,0x30,0x48,0x8b,0x74,0x24,0x38,0x48,0x83,0xc4,0x58,0xc3};
        const auto target=reinterpret_cast<std::uintptr_t>(&dispatch);std::memcpy(code.data()+29,&target,8);return code;
    }
    bool prepare_shim() noexcept {
        if(shim_) {
            std::array<unsigned char,55> code{};DWORD64 base{};
            ShimMetadata metadata{};
            return local_read(reinterpret_cast<std::uintptr_t>(shim_metadata_),&metadata,sizeof(metadata)) && metadata.function.BeginAddress==4096 &&
                metadata.function.EndAddress==4151 && metadata.function.UnwindData==offsetof(ShimMetadata,unwind) && metadata.unwind==ShimMetadata{}.unwind &&
                local_read(shim_,code.data(),code.size()) && code==shim_bytes() &&
                RtlLookupFunctionEntry(shim_+9,&base,nullptr)==&shim_metadata_->function && base==reinterpret_cast<std::uintptr_t>(shim_metadata_);
        }
        auto* allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!allocation)return false;
        auto* metadata=reinterpret_cast<ShimMetadata*>(allocation);*metadata=ShimMetadata{};
        metadata->function={4096,4096+55,static_cast<DWORD>(offsetof(ShimMetadata,unwind))};
        const auto code=shim_bytes();std::memcpy(allocation+4096,code.data(),code.size());DWORD previous{};
        if(!unregistered(reinterpret_cast<std::uintptr_t>(allocation+4096),code.size()) ||
           !VirtualProtect(allocation+4096,4096,PAGE_EXECUTE_READ,&previous) || !FlushInstructionCache(GetCurrentProcess(),allocation+4096,code.size()) ||
           !RtlAddFunctionTable(&metadata->function,1,reinterpret_cast<DWORD64>(allocation))) {VirtualFree(allocation,0,MEM_RELEASE);return false;}
        shim_metadata_=metadata;shim_=reinterpret_cast<std::uintptr_t>(allocation+4096);return prepare_shim();
    }
    static bool unregistered(std::uintptr_t begin,std::size_t size) noexcept {
        for(std::size_t offset=0;offset<size;++offset) {DWORD64 base{};if(RtlLookupFunctionEntry(begin+offset,&base,nullptr))return false;}return true;
    }
    static std::array<unsigned char,14> tail(std::uintptr_t target,bool owned) noexcept {
        std::array<unsigned char,14> bytes{};
        if(owned) {bytes[0]=0x49;bytes[1]=0xbb;std::memcpy(bytes.data()+2,&target,8);bytes[10]=0x41;bytes[11]=0xff;bytes[12]=0xe3;bytes[13]=0x90;}
        else {bytes[0]=0xff;bytes[1]=0x25;std::memcpy(bytes.data()+6,&target,8);}return bytes;
    }
    bool restore_protection(unsigned index) noexcept {
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));const auto prefix=index?5u:6u;DWORD ignored{};
        if(protection_pending_[index]) {if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefix),14,protection_[index],&ignored))return false;protection_pending_[index]=false;}
        if(cache_pending_[index]) {if(!FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(trampoline),prefix+14))return false;cache_pending_[index]=false;}return true;
    }
    bool prepare_trampoline(unsigned index) noexcept {
        if(semantic_fixture_)return true;
        if(!restore_protection(index))return false;
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));const auto prefix=index?5u:6u;
        std::array<unsigned char,34> code{};if(!trampoline || !local_read(trampoline,code.data(),prefix+28) || std::memcmp(code.data(),prologues[index].data(),prefix))return false;
        const auto replacement=tail(base_+rvas[index]+prefix,true),expected=tail(base_+rvas[index]+prefix,patched_[index]);
        const auto relay=tail(index?shim_:reinterpret_cast<std::uintptr_t>(&entry_hook),false);
        if(std::memcmp(code.data()+prefix,expected.data(),14) || std::memcmp(code.data()+prefix+14,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            metadata=static_cast<Metadata*>(alloc_unwind_above(trampoline));if(!metadata)return false;metadata_[index]=metadata;
            const auto rva=reinterpret_cast<std::uintptr_t>(metadata)-trampoline;
            metadata->functions={RUNTIME_FUNCTION{0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},RUNTIME_FUNCTION{prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}};
            metadata->unwind=index?std::array<unsigned char,8>{1,5,2,0,5,0x34,1,0}:std::array<unsigned char,8>{1,6,2,0,6,0xd2,2,0x30};
            metadata->relay={1,0,0,0};
        }
        if(!patched_[index]) {
            DWORD previous{};if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefix),14,PAGE_EXECUTE_READWRITE,&previous))return false;
            protection_[index]=previous;std::memcpy(reinterpret_cast<void*>(trampoline+prefix),replacement.data(),14);patched_[index]=true;protection_pending_[index]=true;cache_pending_[index]=true;
            if(!restore_protection(index))return false;
        }
        const auto base=trampoline,block=reinterpret_cast<std::uintptr_t>(metadata);if(block<=base || block-base>UINT32_MAX-4096)return false;
        const auto rva=block-base;
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{
            {0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},
            {prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}}};
        const auto expected_unwind=index?std::array<unsigned char,8>{1,5,2,0,5,0x34,1,0}:std::array<unsigned char,8>{1,6,2,0,6,0xd2,2,0x30};
        if(std::memcmp(metadata->functions.data(),expected_functions.data(),sizeof(expected_functions)) || metadata->unwind!=expected_unwind || metadata->relay!=std::array<unsigned char,4>{1,0,0,0})return false;
        if(!registered_[index]) {
            if(!unregistered(trampoline,prefix+28) || !RtlAddFunctionTable(metadata->functions.data(),2,base))return false;
            registered_[index]=true;
        }
        DWORD64 lookup{};
        return local_read(trampoline+prefix,code.data(),14) && std::memcmp(code.data(),replacement.data(),14)==0 &&
            RtlLookupFunctionEntry(trampoline+prefix,&lookup,nullptr)==&metadata->functions[0] && lookup==base &&
            RtlLookupFunctionEntry(trampoline+prefix+14,&lookup,nullptr)==&metadata->functions[1] && lookup==base;
    }
    bool stop_locked(std::uint64_t timeout_ms) noexcept {
        stop_complete_=false;
        for(unsigned index=0;index<2;++index)if(created(index) && !original(index))return false;
        if(created(1)) {const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[1]));if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[1]=false;}
        const auto began=GetTickCount64();while(in_flight()) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        if(created(0)) {const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[0]));if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[0]=false;}
        while(in_flight()) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        for(unsigned index=0;index<2;++index)if(created(index) && !restore_protection(index))return false;
        if(base_ && !verify(base_,read_))return false;stop_complete_=true;return true;
    }
    bool rollback() noexcept {stop_locked(0);return false;}
    std::mutex lifecycle_;
    std::uintptr_t base_{},shim_{};Read read_{};Logic logic_{};ShimMetadata* shim_metadata_{};
    std::array<std::atomic<void*>,2> originals_{};
    std::array<std::atomic<bool>,2> created_{},installed_{};
    std::array<Metadata*,2> metadata_{};std::array<bool,2> registered_{},patched_{},protection_pending_{},cache_pending_{};
    std::array<DWORD,2> protection_{};
    std::atomic<bool> policy_enabled_{},stopping_{true},stop_complete_{true},unwind_ready_{};
    std::atomic<float> factor_{1.5f};std::atomic<std::uint64_t> epoch_{},substitutions_{};std::atomic<unsigned> in_flight_{};
    bool semantic_fixture_{};
    inline static thread_local Frame* current_frame_{};
    inline static thread_local unsigned callback_depth_{};
};
static_assert(sizeof(NativeChokeDistanceScope::DistanceResult)==16);
}
