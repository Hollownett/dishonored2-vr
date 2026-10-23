#pragma once
// Plan B task B13: the production hand-body manager (pure: no windows.h, no engine code; the real engine is src/hand_body_runtime.hpp over the
// shared R1 recipe in src/hand_body_native.hpp). Bonelab §2.1 makes each hand a physical body that the game's own physics moves and stops; this
// manager owns those two bodies' lives: it creates one per hand (route R1, B11/B12), keeps it only while gameplay lasts in one world, and
// destroys it BEFORE the world can change (B12 GO condition). Default OFF behind build/hand-body (build/no-hand-body wins) until B17.
//
// Rules (B12 ruling, controller rulings for B13):
// * Creation only from the 0x89fb0 dispatcher outside the step (the runtime's G1 point), only when physics is idle, only when the B10
//   lifecycle says create. One wake velocity write right after the creation (B11: createBody leaves the body asleep).
// * Gameplay lost while the world is the same (pause-menu intent, any menu or loading flag, death clip, the VR settings panel) destroys at
//   once: directly when idle, otherwise a destroy record is pushed on the mod's own queue and drained at the next idle tick (no engine
//   command-2 record: B8 left that encoder out). The dispatcher does not run while the game is paused (B11 run 3), so the pause intent is
//   seen on the render thread BEFORE the game sees Start (HandBodyReleaseGate) and the Start pulse waits for the body (bounded).
// * A world (or load generation) change with the body alive abandons it: no engine call, counted in `abandons`; the last resort only.
// * Any engine fault latches HandBodyFault: the body is destroyed once if it can be, then the hand body stays off for the session and the
//   fallback clamp runs (status available:false, faults).
// Threads: service() and write() run on the dispatcher thread only (a try_lock refuses a re-entry); pose(), ready() and status() are for
// any thread (a mutex-protected copy and atomics; the dispatcher only ever try_locks the pose slot, so it never waits on the render thread).
#include "arm_ik.hpp"
#include "hand_body_commands.hpp"
#include "hand_body_lifecycle.hpp"
#include "tracked_pose.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace d2vr {

// ---- shared with the B11 spike (promoted from hand_body_spike.hpp)

// Session-wide latch for the hand body (plan A's FaultLatch pattern, a separate flag): any SEH fault on a hand-body engine call trips it;
// the hand body stays off until restart.
class HandBodyFault {
public:
    static void trip() noexcept {flag().store(true,std::memory_order_relaxed);}
    static bool tripped() noexcept {return flag().load(std::memory_order_relaxed);}
#ifdef D2VR_TESTING
    static void reset_for_tests() noexcept {flag().store(false,std::memory_order_relaxed);}
#endif
private:
    static std::atomic<bool>& flag() noexcept {static std::atomic<bool> f{false};return f;}
};

// An engine call faulted (the real engine turns a failed guarded_call into this). handle: a body the faulting call may still have left in
// the world (a createBody that faulted after inserting its record); it then gets the one destroy attempt.
struct HandBodyEngineFault : std::runtime_error {
    explicit HandBodyEngineFault(const char* what,std::uint32_t body=0xffffffffu):std::runtime_error(what),handle(body) {}
    std::uint32_t handle;
};
// A read could not be made this tick (an unreadable pool, a page gone for a moment): not "the body is gone".
struct HandBodyUnreadable {};

// The R1 creation recipe (B11 route results, B12 GO): a 5 cm sphere (0xca6550), flags 0, filter hand_filter_word(false) = 0x000885e4,
// motion type 2 (dynamic), 2 kg, props 0xffff (the dynamic preset, gravity on), userdata 0x18cb90 in mod memory with ud+8 = owner (a).
struct R1Recipe {
    static constexpr double radius=.05;
    static constexpr float mass_kg=2.f;
    static constexpr unsigned char motion_type=2;
    static constexpr std::uint32_t flags=0;
    static constexpr std::uint32_t filter=hand_body::hand_filter_word(false);
    static constexpr double wake_speed=.1;   // the one wake write right after creation, straight down (m/s)
};
static_assert(R1Recipe::filter==0x000885e4u,"R1 filter word (B11)");

