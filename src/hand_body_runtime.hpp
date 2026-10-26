#pragma once
// Plan B task B13: the production hand body's real engine (knob build/hand-body, default off until B17; build/no-hand-body wins). It binds
// the pure manager (src/hand_body.hpp) to the shared R1 recipe and count-only hooks (src/hand_body_native.hpp). It never calls into the
// B11 spike, and it refuses to run while the spike knob exists (dxgi_probe.cpp starts one or the other).
//
// Threads:
// * Dispatcher (the 0x89fb0 hook, NativeGrab::invoke after the original dispatcher): only at the G1 point (outside the step, on the
//   0x8ad260 thread), once per game frame. It runs HandBody::service and nothing else touches the engine. It never waits on a lock the
//   render thread holds: the render publication and the event log are taken with try_lock (the last copy is used when it fails), and the
//   game logic is the atomic the render thread publishes with the release gate (final review P3-2: game_logic_mask() locks; `logic` given to
//   install() is never called on the dispatcher).
// * Render (Present / the head view): publish_hands() once per frame (tracked bits, calibrated palms, authored camera, load generation),
//   release_gate() once per frame where the controller input is composed (menu / pause event / settings panel / logic), pose() and
//   status_json(). All of them are cheap and none calls the engine.
// * Control (StartProbe / StopProbe worker): install() and stop(); stop() waits up to 3 s for the dispatcher to destroy the bodies.
// With the knob off nothing here is installed: no hook, no thread, no engine call; the dispatcher pays one relaxed load (hand_body_dispatch).
#include "native_guarded_call.hpp"
#include <windows.h>
#include "hand_body.hpp"
#include "hand_body_native.hpp"
#include "hand_body_probe.hpp"
#include "native_world_query_source.hpp"
#include "native_world_trace.hpp"
#include "crash_observer.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace d2vr {

// The render thread's per-frame facts for the hand bodies (outside the class: clang-cl needs its member initializers complete).
// B14: target = the raw calibrated palm pose per side (C9: intent; the servo's target), free = the hand clamp's palm cast found it free this frame
// (creates and snaps need it), yield = the clamp yields (an authored arm, a native carry, a physical hold), locomotion = the move stick is
// published non-zero, player_root = the player entity's +0x300 (B11: equal to the query body's record translation).
// B16: filter = the word each side's body should carry (HandFilterKeeper, render thread: the R1 default unless a coupled hold takes a layer out).
struct HandBodyHandsInput {unsigned tracked{};std::array<Vec3,2> palm{};bool authored{};bool focused{};
    std::array<WorldTrackedPose,2> target{};unsigned free{};unsigned yield{};bool locomotion{};std::optional<Vec3> player_root{};
    unsigned root_gate_blocked{}; // confirmed surface contact or a held object not known free
    bool root_feedforward_gate{};
    Vec3 player_velocity{};   // Fix B: the player's horizontal velocity, the drive's feedforward (zero with build/no-hand-body-feedforward)
    double target_time_s{std::numeric_limits<double>::quiet_NaN()};   // v613 item 6: QPC seconds when the render thread published the targets (set by publish_hands)
    std::array<std::uint32_t,2> filter{{R1Recipe::filter,R1Recipe::filter}};
    unsigned holding{};     // B16 fix round 2: sides that hold something (B14's snaps are suppressed for them) ...
    unsigned hold_snap_ok{};};   // final review P2-1: ... except a native hold whose object is free (a snag on the hand alone: the stuck snap runs)
// One body's mod memory and identity (free: reusable; in_use: our body may exist; retired: never reused, the engine may still read it).
struct HandBodySlot {enum State {free,in_use,retired} state{free};R1BodyStorage storage{};std::uintptr_t world{};std::uint32_t handle{0xffffffffu};};
struct HandBodyEvent {std::uint64_t t_ms{};unsigned side{};const char* kind{""};std::uint32_t handle{};std::uintptr_t world{};std::uint32_t generation{};unsigned logic{};bool release{};};

class HandBodyRuntime {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Camera=std::uintptr_t (*)();
    static constexpr double g1_min_s=8;                     // gameplay seconds of count-only observation before the first create (in-run G1)
    static constexpr std::uint64_t fresh_ms=250;            // the render publication older than this means "not gameplay" (no create, destroy)
    static constexpr std::uint64_t free_fresh_ms=50;        // B14: the clamp's free bit counts for a create / snap only while this fresh
    static constexpr unsigned storage_per_side=8;           // storage abandoned with its world is never reused; when all are retired the side stays off

    using HandsInput=HandBodyHandsInput;

