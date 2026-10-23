#pragma once
// Plan B task B11: the real engine behind the hand-body spike seam (src/hand_body_spike.hpp). Dev knob build/hand-body-spike only; with the knob
// absent nothing here is installed (no hook, no counter, no thread). docs/native-hand-body-re.md is the authority for every RVA and layout.
// B13: the pins, the count-only hooks, the pool reads and the R1 recipe moved to src/hand_body_native.hpp (shared with the production hand
// body, not duplicated); this file keeps the spike's session, its single-shot storage and its log.
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
#include "hand_body_spike.hpp"
#include "hand_body_native.hpp"
#include "hand_body_probe.hpp"
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
    static constexpr double g1_min_s=8,body_life_s=60;
    static bool verify(std::uintptr_t base,Read read,std::string& why) {return HandBodyNative::verify(base,read,why);}

    // Installs the count-only hooks and the session (route R1), or only writes the refusal (R2/R3: no hook, no engine call). Returns false and
    // says why when the spike stays off (a pin mismatch, a hook failure, a latched fault).
    static bool install(std::uintptr_t base,Read read,Logic logic,Camera camera,SpikeRoute route,const std::filesystem::path& output,std::string& why) {
        stop();
        base_=base;read_=read;logic_=logic;camera_=camera;route_=route;output_=output;
        if(HandBodyFault::tripped()) {why="HandBodyFault latched";write_final_note(why);return false;}
        if(const char* refusal=spike_route_refusal(route)) {why=refusal;write_final_note(std::string("route ")+spike_route_name(route)+" returns false: "+refusal);return true;}
        if(!verify(base,read,why)) {write_final_note("disabled: "+why);return false;}
        body_alive_=false;
        if(!HandBodyNative::install_hooks(base,read,why)) {write_final_note("disabled: "+why);return false;}
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
        const bool unhooked=HandBodyNative::disable_hooks();
        const auto drain=GetTickCount64()+2000;
        while(dispatch_inflight_.load() && GetTickCount64()<drain) Sleep(1);
        cast_excluded_bodies[0].store(0xffffffffu,std::memory_order_relaxed);
        {std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.body_alive_at_stop=false;snapshot_.unhooked=unhooked;}
        stop_writer();
        return unhooked;
    }
    static bool active() {return active_.load(std::memory_order_acquire);}