// ---- G1: thread and drain point (docs Gates for B11). Counted by the count-only hooks; judged here.
struct G1Stats {
    double observed_s{};                         // gameplay seconds observed so far
    std::uint64_t frames{},steps{},step_busy{};  // 0x8ad260 entries; 0x97bc0 entries and how many found 0x8a7820 true
    std::uint64_t dispatches{},dispatch_busy_samples{},dispatch_busy{};
    std::uint64_t dispatch_outside_step{},dispatch_step_drain{},dispatch_overlap{},dispatch_off_frame_thread{},dispatch_before_frame{};
    std::uint64_t frame_threads{};               // distinct threads that entered 0x8ad260
    std::uint32_t frame_thread{},dispatch_thread{};
};
struct G1Verdict {bool decided{},pass{};const char* reason{""};};
// The create/destroy point is a 0x89fb0 dispatch outside the step (0x97bc0 depth 0) on the 0x8ad260 thread. It passes only if, over at least
// min_s of observation: frames, steps and such dispatches exist; every dispatch ran on the frame thread; 0x8ad260 ran on one thread; no
// dispatch overlapped the step from another thread or outside its 0x945f0 drain; 0x8a7820 was readable and false at every sampled dispatch
// and at every step entry. This is the in-run G1 of the B11 ruling; the doc's full G1 soak is B14's.
inline G1Verdict judge_g1(const G1Stats& s,double min_s) {
    if(s.observed_s<min_s) return {false,false,"observing"};
    if(!s.frames) return {true,false,"no 0x8ad260 frame seen"};
    if(!s.steps) return {true,false,"no 0x97bc0 step seen"};
    if(!s.dispatch_outside_step) return {true,false,"no 0x89fb0 dispatch outside the step"};
    if(s.frame_threads!=1) return {true,false,"0x8ad260 ran on more than one thread"};
    if(s.dispatch_off_frame_thread) return {true,false,"a 0x89fb0 dispatch ran off the frame thread"};
    if(s.dispatch_overlap) return {true,false,"a 0x89fb0 dispatch overlapped the step"};
    if(!s.dispatch_busy_samples) return {true,false,"0x8a7820 never readable at a 0x89fb0 dispatch"};
    if(s.dispatch_busy) return {true,false,"0x8a7820 true at a 0x89fb0 dispatch"};
    if(s.step_busy) return {true,false,"0x8a7820 true at 0x97bc0 entry"};
    return {true,true,"pass"};
}
// After the pass the same counters must not grow (checked every tick; a violation latches the hand body off after its destroy).
inline bool g1_still_holds(const G1Stats& at_pass,const G1Stats& now) {
    return now.dispatch_off_frame_thread==at_pass.dispatch_off_frame_thread && now.dispatch_overlap==at_pass.dispatch_overlap &&
           now.step_busy==at_pass.step_busy && now.frame_threads==at_pass.frame_threads && now.dispatch_busy==at_pass.dispatch_busy;
}

// ---- G2: owner (a), the player's userdata owner (B7 chain on the player's query body), read live.
struct OwnerEvidence {
    bool a_resolved{},a_userdata_is_player{},a_entity_is_player{},a_type_decoded{},a_type_in_range{};
    bool b_available{};const char* b_reason{""};
};
inline bool owner_a_static_ok(const OwnerEvidence& e) {
    return e.a_resolved && e.a_userdata_is_player && e.a_entity_is_player && e.a_type_decoded && !e.a_type_in_range;
}
// The runtime half of G2: 0x853220 calls with the hand's owner and 0x8527a0 calls for a hand's handle, counted from creation.
struct G2Counts {std::uint64_t wake_owner{},exit_hand{};};
inline bool g2_rejects(const G2Counts& since_create) {return since_create.wake_owner || since_create.exit_hand;}

// ---- knobs
// B13 (default off until B17): the hand body runs when build/hand-body exists and build/no-hand-body does not. After B17: unless no-hand-body.
inline bool hand_body_knob_enabled(bool hand_body_file,bool no_hand_body_file) {return hand_body_file && !no_hand_body_file;}