    // Verifies the pins, installs the count-only hooks and arms the dispatcher entry. false (with why) leaves everything off.
    // The reason a requested hand body is off (review P3-4: kept for the status). Control thread.
    static void note_install_failure(const std::string& why) {std::lock_guard<std::mutex> lock(install_mutex_);install_why_=why;}
    static bool install(std::uintptr_t base,Read read,Logic logic,Camera camera,const std::filesystem::path& output,std::string& why) {
        note_install_failure("");
        if(!stop()) {why="a previous hand body is still alive";note_install_failure(why);return false;}
        base_=base;read_=read;logic_=logic;camera_=camera;output_=output;
        if(HandBodyFault::tripped()) {why="HandBodyFault latched";note_install_failure(why);return false;}
        if(!HandBodyNative::verify(base,read,why)) {note_install_failure(why);return false;}
        if(!HandBodyNative::install_hooks(base,read,why)) {note_install_failure(why);return false;}
        g1_violation_.store(nullptr);
        for(auto& side:storage_) for(auto& slot:side) if(slot.state==Slot::in_use) slot.state=Slot::retired;   // none after a clean stop; retired stays retired
        for(auto& s:current_) s=no_slot;
        {std::lock_guard<std::mutex> lock(hands_mutex_);hands_={};hands_tick_=0;}
        last_hands_={};last_hands_tick_=0;release_.store(true);release_tick_.store(0);logic_published_.store(0);
        g1_={};g1_pass_={};g1_decided_=false;g1_passed_=false;g1_state_.store(0);g1_reason_.store("observing");gameplay_since_ms_=0;last_frame_=0;
        g2_prev_={};g2_watching_=false;g2_events_.store(0);
        {std::lock_guard<std::mutex> lock(gate_mutex_);gate_.reset();}
        for(auto& st:stranded_) st.store(false);authored_.store(false);authored_tick_.store(0);
        alive_outside_gameplay_.store(0);storage_exhausted_.store(0);retired_.store(0);
        {std::lock_guard<std::mutex> lock(events_mutex_);events_.clear();dropped_events_=0;}
        cycles_per_us_=calibrate_cycles_per_us();
        auto* body=new HandBody(engine(),true);   // never freed: a render-thread reader may still hold the previous one (tiny, per install)
        body_.store(body,std::memory_order_release);
        for(auto& s:last_status_) s={};
        for(auto& s:last_drive_) s={};
        tick_changed_=false;tick_cost_us_.store(0);tick_cost_max_us_.store(0);steady_tick_cost_max_us_.store(0);
        start_writer();
        stop_requested_.store(false,std::memory_order_release);
        active_.store(true,std::memory_order_release);
        hand_body_dispatch.store(&dispatch_entry,std::memory_order_release);
        return true;
    }
    // Session end: the next dispatcher tick destroys every body; wait up to 3 s. The dispatcher entry is then cut and the in-flight tick
    // drained BEFORE the final check, so a body created by a tick that began before the stop is seen (and the entry restored to destroy
    // it). A body still alive at the deadline (the game paused: no dispatcher) is stranded: the entry is cut, HandBodyFault latches (no
    // re-install), and its cast exclusion is kept; it then goes with its world (the abandon case). false in that case.
    static bool stop() {
        if(!active_.load(std::memory_order_acquire)) {stop_writer();return true;}
        stop_requested_.store(true,std::memory_order_release);
        if(auto* body=body_.load(std::memory_order_acquire)) body->shutdown();
        const auto deadline=GetTickCount64()+3000;
        bool alive=true;
        for(;;) {
            while(body_alive() && GetTickCount64()<deadline) Sleep(10);
            hand_body_dispatch.store(nullptr,std::memory_order_release);
            const auto drain=GetTickCount64()+2000;
            while(dispatch_inflight_.load() && GetTickCount64()<drain) Sleep(1);
            alive=body_alive() || dispatch_inflight_.load();
            if(!alive || GetTickCount64()>=deadline) break;
            hand_body_dispatch.store(&dispatch_entry,std::memory_order_release);   // a late create: let the dispatcher destroy it
        }
        active_.store(false,std::memory_order_release);
        const bool unhooked=HandBodyNative::disable_hooks();
        for(unsigned side=0;side<2;++side) {
            const bool keep=alive || stranded_[side].load();
            if(!keep) cast_excluded_bodies[side].store(0xffffffffu,std::memory_order_relaxed);
        }
        if(alive) {note_event(0,"stop_timeout_alive",0);HandBodyFault::trip();stop_writer();return false;}
        note_event(0,unhooked?"stopped":"stopped_hooks_kept",0);
        stop_writer();
        return unhooked;
    }
    static bool active() {return active_.load(std::memory_order_acquire);}
    // A body exists or is being destroyed (any thread).
    static bool any_alive() {return active() && body_alive();}

