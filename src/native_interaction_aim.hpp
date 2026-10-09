#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_player_camera.hpp"
#include "interaction_ray.hpp"
#include "native_lean_query_trace.hpp"
#include "native_lean_lifecycle.hpp"
#include <atomic>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <iomanip>
#include "crash_observer.hpp"

namespace d2vr {
// Native sensor jobs and direct/queued rays, verified in the pinned PE/Ghidra.
// Retarget only local query arguments. Never write shared native camera aim.
class NativeInteractionAim {
    using Stage=void (__fastcall*)(void*);
    using Direct=std::uintptr_t (__fastcall*)(void*,const InteractionRay*,void*,void*,void*);
    using Queue=void* (__fastcall*)(void*,void*,void*,const InteractionRay*,UINT,UINT);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    struct Publication {std::uint64_t tick{},epoch{};std::uintptr_t player{},camera{};int generation{};WorldTrackedPose hand{};bool active{};};
    struct Context {NativePlayerCamera camera;Publication hand;unsigned stage;std::uintptr_t sensor;};
    inline static NativeInteractionAim* instance_{};
    inline static thread_local const Context* context_{};
    inline static thread_local const NativeLeanQuerySnapshot* lean_context_{};
    inline static thread_local NativeLeanQueryTransaction* lean_transaction_{};
    inline static constexpr std::array<std::uintptr_t,6> rvas{0x3b8430,0x3b8d60,0x3b9650,0x6c6760,0x95280,0x95550};
    inline static constexpr std::array<std::array<std::byte,32>,6> expected{{
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x53},std::byte{0x56},std::byte{0x57},std::byte{0x41},std::byte{0x54},std::byte{0x41},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x98},std::byte{0xfe},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x28},std::byte{0x02},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0xc7},std::byte{0x45}},
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x38},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xc0},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x79},std::byte{0x38},std::byte{0x00},std::byte{0x0f},std::byte{0x84},std::byte{0x76},std::byte{0x03},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x89},std::byte{0x58},std::byte{0x18}},
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x56},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x38},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xb8},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x79},std::byte{0x38},std::byte{0x00},std::byte{0x48},std::byte{0x8b},std::byte{0xf1},std::byte{0x0f},std::byte{0x84},std::byte{0x28},std::byte{0x04},std::byte{0x00},std::byte{0x00}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x56},std::byte{0x57},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xb0},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x39},std::byte{0x00},std::byte{0x49},std::byte{0x8b},std::byte{0xf9},std::byte{0x49},std::byte{0x8b},std::byte{0xf0},std::byte{0x48},std::byte{0x8b},std::byte{0xea},std::byte{0x48},std::byte{0x8b},std::byte{0xd9},std::byte{0x75},std::byte{0x07}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x40},std::byte{0x48},std::byte{0x83},std::byte{0xb9},std::byte{0x90},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x4d},std::byte{0x8b},std::byte{0xf1},std::byte{0x4d},std::byte{0x8b},std::byte{0xf8},std::byte{0x48},std::byte{0x8b},std::byte{0xda},std::byte{0x48}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x40},std::byte{0x48},std::byte{0x83},std::byte{0xb9},std::byte{0x90},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x4d},std::byte{0x8b},std::byte{0xf1},std::byte{0x4d},std::byte{0x8b},std::byte{0xf8},std::byte{0x48},std::byte{0x8b},std::byte{0xda},std::byte{0x48}}
    }};
    std::array<Stage,3> stages_{};
    Direct direct_{};std::array<Queue,2> queues_{};
    std::array<bool,6> enabled_{};
    Read read_{};Logic logic_{};std::uintptr_t base_{};
    bool route_{};
    bool observe_lean_{};
    bool observe_lean_lifetime_{};
    NativeLeanQueryTrace lean_trace_;
    NativeLeanLifecycle lean_lifetime_;
    std::atomic<bool> active_{false};
    std::atomic<UINT> depth_{};
    std::atomic<UINT64> stage_calls_{},rays_{},routed_{},rejected_{},samples_{},overlap_{};
    std::atomic<UINT> owned_depth_{};
    std::mutex mutex_;
    Publication hand_;
    // Latest native selected interactable (the game's own choice under the routed ray).
    std::uintptr_t target_object_{};std::uint64_t target_tick_{};
    std::ofstream log_;
    std::ofstream targets_;
    UINT64 target_samples_{};

    template<unsigned Index> static void __fastcall stage(void* sensor) {
        auto* self=instance_;++self->depth_;++self->stage_calls_;
        const auto* previous=context_;std::optional<Context> scope;context_=nullptr;
        const auto* previous_lean=lean_context_;std::optional<NativeLeanQuerySnapshot> lean_scope;lean_context_=nullptr;
        auto* previous_transaction=lean_transaction_;std::optional<NativeLeanQueryTransaction> lean_transaction;lean_transaction_=nullptr;
        try {
            const auto at=reinterpret_cast<std::uintptr_t>(sensor);
            if(self->active_.load(std::memory_order_acquire) && self->logic_()==2 &&
               rig::typed(self->read_,self->base_,at,0x1b403b8) && rig::started(self->read_,at)) {
                const auto camera=rig::value<std::uintptr_t>(self->read_,at+0x40);
                auto owned=camera?native_player_camera(self->read_,self->base_,*camera):std::nullopt;
                if(owned && rig::value<std::uintptr_t>(self->read_,at+0x10)==owned->player && !owned->authored_view()) {
                    Publication hand;{std::lock_guard<std::mutex> lock(self->mutex_);hand=self->hand_;}
                    scope=Context{*owned,hand,Index,at};context_=&*scope;
                }
            }
        } catch(...) {}
        if constexpr(Index==2) {
            try {
                const auto at=reinterpret_cast<std::uintptr_t>(sensor);
                if(self->observe_lean_ && self->active_.load(std::memory_order_acquire) && self->logic_()==2 &&
                   rig::typed(self->read_,self->base_,at,0x1b40648)) {
                    lean_scope=native_lean_query_snapshot(self->read_,self->base_,at);
                    if(lean_scope) {
                        lean_context_=&*lean_scope;lean_transaction.emplace();lean_transaction_=&*lean_transaction;
                        NativeLeanQueryEvent event{};event.state=*lean_scope;event.thread=GetCurrentThreadId();event.mask=lean_scope->mask;
                        lean_transaction_->push(event,GetTickCount64());
                    } else self->lean_trace_.reject();
                }
            } catch(...) {self->lean_trace_.reject();}
        }
        if(scope && self->owned_depth_.fetch_add(1)>0) ++self->overlap_;
        self->stages_[Index](sensor);
        if(lean_scope) {
            const auto after=native_lean_query_snapshot(self->read_,self->base_,lean_scope->sensor);
            NativeLeanQueryEvent event{};event.state=after.value_or(*lean_scope);event.phase=1;event.thread=GetCurrentThreadId();event.mask=event.state.mask;
            if(!after || !same_lean_resources(*lean_scope,*after)) {event.rejection=1;self->lean_trace_.reject();}
            lean_transaction_->push(event,GetTickCount64());
        }
        if(lean_scope) self->lean_trace_.commit(*lean_transaction,[] {return GetTickCount64();});
        if(scope) self->target(*scope);
        if(scope) --self->owned_depth_;
        lean_transaction_=previous_transaction;lean_context_=previous_lean;context_=previous;--self->depth_;
    }
    void target(const Context& scope) noexcept {
        try {
            const auto machine=rig::value<std::uintptr_t>(read_,scope.sensor+0x20);
            const auto interaction=machine?rig::value<std::uintptr_t>(read_,*machine+0x318):std::nullopt;
            if(!machine || !interaction || !rig::typed(read_,base_,*machine,0x1c73d58) ||
               !rig::typed(read_,base_,*interaction,0x1c72e98) || rig::value<std::uintptr_t>(read_,*interaction+0x10)!=scope.camera.player) return;
            const auto generation=rig::value<int>(read_,*interaction+0x30);
            const auto selected=rig::value<std::uintptr_t>(read_,*interaction+0x38);
            std::uintptr_t object=0,table=0,owner=0;
            if(generation && selected && *generation!=-1 && rig::pointer(*selected) &&
               rig::value<int>(read_,*selected+0x18)==generation) {
                object=*selected;table=rig::value<std::uintptr_t>(read_,object).value_or(0);
                owner=rig::value<std::uintptr_t>(read_,object+0x10).value_or(0);
            }
            const auto interacting=rig::value<unsigned char>(read_,*interaction+0x40).value_or(0);
            std::array<UINT,3> candidates{};
            if(!read_(scope.sensor+0x84,candidates.data(),sizeof(candidates))) return;
            std::lock_guard<std::mutex> lock(mutex_);
            target_object_=object;target_tick_=GetTickCount64();
            if(target_samples_<2000) {
                targets_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<scope.stage<<','<<*interaction<<','<<object<<','
                    <<generation.value_or(-1)<<','<<(table>=base_?table-base_:0)<<','<<owner<<','<<static_cast<unsigned>(interacting);
                for(auto id:candidates) targets_<<','<<id;
                targets_<<'\n';
                ++target_samples_;if(target_samples_%120==0) targets_.flush();
            }
        } catch(...) {}
    }
    const InteractionRay* ray(const InteractionRay* native,InteractionRay& copy,std::uintptr_t caller) noexcept {
        const auto* scope=context_;
        if(!active_.load(std::memory_order_acquire) || !scope) return native;
        const std::array<std::array<std::uintptr_t,2>,3> callers{{{0x3b867b,0},{0x3b9037,0},{0x3b9962,0x3b9a49}}};
        const auto rva=caller-base_;
        if(rva!=callers[scope->stage][0] && rva!=callers[scope->stage][1]) return native;
        ++rays_;
        InteractionRay original{};
        if(!read_(reinterpret_cast<std::uintptr_t>(native),&original,sizeof(original))) {++rejected_;return native;}
        bool replaced=false;
        const auto& hand=scope->hand;const auto now=GetTickCount64();
        if(route_ && hand.active && hand.epoch && now>=hand.tick && now-hand.tick<=250 &&
           hand.player==scope->camera.player && hand.camera==scope->camera.camera &&
           hand.generation==scope->camera.camera_identity.generation) {
            const auto next=controller_interaction_ray(original,scope->camera.aim_origin,scope->camera.aim_axis,hand.hand);
            if(next) {copy=*next;replaced=true;++routed_;} else ++rejected_;
        }
        try {
            if(samples_<2000) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(samples_<2000) {
                    log_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<scope->stage<<','<<scope->sensor<<','<<scope->camera.camera<<','<<replaced;
                    for(auto f:original.f) log_<<','<<f;
                    const auto& effective=replaced?copy:original;for(auto f:effective.f) log_<<','<<f;
                    log_<<'\n';++samples_;if(samples_%120==0) log_.flush();
                }
            }
        } catch(...) {}
        return replaced?&copy:native;
    }
    static std::uintptr_t __fastcall direct(void* manager,const InteractionRay* ray,void* hit,void* distance,void* normal) {
        CrashOperation operation("native interaction direct ray");
        auto* self=instance_;++self->depth_;alignas(16) InteractionRay copy{};
        const auto* selected=self->ray(ray,copy,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        const auto result=self->direct_(manager,selected,hit,distance,normal);--self->depth_;return result;
    }
    template<unsigned Index> static void* __fastcall queue(void* manager,void* id,void* filter,const InteractionRay* ray,UINT mask,UINT flags) {
        CrashOperation operation("native interaction queued ray");
        auto* self=instance_;++self->depth_;alignas(16) InteractionRay copy{};
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if constexpr(Index==1) {
            if(self->observe_lean_ && lean_context_ && lean_transaction_ && (caller==self->base_+0x3b9962 || caller==self->base_+0x3b9a49)) {
                NativeLeanQueryEvent event{};event.state=*lean_context_;event.phase=2;event.thread=GetCurrentThreadId();
                event.caller=caller;event.id_pointer=reinterpret_cast<std::uintptr_t>(id);event.mask=mask;event.flags=flags;
                event.request_pointer=reinterpret_cast<std::uintptr_t>(filter);event.axes_pointer=reinterpret_cast<std::uintptr_t>(ray);
                bool accepted=false;
                std::optional<NativeLeanLifecycle::QueueScope> lifetime_scope;
                const auto result=passive_lean_call([&] {
                    self->lean_trace_.queue();
                    const auto current=native_lean_query_snapshot(self->read_,self->base_,lean_context_->sensor);
                    accepted=self->active_.load(std::memory_order_acquire) && self->logic_()==2 && current &&
                        same_lean_resources(*lean_context_,*current) && native_lean_query_arguments(self->read_,*current,caller,self->base_,
                            reinterpret_cast<std::uintptr_t>(manager),reinterpret_cast<std::uintptr_t>(filter),
                            reinterpret_cast<std::uintptr_t>(ray),mask,flags,event);
                    const auto before_id=rig::value<int>(self->read_,event.id_pointer);
                    accepted=accepted && before_id.has_value();if(before_id) event.query_id=*before_id;
                    if(!accepted) {event.rejection=2;self->lean_trace_.reject();}
                    lean_transaction_->push(event,GetTickCount64());
                    if(self->observe_lean_lifetime_) lifetime_scope.emplace(self->lean_lifetime_,accepted?&event:nullptr);
                },[&] {return self->queues_[Index](manager,id,filter,ray,mask,flags);},[&](void* returned) {
                    event.phase=3;event.result_pointer=reinterpret_cast<std::uintptr_t>(returned);
                    const auto after=native_lean_query_snapshot(self->read_,self->base_,lean_context_->sensor);
                    std::array<std::byte,112> request_after{};std::array<std::byte,48> axes_after{};
                    const auto after_id=rig::value<int>(self->read_,event.id_pointer);
                    if(!accepted || returned!=id || !after || !same_lean_resources(event.state,*after) || !after_id ||
                       !self->read_(reinterpret_cast<std::uintptr_t>(filter),request_after.data(),112) || request_after!=event.request ||
                       !self->read_(reinterpret_cast<std::uintptr_t>(ray),axes_after.data(),48) || axes_after!=event.axes) {
                        event.rejection=3;self->lean_trace_.reject();
                    }
                    if(after) event.state=*after;if(after_id) event.query_id=*after_id;
                    lean_transaction_->push(event,GetTickCount64());
                });
                --self->depth_;return result;
            }
        }
        // The queued ABI embeds the ray at filter+0x30; its fourth argument is
        // a separate three-axis frame. 0x08ffb0 eagerly copies 112+48 bytes.
        alignas(16) std::array<std::byte,112> filter_copy{};
        alignas(16) InteractionRay frame_copy{};
        void* selected_filter=filter;const auto* selected_frame=ray;
        if(filter && self->read_(reinterpret_cast<std::uintptr_t>(filter),filter_copy.data(),filter_copy.size()) &&
           self->read_(reinterpret_cast<std::uintptr_t>(ray),&frame_copy,sizeof(frame_copy))) {
            const auto* input=reinterpret_cast<const InteractionRay*>(static_cast<const std::byte*>(filter)+0x30);
            const auto* selected=self->ray(input,copy,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            if(selected!=input && context_) {
                std::memcpy(filter_copy.data()+0x30,&copy,sizeof(copy));
                for(unsigned axis=0;axis<3;++axis) {
                    const auto& v=context_->hand.hand.basis[axis];
                    frame_copy.f[axis*4]=static_cast<float>(v.x);frame_copy.f[axis*4+1]=static_cast<float>(v.y);
                    frame_copy.f[axis*4+2]=static_cast<float>(v.z);
                }
                selected_filter=filter_copy.data();selected_frame=&frame_copy;
            }
        }
        const auto result=self->queues_[Index](manager,id,selected_filter,selected_frame,mask,flags);--self->depth_;return result;
    }
public:
    void publish(const NativePlayerCamera& camera,const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        hand_={tick,epoch,camera.player,camera.camera,camera.camera_identity.generation,hand,active};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);hand_={};target_object_=0;}
    // True while the native game has an interactable selected, sampled within the last quarter second.
    bool has_target(std::uint64_t now) {std::lock_guard<std::mutex> lock(mutex_);return target_object_ && now-target_tick_<=250;}
    void install(std::uintptr_t base,Read read,Logic logic,bool route,const std::filesystem::path& file,bool observe_lean=false,bool observe_lean_lifetime=false) {
        if(observe_lean_lifetime && !observe_lean) throw std::runtime_error("Native Lean lifetime requires typed Lean observation");
        base_=base;read_=read;logic_=logic;route_=route;observe_lean_=observe_lean;observe_lean_lifetime_=observe_lean_lifetime;clear();lean_trace_.reset();
        for(unsigned i=0;i<6;++i) {
            std::array<std::byte,32> bytes{};
            if(!read(base+rvas[i],bytes.data(),bytes.size()) || bytes!=expected[i]) throw std::runtime_error("Native interaction query code differs");
        }
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native interaction trace");
        targets_.open(file.parent_path()/L"native-interaction-targets.csv",std::ios::trunc);
        if(!targets_) throw std::runtime_error("Cannot open native interaction targets");
        if(observe_lean_) lean_trace_.open(file.parent_path()/L"native-lean-query-trace.csv");
        if(observe_lean_lifetime_) lean_lifetime_.install(base,read,file.parent_path()/L"native-lean-lifecycle.csv");
        targets_<<"tick_ms,thread,stage,interaction,target,generation,target_vtable_rva,owner,interacting,candidate_primary,candidate_secondary,candidate_character\n";
        log_<<std::setprecision(9);target_samples_=0;
        log_<<"tick_ms,thread,stage,sensor,camera,routed";
        for(const auto* prefix:{"native","effective"}) for(unsigned i=0;i<12;++i) log_<<','<<prefix<<i;log_<<'\n';
        stage_calls_=rays_=routed_=rejected_=samples_=overlap_=0;instance_=this;
        const std::array<void*,6> hooks{reinterpret_cast<void*>(&stage<0>),reinterpret_cast<void*>(&stage<1>),reinterpret_cast<void*>(&stage<2>),
            reinterpret_cast<void*>(&direct),reinterpret_cast<void*>(&queue<0>),reinterpret_cast<void*>(&queue<1>)};
        const std::array<void**,6> originals{reinterpret_cast<void**>(&stages_[0]),reinterpret_cast<void**>(&stages_[1]),reinterpret_cast<void**>(&stages_[2]),
            reinterpret_cast<void**>(&direct_),reinterpret_cast<void**>(&queues_[0]),reinterpret_cast<void**>(&queues_[1])};
        for(unsigned i=0;i<6;++i) {
            const auto target=reinterpret_cast<void*>(base+rvas[i]);
            if(!*originals[i] && MH_CreateHook(target,hooks[i],originals[i])!=MH_OK) throw std::runtime_error("Cannot create interaction query hook");
            if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable interaction query hook");
            enabled_[i]=true;
        }
        active_=true;
    }
    bool disable() noexcept {
        active_=false;bool restored=true;
        for(unsigned i=0;i<6;++i) if(enabled_[i]) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[i]))==MH_OK) enabled_[i]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        if(observe_lean_lifetime_ && !lean_lifetime_.disable()) restored=false;
        for(unsigned i=0;i<6;++i) if(i<3?stages_[i]!=nullptr:i==3?direct_!=nullptr:queues_[i-4]!=nullptr) {
            std::array<std::byte,32> bytes{};
            restored=restored && read_(base_+rvas[i],bytes.data(),bytes.size()) && bytes==expected[i];
        }
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();targets_.close();hand_={};if(observe_lean_ && !lean_trace_.close()) restored=false;} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return stage_calls_.load();}
    UINT64 rays() const {return rays_.load();}
    UINT64 routed() const {return routed_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 lean_rows() const {return lean_trace_.rows();}
    UINT64 lean_dropped() const {return lean_trace_.dropped();}
    UINT64 lean_rejected() const {return lean_trace_.rejected();}
    UINT64 lean_queues() const {return lean_trace_.queues();}
    UINT64 lean_transactions() const {return lean_trace_.transactions();}
    UINT64 lean_skipped() const {return lean_trace_.skipped();}
    bool lean_trace_failed() const {return lean_trace_.failed();}
    UINT64 lean_lifetime_rows() const {return lean_lifetime_.rows();}
    UINT64 lean_lifetime_dropped() const {return lean_lifetime_.dropped();}
    UINT64 lean_lifetime_rejected() const {return lean_lifetime_.rejected();}
    UINT64 lean_lifetime_overflow() const {return lean_lifetime_.overflow();}
    bool lean_lifetime_failed() const {return lean_lifetime_.failed();}
};
} // namespace d2vr
