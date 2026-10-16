#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_slash_speed.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace d2vr {
class NativeBlockEligibility {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Original=std::uint8_t (__fastcall*)(std::uintptr_t,std::int32_t);
    inline static constexpr std::uintptr_t prologue_rva=0x8031b0,caller_rva=0x85a2ed,return_rva=0x85a304,
        block_vtable_rva=0x1c81a68,equip_vtable_rva=0x1c79cf8;
    inline static constexpr std::array<unsigned char,48> prologue{
        0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x20,0x40,0x32,0xf6,0x48,0x8b,0xd9,
        0x83,0xfa,0x5a,0x0f,0x8d,0xa3,0x00,0x00,0x00,0x48,0x89,0x7c,0x24,0x30,0x48,0x63,
        0xfa,0x40,0x38,0xb4,0x0f,0x30,0x02,0x00,0x00,0x0f,0x84,0x88,0x00,0x00,0x00,0x40};
    inline static constexpr std::array<unsigned char,68> caller_code{
        0x41,0x0f,0x2f,0x87,0x00,0x01,0x00,0x00,0x72,0x3a,0xba,0x40,0x00,0x00,0x00,0x48,0x8b,0xce,
        0xe8,0xac,0x8e,0xfa,0xff,0x84,0xc0,0x74,0x29,0x4c,0x8d,0x45,0xd7,0xba,0x40,0x00,0x00,0x00,
        0x48,0x8b,0xce,0xe8,0x97,0x91,0xfa,0xff,0x84,0xc0,0x74,0x14,0x4c,0x8d,0x45,0xd7,0xba,0x40,
        0x00,0x00,0x00,0x48,0x8b,0xce,0xe8,0xa2,0x81,0xfa,0xff,0x89,0x7b,0x54};
    inline static constexpr std::array<std::pair<std::uintptr_t,std::uintptr_t>,2> slot_pins{{
        {block_vtable_rva+0x68,0x85a160},{equip_vtable_rva+0x30,0x854890}}};
    static NativeBlockEligibility& process_owner() {
        static auto* owner=new NativeBlockEligibility;return *owner;
    }
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;
        SafeReader reader{read};std::array<unsigned char,prologue.size()> entry{};std::array<unsigned char,caller_code.size()> caller{};
        if(!reader(base+prologue_rva,entry.data(),entry.size()) || entry!=prologue ||
           !reader(base+caller_rva,caller.data(),caller.size()) || caller!=caller_code)return false;
        for(const auto& pin:slot_pins)if(rig::value<std::uintptr_t>(reader,base+pin.first)!=base+pin.second)return false;
        return true;
    }
    bool install(std::uintptr_t base,Read read) noexcept {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock() || installed_ || in_flight_ || !stop_complete_ || (created_ && !original_.load(std::memory_order_acquire)))return false;
        if(created_ && (base!=base_ || read!=read_))return false;
        if(!verify(base,read))return false;
        policy_enabled_=false;stopping_=true;stop_complete_=false;
        if(!created_) {
            void* trampoline{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+prologue_rva),reinterpret_cast<void*>(&hook),&trampoline)!=MH_OK) {
                stop_complete_=true;return false;
            }
            base_=base;read_=read;original_.store(reinterpret_cast<Original>(trampoline),std::memory_order_release);created_=true;
            if(!trampoline)return false;
        }
        if(MH_EnableHook(reinterpret_cast<void*>(base_+prologue_rva))!=MH_OK)return false;
        installed_=true;stopping_=false;return true;
    }
    void enabled(bool value) noexcept {
        policy_enabled_.store(value && installed_.load(std::memory_order_acquire) && !stopping_.load(std::memory_order_acquire),std::memory_order_release);
    }
    bool enabled() const noexcept {return policy_enabled_.load(std::memory_order_acquire) && !stopping_.load(std::memory_order_acquire);}
    bool installed() const noexcept {return installed_.load(std::memory_order_acquire);}
    bool has_trampoline() const noexcept {return original_.load(std::memory_order_acquire)!=nullptr;}
    bool stopped() const noexcept {return stop_complete_.load(std::memory_order_acquire) && !installed() && !in_flight_.load(std::memory_order_acquire);}
    bool ready_for_install() const noexcept {return stopped() && (!created_.load(std::memory_order_acquire) || has_trampoline());}
    unsigned in_flight() const noexcept {return in_flight_.load(std::memory_order_acquire);}
    std::uint64_t vetoes() const noexcept {return vetoes_.load(std::memory_order_relaxed);}
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        policy_enabled_=false;stopping_=true;
        if(callback_depth_)return false;
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);if(!lock.owns_lock())return false;
        if(!created_) {stop_complete_=!in_flight_;return stop_complete_;}
        stop_complete_=false;
        if(!original_.load(std::memory_order_acquire))return false;
        const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+prologue_rva));
        if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;
        installed_=false;
        const auto began=GetTickCount64();
        while(in_flight_.load(std::memory_order_acquire)) {
            if(GetTickCount64()-began>=timeout_ms)return false;
            Sleep(1);
        }
        SafeReader reader{read_};std::array<unsigned char,prologue.size()> entry{};
        if(!reader(base_+prologue_rva,entry.data(),entry.size()) || entry!=prologue)return false;
        stop_complete_=true;return true;
    }
    std::uint8_t invoke(std::uintptr_t return_address,std::uintptr_t machine,std::int32_t state) {
        ++in_flight_;++callback_depth_;
        __try {return invoke_cpp(return_address,machine,state);}
        __finally {--callback_depth_;--in_flight_;}
    }
