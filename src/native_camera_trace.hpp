#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_player_camera.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>

namespace d2vr {
// Passive one-call-through observation on the native gameplay update, before
// and after animation/aim construction. No engine fields are changed.
class NativeCameraTrace {
    using Update=void (__fastcall*)(void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    inline static NativeCameraTrace* instance_{};
    inline static constexpr std::uintptr_t update_rva=0x7f6140;
    inline static constexpr std::array<std::byte,32> expected{
        std::byte{0x40},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x48},std::byte{0x8d},std::byte{0x6c},std::byte{0x24},
        std::byte{0xa8},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x58},std::byte{0x01},std::byte{0x00},std::byte{0x00},
        std::byte{0x4c},std::byte{0x8b},std::byte{0xf1},std::byte{0x48},std::byte{0x8b},std::byte{0x49},std::byte{0x10},std::byte{0x48},
        std::byte{0x8d},std::byte{0x55},std::byte{0x70},std::byte{0x44},std::byte{0x0f},std::byte{0x29},std::byte{0x84},std::byte{0x24}};
    Update original_{};
    Read read_{};Logic logic_{};
    std::uintptr_t base_{};
    bool enabled_{};
    std::atomic<bool> active_{false};
    std::atomic<UINT> depth_{0};
    std::atomic<UINT64> calls_{0},rejected_{0},samples_{0},errors_{0};
    std::mutex mutex_;
    std::ofstream log_;

    void observe(void* camera,UINT64 call,int phase) noexcept {
        if(!active_.load(std::memory_order_acquire) || samples_.load(std::memory_order_relaxed)>=4000) return;
        try {
            auto s=native_player_camera(read_,base_,reinterpret_cast<std::uintptr_t>(camera));
            if(!s) {++rejected_;return;}
            const auto logic=logic_();
            if(logic!=2 || (call%6!=0 && !s->authored_view())) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if(samples_>=4000 || !active_.load(std::memory_order_acquire)) return;
            log_<<call<<','<<phase<<','<<GetCurrentThreadId()<<','<<GetTickCount64()<<','<<logic<<','
                <<s->camera<<','<<s->physics<<','<<s->heading_delegate<<','<<s->mode<<','<<s->target_mode<<','
                <<s->heading<<','<<s->pitch<<','<<s->yaw<<','<<s->roll<<','<<s->pitch_velocity<<','<<s->yaw_velocity;
            for(auto f:s->look) log_<<','<<f;
            log_<<','<<s->physics_yaw_velocity<<','<<s->physics_yaw_impulse;
            for(auto f:s->entity_axis) log_<<','<<f;
            for(auto f:s->aim_origin) log_<<','<<f;
            for(auto f:s->aim_axis) log_<<','<<f;
            log_<<','<<bool(s->view);
            const auto view=s->view.value_or(GameCameraPose{});
            for(auto f:view.position) log_<<','<<f;
            for(auto f:view.basis) log_<<','<<f;
            log_<<'\n';++samples_;
        } catch(...) {++errors_;active_.store(false,std::memory_order_release);}
    }
    static void __fastcall update(void* camera) {
        auto& self=*instance_;
        self.depth_.fetch_add(1,std::memory_order_acq_rel);
        const auto call=++self.calls_;
        __try {
            self.observe(camera,call,0);
            self.original_(camera);
            self.observe(camera,call,1);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
public:
    void install(std::uintptr_t base,const std::filesystem::path& file,Read read,Logic logic) {
        if(enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Native camera trace is already owned");
        if(file.empty()) return;   // v1046: diagnostics off = no folder; a passive trace installs no hook for nothing
        base_=base;read_=read;logic_=logic;
        std::array<std::byte,32> code{};
        if(!read_(base+update_rva,code.data(),code.size()) || code!=expected)
            throw std::runtime_error("Native gameplay camera update differs from the pinned executable");
        log_.open(file,std::ios::trunc);
        if(!log_) throw std::runtime_error("Cannot open native gameplay camera trace");
        log_<<std::setprecision(9)<<"call,phase,thread,tick_ms,logic_mask,camera,physics,delegate,mode,target_mode,heading,pitch,yaw,roll,pitch_velocity,yaw_velocity,look_x,look_y,physics_yaw_velocity,physics_yaw_impulse";
        for(int i=0;i<9;++i) log_<<",entity_b"<<i;
        log_<<",aim_x,aim_y,aim_z";
        for(int i=0;i<9;++i) log_<<",aim_b"<<i;
        log_<<",view_valid,view_x,view_y,view_z";
        for(int i=0;i<9;++i) log_<<",view_b"<<i;
        log_<<'\n';calls_=0;rejected_=0;samples_=0;errors_=0;
        instance_=this;
        auto target=reinterpret_cast<void*>(base+update_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native gameplay camera observer");
        active_.store(true,std::memory_order_release);
        if(MH_EnableHook(target)!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native gameplay camera observer");}
        enabled_=true;
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+update_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<std::byte,32> code{};
        const bool restored=!original_ || (read_(base_+update_rva,code.data(),code.size()) && code==expected);
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 samples() const {return samples_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 errors() const {return errors_.load();}
};
} // namespace d2vr