    // ---- render thread
    static void publish_hands(const HandsInput& in,std::uint64_t now_ms) {
        if(!active()) return;
        authored_.store(in.authored,std::memory_order_relaxed);authored_tick_.store(now_ms,std::memory_order_release);
        std::lock_guard<std::mutex> lock(hands_mutex_);hands_=in;hands_.target_time_s=static_cast<double>(qpc_us())*1e-6;hands_tick_=now_ms;
    }
    // Once per frame where the controller input is composed: returns the menu button / pause event to publish (Start waits for the destroy)
    // and publishes `release` for the dispatcher. Knob off: the caller does not call this at all.
    static ReleaseOutput release_gate(const ReleaseSignals& signals,std::uint64_t now_ms) {
        ReleaseOutput out;out.menu=signals.menu;out.pause_request=signals.pause_request;
        if(!active()) return out;
        const bool alive=any_alive();
        ReleaseSignals sig=signals;   // the authored camera (a death clip, a takedown) comes from the head-view publication
        const auto authored_at=authored_tick_.load(std::memory_order_acquire);
        sig.authored=sig.authored || (authored_.load(std::memory_order_relaxed) && authored_at && now_ms>=authored_at && now_ms-authored_at<=fresh_ms);
        {std::lock_guard<std::mutex> lock(gate_mutex_);out=gate_.update(sig,alive,now_ms);gate_holds_=gate_.holds();gate_timeouts_=gate_.timeouts();gate_intents_=gate_.intents();}
        if(signals.logic!=2 && alive) alive_outside_gameplay_.fetch_add(1,std::memory_order_relaxed);   // the game left gameplay with a body alive
        // Load generation (review P2-1): every load is a new world even when the world object comes back at the same address. It is bumped
        // when a loading state (logic 0 or 1<<15: the loading screen, a death reload) ENDS, i.e. after the dispatcher had its chance to destroy
        // the bodies in the old world (loading itself is a same-world loss of gameplay: `release`). A body still alive at the bump (the
        // dispatcher did not tick during the load) is abandoned: no engine call, since the old handle may name another body in a world
        // rebuilt at the same address.
        const bool loading=signals.logic==0 || signals.logic==(1u<<15);
        if(loading) loading_.store(true);
        else if(loading_.exchange(false)) generation_.fetch_add(1,std::memory_order_acq_rel);   // on loading EXIT
        logic_published_.store(signals.logic,std::memory_order_relaxed);   // P3-2: the dispatcher's copy (published by the release_tick_ store below)
        release_.store(out.release,std::memory_order_release);release_tick_.store(now_ms,std::memory_order_release);
        return out;
    }
    static std::optional<WorldTrackedPose> pose(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);return active() && body?body->pose(side):std::nullopt;}
    // B15: the pose with its publication serial (the visible hand judges a pose that stopped updating) and the drive's contact signal
    static std::pair<std::optional<WorldTrackedPose>,std::uint64_t> pose_stamped(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);
        return active() && body?body->pose_stamped(side):std::pair<std::optional<WorldTrackedPose>,std::uint64_t>{std::nullopt,0};}
    static DriveStatus drive_status(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);return active() && body?body->drive_status(side):DriveStatus{};}
    static bool contact_now(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);return active() && body && body->drive_status(side).contact_now;}
    static bool ready(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);return active() && body && body->ready(side);}
    static HandBodyStatus status(unsigned side) {const auto* body=body_.load(std::memory_order_acquire);return body?body->status(side):HandBodyStatus{};}
    // The status object for xr-live-status.json (`"hand_body":`). Knob off: {"enabled":false}.
    static std::string status_json(bool enabled) {
        std::ostringstream o;o<<std::setprecision(6);
        const auto* body=body_.load(std::memory_order_acquire);
        o<<"{\"enabled\":"<<(enabled?"true":"false");
        const auto str=[](const std::string& v) {std::string out="\"";for(char ch:v) {if(ch=='"' || ch=='\\') out+='\\';out+=(ch=='\n' || static_cast<unsigned char>(ch)<32?' ':ch);}return out+"\"";};
        std::string install_why;{std::lock_guard<std::mutex> lock(install_mutex_);install_why=install_why_;}
        if(!install_why.empty()) o<<",\"install_why\":"<<str(install_why);
        if(!enabled || !body) {o<<",\"installed\":"<<(active()?"true":"false")<<'}';return o.str();}
        const int g1state=g1_state_.load();const char* g1reason=g1_reason_.load();
        const std::string g1why=g1state==0?std::string("observing"):g1state==1?std::string("pass"):std::string("failed: ")+(g1reason?g1reason:"");
        o<<",\"installed\":"<<(active()?"true":"false")<<",\"available\":"<<(body->status(0).available?"true":"false")
         <<",\"hand_body_fault\":"<<(HandBodyFault::tripped()?"true":"false")<<",\"why\":"<<str(body->why())<<",\"g1\":"<<str(g1why)
         <<",\"g1_violation\":"<<str(g1_violation_.load()?g1_violation_.load():"")<<",\"ticks\":"<<body->ticks()<<",\"unreadable_ticks\":"<<body->unreadable_ticks()<<",\"reentries\":"<<body->reentries()<<",\"pose_skips\":"<<body->pose_skips()
         <<",\"sides\":[";
        for(unsigned side=0;side<HandBody::sides;++side) {
            const auto st=body->status(side);const auto p=body->pose(side);
            o<<(side?",":"")<<"{\"state\":"<<static_cast<int>(st.state)<<",\"handle\":"<<st.handle<<",\"creates\":"<<st.creates<<",\"destroys\":"<<st.destroys
             <<",\"direct_destroys\":"<<st.direct_destroys<<",\"queued_destroys\":"<<st.queued_destroys<<",\"releases\":"<<st.releases<<",\"abandons\":"<<st.abandons
             <<",\"lost\":"<<st.lost<<",\"faults\":"<<st.faults<<",\"refused_writes\":"<<st.refused_writes<<",\"writes\":"<<st.writes<<",\"wake_writes\":"<<st.wake_writes
             <<",\"create_refusals\":"<<st.create_refusals<<",\"busy_waits\":"<<st.busy_waits
             <<",\"misplaced_writes\":"<<st.misplaced_writes<<",\"reentrant_writes\":"<<st.reentrant_writes
             <<",\"filter\":"<<st.filter<<",\"filter_writes\":"<<st.filter_writes<<",\"filter_refused\":"<<st.filter_refused<<",\"filter_stranded\":"<<st.filter_stranded;
            const auto d=body->drive_status(side);
            o<<",\"drive\":{\"gap_m\":"<<d.gap_m<<",\"speed\":"<<d.speed<<",\"feedforward_mps\":"<<d.feedforward_mps<<",\"root_gate\":"<<(d.root_gate?"true":"false")<<",\"capped\":"<<(d.capped?"true":"false")<<",\"contact_frames\":"<<d.contact_frames<<",\"contact_now\":"<<(d.contact_now?"true":"false")
             <<",\"snaps\":"<<d.snaps<<",\"snap_requests\":"<<d.snap_requests<<",\"snap_blocked\":"<<d.snap_blocked<<",\"cost_us\":"<<d.cost_us
             <<",\"cost_max_us\":"<<d.cost_max_us<<",\"player_drift_um\":"<<d.player_drift_um<<",\"dt_s\":"<<d.dt_s<<",\"drives\":"<<d.drives
             <<",\"drive_writes\":"<<d.drive_writes<<",\"yields\":"<<d.yields<<",\"skipped\":"<<d.skipped<<",\"garbage\":"<<d.garbage
             <<",\"second_drives\":"<<d.second_drives<<",\"root_jumps\":"<<d.root_jumps<<",\"pressed_ticks\":"<<d.pressed_ticks
             <<",\"cpu_us\":"<<d.cpu_us<<",\"cpu_max_us\":"<<d.cpu_max_us<<",\"root_moved_ticks\":"<<d.root_moved_ticks<<",\"root_travel_um\":"<<d.root_travel_um<<",\"held_ticks\":"<<d.held_ticks<<'}';
            o<<",\"pose\":";
            if(p) o<<'['<<p->position.x<<','<<p->position.y<<','<<p->position.z<<']';else o<<"null";
            o<<'}';
        }
        const auto d0=body->drive_status(0),d1=body->drive_status(1);
        o<<"],\"faults\":"<<(body->status(0).faults+body->status(1).faults)
         <<",\"cost_us\":"<<body->drive_cost_us()<<",\"cost_max_us\":"<<body->drive_cost_max_us()   // drive, both hands summed per tick (proof d)
         <<",\"tick_cost_us\":"<<tick_cost_us_.load()<<",\"tick_cost_max_us\":"<<tick_cost_max_us_.load()<<",\"steady_tick_cost_max_us\":"<<steady_tick_cost_max_us_.load()
         <<",\"player_drift_um\":"<<std::max(d0.player_drift_um,d1.player_drift_um)<<",\"pressed_ticks\":"<<(d0.pressed_ticks+d1.pressed_ticks)<<",\"contact_frames\":"<<(d0.contact_frames+d1.contact_frames)
         <<",\"snaps\":"<<(d0.snaps+d1.snaps)<<",\"gap_m\":["<<d0.gap_m<<','<<d1.gap_m<<']'
         <<",\"root_moved_ticks\":"<<(d0.root_moved_ticks+d1.root_moved_ticks)<<",\"root_travel_um\":"<<std::max(d0.root_travel_um,d1.root_travel_um)
         <<",\"timing\":"<<[&] {
             const auto t=body->drive_timing();std::ostringstream v;
             v<<"{\"cpu_available\":"<<(t.cpu_available?"true":"false")<<",\"cycles_per_us\":"<<t.cycles_per_us<<",\"ticks\":"<<t.ticks
              <<",\"over_wall\":"<<t.over_wall<<",\"over_cpu\":"<<t.over_cpu<<",\"wall_max_us\":"<<t.wall_max_us<<",\"cpu_max_us\":"<<t.cpu_max_us<<",\"hist_edges_us\":[";
             for(std::size_t i=0;i<kDriveHistEdgesUs.size();++i) v<<(i?",":"")<<kDriveHistEdgesUs[i];
             v<<"],\"hist\":[";for(std::size_t i=0;i<t.hist.size();++i) v<<(i?",":"")<<t.hist[i];
             v<<"],\"slow\":{\"wall_us\":"<<t.slow_wall_us<<",\"cpu_us\":"<<t.slow_cpu_us<<",\"read_us\":"<<t.slow_read_us<<",\"servo_us\":"<<t.slow_servo_us
              <<",\"write_us\":"<<t.slow_write_us<<",\"other_us\":"<<t.slow_other_us<<"}}";
             return v.str();}()
         <<",\"time_factor\":1,\"time_factor_verified\":false,\"gravity_comp\":"<<HandDriveConfig{}.gravity
         <<",\"abandons\":"<<(body->status(0).abandons+body->status(1).abandons)
         <<",\"release\":{\"holds\":"<<gate_holds_.load()<<",\"timeouts\":"<<gate_timeouts_.load()<<",\"intents\":"<<gate_intents_.load()
         <<",\"alive_outside_gameplay\":"<<alive_outside_gameplay_.load()<<'}'
         <<",\"stranded\":["<<(stranded_[0].load()?1:0)<<','<<(stranded_[1].load()?1:0)<<']'
         <<",\"unverified\":"<<(body->status(0).unverified+body->status(1).unverified)
         <<",\"g2_events\":"<<g2_events_.load()<<",\"storage_retired\":"<<retired_.load()<<",\"storage_exhausted\":"<<storage_exhausted_.load()
         <<",\"cast_excluded_hits\":"<<cast_excluded_hits.load()<<",\"cast_continuations\":"<<cast_continuations.load()
         <<",\"cast_continuation_exhausted\":"<<cast_continuation_exhausted.load()<<",\"dropped_events\":"<<dropped_events_.load()
         <<",\"filter_readback_mismatches\":"<<(body->status(0).filter_readback_mismatches+body->status(1).filter_readback_mismatches)<<'}';
        return o.str();
    }

