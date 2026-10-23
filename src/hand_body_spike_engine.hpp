#pragma once
// Plan B task B11: the real engine behind the hand-body spike seam (src/hand_body_spike.hpp). Dev knob build/hand-body-spike only; with the knob
// absent nothing here is installed (no hook, no counter, no thread). docs/native-hand-body-re.md is the authority for every RVA and layout.
//
// Hooks (count-only, MinHook, byte-pinned): 0x8ad260 game frame entry (frames, thread), 0x97bc0 physics step (depth, thread, 0x8a7820 at entry),
// 0x945f0 the step's drain (depth), 0x853220 owner wake (calls with the hand's owner), 0x8527a0 OnExitedBroadPhase (calls for the hand's handle).
// The spike runs from the existing 0x89fb0 hook (NativeGrab::invoke calls hand_body_spike_dispatch after the original dispatcher): only at a
// dispatch outside the step (0x97bc0 depth 0), on the 0x8ad260 thread, once per game frame, and only after G1 passed.
// Engine calls (all pinned, all through guarded_call, all on that thread and only when 0x8a7820 reads false):
//   0x18cb90 userdata ctor and 0xca6550 sphere ctor (pure stores into mod memory, Q1 1.4 / Q2), 0xca65f0 cinfo ctor, 0xc8e5b0 createBody
//   (addMode 1, activation 1), 0xc85dc0 velocity (the press), 0xc8f6b0 destroyBodies (mode 0, as every engine caller passes).
// Never called: 0x91b50 / 0x91e20 (no pair is ever disabled, so none needs re-enabling before the destroy), 0xc85860, 0xc90de0, any spawn.
#include "native_guarded_call.hpp"
#include <windows.h>
#include <MinHook.h>
#include "hand_body_spike.hpp"
#include "hand_body_probe.hpp"
#include "native_hand_volume.hpp"
#include "native_player_camera.hpp"
#include "native_world_query_source.hpp"
#include "native_world_trace.hpp"
#include "crash_observer.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace d2vr {

struct HandBodySpikeSnapshot {   // the spike log row (outside the class: clang-cl needs its member initializers complete)
    double t{};int phase{};std::string g1_reason,outcome,owner,owner_reason;G1Stats g1;G2Counts g2_since;std::uint64_t wake_total{},exit_total{};
    bool g2_rejected{};std::optional<BodyProbeRow> owner_row;std::optional<Vec3> player,body;double floor_z{std::numeric_limits<double>::quiet_NaN()};
    std::optional<SpikeReport> report;int stage{-1};std::uint64_t cast_excluded_hits{};std::array<std::uint32_t,3> after_destroy{};bool after_destroy_read{};
    bool body_alive_at_stop{},unhooked{},done{};std::string note;
};