private:
    inline static std::atomic<bool> active_{};
    inline static std::atomic<unsigned> dispatch_inflight_{};
    inline static std::atomic<bool> body_alive_{};
    static bool busy_now() {return HandBodyNative::busy_now();}
    static G1Stats g1_now(double observed_s) {return HandBodyNative::g1_now(observed_s);}

    // ---- the 0x89fb0 entry (NativeGrab::invoke, after the original dispatcher)
    inline static std::uint64_t last_frame_{};
    static void dispatch_entry(void*) {
        if(!active_.load(std::memory_order_acquire)) return;
        struct Inflight {Inflight() {dispatch_inflight_.fetch_add(1);} ~Inflight() {dispatch_inflight_.fetch_sub(1);}} inflight;
        const auto point=HandBodyNative::note_dispatch();   // counts the dispatch for G1 (shared hooks)
        if(!point.g1_point) return;   // the G1 point only: outside the step, on the frame thread
        const auto frame=point.frame;
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
    inline static R1BodyStorage storage_{};   // userdata, sphere, cinfo; lives for the process: never freed while a body may hold it
    inline static bool used_{};               // single shot per process: the storage above is never reused
    inline static std::uint32_t handle_{0xffffffffu};
    inline static std::array<std::uint32_t,3> after_destroy_words_{};inline static bool after_destroy_read_{};

    static std::uintptr_t world_now() {return HandBodyNative::world_now();}
    using Own=HandBodyNative::Own;
    static Own own_state(std::uintptr_t world,std::uint32_t handle,std::uintptr_t* record_out=nullptr) {return HandBodyNative::own_state(world,handle,storage_,record_out);}
    static std::optional<std::uintptr_t> own_record(std::uintptr_t world,std::uint32_t handle) {
        std::uintptr_t record{};return own_state(world,handle,&record)==Own::present?std::optional<std::uintptr_t>(record):std::nullopt;
    }
    static void body_gone() {body_alive_=false;HandBodyNative::watch_handle(0,HandBodyNative::no_handle);cast_excluded_bodies[0].store(0xffffffffu,std::memory_order_relaxed);}
    static void body_tracked(std::uint32_t handle) {handle_=handle;body_alive_=true;HandBodyNative::watch_handle(0,handle);cast_excluded_bodies[0].store(handle,std::memory_order_relaxed);}
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
        const auto a=HandBodyNative::owner_a_evidence(owner_);
        e.a_resolved=a.a_resolved;e.a_userdata_is_player=a.a_userdata_is_player;e.a_entity_is_player=a.a_entity_is_player;
        e.a_type_decoded=a.a_type_decoded;e.a_type_in_range=a.a_type_in_range;
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
            s.position=[](std::uint32_t handle)->std::optional<Vec3> {return HandBodyNative::position(world_now(),handle,storage_);};
            s.flags=[](std::uint32_t handle)->std::optional<std::uint32_t> {
                const auto world=world_now();
                // Raw words of the slot after the destroy, for the log: handle (+0x70), flags (+0x44), userdata low half (+0x90); index and count checked.
                if(const auto words=HandBodyNative::slot_words(world,handle)) {after_destroy_words_=*words;after_destroy_read_=true;}
                return HandBodyNative::flags(world,handle,storage_);
            };
            s.push=[](std::uint32_t handle,Vec3 v) {return push_body(handle,v);};
            return s;
        }();
        return e;
    }
    // The shared R1 recipe (hand_body_native.hpp), single shot per process. A fault that left a record holding our userdata tracks it so it
    // gets its one destroy attempt.
    static bool create_body(SpikeRoute route,Vec3 at,double radius,std::uint32_t& handle) {
        if(route!=SpikeRoute::R1 || used_ || !owner_ || !finite(at) || !(radius>0 && radius<.2)) return false;
        const auto owner=owner_->userdata_plus8;
        const auto world=world_now();
        if(!world || !hand_body_probe_detail::user_pointer(owner) || busy_now()) return false;
        used_=true;g2_at_create_.reset();
        G2Counts g2{};
        try {
            if(!HandBodyNative::create(storage_,world,owner,at,radius,handle,&g2)) {g2_at_create_=g2;return false;}
        } catch(const HandBodyEngineFault& f) {
            g2_at_create_=g2;
            if(f.handle!=HandBodyNative::no_handle) {world_at_create_=world;body_tracked(f.handle);}
            throw;
        }
        g2_at_create_=g2;world_at_create_=world;body_tracked(handle);
        return true;
    }
    static bool destroy_body(std::uint32_t handle) {
        const auto r=HandBodyNative::destroy(storage_,world_now(),world_at_create_,handle);   // throws: the body stays tracked and excluded
        if(r==DestroyOutcome::absent) {body_gone();return false;}   // not our body any more (removed, recycled): never destroy someone else's
        if(r==DestroyOutcome::refused) return false;
        if(r==DestroyOutcome::gone) body_gone();
        return true;
    }
    static bool push_body(std::uint32_t handle,Vec3 v) {return HandBodyNative::velocity(storage_,world_now(),world_at_create_,handle,v,Vec3{});}
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
        if(in.owner.a_resolved) HandBodyNative::watch_owner(owner_->userdata_plus8);
        in.g2=HandBodyNative::g2_now();in.g2_at_create=g2_at_create_;
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
        s.wake_total=HandBodyNative::wake_total();s.exit_total=HandBodyNative::exit_total();s.g2_rejected=session_->g2_rejected();s.owner_row=owner_;s.player=in.player_root;s.floor_z=in.floor_z;
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