private:
    using Slot=HandBodySlot;
    static constexpr unsigned no_slot=0xffffffffu;
    inline static std::array<std::array<Slot,storage_per_side>,2> storage_{};
    inline static std::array<unsigned,2> current_{{no_slot,no_slot}};   // dispatcher thread only
    inline static std::array<unsigned,2> last_pick_{{storage_per_side-1,storage_per_side-1}};   // P3-4: the storage last created into (dispatcher thread)
    inline static std::atomic<HandBody*> body_{nullptr};
    inline static std::atomic<bool> active_{},stop_requested_{};
    inline static std::atomic<unsigned> dispatch_inflight_{};
    inline static std::uintptr_t base_{};inline static Read read_{};inline static Logic logic_{};inline static Camera camera_{};
    inline static std::filesystem::path output_;
    // render -> dispatcher
    inline static std::mutex hands_mutex_;inline static HandsInput hands_{};inline static std::uint64_t hands_tick_{};
    inline static HandsInput last_hands_{};inline static std::uint64_t last_hands_tick_{};   // dispatcher's copy
    inline static std::atomic<bool> release_{true};inline static std::atomic<std::uint64_t> release_tick_{};
    inline static std::atomic<unsigned> logic_published_{0};   // P3-2: game_logic_mask() as release_gate saw it (render thread writes, dispatcher reads)
    inline static thread_local bool in_g1_tick_{};
    inline static std::atomic<const char*> g1_violation_{nullptr};
    inline static std::mutex install_mutex_;inline static std::string install_why_;   // control / status threads only (never the dispatcher)
    inline static std::atomic<bool> loading_{};inline static std::atomic<std::uint32_t> generation_{};
    inline static std::mutex gate_mutex_;inline static HandBodyReleaseGate gate_{};
    inline static std::atomic<std::uint64_t> gate_holds_{},gate_timeouts_{},gate_intents_{},alive_outside_gameplay_{};
    // dispatcher state
    inline static std::mutex tick_mutex_;inline static std::uint64_t last_frame_{};
    inline static G1Stats g1_{},g1_pass_{};inline static bool g1_decided_{},g1_passed_{};inline static std::uint64_t gameplay_since_ms_{};
    inline static std::atomic<int> g1_state_{};inline static std::atomic<const char*> g1_reason_{"observing"};   // 0 observing, 1 pass, 2 failed (judge_g1's literal)
    inline static std::array<std::atomic<bool>,2> stranded_{};   // a body that may still exist and that we can no longer destroy (kept excluded)
    inline static std::atomic<bool> authored_{};inline static std::atomic<std::uint64_t> authored_tick_{};
    static bool body_alive() {const auto* body=body_.load(std::memory_order_acquire);return body && body->any_alive();}
    inline static G2Counts g2_prev_{};inline static bool g2_watching_{};inline static std::atomic<std::uint64_t> g2_events_{};
    inline static std::atomic<std::uint64_t> storage_exhausted_{},retired_{};
    inline static std::array<HandBodyStatus,2> last_status_{};inline static std::array<DriveStatus,2> last_drive_{};
    inline static bool tick_changed_{};
    inline static std::atomic<std::uint64_t> tick_cost_us_{},tick_cost_max_us_{},steady_tick_cost_max_us_{};
    inline static double cycles_per_us_{};   // fix round 1: QueryThreadCycleTime calibration (set by install before the manager exists)
    inline static std::atomic<std::uint32_t> tick_generation_{};inline static std::atomic<unsigned> tick_logic_{};inline static std::atomic<bool> tick_release_{};

    static Slot* slot_of(unsigned side) {return side<2 && current_[side]!=no_slot?&storage_[side][current_[side]]:nullptr;}
    static void exclude(unsigned side,std::uint32_t handle) {
        cast_excluded_bodies[side].store(handle,std::memory_order_relaxed);HandBodyNative::watch_handle(side,handle);
    }
    // The slot's body is gone (confirmed) or its state is unknown (retire: never reused, kept excluded only while it may exist).
    static void release_slot(unsigned side,bool retire,bool keep_excluded) {
        if(Slot* s=slot_of(side)) {
            s->state=retire?Slot::retired:Slot::free;
            if(retire) retired_.fetch_add(1,std::memory_order_relaxed);
        }
        current_[side]=no_slot;
        if(!keep_excluded) exclude(side,HandBodyNative::no_handle);
    }
    // Owner (a), resolved fresh for each create in the current world (a load replaces the player's owner: a stale one is never stored).
    static std::optional<std::uintptr_t> owner_now(std::uintptr_t world) {
        const auto camera=camera_?camera_():0;
        const auto source=camera?native_world_query_source(read_,base_,camera,world):std::nullopt;
        if(!source || source->world!=world) return std::nullopt;
        const auto row=probe_body_handle(read_,base_,world,source->handle,0,"owner_a",source->player);
        if(!owner_a_static_ok(HandBodyNative::owner_a_evidence(row))) return std::nullopt;
        return row->userdata_plus8;
    }
    static HandBodyEngine engine() {
        HandBodyEngine e;
        e.world=[] {return HandBodyNative::world_now();};
        e.busy=[] {return HandBodyNative::busy_now();};
        e.create=[](unsigned side,std::uintptr_t world,Vec3 at,std::uint32_t& handle) {   // world: the tick's snapshot (never re-read)
            if(side>=2 || current_[side]!=no_slot) return false;
            // final review P3-4: round-robin from the slot after the last one used, so storage a destroy (a snap) just freed rests while the others
            // are free (the engine never sees the same userdata rebuilt right after its body went)
            unsigned pick=no_slot;
            for(unsigned k=1;k<=storage_per_side;++k) {const unsigned i=(last_pick_[side]+k)%storage_per_side;if(storage_[side][i].state==Slot::free) {pick=i;break;}}
            if(pick==no_slot) {storage_exhausted_.fetch_add(1,std::memory_order_relaxed);return false;}
            const auto owner=world?owner_now(world):std::nullopt;
            if(!owner) return false;
            HandBodyNative::watch_owner(*owner);
            Slot& s=storage_[side][pick];
            G2Counts g2{};
            try {
                if(!HandBodyNative::create(s.storage,world,*owner,at,R1Recipe::radius,handle,&g2)) return false;
            } catch(const HandBodyEngineFault& f) {
                current_[side]=pick;last_pick_[side]=pick;s.state=Slot::in_use;s.world=world;s.handle=f.handle;
                if(f.handle!=HandBodyNative::no_handle) exclude(side,f.handle);   // a record holding our userdata exists: it gets its one destroy
                else release_slot(side,true,false);                             // nothing found: retire the storage (the state is unknown)
                g2_prev_=g2;g2_watching_=true;
                throw;
            }
            current_[side]=pick;last_pick_[side]=pick;s.state=Slot::in_use;s.world=world;s.handle=handle;
            exclude(side,handle);
            g2_prev_=g2;g2_watching_=true;   // an owner wake or broadphase exit inside createBody counts
            return true;
        };
        e.velocity=[](unsigned side,std::uint32_t handle,Vec3 v) {
            const Slot* s=slot_of(side);
            return s && HandBodyNative::velocity(s->storage,HandBodyNative::world_now(),s->world,handle,v,Vec3{});
        };
        e.destroy=[](unsigned side,std::uint32_t handle) {
            Slot* s=slot_of(side);
            if(!s) return DestroyOutcome::absent;
            DestroyOutcome r=DestroyOutcome::refused;
            try {r=HandBodyNative::destroy(s->storage,HandBodyNative::world_now(),s->world,handle);}
            catch(const HandBodyEngineFault&) {stranded_[side].store(true);release_slot(side,true,true);throw;}   // unknown: retired, still excluded
            if(r==DestroyOutcome::gone) release_slot(side,false,false);
            else if(r==DestroyOutcome::still_present) {stranded_[side].store(true);release_slot(side,true,true);}
            else if(r==DestroyOutcome::unknown) release_slot(side,true,true);   // most likely gone: retired, excluded until this side's next body
            return r;   // absent: the manager calls forget(); refused: retried at the next idle tick
        };
        e.position=[](unsigned side,std::uint32_t handle)->std::optional<Vec3> {
            const Slot* s=slot_of(side);
            if(!s) return std::nullopt;
            return HandBodyNative::position(HandBodyNative::world_now(),handle,s->storage);
        };
        e.apply=[](unsigned side,const hand_body::CommandRecord& record) {
            // The B8 velocity record (type 9) and, B16, the filter record (type 0x1a), directly (the manager has checked: the allow-list, idle,
            // our body, this world). Others: refused.
            std::uint16_t type{};std::memcpy(&type,record.bytes.data()+4,2);
            const Slot* s=slot_of(side);
            if(s && type==0x1a && record.size==0x20) {
                std::uint32_t handle{},filter{};std::memcpy(&handle,record.bytes.data()+8,4);std::memcpy(&filter,record.bytes.data()+0xc,4);
                return HandBodyNative::set_filter(s->storage,HandBodyNative::world_now(),s->world,handle,filter);
            }
            if(!s || type!=9 || record.size!=0x30) return false;
            std::uint32_t handle{};std::memcpy(&handle,record.bytes.data()+8,4);
            float f[6]{};std::memcpy(f,record.bytes.data()+0x10,12);std::memcpy(f+3,record.bytes.data()+0x20,12);
            return HandBodyNative::velocity(s->storage,HandBodyNative::world_now(),s->world,handle,Vec3{f[0],f[1],f[2]},Vec3{f[3],f[4],f[5]});
        };
        e.filter=[](unsigned side,std::uint32_t handle)->std::optional<std::uint32_t> {   // B16 fix round 2: the word the engine holds
            const Slot* s=slot_of(side);
            if(!s) return std::nullopt;
            return HandBodyNative::filter_word(HandBodyNative::world_now(),handle,s->storage);
        };
        e.forget=[](unsigned side) {release_slot(side,true,false);};   // abandoned with its world / lost: no engine call; the storage is retired
        e.at_g1_point=[] {return in_g1_tick_;};   // write() only from inside the dispatcher's G1 tick (B14's drive runs there)
        e.motion=[](unsigned side,std::uint32_t handle)->std::optional<BodyMotion> {   // B14 read-back (raw; the manager judges garbage)
            const Slot* s=slot_of(side);
            if(!s) return std::nullopt;
            return HandBodyNative::motion(HandBodyNative::world_now(),handle,s->storage);
        };
        e.now_us=[] {return qpc_us();};
        // Fix round 1: thread CPU time. QueryThreadCycleTime returns FALSE (0 cycles) under Wine 11 and Proton Experimental 11.0-100 (probed
        // 2026-10-24): then cycles_per_us is 0 and the status says cpu_available:false; on Windows it is calibrated at install.
        e.cpu_cycles=[]()->std::optional<std::uint64_t> {ULONG64 c{};if(!QueryThreadCycleTime(GetCurrentThread(),&c)) return std::nullopt;return static_cast<std::uint64_t>(c);};
        e.cycles_per_us=cycles_per_us_;
        return e;
    }

    // ---- the 0x89fb0 entry (NativeGrab::invoke, after the original dispatcher)
    // Cycles per microsecond of QueryThreadCycleTime against QPC: three 10 ms spins on the control thread, the highest rate kept (a spin that
    // was preempted counts fewer cycles, so the highest is the least disturbed; a low rate would overstate CPU time, never hide it). 0: unavailable.
    static double calibrate_cycles_per_us() {
        double best=0;
        for(int k=0;k<3;++k) {
            ULONG64 c0{},c1{};
            if(!QueryThreadCycleTime(GetCurrentThread(),&c0)) return 0;
            const auto t0=qpc_us();std::uint64_t t1=t0;
            while((t1=qpc_us())-t0<10000) {}
            if(!QueryThreadCycleTime(GetCurrentThread(),&c1) || c1<=c0 || t1<=t0) return 0;
            best=std::max(best,static_cast<double>(c1-c0)/static_cast<double>(t1-t0));
        }
        return best;
    }
    static std::uint64_t qpc_us() {
        static const std::int64_t freq=[] {LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart>0?f.QuadPart:1;}();
        LARGE_INTEGER c{};QueryPerformanceCounter(&c);
        return static_cast<std::uint64_t>(static_cast<double>(c.QuadPart)*1e6/static_cast<double>(freq));
    }
    static void dispatch_entry(void*) {
        if(!active_.load(std::memory_order_acquire)) return;
        struct Inflight {Inflight() {dispatch_inflight_.fetch_add(1);} ~Inflight() {dispatch_inflight_.fetch_sub(1);}} inflight;
        const auto point=HandBodyNative::note_dispatch();
        if(!point.g1_point) return;                                         // outside the step, on the 0x8ad260 thread only
        std::unique_lock<std::mutex> lock(tick_mutex_,std::try_to_lock);
        if(!lock.owns_lock() || point.frame==last_frame_) return;           // once per game frame
        last_frame_=point.frame;
        in_g1_tick_=true;
        const auto t0=qpc_us();
        try {tick();} catch(...) {HandBodyFault::trip();note_event(0,"tick_exception",0);}
        in_g1_tick_=false;
        // B14 proof (d): the whole tick (service + both drives) lands on the render thread (B11 G1: frame, drain and present share it)
        const auto t1=qpc_us();const auto us=t1>=t0?t1-t0:0;
        tick_cost_us_.store(us,std::memory_order_relaxed);if(us>tick_cost_max_us_.load(std::memory_order_relaxed)) tick_cost_max_us_.store(us,std::memory_order_relaxed);
        if(!tick_changed_ && us>steady_tick_cost_max_us_.load(std::memory_order_relaxed)) steady_tick_cost_max_us_.store(us,std::memory_order_relaxed);
    }
    static const char* g1_violation_reason(const G1Stats& at_pass,const G1Stats& now) {
        if(now.dispatch_off_frame_thread!=at_pass.dispatch_off_frame_thread) return "G1 violated after the pass: a 0x89fb0 dispatch ran off the frame thread";
        if(now.dispatch_overlap!=at_pass.dispatch_overlap) return "G1 violated after the pass: a 0x89fb0 dispatch overlapped the step";
        if(now.step_busy!=at_pass.step_busy) return "G1 violated after the pass: 0x8a7820 true at 0x97bc0 entry";
        if(now.frame_threads!=at_pass.frame_threads) return "G1 violated after the pass: 0x8ad260 ran on another thread";
        if(now.dispatch_busy!=at_pass.dispatch_busy) return "G1 violated after the pass: 0x8a7820 true at a 0x89fb0 dispatch";
        return "G1 violated after the pass";
    }
    static void tick() {
        auto* body=body_.load(std::memory_order_acquire);
        if(!body) return;
        const auto now=GetTickCount64();
        // final review P3-2: the game logic as the render thread published it with the release gate (game_logic_mask() takes blocking locks, so
        // the dispatcher never calls it); a publication older than fresh_ms reads as "not gameplay"
        const auto logic_tick=release_tick_.load(std::memory_order_acquire);
        const unsigned logic=logic_tick && now>=logic_tick && now-logic_tick<=fresh_ms?logic_published_.load(std::memory_order_relaxed):0u;
        const bool gameplay_logic=logic==2;
        if(gameplay_logic && !gameplay_since_ms_) gameplay_since_ms_=now;
        // G1: in-run observation before the first create; after the pass, any growth of a violation counter latches the hand body off.
        HandBodyGate gate;
        g1_=HandBodyNative::g1_now(gameplay_since_ms_?static_cast<double>(now-gameplay_since_ms_)/1000.:0.);
        if(!g1_decided_) {
            const auto v=judge_g1(g1_,g1_min_s);
            if(v.decided) {g1_decided_=true;g1_passed_=v.pass;g1_pass_=g1_;g1_reason_.store(v.reason);g1_state_.store(v.pass?1:2);
                           note_event(0,v.pass?"g1_pass":"g1_fail",0);}
        }
        gate.g1_pass=g1_decided_ && g1_passed_;
        if(gate.g1_pass && !g1_still_holds(g1_pass_,g1_)) {   // safe: the bodies are destroyed once and the hand body stays off (review P3-3: logged once, reason in the status)
            gate.violated=true;gate.why=g1_violation_reason(g1_pass_,g1_);
            if(!g1_violation_.load()) {g1_violation_.store(gate.why);note_event(0,"g1_violated",0);}
        }
        // G2: an owner (a) wake or a broadphase exit for a hand handle while a body lives rejects owner (a); no fallback owner: off.
        const auto g2=HandBodyNative::g2_now();
        if(g2_watching_ && (g2.wake_owner>g2_prev_.wake_owner || g2.exit_hand>g2_prev_.exit_hand)) {
            g2_events_.fetch_add(1,std::memory_order_relaxed);gate.violated=true;gate.why="G2 rejected owner (a) at run time";
        }
        g2_prev_=g2;
        // The render publication (try_lock: the dispatcher never waits; the last copy is used and ages out).
        {std::unique_lock<std::mutex> hands_lock(hands_mutex_,std::try_to_lock);if(hands_lock.owns_lock()) {last_hands_=hands_;last_hands_tick_=hands_tick_;}}
        const auto release_tick=release_tick_.load(std::memory_order_acquire);
        const bool release=release_.load(std::memory_order_acquire);
        const bool fresh=last_hands_tick_ && now>=last_hands_tick_ && now-last_hands_tick_<=fresh_ms && release_tick && now>=release_tick && now-release_tick<=fresh_ms;
        const bool gameplay=gameplay_logic && fresh && !release && !last_hands_.authored;
        const auto generation=generation_.load(std::memory_order_acquire);
        std::array<LifecycleInput,2> in{};std::array<std::optional<Vec3>,2> spawn{};
        for(unsigned side=0;side<2;++side) {
            auto& li=in[side];
            li.world_generation=generation;li.gameplay=gameplay;li.focused=last_hands_.focused;
            li.tracked=((last_hands_.tracked>>side)&1u)!=0;li.shutdown=stop_requested_.load(std::memory_order_acquire);li.now_ms=now;
            // B14 ruling: a body is created (and snapped) only where the clamp's last palm cast found the target free; otherwise it waits
            const bool free=fresh && now-last_hands_tick_<=free_fresh_ms && ((last_hands_.free>>side)&1u)!=0;
            if(li.tracked && free && finite(last_hands_.palm[side])) spawn[side]=last_hands_.palm[side];
        }
        tick_generation_=generation;tick_logic_=logic;tick_release_=release;
        body->service(in,spawn,gate);
        // B16: the filter word the render thread's HandFilterKeeper asks for (a coupled hold of a corpse limb or a rigid prop: the mask without
        // its layer; otherwise the R1 default, which returns only through the keeper's SafeRelease or with a new body). Only from a fresh
        // publication (a stale one means no gameplay: the body is going anyway), one filter write per side per tick, idle only (write() refuses).
        if(fresh) for(unsigned side=0;side<2;++side) {
            const std::uint32_t want=last_hands_.filter[side];
            if(body->ready(side) && body->filter_word(side)!=want && hand_filter_word_allowed(want)) body->write_filter(side,want);
        }
        // B14: drive both hands in the same tick, once per game frame (this tick's cadence: the 0x89fb0 G1 point, first dispatch of each
        // 0x8ad260 frame; ~100/s in the sim against 7-11k dispatches/s). The servo's dt is QPC time since that side's last drive.
        const double now_s=static_cast<double>(qpc_us())*1e-6;
        for(unsigned side=0;side<2;++side) {
            DriveInput d;d.side=side;d.target=last_hands_.target[side];d.held_kg=0;d.now=now_s;   // held_kg: B16 feeds the held body
            const bool tracked=((last_hands_.tracked>>side)&1u)!=0;
            d.yield=!fresh || !tracked || ((last_hands_.yield>>side)&1u)!=0;
            d.target_free=fresh && now-last_hands_tick_<=free_fresh_ms && ((last_hands_.free>>side)&1u)!=0;
            d.player_root=fresh?last_hands_.player_root:std::nullopt;d.locomotion=last_hands_.locomotion;d.player_velocity=fresh?last_hands_.player_velocity:Vec3{};d.target_time_s=last_hands_.target_time_s;d.root_feedforward_gate=fresh && last_hands_.root_feedforward_gate && !(last_hands_.root_gate_blocked&(1u<<side));
            d.holding=fresh && ((last_hands_.holding>>side)&1u)!=0;   // B16 fix round 2: no snap while this hand holds something ...
            d.hold_snap_ok=fresh && ((last_hands_.hold_snap_ok>>side)&1u)!=0;   // ... unless the held object is free (P2-1)
            body->drive(d);
        }
        g2_watching_=body->any_alive();
        if(!g2_watching_) g2_prev_=HandBodyNative::g2_now();
        tick_changed_=log_changes(*body);
    }

    // ---- the event log (hand-body-events.csv; a writer thread flushes once per second, the dispatcher only try_locks)
    inline static std::mutex events_mutex_;inline static std::vector<HandBodyEvent> events_;inline static std::atomic<std::uint64_t> dropped_events_{};
    static void note_event(unsigned side,const char* kind,std::uint32_t handle) {
        HandBodyEvent ev;ev.t_ms=GetTickCount64();ev.side=side;ev.kind=kind;ev.handle=handle;ev.world=HandBodyNative::reader()?HandBodyNative::world_now():0;
        ev.generation=tick_generation_;ev.logic=tick_logic_;ev.release=tick_release_;
        std::unique_lock<std::mutex> lock(events_mutex_,std::try_to_lock);
        if(!lock.owns_lock() || events_.size()>=1024) {dropped_events_.fetch_add(1,std::memory_order_relaxed);return;}
        events_.push_back(ev);
    }
    // true: a create, destroy, abandon or fault happened this tick (not a steady tick for the cost)
    static bool log_changes(const HandBody& body) {
        bool changed=false;
        for(unsigned side=0;side<2;++side) {
            const auto now=body.status(side);auto& was=last_status_[side];
            const auto drive=body.drive_status(side);auto& drive_was=last_drive_[side];
            changed=changed || now.creates>was.creates || now.destroys>was.destroys || now.abandons>was.abandons || now.faults>was.faults || now.lost>was.lost;
            if(drive.snaps>drive_was.snaps) note_event(side,"snap",now.handle);
            if(drive.garbage>drive_was.garbage) note_event(side,"readback_garbage",was.handle);
            if(drive.snap_blocked>drive_was.snap_blocked && drive_was.snap_blocked<16) note_event(side,"snap_blocked",now.handle);
            drive_was=drive;
            if(now.creates>was.creates) note_event(side,"create",now.handle);
            if(now.wake_writes>was.wake_writes) note_event(side,"wake",now.handle);
            if(now.destroys>was.destroys) note_event(side,now.releases>was.releases?"destroy_release":"destroy",was.handle);
            if(now.queued_destroys>was.queued_destroys) note_event(side,"destroy_queued",was.handle);
            if(now.abandons>was.abandons) note_event(side,"abandon",was.handle);
            if(now.lost>was.lost) note_event(side,"lost",was.handle);
            if(now.faults>was.faults) note_event(side,"fault",was.handle);
            if(now.create_refusals>was.create_refusals && was.create_refusals<16) note_event(side,"create_refused",0);
            if(now.filter_writes>was.filter_writes) note_event(side,now.filter==R1Recipe::filter?"filter_default":"filter_held",now.handle);
            was=now;
        }
        return changed;
    }
    inline static std::thread writer_;inline static std::mutex writer_mutex_;inline static std::condition_variable writer_wake_;inline static bool writer_quit_{};
    static void start_writer() {
        stop_writer();
        {std::lock_guard<std::mutex> lock(writer_mutex_);writer_quit_=false;}
        std::error_code ec;if(output_.empty() || !std::filesystem::exists(output_,ec)) return;
        writer_=std::thread([] {
            std::ofstream csv(output_/L"hand-body-events.csv",std::ios::trunc);
            csv<<"t_ms,side,event,handle,world,generation,logic,release\n";csv.flush();
            for(;;) {
                bool quit=false;
                {std::unique_lock<std::mutex> lock(writer_mutex_);quit=writer_wake_.wait_for(lock,std::chrono::seconds(1),[] {return writer_quit_;});}
                std::vector<HandBodyEvent> batch;{std::lock_guard<std::mutex> lock(events_mutex_);batch.swap(events_);}
                try {
                    for(const auto& ev:batch) csv<<ev.t_ms<<','<<ev.side<<','<<ev.kind<<",0x"<<std::hex<<ev.handle<<",0x"<<ev.world<<std::dec<<','<<ev.generation<<','<<ev.logic<<','<<(ev.release?1:0)<<'\n';
                    csv.flush();
                } catch(...) {}
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