// ---- the render-side release gate
// What the render thread knows about the game leaving gameplay, sampled once per frame where the controller input is composed.
struct ReleaseSignals {
    unsigned logic{2};          // game_logic_mask(): 2 is gameplay; 4 pause menu, 0 / 1<<15 loading or death reload, 16/128/256/512/1024 world UI
    bool menu{};                // the menu button as the composer will see it (the Start pulse fires on its RELEASE, MenuTap)
    bool pause_request{};       // the qualified hold-Y pause event (the composer pulses Start on its rising edge)
    bool settings_open{};       // the mod's VR settings panel
    bool authored{};            // an authored camera owns the view (a death clip, a takedown, a cutscene): the arms are the game's (C9)
};
struct ReleaseOutput {
    bool release{};             // the hand bodies must be gone: gameplay is false for them at the next dispatcher tick
    bool menu{};                // the menu button to publish (held on while a body still exists, so Start waits for the destroy)
    bool pause_request{};       // the pause event to publish (deferred while a body still exists)
};
// The 0x89fb0 dispatcher (and the game frame 0x8ad260) stop while the game is paused, so a destroy can only happen on a tick BEFORE the
// pause. The game pauses on the Start pulse the mod's composer sends; this gate (a) raises `release` as soon as the menu button goes down
// or a pause event arrives, (b) keeps the button held / the event deferred until no body exists, at most hold_cap_ms, and (c) keeps
// `release` up for settle_ms after the last reason so no body is re-created in the frames between the destroy and the pause.
class HandBodyReleaseGate {
public:
    static constexpr std::uint64_t hold_cap_ms=300,settle_ms=500;
    ReleaseOutput update(const ReleaseSignals& s,bool body_alive,std::uint64_t now_ms) {
        ReleaseOutput out;
        if(s.menu && !menu_was_ && body_alive) ++intents_;
        if(menu_was_ && !s.menu && body_alive) {hold_menu_=true;hold_since_=now_ms;++holds_;}
        menu_was_=s.menu;
        if(hold_menu_) {
            if(!body_alive) hold_menu_=false;
            else if(now_ms<hold_since_ || now_ms-hold_since_>=hold_cap_ms) {hold_menu_=false;++timeouts_;}
        }
        const bool rise=s.pause_request && !pause_was_;pause_was_=s.pause_request;
        if(rise && body_alive) {pause_pending_=true;pause_since_=now_ms;++holds_;++intents_;}
        out.pause_request=s.pause_request && !pause_pending_;
        if(pause_pending_ && (!body_alive || now_ms<pause_since_ || now_ms-pause_since_>=hold_cap_ms)) {
            if(body_alive) ++timeouts_;
            pause_pending_=false;out.pause_request=true;   // a fresh rising edge for the composer, now that the body is gone
        }
        out.menu=s.menu || hold_menu_;
        const bool reason=s.logic!=2 || s.menu || s.pause_request || s.settings_open || s.authored || hold_menu_ || pause_pending_ || out.pause_request;
        if(reason) release_until_=now_ms+settle_ms;
        out.release=reason || now_ms<release_until_;
        return out;
    }
    std::uint64_t holds() const {return holds_;}
    std::uint64_t timeouts() const {return timeouts_;}
    std::uint64_t intents() const {return intents_;}
    void reset() {*this=HandBodyReleaseGate{};}
private:
    bool menu_was_{},hold_menu_{},pause_was_{},pause_pending_{};
    std::uint64_t hold_since_{},pause_since_{},release_until_{},holds_{},timeouts_{},intents_{};
};

