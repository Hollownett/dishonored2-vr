#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_player_camera.hpp"
#include "gameplay_aim.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace d2vr {
// Passive observation of the actual native aim constructor, not the earlier
// animation update. No pose, camera, weapon, input or power fields are written.
class NativeGameplayAimTrace {
    using Build=void (__fastcall*)(void*,char);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    inline static NativeGameplayAimTrace* instance_{};
    inline static constexpr std::uintptr_t rva=0x7f4b30,caller_rva=0x7f727a;
    inline static constexpr std::array<std::byte,32> expected{
        std::byte{0x4c},std::byte{0x8b},std::byte{0xdc},std::byte{0x57},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x80},
        std::byte{0},std::byte{0},std::byte{0},std::byte{0x48},std::byte{0x8b},std::byte{0xf9},std::byte{0x84},std::byte{0xd2},
        std::byte{0x0f},std::byte{0x84},std::byte{0x5e},std::byte{0x01},std::byte{0},std::byte{0},std::byte{0x44},std::byte{0x0f},
        std::byte{0xb7},std::byte{0x81},std::byte{0x9a},std::byte{0x51},std::byte{0x02},std::byte{0},std::byte{0x48},std::byte{0x8b}};
    Build original_{};Read read_{};Logic logic_{};std::uintptr_t base_{};
    bool enabled_{};
    std::atomic<bool> active_{false};
    std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},rejected_{},samples_{},overlap_{};
    std::mutex mutex_;TrackedGameplayAim tracked_;std::ofstream log_;
    void observe(void* camera,char native_mode,std::uintptr_t caller,UINT64 call,int phase) noexcept {
        try {
            if(!active_.load(std::memory_order_acquire) || samples_.load(std::memory_order_relaxed)>=4000 ||
               caller!=base_+caller_rva || logic_()!=2) return;
            const auto state=native_player_camera(read_,base_,reinterpret_cast<std::uintptr_t>(camera));
            if(!state) {++rejected_;return;}
            GameplayAim native{state->aim_origin,state->aim_axis};
            std::lock_guard<std::mutex> lock(mutex_);
            if(samples_>=4000) return;
            const auto now=GetTickCount64();
            const auto candidate=controller_gameplay_aim(native,
                {state->player,state->camera,state->camera_identity.generation},tracked_,tracked_.epoch,now,!state->authored_view());
            log_<<call<<','<<phase<<','<<GetCurrentThreadId()<<','<<now<<','<<state->camera<<','
                <<state->camera_identity.generation<<','<<static_cast<int>(native_mode)<<','<<state->mode<<','<<state->target_mode
                <<','<<tracked_.epoch<<','<<(now>=tracked_.tick?now-tracked_.tick:UINT64_MAX)<<','<<bool(candidate);
            for(float value:native.origin) log_<<','<<value;
            for(float value:native.axis) log_<<','<<value;
            const auto planned=candidate.value_or(GameplayAim{});
            for(float value:planned.origin) log_<<','<<value;
            for(float value:planned.axis) log_<<','<<value;
            log_<<'\n';++samples_;if(samples_%120==0) log_.flush();
        } catch(...) {++rejected_;}
    }
    static void __fastcall build(void* camera,char mode) {
        auto& self=*instance_;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        const auto call=++self.calls_;
        __try {
            self.observe(camera,mode,caller,call,0);
            self.original_(camera,mode);
            self.observe(camera,mode,caller,call,1);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
public:
    void publish(const NativePlayerCamera& camera,const WorldTrackedPose& left,
                 UINT64 epoch,UINT64 tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        tracked_={{camera.player,camera.camera,camera.camera_identity.generation},epoch,tick,left,active};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);tracked_={};}
    void install(std::uintptr_t base,Read read,Logic logic,const std::filesystem::path& file) {
        if(!base || !read || !logic) throw std::runtime_error("Invalid native aim trace callbacks");
        if(file.empty()) return;   // v1046: diagnostics off = a passive trace hooks nothing
        if(enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Native aim trace already owned");
        base_=base;read_=read;logic_=logic;
        std::array<std::byte,32> code{};
        if(!read_(base_+rva,code.data(),code.size()) || code!=expected) throw std::runtime_error("Native aim constructor code differs");
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native aim constructor trace");
        log_<<std::setprecision(9)<<"call,phase,thread,tick_ms,camera,generation,native_mode,mode,target_mode,epoch,pose_age_ms,candidate_valid";
        for(const auto* kind:{"native","candidate"}) {
            for(unsigned i=0;i<3;++i) log_<<','<<kind<<"_origin"<<i;
            for(unsigned i=0;i<9;++i) log_<<','<<kind<<"_axis"<<i;
        }
        log_<<'\n';clear();calls_=rejected_=samples_=overlap_=0;instance_=this;
        const auto target=reinterpret_cast<void*>(base_+rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&build),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native aim constructor trace");
        if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native aim constructor trace");
        enabled_=true;active_.store(true,std::memory_order_release);
    }
    bool disable() noexcept {
        if(!read_ || !original_) return true;
        active_.store(false,std::memory_order_release);
        if(enabled_ && MH_DisableHook(reinterpret_cast<void*>(base_+rva))!=MH_OK) return false;
        enabled_=false;const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<std::byte,32> code{};
        const bool restored=read_(base_+rva,code.data(),code.size()) && code==expected;
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();tracked_={};} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 samples() const {return samples_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
};
} // namespace d2vr
