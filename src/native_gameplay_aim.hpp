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
// Developer experiment. Default is observation only. Canonical phase ordering
// is checked, but indirect/event reader scheduling still needs live evidence.
// Mutually exclusive with NativeGameplayAimTrace's hook of 7f4b30.
class NativeGameplayAim {
    using Finalize=void (__fastcall*)(void*);
    using Build=void (__fastcall*)(void*,char);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Write=bool (*)(std::uintptr_t,const void*,std::size_t);
    using Logic=UINT (*)();
    struct Scope {void* camera{};AimOwner owner{};UINT64 overlap_serial{};bool owned{};};
    inline static NativeGameplayAim* instance_{};
    inline static thread_local Scope* scope_{};
    inline static constexpr std::array<std::uintptr_t,2> rvas{0x7f6a80,0x7f4b30};
    inline static constexpr std::array<std::array<std::byte,32>,2> expected{{
        {std::byte{0x48},std::byte{0x89},std::byte{0x7c},std::byte{0x24},std::byte{0x20},std::byte{0x55},std::byte{0x48},std::byte{0x8d},
         std::byte{0x6c},std::byte{0x24},std::byte{0xd0},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x30},std::byte{0x01},
         std::byte{0},std::byte{0},std::byte{0x48},std::byte{0x8b},std::byte{0xf9},std::byte{0x48},std::byte{0x8b},std::byte{0x49},
         std::byte{0x10},std::byte{0x48},std::byte{0x8d},std::byte{0x55},std::byte{0x40},std::byte{0x0f},std::byte{0x29},std::byte{0xbc}},
        {std::byte{0x4c},std::byte{0x8b},std::byte{0xdc},std::byte{0x57},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x80},
         std::byte{0},std::byte{0},std::byte{0},std::byte{0x48},std::byte{0x8b},std::byte{0xf9},std::byte{0x84},std::byte{0xd2},
         std::byte{0x0f},std::byte{0x84},std::byte{0x5e},std::byte{0x01},std::byte{0},std::byte{0},std::byte{0x44},std::byte{0x0f},
         std::byte{0xb7},std::byte{0x81},std::byte{0x9a},std::byte{0x51},std::byte{0x02},std::byte{0},std::byte{0x48},std::byte{0x8b}}
    }};
    Finalize finalize_{};Build build_{};Read read_{};Write write_{};Logic logic_{};std::uintptr_t base_{};
    std::array<bool,2> enabled_{};
    std::atomic<bool> active_{false},route_{false},failed_{false};
    std::atomic<UINT> depth_{},producer_depth_{};
    std::atomic<UINT64> calls_{},candidates_{},applied_{},rejected_{},overlap_{},write_failures_{},rollback_failures_{};
    std::mutex mutex_;TrackedGameplayAim tracked_;std::ofstream log_;
    static AimOwner owner(const NativePlayerCamera& s) {return {s.player,s.camera,s.camera_identity.generation};}
    void plan(void* camera,char native_mode,std::uintptr_t caller) noexcept {
        try {
            auto* scope=scope_;
            if(!active_.load(std::memory_order_acquire) || failed_ || !scope || !scope->owned || scope->camera!=camera ||
               caller!=base_+0x7f727a || native_mode!=1 || logic_()!=2 || producer_depth_!=1 || overlap_!=scope->overlap_serial) return;
            auto state=native_player_camera(read_,base_,reinterpret_cast<std::uintptr_t>(camera));
            if(!state || !same_aim_owner(owner(*state),scope->owner) || state->authored_view()) {++rejected_;return;}
            std::lock_guard<std::mutex> lock(mutex_);
            const GameplayAim native{state->aim_origin,state->aim_axis};
            const auto planned=controller_gameplay_aim(native,scope->owner,tracked_,tracked_.epoch,GetTickCount64(),true);
            if(!planned) {++rejected_;return;}
            ++candidates_;bool applied=false;
            if(route_.load(std::memory_order_acquire)) {
                const auto destination=state->camera+0x2527c;
                GameplayAim before{};
                if(!read_(destination,&before,sizeof(before)) || std::memcmp(&before,&native,sizeof(native))!=0 ||
                   !active_ || logic_()!=2 || producer_depth_!=1 || overlap_!=scope->overlap_serial) {++rejected_;return;}
                const auto result=write_gameplay_aim(read_,write_,destination,native,*planned);
                if(result==AimWriteResult::changed_before_write) {++rejected_;return;}
                applied=result==AimWriteResult::applied;
                if(applied) ++applied_;
                else {
                    ++write_failures_;failed_=true;route_=false;
                    if(result==AimWriteResult::rollback_failed) ++rollback_failures_;
                }
            }
            if(candidates_<=4000) {
                log_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<state->camera<<','<<state->camera_identity.generation
                    <<','<<tracked_.epoch<<','<<(GetTickCount64()-tracked_.tick)<<','<<int(native_mode)
                    <<','<<route_.load()<<','<<applied;
                for(float v:native.origin) log_<<','<<v;for(float v:native.axis) log_<<','<<v;
                for(float v:planned->origin) log_<<','<<v;for(float v:planned->axis) log_<<','<<v;
                log_<<'\n';if(candidates_%120==0) log_.flush();
            }
        } catch(...) {++rejected_;failed_=true;route_=false;}
    }
    void enter(void* camera,std::uintptr_t caller,Scope& scope) noexcept {
        try {
            if(active_ && logic_()==2 && caller==base_+0x9533f3) {
                const auto state=native_player_camera(read_,base_,reinterpret_cast<std::uintptr_t>(camera));
                if(state && !state->authored_view()) {
                    scope.owner=owner(*state);scope.owned=true;
                    if(producer_depth_.fetch_add(1,std::memory_order_acq_rel)>0) {++overlap_;failed_=true;route_=false;}
                    scope.overlap_serial=overlap_.load();scope_=&scope;
                }
            }
        } catch(...) {++rejected_;}
    }
    static void __fastcall finalize(void* camera) {
        auto& self=*instance_;++self.depth_;
        Scope scope{camera,{},self.overlap_.load(),false};auto* previous=scope_;scope_=nullptr;
        self.enter(camera,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),scope);
        __try {self.finalize_(camera);}
        __finally {scope_=previous;if(scope.owned) --self.producer_depth_;--self.depth_;}
    }
    static void __fastcall build(void* camera,char mode) {
        auto& self=*instance_;++self.depth_;++self.calls_;
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        __try {
            // Always regenerate inherited native pose first, including when
            // tracking/route is inactive. No render worker restores these fields.
            self.build_(camera,mode);self.plan(camera,mode,caller);
        } __finally {--self.depth_;}
    }