// ---- the engine seam (the runtime binds it to src/hand_body_native.hpp; tests bind a fake)
enum class DestroyOutcome {
    gone,           // destroyBodies ran and the slot no longer holds our body
    refused,        // no engine call was made (busy at the re-read, unreadable pool): retry at the next idle tick
    absent,         // no engine call: the slot no longer held our body before the call (removed or recycled): never destroy someone else's
    still_present,  // destroyBodies returned but the slot still holds our body: the state is unknown, latch
    unknown,        // destroyBodies returned but the slot could not be read afterwards: tracked no longer, its storage never reused
};
struct HandBodyEngine {
    std::function<std::uintptr_t()> world;                                            // the live world; 0 when unreadable this tick
    std::function<bool()> busy;                                                       // 0x8a7820 (an unreadable flag counts as busy)
    std::function<bool(unsigned side,Vec3 at,std::uint32_t& handle)> create;          // R1 createBody; throws HandBodyEngineFault
    std::function<bool(unsigned side,std::uint32_t handle,Vec3 linear)> velocity;     // 0xc85dc0 (the wake write); throws HandBodyEngineFault
    std::function<DestroyOutcome(unsigned side,std::uint32_t handle)> destroy;        // direct 0xc8f6b0; throws HandBodyEngineFault
    std::function<std::optional<Vec3>(unsigned side,std::uint32_t handle)> position;  // nullopt: the record no longer holds our body; throws HandBodyUnreadable
    std::function<bool(unsigned side,const hand_body::CommandRecord&)> apply;         // a write; throws HandBodyEngineFault
    std::function<void(unsigned side)> forget;                                        // abandon / lost: mod-side bookkeeping only, NEVER an engine call
};

// Gate state the runtime passes each tick (defaults: open, for the fakes).
struct HandBodyGate {bool g1_pass{true};bool violated{};const char* why{""};};

struct HandBodyStatus {
    bool available{};BodyState state{};std::uint32_t handle{};std::uint64_t creates{},destroys{},faults{},refused_writes{};
    std::uint64_t abandons{},queued_destroys{},direct_destroys{},busy_waits{},wake_writes{},create_refusals{},writes{},releases{},lost{},unverified{};
};

// The last read-back pose for the render thread: the dispatcher publishes with try_lock (never waits), readers copy under the lock.
class HandBodyPoseSlot {
public:
    bool publish(const std::optional<WorldTrackedPose>& pose) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {skipped_.fetch_add(1,std::memory_order_relaxed);return false;}
        pose_=pose;return true;
    }
    std::optional<WorldTrackedPose> read() const {std::lock_guard<std::mutex> lock(mutex_);return pose_;}
    std::uint64_t skipped() const {return skipped_.load(std::memory_order_relaxed);}
private:
    mutable std::mutex mutex_;std::optional<WorldTrackedPose> pose_;std::atomic<std::uint64_t> skipped_{};
};

class HandBody {
public:
    static constexpr unsigned sides=2;
    static constexpr std::uint64_t create_retry_ms=1000;   // after a refused create, wait before the next attempt (owner resolution is not free)
    static constexpr unsigned max_destroy_retries=240;     // a queued destroy refused this many times latches the fault
    static constexpr unsigned max_wake_tries=30;           // a refused wake write (busy, unreadable) is retried on the next ticks, this many times
    explicit HandBody(HandBodyEngine engine,bool enabled=true):engine_(std::move(engine)),enabled_(enabled) {}

