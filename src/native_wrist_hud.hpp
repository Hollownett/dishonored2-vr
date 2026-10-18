#pragma once
#include <windows.h>
#include <intrin.h>
#include "../third_party/minhook/include/MinHook.h"
#include "native_menu_state.hpp"
#include <atomic>
#include <mutex>
#include <cstring>
#include <limits>

namespace d2vr {
struct WristHudSnapshot {
    std::uintptr_t file{},iggy{};
    double health_fill{},mana_fill{};
    int equipment_id{},ammo_count{},loaded_count{};
    bool health_valid{},mana_valid{},ammo_valid{};
};
// These are native MasterHUD method arguments, not inferred maximum stat values.
// Evidence: artifacts/hud-re/wrist-native-{bindings,arguments,values}.txt.
enum class WristHudField {none,health,mana,gadget,power};
enum class WristHudReject : unsigned {none,caller,receiver,binding,file_owner,argument_block,argument_type,argument_range,native_result,exception,count};
using WristHudObserver=void (*)(bool before,std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block,std::uint32_t result) noexcept;
template<class Call> std::uint32_t wrist_hud_observed_call(Call original,WristHudObserver observer,std::uintptr_t caller,void* method,void* receiver,void* block) {
    if(observer) observer(true,caller,reinterpret_cast<std::uintptr_t>(method),reinterpret_cast<std::uintptr_t>(receiver),reinterpret_cast<std::uintptr_t>(block),0);
    const auto result=original(method,receiver,block);
    if(observer) observer(false,caller,reinterpret_cast<std::uintptr_t>(method),reinterpret_cast<std::uintptr_t>(receiver),reinterpret_cast<std::uintptr_t>(block),result);
    return result;
}
struct WristHudDiagnostic {
    WristHudReject stage{WristHudReject::none};
    std::uintptr_t caller{},method{},receiver{},block{},file{},iggy{},handle{},path{},slots{},element{},element_vtable{};
    std::uint32_t count{},capacity{},native_result{},read_mask{};
    std::array<char,64> path_text{};
    std::array<unsigned char,64> method_bytes{};
    std::array<unsigned char,32> receiver_bytes{};
    std::array<unsigned char,24> block_bytes{};
    std::array<unsigned char,96> slot_bytes{};
};
template<class Read> void wrist_hud_diagnostic_raw(Read& read,WristHudDiagnostic& out) {
    // Raw evidence is deliberately independent of eligibility. If receiver
    // validation failed, capture the verified caller's binding layout too.
    if(!out.file && out.method>=0x38) out.file=rig::value<std::uintptr_t>(read,out.method-(out.caller==0x673e7e?0x38u:8u)).value_or(0);
    if(!out.iggy) out.iggy=rig::value<std::uintptr_t>(read,out.method).value_or(0);
    if(!out.handle) out.handle=rig::value<std::uintptr_t>(read,out.method+8).value_or(0);
    if(read(out.method,out.method_bytes.data(),out.method_bytes.size())) out.read_mask|=1;
    if(read(out.receiver,out.receiver_bytes.data(),out.receiver_bytes.size())) out.read_mask|=16;
    if(read(out.block,out.block_bytes.data(),out.block_bytes.size())) out.read_mask|=2;
    out.slots=rig::value<std::uintptr_t>(read,out.block).value_or(0);
    out.capacity=rig::value<std::uint32_t>(read,out.block+0x10).value_or(0);
    out.count=rig::value<std::uint32_t>(read,out.block+0x14).value_or(0);
    if(rig::pointer(out.slots) && read(out.slots,out.slot_bytes.data(),out.slot_bytes.size())) out.read_mask|=4;
    if(rig::pointer(out.file)) out.path=rig::value<std::uintptr_t>(read,out.file+0x1658).value_or(0);
    if(rig::pointer(out.path) && read(out.path,out.path_text.data(),out.path_text.size())) out.read_mask|=8;
    out.path_text.back()=0;
    const auto offset=out.caller==0x6749ba?0x70u:out.caller==0x6752a8?0x88u:out.caller==0x673e7e?0xb0u:0x80u;
    if(out.method>=offset) {out.element=out.method-offset;out.element_vtable=rig::value<std::uintptr_t>(read,out.element).value_or(0);}
}
// Current renderer identity supplied ONLY by the already byte-verified native
// Draw hook (0x2dd580). This is not the frontend menu's unrelated batch list.
class WristHudDrawIdentity {
    struct Entry {std::uintptr_t iggy{};UINT64 tick{};};
    inline static std::mutex mutex_;
    static std::array<Entry,16> entries_; // defined after the class: clang-cl needs Entry's member initializers complete
public:
    static void clear() {std::lock_guard<std::mutex> lock(mutex_);entries_={};}
    static void observe(std::uintptr_t iggy,UINT64 tick) {
        if(!rig::pointer(iggy)||!tick) return;
        std::lock_guard<std::mutex> lock(mutex_);unsigned oldest=0;
        for(unsigned i=0;i<entries_.size();++i) {
            if(entries_[i].iggy==iggy) {entries_[i].tick=tick;return;}
            if(entries_[i].tick<entries_[oldest].tick) oldest=i;
        }
        entries_[oldest]={iggy,tick};
    }
    static bool current(std::uintptr_t iggy,UINT64 now) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& entry:entries_) if(entry.iggy==iggy && entry.tick && now>=entry.tick && now-entry.tick<=250) return true;
        return false;
    }
};
inline std::array<WristHudDrawIdentity::Entry,16> WristHudDrawIdentity::entries_{};
inline WristHudField wrist_hud_caller(std::uintptr_t caller) noexcept {
    return caller==0x6749ba?WristHudField::health:caller==0x6752a8?WristHudField::mana:
        (caller==0x673e19 || caller==0x673ef0)?WristHudField::gadget:caller==0x673e7e?WristHudField::power:WristHudField::none;
}
template<class Read> bool wrist_hud_file_owned(Read& read,std::uintptr_t /*base*/,std::uintptr_t file) {
    using namespace rig;
    if(!pointer(file)) return false;
    const auto iggy=value<std::uintptr_t>(read,file+0x1608),path=value<std::uintptr_t>(read,file+0x1658);
    char name[12]{},again[12]{};
    if(!iggy || !pointer(*iggy) || !path || !pointer(*path) || !read(*path,name,sizeof(name)) ||
       std::memcmp(name,"ui/hud.iggy",12) || !WristHudDrawIdentity::current(*iggy,GetTickCount64())) return false;
    return value<std::uintptr_t>(read,file+0x1608)==iggy && value<std::uintptr_t>(read,file+0x1658)==path &&
        read(*path,again,sizeof(again)) && !std::memcmp(name,again,sizeof(name));
}
template<class Read> std::optional<WristHudSnapshot> wrist_hud_arguments(Read& read,std::uintptr_t base,
    std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block,WristHudDiagnostic* diagnostic=nullptr) {
    using namespace rig;
    const auto field=wrist_hud_caller(caller);
    if(diagnostic) {*diagnostic={};diagnostic->caller=caller;diagnostic->method=method;diagnostic->receiver=receiver;diagnostic->block=block;diagnostic->stage=WristHudReject::caller;}
    if(field==WristHudField::none || method<8) return {};
    const auto delta=field==WristHudField::health?0xf0u:field==WristHudField::mana?0x30u:
        field==WristHudField::gadget?0x90u:0x60u;
    if(diagnostic) diagnostic->stage=WristHudReject::receiver;
    if(receiver!=method+delta) return {};
    const auto file=value<std::uintptr_t>(read,method-(field==WristHudField::power?0x38u:8u));
    const auto iggy=value<std::uintptr_t>(read,method),handle=value<std::uintptr_t>(read,method+8);
    if(diagnostic) {diagnostic->stage=WristHudReject::binding;diagnostic->file=file.value_or(0);diagnostic->iggy=iggy.value_or(0);diagnostic->handle=handle.value_or(0);}
    if(!file || !pointer(*file) || !iggy || !handle || !*handle ||
       value<std::uintptr_t>(read,*file+0x1608)!=iggy) return {};
    // Exact primary vtables verified through pinned PE MSVC RTTI COL/type
    // descriptors; native binding routines prove these method/file offsets.
    const auto offset=field==WristHudField::health?0x70u:field==WristHudField::mana?0x88u:field==WristHudField::power?0xb0u:0x80u;
    const auto element_type=field==WristHudField::health?0x1c39598u:field==WristHudField::mana?0x1c398c8u:0x1c390f8u;
    if(method<offset || !typed(read,base,method-offset,element_type)) return {};
    if(diagnostic) diagnostic->stage=WristHudReject::file_owner;
    if(!wrist_hud_file_owned(read,base,*file)) return {};
    if(value<std::uintptr_t>(read,*file+0x1608)!=iggy) return {};
    const auto binding_unchanged=[&] {
        return typed(read,base,method-offset,element_type) &&
            value<std::uintptr_t>(read,method-(field==WristHudField::power?0x38u:8u))==file &&
            value<std::uintptr_t>(read,method)==iggy && value<std::uintptr_t>(read,method+8)==handle;
    };
    if(!binding_unchanged()) {if(diagnostic)diagnostic->stage=WristHudReject::binding;return {};}
    WristHudSnapshot out{};out.file=*file;out.iggy=*iggy;
    if(field==WristHudField::power) {if(diagnostic)diagnostic->stage=WristHudReject::none;return out;} // Power selection hides the ammo widget.
    const auto count=value<std::uint32_t>(read,block+0x14),capacity=value<std::uint32_t>(read,block+0x10);
    const auto slots=value<std::uintptr_t>(read,block);
    const auto expected=field==WristHudField::health?4u:field==WristHudField::mana?6u:3u;
    if(diagnostic) diagnostic->stage=WristHudReject::argument_block;
    if(!count || !capacity || *count!=expected || *capacity<*count || *capacity>16 || !slots || !pointer(*slots)) return {};
    double numbers[3]{};
    const auto needed=field==WristHudField::gadget?3u:1u;
    for(unsigned i=0;i<needed;++i) {
        if(diagnostic) diagnostic->stage=WristHudReject::argument_type;
        const auto type=value<std::uint32_t>(read,*slots+i*0x20);
        const auto number=value<double>(read,*slots+i*0x20+0x10);
        if(!type || *type!=4 || !number || !std::isfinite(*number)) return {};
        numbers[i]=*number;
    }
    if(field==WristHudField::health || field==WristHudField::mana) {
        if(diagnostic) diagnostic->stage=WristHudReject::argument_range;
        if(numbers[0]<0 || numbers[0]>1) return {};
        if(field==WristHudField::health) {out.health_valid=true;out.health_fill=numbers[0];}
        else {out.mana_valid=true;out.mana_fill=numbers[0];}
    } else {
        if(diagnostic) diagnostic->stage=WristHudReject::argument_range;
        for(auto number:numbers) if(number<std::numeric_limits<int>::min() || number>std::numeric_limits<int>::max() || std::trunc(number)!=number) return {};
        out.equipment_id=static_cast<int>(numbers[0]);out.ammo_count=static_cast<int>(numbers[1]);out.loaded_count=static_cast<int>(numbers[2]);
        out.ammo_valid=out.ammo_count>=0 && out.loaded_count>=0;
    }
    if(!binding_unchanged()) {if(diagnostic)diagnostic->stage=WristHudReject::binding;return {};}
    if(diagnostic) diagnostic->stage=WristHudReject::none;
    return out;
}
class NativeWristHud {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Observer=WristHudObserver;
    inline static std::atomic<Observer> observer{};
    inline static std::atomic<std::uint64_t> samples{},rejected{};
    struct Diagnostics {std::array<std::uint64_t,static_cast<unsigned>(WristHudReject::count)> stages{};unsigned count{};std::array<WristHudDiagnostic,16> records{};};
private:
    using Call=std::uint32_t (__fastcall*)(void*,void*,void*);
    inline static Call original_{};
    inline static Read read_{};
    inline static std::uintptr_t base_{};
    inline static void* target_{};
    inline static std::atomic<bool> active_{};
    inline static std::atomic<unsigned> depth_{};
    inline static std::mutex mutex_;
    inline static WristHudSnapshot snapshot_{};
    static Diagnostics diagnostics_; // defined after the class: clang-cl needs Diagnostics' member initializers complete
    inline static constexpr std::array<unsigned char,26> code{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x30,0x48,0x83,0x79,0x08,0x00};
    static bool matches() noexcept {
        std::array<unsigned char,26> live{};SIZE_T size{};
        return ReadProcessMemory(GetCurrentProcess(),target_,live.data(),live.size(),&size) && size==live.size() && live==code;
    }
    static std::uint32_t __fastcall call(void* method,void* receiver,void* block) {
        ++depth_;
        const auto address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto caller=address>=base_?address-base_:0;
        const auto observe=observer.load(std::memory_order_acquire);
        std::optional<WristHudSnapshot> sample;
        WristHudDiagnostic diagnostic{};bool observed=false;unsigned record_index=16;
        if(active_ && wrist_hud_caller(caller)!=WristHudField::none) {
            observed=true;
            try {sample=wrist_hud_arguments(read_,base_,caller,reinterpret_cast<std::uintptr_t>(method),reinterpret_cast<std::uintptr_t>(receiver),reinterpret_cast<std::uintptr_t>(block),&diagnostic);} catch(...) {diagnostic.stage=WristHudReject::exception;}
            {std::lock_guard<std::mutex> lock(mutex_);if(diagnostics_.count<diagnostics_.records.size())record_index=diagnostics_.count++;}
            if(record_index<16) try {wrist_hud_diagnostic_raw(read_,diagnostic);} catch(...) {}
            if(!sample) ++rejected;
        }
        const auto result=wrist_hud_observed_call(original_,observe,caller,method,receiver,block);
        if(observed) {
            if(sample && result!=1) diagnostic.stage=WristHudReject::native_result;
            diagnostic.native_result=result;
            std::lock_guard<std::mutex> lock(mutex_);
            if(!sample || result!=1) ++diagnostics_.stages[static_cast<unsigned>(diagnostic.stage)];
            if(record_index<16) diagnostics_.records[record_index]=diagnostic;
        }
        if(sample && result==1 && active_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if(snapshot_.file!=sample->file || snapshot_.iggy!=sample->iggy) snapshot_={};
            snapshot_.file=sample->file;snapshot_.iggy=sample->iggy;
            if(sample->health_valid) {snapshot_.health_valid=true;snapshot_.health_fill=sample->health_fill;}
            if(sample->mana_valid) {snapshot_.mana_valid=true;snapshot_.mana_fill=sample->mana_fill;}
            if(wrist_hud_caller(caller)==WristHudField::gadget || wrist_hud_caller(caller)==WristHudField::power) {
                snapshot_.equipment_id=sample->equipment_id;snapshot_.ammo_count=sample->ammo_count;
                snapshot_.loaded_count=sample->loaded_count;snapshot_.ammo_valid=sample->ammo_valid;
            }
            ++samples;
        }
        --depth_;return result;
    }
public:
    static bool install(std::uintptr_t base,Read read) noexcept {
        if(!base || !read || depth_) return false;
        if(target_ && base!=base_) return false;
        if(!target_) {
            target_=reinterpret_cast<void*>(base+0x28a90);
            if(!matches() || MH_CreateHook(target_,reinterpret_cast<void*>(&call),reinterpret_cast<void**>(&original_))!=MH_OK) {target_=nullptr;return false;}
            base_=base;read_=read;
        }
        {std::lock_guard<std::mutex> lock(mutex_);snapshot_={};diagnostics_={};samples=0;rejected=0;WristHudDrawIdentity::clear();}
        const auto result=MH_EnableHook(target_);active_=result==MH_OK || result==MH_ERROR_ENABLED;
        return active_;
    }
    static WristHudSnapshot snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!active_ || !snapshot_.file || !wrist_hud_file_owned(read_,base_,snapshot_.file) ||
           rig::value<std::uintptr_t>(read_,snapshot_.file+0x1608)!=snapshot_.iggy) return {};
        return snapshot_;
    }
    static Diagnostics diagnostics() {std::lock_guard<std::mutex> lock(mutex_);return diagnostics_;}
    static void observe_draw(std::uintptr_t iggy,UINT64 tick) noexcept {try {WristHudDrawIdentity::observe(iggy,tick);} catch(...) {}}
    static bool disable() noexcept {
        active_=false;if(!target_) return true;
        const auto status=MH_DisableHook(target_);
        if(status!=MH_OK && status!=MH_ERROR_DISABLED) return false;
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        return !depth_ && matches(); // Retain the original trampoline.
    }
};
inline NativeWristHud::Diagnostics NativeWristHud::diagnostics_{};
} // namespace d2vr