public:
    void publish(const NativePlayerCamera& camera,const WorldTrackedPose& left,UINT64 epoch,UINT64 tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);tracked_={owner(camera),epoch,tick,left,active};
    }
    // Clears intent only; the next native constructor restores normal pose.
    void clear() {std::lock_guard<std::mutex> lock(mutex_);tracked_={};}
    void install(std::uintptr_t base,Read read,Logic logic,Write write,const std::filesystem::path& file,bool route=false) {
        if(!base || !read || !logic || !write || active_ || (instance_ && instance_!=this)) throw std::runtime_error("Invalid native aim writer ownership");
        base_=base;read_=read;logic_=logic;write_=write;
        for(unsigned i=0;i<2;++i) {
            std::array<std::byte,32> code{};
            if(!read_(base_+rvas[i],code.data(),code.size()) || code!=expected[i]) throw std::runtime_error("Native aim producer code differs");
        }
        for(const auto& call:std::array<std::array<std::uintptr_t,2>,2>{{{0x9533ee,0x7f6a80},{0x7f7275,0x7f4b30}}}) {
            std::array<std::byte,5> code{};std::int32_t displacement{};
            if(!read_(base_+call[0],code.data(),code.size()) || code[0]!=std::byte{0xe8})
                throw std::runtime_error("Native aim producer caller differs");
            std::memcpy(&displacement,code.data()+1,4);
            if(static_cast<std::intptr_t>(base_+call[0]+5)+displacement!=static_cast<std::intptr_t>(base_+call[1]))
                throw std::runtime_error("Native aim producer caller target differs");
        }
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native aim writer log");
        log_<<std::setprecision(9)<<"tick_ms,thread,camera,generation,epoch,pose_age_ms,native_mode,routing,applied";
        for(const auto* kind:{"native","planned"}) {
            for(unsigned i=0;i<3;++i) log_<<','<<kind<<"_origin"<<i;
            for(unsigned i=0;i<9;++i) log_<<','<<kind<<"_axis"<<i;
        }
        log_<<'\n';clear();calls_=candidates_=applied_=rejected_=overlap_=write_failures_=rollback_failures_=0;failed_=false;route_=route;instance_=this;
        const std::array<void*,2> hooks{reinterpret_cast<void*>(&finalize),reinterpret_cast<void*>(&build)};
        const std::array<void**,2> originals{reinterpret_cast<void**>(&finalize_),reinterpret_cast<void**>(&build_)};
        for(unsigned i=0;i<2;++i) {
            const auto target=reinterpret_cast<void*>(base_+rvas[i]);
            if(!*originals[i] && MH_CreateHook(target,hooks[i],originals[i])!=MH_OK) throw std::runtime_error("Cannot create native aim producer hook; retire passive trace first");
        }
        for(unsigned i=0;i<2;++i) {
            if(MH_EnableHook(reinterpret_cast<void*>(base_+rvas[i]))!=MH_OK) {
                for(unsigned j=0;j<i;++j) if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[j]))==MH_OK) enabled_[j]=false;
                throw std::runtime_error("Cannot enable native aim producer hook");
            }
            enabled_[i]=true;
        }
        active_=true;
    }
    bool disable() noexcept {
        if(!read_ || (!finalize_ && !build_)) return true;
        active_=false;route_=false;bool restored=true;
        for(unsigned i=0;i<2;++i) if(enabled_[i]) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[i]))==MH_OK) enabled_[i]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        for(unsigned i=0;i<2;++i) {
            std::array<std::byte,32> code{};restored=restored && read_(base_+rvas[i],code.data(),code.size()) && code==expected[i];
        }
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();tracked_={};} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 candidates() const {return candidates_.load();}
    UINT64 applied() const {return applied_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 write_failures() const {return write_failures_.load();}
    UINT64 rollback_failures() const {return rollback_failures_.load();}
    bool routing() const {return active_.load() && route_.load() && !failed_.load();}
    bool failed() const {return failed_.load();}
};
} // namespace d2vr