    // Dispatcher tick (the G1 point, idle or not): run the lifecycle per side, then create / destroy / drain the queue / read back.
    // spawn: where a new body is created (the calibrated palm, world space); no spawn, no create. gate: the runtime's G1/G2 state.
    void service(const std::array<LifecycleInput,sides>& per_side,const std::array<std::optional<Vec3>,sides>& spawn={},const HandBodyGate& gate={}) {
        std::unique_lock<std::mutex> lock(tick_mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {reentries_.fetch_add(1,std::memory_order_relaxed);return;}
        ticks_.fetch_add(1,std::memory_order_relaxed);
        if(HandBodyFault::tripped() && !latched_) latch(0,"HandBodyFault tripped elsewhere",false);
        if(gate.violated && !latched_) latch(0,gate.why && *gate.why?gate.why:"gate violated",false);
        std::uintptr_t world{};
        try {world=engine_.world?engine_.world():0;} catch(...) {world=0;}
        bool busy=true;
        try {busy=engine_.busy?engine_.busy():false;} catch(...) {busy=true;}
        if(!world) {   // unreadable this tick: never read as a world change (that would drop a live body); nothing is decided
            unreadable_ticks_.fetch_add(1,std::memory_order_relaxed);
            for(unsigned side=0;side<sides;++side) publish_pose(side,std::nullopt);
            return;
        }
        last_world_=world;
        for(unsigned side=0;side<sides;++side) {
            LifecycleInput li=per_side[side];
            li.world=world;li.physics_busy=busy;li.fault=latched_;li.shutdown=li.shutdown || shutdown_.load(std::memory_order_acquire);
            li.enabled=li.enabled && enabled_ && gate.g1_pass;
            last_gen_=li.world_generation;
            const BodyAction action=life_.update(side,li);
            Side& s=side_[side];
            switch(action) {
            case BodyAction::abandon:            // the world changed under a live body: no engine call at all, counted (last resort)
                s.pending_destroy=false;s.handle=0;s.attempts=0;
                count(side,&Counters::abandons);
                if(engine_.forget) engine_.forget(side);
                break;
            case BodyAction::create:
                if(!busy && !latched_ && spawn[side] && finite(*spawn[side]) && li.now_ms>=s.next_create_ms) do_create(side,world,*spawn[side],li.now_ms);
                break;
            case BodyAction::destroy:
                if(!li.gameplay) count(side,&Counters::releases);
                do_destroy(side,world,busy);
                break;
            case BodyAction::destroy_queued:     // physics busy: the destroy record goes on the mod's queue, drained at the next idle tick
                if(!li.gameplay) count(side,&Counters::releases);
                s.pending_destroy=true;count(side,&Counters::queued_destroys);
                break;
            case BodyAction::none:
                if(s.pending_destroy) {if(busy) count(side,&Counters::busy_waits);else do_destroy(side,world,busy);}
                else if(!s.woken && !busy && !latched_ && s.wake_tries<max_wake_tries && life_.state(side)==BodyState::ready) wake(side);
                break;
            }
            readback(side,world);
            publish_state(side);
        }
    }
    // Lifecycle ready, no fault latch, not disabled. Any thread.
    bool ready(unsigned side) const {
        return side<sides && enabled_ && !latched_flag_.load(std::memory_order_acquire) && !HandBodyFault::tripped() &&
               static_cast<BodyState>(counters_[side].state.load(std::memory_order_acquire))==BodyState::ready;
    }
    // A body exists or is being destroyed on either side (the release gate holds Start while this is true). Any thread.
    bool any_alive() const {
        for(const auto& c:counters_) {const auto st=static_cast<BodyState>(c.state.load(std::memory_order_acquire));if(st==BodyState::ready || st==BodyState::destroying) return true;}
        return false;
    }
    std::optional<WorldTrackedPose> pose(unsigned side) const {return side<sides?poses_[side].read():std::nullopt;}
    // Dispatcher thread only. Refused (counted) unless the lifecycle allows writes to this body in this world and load generation, physics
    // is idle and the record names the body's own full handle.
    bool write(unsigned side,const hand_body::CommandRecord& record) {
        if(side>=sides) return false;
        std::unique_lock<std::mutex> lock(tick_mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {count(side,&Counters::refused_writes);return false;}
        std::uint32_t handle{};
        if(record.size<0x10 || record.size>record.bytes.size()) {count(side,&Counters::refused_writes);return false;}
        std::memcpy(&handle,record.bytes.data()+8,sizeof handle);
        std::uintptr_t world{};
        try {world=engine_.world?engine_.world():0;} catch(...) {world=0;}
        bool busy=true;
        try {busy=engine_.busy?engine_.busy():false;} catch(...) {busy=true;}
        const Side& s=side_[side];
        if(latched_ || !enabled_ || !world || world!=last_world_ || last_gen_!=s.gen || busy || s.pending_destroy || !life_.can_write(side,world,handle) ||
           !engine_.apply) {count(side,&Counters::refused_writes);return false;}
        try {
            if(!engine_.apply(side,record)) {count(side,&Counters::refused_writes);return false;}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);return false;}
        count(side,&Counters::writes);
        return true;
    }
    HandBodyStatus status(unsigned side) const {
        HandBodyStatus st;if(side>=sides) return st;
        const auto& c=counters_[side];
        st.available=enabled_ && !latched_flag_.load(std::memory_order_acquire) && !HandBodyFault::tripped();
        st.state=static_cast<BodyState>(c.state.load(std::memory_order_acquire));st.handle=c.handle.load(std::memory_order_acquire);
        st.creates=c.creates.load();st.destroys=c.destroys.load();st.faults=c.faults.load();st.refused_writes=c.refused_writes.load();
        st.abandons=c.abandons.load();st.queued_destroys=c.queued_destroys.load();st.direct_destroys=c.direct_destroys.load();
        st.busy_waits=c.busy_waits.load();st.wake_writes=c.wake_writes.load();st.create_refusals=c.create_refusals.load();
        st.writes=c.writes.load();st.releases=c.releases.load();st.lost=c.lost.load();st.unverified=c.unverified.load();
        return st;
    }
    // Session end: the next service() tick destroys every body (the lifecycle's shutdown input is forced on).
    void shutdown() {shutdown_.store(true,std::memory_order_release);}
    bool shutdown_requested() const {return shutdown_.load(std::memory_order_acquire);}
    bool enabled() const {return enabled_;}
    bool latched() const {return latched_flag_.load(std::memory_order_acquire);}
    std::string why() const {std::lock_guard<std::mutex> lock(why_mutex_);return why_;}
    std::uint64_t ticks() const {return ticks_.load();}
    std::uint64_t unreadable_ticks() const {return unreadable_ticks_.load();}
    std::uint64_t reentries() const {return reentries_.load();}
    std::uint64_t pose_skips() const {return poses_[0].skipped()+poses_[1].skipped();}
private:
    struct Counters {
        std::atomic<std::uint64_t> creates{},destroys{},faults{},refused_writes{},abandons{},queued_destroys{},direct_destroys{},busy_waits{},
            wake_writes{},create_refusals{},writes{},releases{},lost{},unverified{};
        std::atomic<int> state{};std::atomic<std::uint32_t> handle{};
    };
    struct Side {std::uint32_t handle{},gen{};std::uintptr_t world{};bool pending_destroy{},woken{};unsigned attempts{},wake_tries{};std::uint64_t next_create_ms{};};
    void count(unsigned side,std::atomic<std::uint64_t> Counters::* field) {(counters_[side].*field).fetch_add(1,std::memory_order_relaxed);}
    void latch(unsigned side,const char* why,bool counted) {
        latched_=true;latched_flag_.store(true,std::memory_order_release);HandBodyFault::trip();
        if(counted) count(side,&Counters::faults);
        std::unique_lock<std::mutex> lock(why_mutex_,std::try_to_lock);   // the dispatcher never waits on a status reader
        if(lock.owns_lock() && why_.empty() && why) why_=why;
    }
    void do_create(unsigned side,std::uintptr_t world,Vec3 at,std::uint64_t now_ms) {
        Side& s=side_[side];
        std::uint32_t handle=0;bool ok=false;
        if(shutdown_.load(std::memory_order_acquire)) return;   // a stop that began during this tick: no new body (stop re-checks after the drain)
        try {ok=engine_.create && engine_.create(side,at,handle);}
        catch(const HandBodyEngineFault& f) {
            if(f.handle!=0xffffffffu) {life_.created(side,world,f.handle);s.handle=f.handle;s.world=world;s.gen=last_gen_;}   // tracked: its one destroy comes next tick
            latch(side,f.what(),true);return;
        }
        if(!ok) {count(side,&Counters::create_refusals);s.next_create_ms=now_ms+create_retry_ms;return;}
        life_.created(side,world,handle);s.handle=handle;s.world=world;s.gen=last_gen_;s.attempts=0;s.pending_destroy=false;s.woken=false;s.wake_tries=0;
        count(side,&Counters::creates);
        // The one wake write, right after the creation (B11 run 1: createBody's activation is forced to 0 and the body hangs asleep).
        // Refused (busy at the re-read): retried on the next idle ticks, at most max_wake_tries.
        try {
            ++s.wake_tries;
            if(engine_.velocity && engine_.velocity(side,handle,Vec3{0,0,-R1Recipe::wake_speed})) {s.woken=true;count(side,&Counters::wake_writes);}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);}
        catch(const HandBodyUnreadable&) {}
    }
    void wake(unsigned side) {
        Side& s=side_[side];++s.wake_tries;
        try {
            if(engine_.velocity && engine_.velocity(side,s.handle,Vec3{0,0,-R1Recipe::wake_speed})) {s.woken=true;count(side,&Counters::wake_writes);}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);}
        catch(const HandBodyUnreadable&) {}
    }
    void do_destroy(unsigned side,std::uintptr_t world,bool busy) {
        Side& s=side_[side];
        if(busy) {s.pending_destroy=true;count(side,&Counters::busy_waits);return;}   // never a direct call while busy
        s.pending_destroy=false;
        if(world!=s.world) {   // the lifecycle abandons on a world change first; never destroy in another world
            count(side,&Counters::abandons);if(engine_.forget) engine_.forget(side);finish(side);return;
        }
        DestroyOutcome r=DestroyOutcome::refused;
        try {r=engine_.destroy?engine_.destroy(side,s.handle):DestroyOutcome::refused;}
        catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);finish(side);return;}   // final: never retried; the engine keeps it excluded
        switch(r) {
        case DestroyOutcome::gone:
            count(side,&Counters::destroys);count(side,&Counters::direct_destroys);finish(side);return;
        case DestroyOutcome::absent:
            count(side,&Counters::lost);if(engine_.forget) engine_.forget(side);finish(side);return;
        case DestroyOutcome::unknown:   // the call returned cleanly; the slot could not be confirmed empty: never reused (runtime), counted
            count(side,&Counters::destroys);count(side,&Counters::direct_destroys);count(side,&Counters::unverified);finish(side);return;
        case DestroyOutcome::still_present:
            latch(side,"destroyBodies returned but the body is still there",true);finish(side);return;
        case DestroyOutcome::refused:
            if(++s.attempts>=max_destroy_retries) {latch(side,"destroy refused repeatedly",true);finish(side);return;}
            s.pending_destroy=true;return;
        }
    }
    void finish(unsigned side) {Side& s=side_[side];life_.destroyed(side);s.handle=0;s.attempts=0;s.pending_destroy=false;}
    void readback(unsigned side,std::uintptr_t world) {
        const Side& s=side_[side];
        if(life_.state(side)!=BodyState::ready || !life_.can_write(side,world,s.handle) || latched_) {publish_pose(side,std::nullopt);return;}
        std::optional<Vec3> p;
        try {p=engine_.position?engine_.position(side,s.handle):std::nullopt;}
        catch(const HandBodyUnreadable&) {return;}   // keep the last pose for one unreadable tick
        catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);publish_pose(side,std::nullopt);return;}
        if(!p) {   // the record no longer holds our body in this world: gone without us (no engine call)
            count(side,&Counters::lost);if(engine_.forget) engine_.forget(side);finish(side);publish_pose(side,std::nullopt);return;
        }
        if(!finite(*p)) {publish_pose(side,std::nullopt);return;}   // garbage never reaches a reader (B14 latches on it)
        WorldTrackedPose pose;pose.position=*p;pose.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};   // a sphere: the position is the pose
        publish_pose(side,pose);
    }
    void publish_pose(unsigned side,const std::optional<WorldTrackedPose>& pose) {poses_[side].publish(pose);}
    void publish_state(unsigned side) {
        counters_[side].state.store(static_cast<int>(life_.state(side)),std::memory_order_release);
        counters_[side].handle.store(side_[side].handle,std::memory_order_release);
    }
    HandBodyEngine engine_;bool enabled_;
    HandBodyLifecycle life_;std::array<Side,sides> side_{};std::array<Counters,sides> counters_{};std::array<HandBodyPoseSlot,sides> poses_{};
    std::mutex tick_mutex_;bool latched_{};std::atomic<bool> latched_flag_{},shutdown_{};
    std::uintptr_t last_world_{};std::uint32_t last_gen_{};
    mutable std::mutex why_mutex_;std::string why_;
    std::atomic<std::uint64_t> ticks_{},unreadable_ticks_{},reentries_{};
};

// The 0x89fb0 hook's entry into the production hand body (native_grab.hpp calls it after the original dispatcher when it is set). nullptr
// unless build/hand-body installed the runtime: the knob-off cost is one relaxed load.
inline std::atomic<void (*)(void* manager)> hand_body_dispatch{nullptr};

} // namespace d2vr