class HandBodySpikeEngine {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Camera=std::uintptr_t (*)();
    struct Pin {std::uintptr_t rva;std::array<unsigned char,16> code;};
    // First 16 file bytes at every called or hooked RVA (Dishonored2.exe b671f824...; docs/native-hand-body-re.md pins, 0xc8f6b0 and 0xc85dc0 read
    // from the exe for this task). The sphere ctor is pinned over its whole 113 bytes by NativeHandVolume::constructor_code.
    static constexpr std::array<Pin,11> pins{{
        {0x8ad260,{0x40,0x57,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48}},
        {0x97bc0,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48}},
        {0x945f0,{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x70,0x48,0xc7,0x40,0x98}},
        {0x853220,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48}},
        {0x8527a0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0x12}},
        {0xc8e5b0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x4c}},
        {0xca65f0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0x0f,0x57,0xc9,0x48}},
        {0xc8f6b0,{0x48,0x8b,0xc4,0x48,0x83,0xec,0x48,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48}},
        {0x18cb90,{0x0f,0x57,0xc0,0x33,0xd2,0x48,0x8d,0x05,0x9c,0x24,0x97,0x01,0x48,0x89,0x01,0x48}},
        {0x8a7820,{0x80,0xb9,0x21,0x71,0x33,0x00,0x00,0x75,0x1c,0x80,0xb9,0x22,0x71,0x33,0x00,0x00}},
        {0xc85dc0,{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48}},
    }};
    static constexpr std::uintptr_t frame_rva=0x8ad260,step_rva=0x97bc0,drain_rva=0x945f0,wake_rva=0x853220,exit_rva=0x8527a0,
        create_rva=0xc8e5b0,cinfo_rva=0xca65f0,destroy_rva=0xc8f6b0,userdata_rva=0x18cb90,velocity_rva=0xc85dc0,sphere_rva=0xca6550,
        userdata_vtable_rva=0x1aff038,userdata_dtor_rva=0x801380,player_userdata_vtable_rva=0x1b02768,sphere_vtable_rva=0x1ec1e78;
    static constexpr double g1_min_s=8,body_life_s=60;

    // Checks every pin and the two userdata vtables' first slot (the base dtor 0x801380). False disables the spike.
    static bool verify(std::uintptr_t base,Read read,std::string& why) {
        for(const auto& pin:pins) {
            std::array<unsigned char,16> got{};
            if(!read(base+pin.rva,got.data(),got.size()) || got!=pin.code) {std::ostringstream o;o<<"pin mismatch at 0x"<<std::hex<<pin.rva;why=o.str();return false;}
        }
        std::array<unsigned char,113> sphere{};
        if(!read(base+sphere_rva,sphere.data(),sphere.size()) || sphere!=NativeHandVolume::constructor_code) {why="pin mismatch at 0xca6550";return false;}
        for(const auto vt:{userdata_vtable_rva,player_userdata_vtable_rva}) {
            std::uint64_t slot{};
            if(!read(base+vt,&slot,8) || slot!=base+userdata_dtor_rva) {std::ostringstream o;o<<"vtable slot mismatch at 0x"<<std::hex<<vt;why=o.str();return false;}
        }
        return true;
    }

    // Installs the count-only hooks and the session (route R1), or only writes the refusal (R2/R3: no hook, no engine call). Returns false and
    // says why when the spike stays off (a pin mismatch, a hook failure, a latched fault).
    static bool install(std::uintptr_t base,Read read,Logic logic,Camera camera,SpikeRoute route,const std::filesystem::path& output,std::string& why) {
        stop();
        base_=base;read_=read;logic_=logic;camera_=camera;route_=route;output_=output;
        if(HandBodyFault::tripped()) {why="HandBodyFault latched";write_final_note(why);return false;}
        if(const char* refusal=spike_route_refusal(route)) {why=refusal;write_final_note(std::string("route ")+spike_route_name(route)+" returns false: "+refusal);return true;}
        if(!verify(base,read,why)) {write_final_note("disabled: "+why);return false;}
        reset_counters();
        struct Target {std::uintptr_t rva;void* detour;void** original;};
        const std::array<Target,5> targets{{{frame_rva,reinterpret_cast<void*>(&frame_hook),reinterpret_cast<void**>(&frame_original_)},
            {step_rva,reinterpret_cast<void*>(&step_hook),reinterpret_cast<void**>(&step_original_)},
            {drain_rva,reinterpret_cast<void*>(&drain_hook),reinterpret_cast<void**>(&drain_original_)},
            {wake_rva,reinterpret_cast<void*>(&wake_hook),reinterpret_cast<void**>(&wake_original_)},
            {exit_rva,reinterpret_cast<void*>(&exit_hook),reinterpret_cast<void**>(&exit_original_)}}};
        for(const auto& t:targets) {
            void* target=reinterpret_cast<void*>(base+t.rva);
            if(!*t.original && MH_CreateHook(target,t.detour,t.original)!=MH_OK) {why="cannot create a count-only hook";disable_hooks();write_final_note("disabled: "+why);return false;}
            if(MH_EnableHook(target)!=MH_OK) {why="cannot enable a count-only hook";disable_hooks();write_final_note("disabled: "+why);return false;}
            enabled_rvas_.push_back(t.rva);
        }
        {std::lock_guard<std::mutex> lock(session_mutex_);
         SpikeConfig config;config.life_s=body_life_s;session_.emplace(route,g1_min_s,config);
         t0_=GetTickCount64();gameplay_since_ms_=0;last_frame_=0;stop_requested_=false;session_done_=false;player_.reset();floor_z_=std::numeric_limits<double>::quiet_NaN();
         forward_.reset();owner_.reset();player_record_=0;player_handle_=0;source_refreshed_ms_=0;}
        start_writer();
        active_.store(true,std::memory_order_release);
        hand_body_spike_dispatch.store(&dispatch_entry,std::memory_order_release);
        return true;
    }
    // Session end: ask the game thread to destroy the body (at its next allowed tick), wait up to 3 s, then unhook. Never calls the engine itself.
    static bool stop() {
        if(!active_.load(std::memory_order_acquire)) {stop_writer();return true;}
        stop_requested_.store(true,std::memory_order_release);
        const auto deadline=GetTickCount64()+3000;
        while(!session_done_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(10);
        if(body_alive_.load()) {   // never unhook or drop the exclusion while our body may still exist: the hooks are count-only and harmless
            std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.body_alive_at_stop=true;snapshot_.note="stop timed out with the body alive: hooks and exclusion kept";
            stop_writer();HandBodyFault::trip();return false;
        }
        hand_body_spike_dispatch.store(nullptr,std::memory_order_release);
        active_.store(false,std::memory_order_release);
        const bool unhooked=disable_hooks();
        const auto drain=GetTickCount64()+2000;
        while(dispatch_inflight_.load() && GetTickCount64()<drain) Sleep(1);
        cast_excluded_body.store(0xffffffffu,std::memory_order_relaxed);
        {std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.body_alive_at_stop=false;snapshot_.unhooked=unhooked;}
        stop_writer();
        return unhooked;
    }
    static bool active() {return active_.load(std::memory_order_acquire);}

private:
    // ---- count-only hooks
    using Frame=char (__fastcall*)(void*,void*,void*,void*);
    using Pass2=void (__fastcall*)(void*,void*,void*,void*);
    inline static Frame frame_original_{};inline static Pass2 step_original_{},drain_original_{},wake_original_{},exit_original_{};
    inline static std::atomic<bool> active_{};
    inline static std::atomic<std::uint64_t> frames_{},steps_{},step_busy_{},dispatches_{},dispatch_busy_samples_{},dispatch_busy_{},outside_{},step_drain_{},overlap_{},
        off_frame_thread_{},before_frame_{},frame_threads_{},wake_owner_{},wake_total_{},exit_hand_{},exit_total_{};
    inline static std::atomic<int> step_depth_{},drain_depth_{};inline static std::atomic<unsigned> dispatch_inflight_{};
    inline static std::atomic<std::uint32_t> frame_thread_{},step_thread_{},dispatch_thread_{};
    inline static std::array<std::atomic<std::uint32_t>,8> frame_thread_ids_{};
    inline static std::atomic<std::uintptr_t> watch_owner_{};inline static std::atomic<std::uint32_t> watch_handle_{0xffffffffu};
    inline static std::atomic<bool> body_alive_{};
    inline static std::vector<std::uintptr_t> enabled_rvas_;
    static void reset_counters() {
        for(auto* c:{&frames_,&steps_,&step_busy_,&dispatches_,&dispatch_busy_samples_,&dispatch_busy_,&outside_,&step_drain_,&overlap_,&off_frame_thread_,&before_frame_,
                     &frame_threads_,&wake_owner_,&wake_total_,&exit_hand_,&exit_total_}) c->store(0);
        step_depth_=0;drain_depth_=0;frame_thread_=0;step_thread_=0;dispatch_thread_=0;for(auto& t:frame_thread_ids_) t=0;
        watch_owner_=0;watch_handle_=0xffffffffu;body_alive_=false;
    }
    static bool disable_hooks() {
        bool ok=true;
        std::vector<std::uintptr_t> kept;
        for(const auto rva:enabled_rvas_) if(MH_DisableHook(reinterpret_cast<void*>(base_+rva))!=MH_OK) {ok=false;kept.push_back(rva);}
        enabled_rvas_=kept;return ok;
    }
    static bool busy_now() {return physics_busy_flag(read_,base_).value_or(true);}
    static char __fastcall frame_hook(void* game,void* a,void* b,void* c) {
        if(active_.load(std::memory_order_relaxed)) {
            const auto id=static_cast<std::uint32_t>(GetCurrentThreadId());
            frames_.fetch_add(1,std::memory_order_relaxed);frame_thread_.store(id,std::memory_order_relaxed);
            for(auto& slot:frame_thread_ids_) {
                auto seen=slot.load(std::memory_order_relaxed);
                if(seen==id) break;
                if(!seen && slot.compare_exchange_strong(seen,id)) {frame_threads_.fetch_add(1,std::memory_order_relaxed);break;}
                if(seen==id) break;
            }
        }
        return frame_original_(game,a,b,c);
    }
    static void __fastcall step_hook(void* physics,void* a,void* b,void* c) {
        if(active_.load(std::memory_order_relaxed)) {
            steps_.fetch_add(1,std::memory_order_relaxed);step_thread_.store(static_cast<std::uint32_t>(GetCurrentThreadId()),std::memory_order_relaxed);
            if(busy_now()) step_busy_.fetch_add(1,std::memory_order_relaxed);
        }
        step_depth_.fetch_add(1);step_original_(physics,a,b,c);step_depth_.fetch_sub(1);
    }
    static void __fastcall drain_hook(void* p,void* a,void* b,void* c) {drain_depth_.fetch_add(1);drain_original_(p,a,b,c);drain_depth_.fetch_sub(1);}
    static void __fastcall wake_hook(void* p,void* owner,void* a,void* b) {
        if(active_.load(std::memory_order_relaxed)) {
            wake_total_.fetch_add(1,std::memory_order_relaxed);
            const auto watched=watch_owner_.load(std::memory_order_relaxed);
            if(watched && reinterpret_cast<std::uintptr_t>(owner)==watched) wake_owner_.fetch_add(1,std::memory_order_relaxed);
        }
        wake_original_(p,owner,a,b);
    }
    static void __fastcall exit_hook(void* p,void* event,void* info,void* c) {
        if(active_.load(std::memory_order_relaxed)) {
            exit_total_.fetch_add(1,std::memory_order_relaxed);
            const auto watched=watch_handle_.load(std::memory_order_relaxed);
            if(watched!=0xffffffffu)
                if(const auto h=rig::value<std::uint32_t>(read_,reinterpret_cast<std::uintptr_t>(info)+8); h && *h==watched)
                    exit_hand_.fetch_add(1,std::memory_order_relaxed);
        }
        exit_original_(p,event,info,c);
    }
    static G1Stats g1_now(double observed_s) {
        G1Stats s;s.observed_s=observed_s;s.frames=frames_.load();s.steps=steps_.load();s.step_busy=step_busy_.load();s.dispatches=dispatches_.load();
        s.dispatch_busy_samples=dispatch_busy_samples_.load();s.dispatch_busy=dispatch_busy_.load();s.dispatch_outside_step=outside_.load();
        s.dispatch_step_drain=step_drain_.load();s.dispatch_overlap=overlap_.load();s.dispatch_off_frame_thread=off_frame_thread_.load();
        s.dispatch_before_frame=before_frame_.load();s.frame_threads=frame_threads_.load();s.frame_thread=frame_thread_.load();s.dispatch_thread=dispatch_thread_.load();
        return s;
    }

    // ---- the 0x89fb0 entry (NativeGrab::invoke, after the original dispatcher)
    inline static std::uint64_t last_frame_{};
    static void dispatch_entry(void*) {
        if(!active_.load(std::memory_order_acquire)) return;
        struct Inflight {Inflight() {dispatch_inflight_.fetch_add(1);} ~Inflight() {dispatch_inflight_.fetch_sub(1);}} inflight;
        const auto id=static_cast<std::uint32_t>(GetCurrentThreadId());
        const auto n=dispatches_.fetch_add(1,std::memory_order_relaxed)+1;
        dispatch_thread_.store(id,std::memory_order_relaxed);
        const auto frame_thread=frame_thread_.load(std::memory_order_relaxed);
        if(!frame_thread) before_frame_.fetch_add(1,std::memory_order_relaxed);
        else if(id!=frame_thread) off_frame_thread_.fetch_add(1,std::memory_order_relaxed);
        bool outside=false;
        if(step_depth_.load()==0) {outside_.fetch_add(1,std::memory_order_relaxed);outside=true;}
        else if(drain_depth_.load()>0 && step_thread_.load()==id) step_drain_.fetch_add(1,std::memory_order_relaxed);
        else overlap_.fetch_add(1,std::memory_order_relaxed);
        if(n%64==0) {   // 0x8a7820 sampled on every 64th dispatch (five guarded reads), as the B7 probe does
            if(const auto busy=physics_busy_flag(read_,base_)) {dispatch_busy_samples_.fetch_add(1,std::memory_order_relaxed);if(*busy) dispatch_busy_.fetch_add(1,std::memory_order_relaxed);}
        }
        if(!outside || !frame_thread || id!=frame_thread) return;   // the G1 point only: outside the step, on the frame thread
        const auto frame=frames_.load(std::memory_order_relaxed);
        std::unique_lock<std::mutex> lock(session_mutex_,std::try_to_lock);
        if(!lock.owns_lock() || frame==last_frame_) return;                 // once per game frame
        last_frame_=frame;
        try {tick_session();} catch(...) {HandBodyFault::trip();if(session_) session_->request_stop("exception in the spike tick");}
    }

    // ---- the session (game thread, under session_mutex_)
    inline static std::mutex session_mutex_;inline static std::optional<SpikeSession> session_;
    inline static std::uintptr_t base_{};inline static Read read_{};inline static Logic logic_{};inline static Camera camera_{};
    inline static SpikeRoute route_{};inline static std::filesystem::path output_;
    inline static ULONGLONG t0_{},gameplay_since_ms_{},source_refreshed_ms_{};
    inline static std::atomic<bool> stop_requested_{},session_done_{};
    inline static std::optional<Vec3> player_,forward_;inline static double floor_z_{std::numeric_limits<double>::quiet_NaN()};
    inline static std::uintptr_t player_record_{},player_entity_{};inline static std::uint32_t player_handle_{};
    inline static std::optional<BodyProbeRow> owner_;
    inline static std::uintptr_t world_at_create_{};inline static std::optional<G2Counts> g2_at_create_;
    alignas(16) inline static std::array<std::byte,0x100> userdata_{};   // >= 0x80 (Q1 1.4); lives for the process: never freed while a body may hold it
    alignas(16) inline static std::array<std::byte,0x60> sphere_{};      // Q2: 0x60, memSize 0 after construction (every refcount op is a no-op)
    alignas(16) inline static std::array<std::byte,0x100> cinfo_{};      // >= 0x98 (Q3)
    inline static bool used_{};                                          // single shot per process: the storage above is never reused
    inline static std::uint32_t handle_{0xffffffffu};
    inline static std::array<std::uint32_t,3> after_destroy_words_{};inline static bool after_destroy_read_{};

    static std::uintptr_t world_now() {
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read_,*game+0x60410):std::nullopt;
        return world && rig::pointer(*world)?*world:0;
    }
    // Is the pool slot of `handle` our body? present: the slot holds exactly this handle (+0x70, generation included) and our userdata (+0x90);
    // absent: it does not (destroyed, recycled, another world); unreadable: the pool or the slot could not be read this time (retry, not gone).
    enum class Own {present,absent,unreadable};
    static Own own_state(std::uintptr_t world,std::uint32_t handle,std::uintptr_t* record_out=nullptr) {
        if(!world) return Own::unreadable;
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
        if(!pool || !count || !rig::pointer(*pool) || !*count || *count>0x1000000u) return Own::unreadable;
        const std::uint32_t index=handle&0xffffffu;
        if(index==0xffffffu || index>=*count) return Own::absent;
        const auto record=*pool+static_cast<std::uintptr_t>(index)*0xb0;
        const auto stored=rig::value<std::uint32_t>(read_,record+0x70);const auto user=rig::value<std::uintptr_t>(read_,record+0x90);
        if(!stored || !user) return Own::unreadable;
        if(*stored!=handle || *user!=reinterpret_cast<std::uintptr_t>(userdata_.data())) return Own::absent;
        if(record_out) *record_out=record;
        return Own::present;
    }
    static std::optional<std::uintptr_t> own_record(std::uintptr_t world,std::uint32_t handle) {
        std::uintptr_t record{};return own_state(world,handle,&record)==Own::present?std::optional<std::uintptr_t>(record):std::nullopt;
    }
    // After a createBody fault: is there a record holding our userdata? Its handle, or 0xffffffff.
    static std::uint32_t scan_for_own_body(std::uintptr_t world) {
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
        if(!pool || !count || !rig::pointer(*pool) || *count>0x1000000u) return 0xffffffffu;
        for(std::uint32_t i=0;i<*count;++i) {
            const auto record=*pool+static_cast<std::uintptr_t>(i)*0xb0;
            if(rig::value<std::uintptr_t>(read_,record+0x90)==reinterpret_cast<std::uintptr_t>(userdata_.data()))
                if(const auto h=rig::value<std::uint32_t>(read_,record+0x70); h && (*h&0xffffffu)==i) return *h;
        }
        return 0xffffffffu;
    }
    static void body_gone() {body_alive_=false;watch_handle_.store(0xffffffffu,std::memory_order_relaxed);cast_excluded_body.store(0xffffffffu,std::memory_order_relaxed);}
    static void body_tracked(std::uint32_t handle) {handle_=handle;body_alive_=true;watch_handle_.store(handle,std::memory_order_relaxed);cast_excluded_body.store(handle,std::memory_order_relaxed);}
    // Player root (the player's query body: its record translation), floor (player entity +0x300 z), forward (camera aim axis row 0) and owner (a)
    // (B7's chain on the same body). The query source is re-resolved once per second; the root is read every frame from the cached record.
    static void refresh_player(std::uintptr_t world,ULONGLONG now) {
        if(!player_record_ || now-source_refreshed_ms_>=1000) {
            source_refreshed_ms_=now;player_record_=0;
            const auto camera=camera_?camera_():0;
            if(const auto source=camera?native_world_query_source(read_,base_,camera,world):std::nullopt) {
                player_record_=source->body;player_handle_=source->handle;player_entity_=source->player;
                if(const auto view=native_player_camera(read_,base_,camera)) forward_=Vec3{view->aim_axis[0],view->aim_axis[1],view->aim_axis[2]};
                if(!owner_) owner_=probe_body_handle(read_,base_,world,source->handle,0,"owner_a",source->player);
            }
        }
        player_.reset();
        if(player_record_ && rig::value<std::uint32_t>(read_,player_record_+0x70)==player_handle_)
            if(const auto t=rig::value<std::array<float,3>>(read_,player_record_+0x30)) {const Vec3 p{(*t)[0],(*t)[1],(*t)[2]};if(finite(p)) player_=p;}
        if(player_entity_) if(const auto o=rig::value<std::array<float,3>>(read_,player_entity_+0x300); o && std::isfinite((*o)[2])) floor_z_=(*o)[2];
    }
    static OwnerEvidence owner_evidence() {
        OwnerEvidence e;
        e.b_available=false;e.b_reason="b unavailable in B11: it needs a mod spawn (0x8ae9b0) and removal (0x883f60), outside the B11 call list, "
                                       "and 0x8527a0 calls the unstarted component's +0xa8 unconditionally (unproven)";
        if(!owner_) return e;
        const auto& r=*owner_;
        e.a_resolved=hand_body_probe_detail::user_pointer(r.userdata_plus8);
        e.a_userdata_is_player=r.userdata_vtable_rva==player_userdata_vtable_rva;
        e.a_entity_is_player=r.owner_is_player;e.a_type_decoded=r.typeinfo_decoded;e.a_type_in_range=r.type_in_range;
        return e;
    }
    static SpikeEngine& engine() {
        static SpikeEngine e=[] {
            SpikeEngine s;
            s.world=[] {return world_now();};
            s.busy=[] {return busy_now();};
            s.gameplay=[] {return logic_ && logic_()==2;};
            s.player=[]()->std::optional<Vec3> {return player_;};
            s.create=[](SpikeRoute route,Vec3 at,double radius,std::uint32_t& handle) {return create_body(route,at,radius,handle);};
            s.destroy=[](std::uint32_t handle) {return destroy_body(handle);};
            s.position=[](std::uint32_t handle)->std::optional<Vec3> {
                std::uintptr_t record{};const auto own=own_state(world_now(),handle,&record);
                if(own==Own::unreadable) throw SpikeUnreadable{};
                if(own==Own::absent) return {};
                const auto t=rig::value<std::array<float,3>>(read_,record+0x30);if(!t) throw SpikeUnreadable{};
                return Vec3{(*t)[0],(*t)[1],(*t)[2]};
            };
            s.flags=[](std::uint32_t handle)->std::optional<std::uint32_t> {
                const auto world=world_now();
                // Raw words of the slot after the destroy, for the log: handle (+0x70), flags (+0x44), userdata low half (+0x90); index and count checked.
                const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
                if(pool && count && rig::pointer(*pool) && *count<=0x1000000u && (handle&0xffffffu)<*count) {
                    const auto slot=*pool+static_cast<std::uintptr_t>(handle&0xffffffu)*0xb0;
                    after_destroy_words_={rig::value<std::uint32_t>(read_,slot+0x70).value_or(0xdeadbeef),rig::value<std::uint32_t>(read_,slot+0x44).value_or(0xdeadbeef),
                                          rig::value<std::uint32_t>(read_,slot+0x90).value_or(0xdeadbeef)};after_destroy_read_=true;
                }
                std::uintptr_t record{};const auto own=own_state(world,handle,&record);
                if(own==Own::unreadable) throw SpikeUnreadable{};
                if(own==Own::absent) return {};
                const auto flags=rig::value<std::uint32_t>(read_,record+0x44);if(!flags) throw SpikeUnreadable{};
                return *flags;
            };
            s.push=[](std::uint32_t handle,Vec3 v) {return push_body(handle,v);};
            return s;
        }();
        return e;
    }
    static bool create_body(SpikeRoute route,Vec3 at,double radius,std::uint32_t& handle) {
        if(route!=SpikeRoute::R1 || used_ || !owner_ || !finite(at) || !(radius>0 && radius<.2)) return false;
        const auto owner=owner_->userdata_plus8;
        const auto world=world_now();
        if(!world || !hand_body_probe_detail::user_pointer(owner) || busy_now()) return false;
        used_=true;g2_at_create_.reset();
        std::memset(userdata_.data(),0,userdata_.size());std::memset(sphere_.data(),0,sphere_.size());std::memset(cinfo_.data(),0,cinfo_.size());
        using Ctor=void* (__fastcall*)(void*);
        using Create=std::uint32_t* (__fastcall*)(void*,std::uint32_t*,const void*,unsigned char,unsigned char);
        const auto ud=reinterpret_cast<Ctor>(base_+userdata_rva);
        const auto sphere=reinterpret_cast<NativeHandVolume::Construct>(base_+sphere_rva);
        const auto cinfo_ctor=reinterpret_cast<Ctor>(base_+cinfo_rva);
        const auto create=reinterpret_cast<Create>(base_+create_rva);
        alignas(16) const std::array<float,4> centre{};
        bool built=false;
        auto build=[&] {
            ud(userdata_.data());
            sphere(sphere_.data(),centre.data(),static_cast<float>(radius));
            cinfo_ctor(cinfo_.data());
            built=true;
        };
        if(!guarded_call(build) || !built) throw SpikeFault("userdata/sphere/cinfo ctor faulted");
        std::uintptr_t ud_vtable{},sphere_vtable{};std::memcpy(&ud_vtable,userdata_.data(),8);std::memcpy(&sphere_vtable,sphere_.data(),8);
        if(ud_vtable!=base_+userdata_vtable_rva || sphere_vtable!=base_+sphere_vtable_rva) return false;
        std::memcpy(userdata_.data()+8,&owner,8);                              // 0x694c20 (ud, owner): the single store ud+8 = owner
        const std::uint16_t mem_size=0;std::memcpy(sphere_.data()+0x10,&mem_size,2);   // Q2: memSize 0, refcount ops become no-ops
        const auto put=[](std::size_t at_offset,const void* v,std::size_t n) {std::memcpy(cinfo_.data()+at_offset,v,n);};
        const std::uintptr_t shape=reinterpret_cast<std::uintptr_t>(sphere_.data()),user=reinterpret_cast<std::uintptr_t>(userdata_.data());
        const std::uint32_t flags=0,filter=hand_body::hand_filter_word(false);const std::uint16_t material=0;const unsigned char motion_type=2;
        const std::array<float,4> position{static_cast<float>(at.x),static_cast<float>(at.y),static_cast<float>(at.z),0};const float mass=2.f;
        put(0x00,&shape,8);put(0x08,&flags,4);put(0x10,&filter,4);put(0x14,&material,2);put(0x20,&user,8);put(0x28,&motion_type,1);
        put(0x30,position.data(),16);put(0x70,&mass,4);   // +0x40 orientation stays the ctor's identity, +0x80 props 0xffff (dynamic preset, gravity 1)
        std::uint32_t id=0xffffffffu;std::uint32_t* out=nullptr;
        auto call=[&] {out=create(reinterpret_cast<void*>(world),&id,cinfo_.data(),1,1);};
        CrashOperation operation("hand body spike createBody");
        g2_at_create_=G2Counts{wake_owner_.load(),exit_hand_.load()};   // G2 counts from here: an event inside createBody counts
        if(busy_now()) return false;                                     // 0x8a7820 re-read immediately before the call (nothing runs in between)
        if(!guarded_call(call)) {
            const auto found=scan_for_own_body(world);
            if(found!=0xffffffffu) {world_at_create_=world;body_tracked(found);}
            throw SpikeFault("createBody faulted",found);
        }
        if(out) id=*out;
        world_at_create_=world;
        if(own_state(world,id)!=Own::present) {   // created but not where the doc says: find our record; if any, it gets the one destroy attempt
            const auto found=scan_for_own_body(world);
            if(found!=0xffffffffu) body_tracked(found);
            throw SpikeFault("created body did not verify (record +0x70 / +0x90)",found);
        }
        handle=id;body_tracked(id);
        return true;
    }
    static bool destroy_body(std::uint32_t handle) {
        const auto world=world_now();
        if(!world || world!=world_at_create_) return false;
        const auto own=own_state(world,handle);
        if(own==Own::absent) {body_gone();return false;}   // not our body any more (removed, recycled): never destroy someone else's
        if(own==Own::unreadable) return false;
        using Destroy=void (__fastcall*)(void*,const std::uint32_t*,int,int);
        const auto destroy=reinterpret_cast<Destroy>(base_+destroy_rva);
        const std::uint32_t ids[1]{handle};
        auto call=[&] {destroy(reinterpret_cast<void*>(world),ids,1,0);};
        CrashOperation operation("hand body spike destroyBodies");
        if(busy_now()) return false;                       // 0x8a7820 re-read immediately before the call
        if(!guarded_call(call)) throw SpikeFault("destroyBodies faulted");   // state unknown: the body stays tracked and excluded
        if(own_state(world,handle)==Own::absent) body_gone();
        return true;
    }
    static bool push_body(std::uint32_t handle,Vec3 v) {
        const auto world=world_now();
        if(!world || world!=world_at_create_ || !finite(v) || length(v)>2 || !own_record(world,handle)) return false;
        using Velocity=void (__fastcall*)(void*,UINT,const float*,const float*,int);
        const auto velocity=reinterpret_cast<Velocity>(base_+velocity_rva);
        alignas(16) const std::array<float,4> linear{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z),0};
        alignas(16) const std::array<float,4> angular{};
        auto call=[&] {velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0);};
        if(busy_now()) return false;                       // 0x8a7820 re-read immediately before the call
        if(!guarded_call(call)) throw SpikeFault("velocity write faulted");
        return true;
    }
    static void tick_session() {
        if(!session_ || session_done_.load()) return;
        const auto now=GetTickCount64();
        const bool gameplay=logic_ && logic_()==2;
        if(gameplay && !gameplay_since_ms_) gameplay_since_ms_=now;
        const auto world=world_now();
        if(world && gameplay) refresh_player(world,now);
        if(stop_requested_.load(std::memory_order_acquire)) session_->request_stop("session end");
        SessionInput in;in.now_s=(now-t0_)/1000.;
        in.g1=g1_now(gameplay_since_ms_?(now-gameplay_since_ms_)/1000.:0.);
        in.owner=owner_evidence();
        if(in.owner.a_resolved) watch_owner_.store(owner_->userdata_plus8,std::memory_order_relaxed);
        in.g2={wake_owner_.load(),exit_hand_.load()};in.g2_at_create=g2_at_create_;
        in.player_root=player_;in.floor_z=floor_z_;in.forward=forward_;
        const bool running=session_->tick(engine(),in);
        publish(in,running);
        if(!running) {
            // Gone with its world (abandon): nothing to destroy. A body whose state is unknown (a faulted destroy) stays tracked and excluded.
            if(const auto& r=session_->runner(); r && r->report().abandoned && own_state(world_now(),handle_)!=Own::present) body_gone();
            session_done_.store(true,std::memory_order_release);
        }
    }

    // ---- log (its own thread; the game thread only publishes a copy under try_lock)
    using Snapshot=HandBodySpikeSnapshot;
    inline static std::mutex snapshot_mutex_;inline static Snapshot snapshot_;
    static void publish(const SessionInput& in,bool running) {
        std::unique_lock<std::mutex> lock(snapshot_mutex_,std::try_to_lock);if(!lock.owns_lock()) return;
        auto& s=snapshot_;
        s.t=in.now_s;s.phase=static_cast<int>(session_->phase());s.g1_reason=session_->g1().reason;s.outcome=session_->outcome();
        s.owner=spike_owner_name(session_->owner());s.owner_reason=session_->owner_reason();s.g1=in.g1;s.g2_since=session_->g2_since_create();
        s.wake_total=wake_total_.load();s.exit_total=exit_total_.load();s.g2_rejected=session_->g2_rejected();s.owner_row=owner_;s.player=in.player_root;s.floor_z=in.floor_z;
        s.report.reset();s.stage=-1;s.body.reset();
        if(const auto& r=session_->runner()) {
            s.report=r->report();s.stage=static_cast<int>(r->stage());
            if(body_alive_) if(const auto record=own_record(world_now(),handle_)) if(const auto t=rig::value<std::array<float,3>>(read_,*record+0x30)) s.body=Vec3{(*t)[0],(*t)[1],(*t)[2]};
        }
        s.cast_excluded_hits=cast_excluded_hits.load();s.after_destroy=after_destroy_words_;s.after_destroy_read=after_destroy_read_;s.done=!running;
    }
    inline static std::thread writer_;inline static std::mutex writer_mutex_;inline static std::condition_variable writer_wake_;inline static bool writer_quit_{};
    static std::string fmt(std::optional<Vec3> v) {std::ostringstream o;o<<std::setprecision(6);if(v) o<<v->x<<';'<<v->y<<';'<<v->z;return o.str();}
    static std::string csv_header() {
        return "t,phase,g1,frames,steps,step_busy,dispatches,outside,step_drain,overlap,off_frame_thread,before_frame,busy_samples,dispatch_busy,frame_threads,frame_thread,"
               "dispatch_thread,observed_s,owner,wake_owner_since_create,exit_hand_since_create,wake_total,exit_total,stage,created,handle,body,player,floor_z,settle_z,"
               "collided,drift_m,pushes,busy_waits,destroyed,faulted,abandoned,cast_excluded_hits,outcome";
    }
    static std::string csv_row(const Snapshot& s) {
        std::ostringstream o;o<<std::setprecision(6);
        const auto& g=s.g1;
        o<<s.t<<','<<s.phase<<','<<s.g1_reason<<','<<g.frames<<','<<g.steps<<','<<g.step_busy<<','<<g.dispatches<<','<<g.dispatch_outside_step<<','<<g.dispatch_step_drain
         <<','<<g.dispatch_overlap<<','<<g.dispatch_off_frame_thread<<','<<g.dispatch_before_frame<<','<<g.dispatch_busy_samples<<','<<g.dispatch_busy<<','<<g.frame_threads
         <<','<<g.frame_thread<<','<<g.dispatch_thread<<','<<g.observed_s<<','<<s.owner<<','<<s.g2_since.wake_owner<<','<<s.g2_since.exit_hand<<','<<s.wake_total<<','<<s.exit_total
         <<','<<s.stage;
        if(s.report) {const auto& r=*s.report;
            o<<','<<r.created<<",0x"<<std::hex<<r.handle<<std::dec<<','<<fmt(s.body)<<','<<fmt(s.player)<<','<<s.floor_z<<','<<r.settle_z<<','<<r.collided_with_floor<<','<<r.player_drift_m
             <<','<<r.pushes<<','<<r.busy_waits<<','<<r.destroyed<<','<<r.faulted<<','<<r.abandoned;
        } else o<<",0,,,"<<fmt(s.player)<<','<<s.floor_z<<",,,,,,,,";
        std::string outcome=s.outcome;for(auto& ch:outcome) if(ch==',' || ch=='\n') ch=';';
        o<<','<<s.cast_excluded_hits<<','<<outcome;
        return o.str();
    }
    static std::string json(const Snapshot& s) {
        std::ostringstream o;o<<std::setprecision(9);
        const auto str=[](const std::string& v) {std::string out="\"";for(char ch:v) {if(ch=='"' || ch=='\\') out+='\\';out+=(ch=='\n'?' ':ch);}return out+"\"";};
        const auto& g=s.g1;
        o<<"{\"route\":\""<<spike_route_name(route_)<<"\",\"t\":"<<s.t<<",\"phase\":"<<s.phase<<",\"done\":"<<(s.done?"true":"false")<<",\"outcome\":"<<str(s.outcome)
         <<",\"note\":"<<str(s.note)
         <<",\"g1\":{\"verdict\":"<<str(s.g1_reason)<<",\"observed_s\":"<<g.observed_s<<",\"frames\":"<<g.frames<<",\"steps\":"<<g.steps<<",\"step_busy\":"<<g.step_busy
         <<",\"dispatches\":"<<g.dispatches<<",\"outside_step\":"<<g.dispatch_outside_step<<",\"step_drain\":"<<g.dispatch_step_drain<<",\"overlap\":"<<g.dispatch_overlap
         <<",\"off_frame_thread\":"<<g.dispatch_off_frame_thread<<",\"before_frame\":"<<g.dispatch_before_frame<<",\"busy_samples\":"<<g.dispatch_busy_samples
         <<",\"dispatch_busy\":"<<g.dispatch_busy<<",\"frame_threads\":"<<g.frame_threads<<",\"frame_thread\":"<<g.frame_thread<<",\"dispatch_thread\":"<<g.dispatch_thread<<'}'
         <<",\"g2\":{\"owner\":"<<str(s.owner)<<",\"owner_reason\":"<<str(s.owner_reason)<<",\"rejected\":"<<(s.g2_rejected?"true":"false")
         <<",\"wake_owner_since_create\":"<<s.g2_since.wake_owner<<",\"exit_hand_since_create\":"<<s.g2_since.exit_hand<<",\"wake_total\":"<<s.wake_total<<",\"exit_total\":"<<s.exit_total;
        if(s.owner_row) {const auto& r=*s.owner_row;
            o<<",\"owner_row\":{\"handle\":"<<r.handle<<",\"userdata_vtable_rva\":"<<r.userdata_vtable_rva<<",\"owner_vtable_rva\":"<<r.owner_vtable_rva<<",\"owner_is_player\":"
             <<(r.owner_is_player?"true":"false")<<",\"typeinfo_decoded\":"<<(r.typeinfo_decoded?"true":"false")<<",\"owner_typenum\":"<<r.owner_typenum
             <<",\"type_range\":["<<r.type_range_lo<<','<<r.type_range_hi<<"],\"type_in_range\":"<<(r.type_in_range?"true":"false")<<'}';}
        o<<"},\"floor_z\":"<<(std::isfinite(s.floor_z)?s.floor_z:-1e9)<<",\"player\":\""<<fmt(s.player)<<"\",\"body\":\""<<fmt(s.body)<<'"';
        if(s.report) {const auto& r=*s.report;
            o<<",\"spike\":{\"stage\":"<<s.stage<<",\"created\":"<<(r.created?"true":"false")<<",\"handle\":"<<r.handle<<",\"created_at_s\":"<<r.created_at_s
             <<",\"woke\":"<<(r.woke?"true":"false")<<",\"moved\":"<<(r.moved?"true":"false")<<",\"spawn_z\":"<<r.spawn_z
             <<",\"settled\":"<<(r.settled?"true":"false")<<",\"settle_z\":"<<r.settle_z<<",\"collided_with_floor\":"<<(r.collided_with_floor?"true":"false")
             <<",\"player_drift_m\":"<<r.player_drift_m<<",\"pushes\":"<<r.pushes<<",\"busy_waits\":"<<r.busy_waits<<",\"destroy_attempted\":"<<(r.destroy_attempted?"true":"false")
             <<",\"destroyed\":"<<(r.destroyed?"true":"false")<<",\"destroyed_at_s\":"<<r.destroyed_at_s<<",\"flags_after_destroy\":"<<(r.flags_after_destroy?static_cast<long long>(*r.flags_after_destroy):-1)
             <<",\"faulted\":"<<(r.faulted?"true":"false")<<",\"abandoned\":"<<(r.abandoned?"true":"false")<<",\"refused\":"<<(r.refused?"true":"false")
             <<",\"stop_requested\":"<<(r.stop_requested?"true":"false")<<",\"ticks\":"<<r.ticks<<",\"reason\":"<<str(r.reason)<<'}';}
        o<<",\"after_destroy_words\":["<<s.after_destroy[0]<<','<<s.after_destroy[1]<<','<<s.after_destroy[2]<<"],\"after_destroy_read\":"<<(s.after_destroy_read?"true":"false")
         <<",\"cast_excluded_hits\":"<<s.cast_excluded_hits<<",\"hand_body_fault\":"<<(HandBodyFault::tripped()?"true":"false")
         <<",\"body_alive_at_stop\":"<<(s.body_alive_at_stop?"true":"false")<<",\"unhooked\":"<<(s.unhooked?"true":"false")<<"}\n";
        return o.str();
    }
    static void write_final_note(const std::string& note) {
        std::error_code ec;if(output_.empty() || !std::filesystem::exists(output_,ec)) return;
        Snapshot s;s.note=note;s.outcome=note;s.done=true;
        std::ofstream(output_/L"hand-body-spike.json",std::ios::trunc)<<json(s);
    }
    static void start_writer() {
        stop_writer();
        {std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_=Snapshot{};}
        {std::lock_guard<std::mutex> lock(writer_mutex_);writer_quit_=false;}
        writer_=std::thread([] {
            std::ofstream csv(output_/L"hand-body-spike.csv",std::ios::trunc);
            csv<<csv_header()<<'\n';csv.flush();
            for(unsigned i=0;i<400;++i) {   // one row per second, at most 400
                bool quit=false;
                {std::unique_lock<std::mutex> lock(writer_mutex_);quit=writer_wake_.wait_for(lock,std::chrono::seconds(1),[] {return writer_quit_;});}
                Snapshot s;{std::lock_guard<std::mutex> lock(snapshot_mutex_);s=snapshot_;}
                try {csv<<csv_row(s)<<'\n';csv.flush();std::ofstream(output_/L"hand-body-spike.json",std::ios::trunc)<<json(s);} catch(...) {}
                if(quit) return;
            }
        });
    }
    static void stop_writer() {
        {std::lock_guard<std::mutex> lock(writer_mutex_);writer_quit_=true;}
        writer_wake_.notify_all();
        if(writer_.joinable()) writer_.join();
    }
};

} // namespace d2vr
