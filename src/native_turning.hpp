#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_player_camera.hpp"
#include "turn_control.hpp"
#include "controller_input.hpp"
#include "crash_observer.hpp"
#include "head_locomotion.hpp"
#include "frame_order_trace.hpp"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <filesystem>

namespace d2vr {
class NativeTurning {
    // At 0x8000b2 the engine passes dt's float bits in EDX, not XMM1.
    // Forward all six observed arguments unchanged, including stack buffers.
    using Step=void (__fastcall*)(void*,std::uint32_t,std::uint32_t,void*,void*,void*);
    using AddYaw=void (__fastcall*)(void*,float);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    inline static NativeTurning* instance_{};
    inline static constexpr std::uintptr_t step_rva=0x1b4760,add_rva=0x1b2ff0;
    inline static constexpr std::array<std::byte,32> step_bytes{
        std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x4c},std::byte{0x89},std::byte{0x48},std::byte{0x20},std::byte{0x44},
        std::byte{0x89},std::byte{0x40},std::byte{0x18},std::byte{0x89},std::byte{0x50},std::byte{0x10},std::byte{0x55},std::byte{0x53},
        std::byte{0x56},std::byte{0x57},std::byte{0x41},std::byte{0x54},std::byte{0x41},std::byte{0x55},std::byte{0x41},std::byte{0x56},
        std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x98},std::byte{0xfa},std::byte{0xff}};
    inline static constexpr std::array<std::byte,32> add_bytes{
        std::byte{0x40},std::byte{0x53},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x30},std::byte{0x0f},std::byte{0x29},
        std::byte{0x74},std::byte{0x24},std::byte{0x20},std::byte{0x0f},std::byte{0x28},std::byte{0xf1},std::byte{0x48},std::byte{0x8b},
        std::byte{0xd9},std::byte{0xf3},std::byte{0x0f},std::byte{0x58},std::byte{0xb1},std::byte{0xd0},std::byte{0x00},std::byte{0x00},
        std::byte{0x00},std::byte{0x0f},std::byte{0x2f},std::byte{0x35},std::byte{0x78},std::byte{0x43},std::byte{0x90},std::byte{0x01}};
    Step original_{};AddYaw add_yaw_{};Read read_{};Logic logic_{};
    std::uintptr_t base_{};
    std::atomic<std::uintptr_t> camera_{0};
    bool enabled_{};
    std::atomic<bool> turning_{false};
    std::atomic<bool> native_directional_{false};
    TurnControl control_;
    TurnFeedback feedback_;
    std::mutex feedback_mutex_;
    double deferred_dt_{};
    HeadFollowing head_follow_;
    std::atomic<UINT64> head_applied_{0};
    std::atomic<double> follow_total_{0}; // head_follow_ total, readable without feedback_mutex_ (animation workers: Driver::note_root)
    std::atomic<bool> active_{false},failed_{false};
    std::atomic<UINT> depth_{0};
    std::atomic<UINT64> calls_{0},applied_{0},verified_{0},cancelled_{0},ready_tick_{0},epoch_{0};
    std::mutex log_mutex_;
    std::ofstream log_;
    static double angle_error(double a,double b) {return std::abs(std::remainder(a-b,6.283185307179586));}
    bool ready(const NativePlayerCamera& s) const {
        return !s.authored_view() &&
            std::abs(s.look[1])<.001f && std::abs(s.physics_yaw_velocity)<.001f && std::abs(s.physics_yaw_impulse)<.001f;
    }
    // A mismatched turn (death, teleport, scripted camera) cancels only that
    // turn; turning stays available. Persistent mismatch still latches failure.
    void fail() noexcept {
        ++cancelled_;ready_tick_=0;deferred_dt_=0;try {feedback_.cancel();control_.suspend();} catch(...) {}
        const auto now=GetTickCount64();
        if(now-soft_fail_window_>10000) {soft_fail_window_=now;soft_fails_=0;}
        if(++soft_fails_>20) failed_=true;
    }
    UINT64 soft_fail_window_{};unsigned soft_fails_{};
    static TurnFeedback::Owner owner(const NativePlayerCamera& s) {
        return {s.player,s.heading_delegate,s.camera_identity.generation,s.physics_identity.generation};
    }
    void acknowledge(const NativePlayerCamera& s,UINT64 call) {
        const auto pending=feedback_.pending();
        if(!pending) return;
        const auto now=GetTickCount64();
        const auto entity_heading=std::atan2(s.entity_axis[1],s.entity_axis[0]);
        const auto state=feedback_.poll(owner(s),s.heading,entity_heading,
            logic_()==2 && ready(s) && pending->request.epoch==epoch_.load(std::memory_order_acquire),now);
        if(state==TurnFeedback::State::waiting) return;
        if(state==TurnFeedback::State::acknowledged) ++verified_;
        else if(state==TurnFeedback::State::failed) fail();
        else {++cancelled_;deferred_dt_=0;control_.suspend();}
        if(!log_.is_open()) return;   // v1046: not logging = nothing to format
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_<<pending->request.update<<','<<call<<','<<GetCurrentThreadId()<<','<<now<<','<<pending->request.epoch<<','
            <<pending->dt<<','<<pending->before<<','<<pending->request.radians<<','<<pending->expected<<','
            <<s.heading<<','<<entity_heading<<','<<static_cast<int>(state)<<','<<now-pending->applied_ms<<'\n';
        log_.flush();
    }
    void invoke(void* delegate,std::uint32_t dt_bits,std::uint32_t body_id,void* a,void* b,void* c,UINT64 call) {
        float dt{};std::memcpy(&dt,&dt_bits,4);
        std::optional<TurnControl::Request> request;
        std::optional<NativePlayerCamera> before;
        std::unique_lock<std::mutex> feedback_lock;
        double expected{},turn_dt=dt,physical_delta{};
        if(active_.load(std::memory_order_acquire) && turning_ && !failed_.load(std::memory_order_acquire)) {
            try {
                before=native_player_camera(read_,base_,camera_.load(std::memory_order_acquire));
                if(!before) {before=find_native_player_camera(read_,base_);if(before) camera_=before->camera;}
                if(before && before->heading_delegate==reinterpret_cast<std::uintptr_t>(delegate)) {
                    feedback_lock=std::unique_lock<std::mutex>(feedback_mutex_);
                    acknowledge(*before,call);
                    const auto now=GetTickCount64();
                    // Native lean/wheel owns directional input and body yaw.
                    // Pause body-follow too; retain compensation for normal resumption.
                    const bool usable=!failed_ && !native_directional_.load(std::memory_order_acquire) && logic_()==2 && ready(*before) && std::isfinite(dt) && dt>0 && dt<=.1f;
                    ready_tick_.store(usable?now:0,std::memory_order_release);
                    // Carry only skipped simulation time, never replay a delta.
                    if(feedback_.pending()) {
                        if(usable) deferred_dt_+=dt;
                        else {feedback_.cancel();deferred_dt_=0;control_.suspend();}
                    } else {
                        turn_dt=double(dt)+deferred_dt_;deferred_dt_=0;
                        request=control_.consume(turn_dt,usable,now,epoch_.load(std::memory_order_acquire),call);
                        physical_delta=head_follow_.delta(dt,usable,now);
                        if(physical_delta) {
                            if(!request) request=TurnControl::Request{physical_delta,now,call,epoch_.load(std::memory_order_acquire)};
                            else request->radians+=physical_delta;
                        }
                    }
                    if(request) expected=std::remainder(double(before->heading)+float(request->radians),6.283185307179586);
                }
            } catch(...) {fail();request.reset();}
        }
        if(request) {
            CrashOperation operation("native VR yaw delta");
            add_yaw_(delegate,static_cast<float>(request->radians));++applied_;frame_order::note('T',0,{request->radians,physical_delta});
            auto immediate=rig::value<float>(read_,before->heading_delegate+0xd0);
            if(!immediate || !std::isfinite(*immediate) || angle_error(*immediate,expected)>.0001) fail();
            else {
                // The verified writer changed native heading synchronously.
                // Counter it in the room anchor before another eye can render.
                head_follow_.commit(physical_delta);follow_total_.store(head_follow_.total(),std::memory_order_release);if(physical_delta) ++head_applied_;
                feedback_.begin({owner(*before),*request,before->heading,expected,turn_dt,GetTickCount64()});
            }
        }
        // Never hold the feedback lock across the game's physics step.
        const bool relock=feedback_lock.owns_lock();
        if(relock) feedback_lock.unlock();
        original_(delegate,dt_bits,body_id,a,b,c);
        if(relock) {feedback_lock.lock(); if(feedback_.pending()) {
            try {
                const auto after=native_player_camera(read_,base_,before->camera);
                if(after) acknowledge(*after,call);
                else {feedback_.cancel();deferred_dt_=0;control_.suspend();++cancelled_;}
            } catch(...) {fail();}
        }}
    }
    static void __fastcall step(void* delegate,std::uint32_t dt,std::uint32_t body,void* a,void* b,void* c) {
        auto& self=*instance_;self.depth_.fetch_add(1,std::memory_order_acq_rel);const auto call=++self.calls_;
        __try {self.invoke(delegate,dt,body,a,b,c,call);}
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
public:
    void publish_head(double yaw,UINT64 now,UINT64 reference,const NativePlayerCamera& owner,bool usable) {
        std::lock_guard<std::mutex> lock(feedback_mutex_);
        head_follow_.publish(yaw,now,reference,owner.player,owner.camera,owner.camera_identity.generation,usable);
        follow_total_.store(head_follow_.total(),std::memory_order_release);
    }
    double head_compensation(UINT64 reference) {
        std::lock_guard<std::mutex> lock(feedback_mutex_);return head_follow_.compensation(reference);
    }
    void orient_movement(ControllerInput& input,bool gameplay,UINT64 now) {
        if(!gameplay || !input.active || native_directional_stick(input)) return;
        std::lock_guard<std::mutex> lock(feedback_mutex_);
        head_directed_stick(input.move_x,input.move_y,head_follow_.relative_yaw(now));
    }
    UINT64 head_applied() const {return head_applied_.load();}
    // The player-heading turn head-follow has applied so far (what head_compensation() returns), without the lock: the animation thread reads it at an arm upload.
    double head_follow_total() const {return follow_total_.load(std::memory_order_acquire);}
    void configure(TurnControl::Config config) {
        if(!enabled_ || !active_ || failed_ || logic_()==2)
            throw std::runtime_error("Turning settings require the active native adapter and a paused game");
        std::lock_guard<std::mutex> lock(feedback_mutex_);
        control_.configure(config);feedback_.cancel();deferred_dt_=0;ready_tick_=0;
        turning_=config.mode!=TurnControl::Mode::native;
    }
    void install(std::uintptr_t base,Read read,Logic logic,TurnControl::Config config,const std::filesystem::path& file) {
        if(enabled_ || (instance_ && instance_!=this)) throw std::runtime_error("Native VR turning is already owned");
        base_=base;read_=read;logic_=logic;control_.configure(config);turning_=config.mode!=TurnControl::Mode::native;
        const auto camera=find_native_player_camera(read_,base);
        if(!camera && logic_()==2) throw std::runtime_error("Native VR turning requires an owned, started player camera and physics object");
        camera_=camera?camera->camera:0;
        std::array<std::byte,32> code{};
        if(!read_(base+step_rva,code.data(),code.size()) || code!=step_bytes ||
           !read_(base+add_rva,code.data(),code.size()) || code!=add_bytes)
            throw std::runtime_error("Native VR heading functions differ from the pinned executable");
        if(!file.empty()) {log_.open(file,std::ios::trunc);   // v1046: an empty path (diagnostics off) is not an error
            if(!log_) throw std::runtime_error("Cannot open native VR turning feedback log");}
        log_<<std::setprecision(9)<<"requested_update,feedback_update,thread,tick_ms,pose_epoch,dt,before,requested_delta,expected,after,entity_heading,state,acknowledge_ms\n";
        calls_=0;applied_=0;verified_=0;cancelled_=0;ready_tick_=0;epoch_=0;failed_=false;instance_=this;
        feedback_.cancel();deferred_dt_=0;head_follow_={};follow_total_.store(0,std::memory_order_release);head_applied_=0;
        add_yaw_=reinterpret_cast<AddYaw>(base+add_rva);
        const auto target=reinterpret_cast<void*>(base+step_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&step),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native physics turning adapter");
        active_=true;
        if(MH_EnableHook(target)!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native physics turning adapter");}
        enabled_=true;
    }
    void publish(ControllerInput& input,bool gameplay,UINT64 now,UINT64 epoch) {
        epoch_.store(epoch,std::memory_order_release);
        const auto tick=ready_tick_.load(std::memory_order_acquire);
        const bool writer=active_ && !failed_ && tick && now>=tick && now-tick<=250;
        const bool directional=native_directional_stick(input);
        native_directional_.store(gameplay && input.active && directional,std::memory_order_release);
        if(control_.publish(quest_turn_axis_to_native(input.turn_x),input.active,gameplay,!directional,writer,now,epoch)) input.turn_x=0;
    }
    bool disable() noexcept {
        active_=false;ready_tick_=0;
        try {control_.suspend();} catch(...) {return false;}
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+step_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        feedback_.cancel();deferred_dt_=0;
        std::array<std::byte,32> code{};
        const bool restored=!original_ || (read_(base_+step_rva,code.data(),code.size()) && code==step_bytes);
        try {std::lock_guard<std::mutex> lock(log_mutex_);log_.close();} catch(...) {return false;}
        return restored;
    }
    bool smooth_live() {return !failed_.load() && control_.smooth_live();}
    bool failed() const {return failed_.load();}
    UINT64 calls() const {return calls_.load();}
    UINT64 applied() const {return applied_.load();}
    UINT64 verified() const {return verified_.load();}
    UINT64 cancelled() const {return cancelled_.load();}
};
} // namespace d2vr