private:
    std::uint8_t invoke_cpp(std::uintptr_t return_address,std::uintptr_t machine,std::int32_t state) {
        const auto original=original_.load(std::memory_order_acquire);if(!original)return 0;
        const bool candidate=return_address==base_+return_rva && state==64 && enabled();
        const auto before=candidate?qualify(machine):std::nullopt;
        const auto result=original(machine,state);
        if(!result || !before || !enabled())return result;
        const auto after=qualify(machine);
        if(!after || !same(*before,*after) || !enabled())return result;
        ++vetoes_;return 0;
    }
private:
    friend struct NativeBlockEligibilityFixture;
    NativeBlockEligibility()=default;
    NativeBlockEligibility(const NativeBlockEligibility&)=delete;
    NativeBlockEligibility& operator=(const NativeBlockEligibility&)=delete;
    struct SafeReader {
        Read reader{};
        bool operator()(std::uintptr_t address,void* output,std::size_t size) const noexcept {return read_guard(reader,address,output,size);}
    };
    struct Qualification {NativePlayerStateLease lease;std::uintptr_t pool{},block{},equip{},block_decl{},equip_decl{};};
    static bool read_cpp(Read reader,std::uintptr_t address,void* output,std::size_t size) noexcept {
        try {return reader && reader(address,output,size);}catch(...) {return false;}
    }
    static bool read_guard(Read reader,std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {return read_cpp(reader,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::uint8_t __fastcall hook(std::uintptr_t machine,std::int32_t state) {
        const auto return_address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return process_owner().invoke(return_address,machine,state);
    }
    std::optional<Qualification> qualify(std::uintptr_t machine) const noexcept {
        SafeReader reader{read_};
        const auto lease=native_player_state_lease(reader,base_,machine);if(!lease)return {};
        const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);if(!pool || !rig::pointer(*pool))return {};
        const auto block=rig::value<std::uintptr_t>(reader,*pool+0x10+44*8),equip=rig::value<std::uintptr_t>(reader,*pool+0x10+64*8);
        if(!block || !equip || !rig::typed(reader,base_,*block,block_vtable_rva) || !rig::typed(reader,base_,*equip,equip_vtable_rva) ||
           rig::value<std::uintptr_t>(reader,*block+0x10)!=machine || rig::value<std::uintptr_t>(reader,*equip+0x10)!=machine ||
           rig::value<std::int32_t>(reader,*block+8)!=44 || rig::value<std::int32_t>(reader,*equip+8)!=64 ||
           rig::value<unsigned char>(reader,*block+0x20).value_or(0)==0 || rig::value<std::int32_t>(reader,*block+0x54)!=0)return {};
        for(const auto& pin:slot_pins)if(rig::value<std::uintptr_t>(reader,base_+pin.first)!=base_+pin.second)return {};
        const auto block_decl=rig::value<std::uintptr_t>(reader,*block+0x18),equip_decl=rig::value<std::uintptr_t>(reader,*equip+0x18);
        if(!block_decl || !equip_decl || !rig::pointer(*block_decl) || !rig::pointer(*equip_decl))return {};
        const auto final_lease=native_player_state_lease(reader,base_,machine);
        if(!final_lease || !(*lease==*final_lease) || rig::value<std::uintptr_t>(reader,machine+0x90)!=pool ||
           rig::value<std::uintptr_t>(reader,*pool+0x10+44*8)!=block || rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=equip)return {};
        return Qualification{*lease,*pool,*block,*equip,*block_decl,*equip_decl};
    }
    static bool same(const Qualification& first,const Qualification& second) noexcept {
        return first.lease==second.lease && first.pool==second.pool && first.block==second.block && first.equip==second.equip &&
            first.block_decl==second.block_decl && first.equip_decl==second.equip_decl;
    }
    std::mutex lifecycle_;
    std::uintptr_t base_{};Read read_{};
    std::atomic<Original> original_{};
    std::atomic<bool> created_{},installed_{},policy_enabled_{},stopping_{true},stop_complete_{true};
    std::atomic<unsigned> in_flight_{};
    std::atomic<std::uint64_t> vetoes_{};
    inline static thread_local unsigned callback_depth_{};
};
}
