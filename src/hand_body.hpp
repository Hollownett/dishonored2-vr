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
#include "hand_servo.hpp"
#include "tracked_pose.hpp"
#include <array>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
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
// B16: the only filter words a hand body may be given (type 0x1a, setBodyCollisionFilterInfo): its own DEBRIS layer and a collides-with mask
// inside the R1 mask that keeps every world layer (0, 1, 2, 5). Never a player layer (6, 7, 8, 18): the mutual test 0x992c0 then never pairs
// the hand with the player whatever word it carries.
inline constexpr bool hand_filter_word_allowed(std::uint32_t word) {
    constexpr std::uint32_t r1=hand_body::word_mask(R1Recipe::filter);
    constexpr std::uint32_t world=hand_body::layer_bit(hand_body::layer_world_low)|hand_body::layer_bit(hand_body::layer_world_low_high)|
        hand_body::layer_bit(hand_body::layer_world_high)|hand_body::layer_bit(hand_body::layer_mover);
    return hand_body::word_layer(word)==hand_body::layer_debris && (hand_body::word_mask(word)&~r1)==0 && (hand_body::word_mask(word)&world)==world;
}
static_assert(hand_filter_word_allowed(R1Recipe::filter) && !hand_filter_word_allowed(hand_body::hand_filter_word(true)),"the DEBRIS bit stays out");

// ---- B14: the drive (Bonelab §2.1: the hand is a physical body pulled toward the controller by capped forces, with braking; §2.2: it lags
// when blocked). B9's servo_step gives the capped velocity; one B8 type-9 velocity record per side per dispatcher tick carries it.
struct HandDriveConfig {
    double gravity=9.81;            // the R1 body keeps the dynamic preset's gravity factor 1 (Q4): the drive adds g*dt upward (Q4 option 3)
    double max_write_speed=10;      // = HandServoConfig::max_speed; the write never exceeds kHandWriteMaxSpeed (native guard)
    double max_write_spin=20;       // = HandServoConfig::max_spin
    double contact_shortfall=.25;   // research §2.5 (contact without events): achieved short of commanded by more than 25%
    unsigned contact_ticks=2;       // B15: short of the command this many ticks in a row is `contact_now` (the visible hand then follows the body)
    double contact_min_speed=.1;    // commands slower than this are never judged (rest noise, the gravity term)
    double far_snap_gap=1.;         // a gap this large (Blink, a teleport, a long yield) snaps at once when the target is free: never fly there
    double garbage_speed=100;       // read-back speed or spin above this is garbage (native_motion's own bound)
    double garbage_position=1e6;    // |position| above this is garbage
    double capsule_radius=.4;       // "pressed into the capsule": the body within this horizontal distance of the player root (capsule + hand radius)
    double capsule_height=1.95;     // ... and between the root (the feet) and this height above it
    double root_jump=.2;            // the root moving more than this in one drive tick is Blink / a teleport / a platform, not a push: new window
    unsigned max_unreadable_motion=90;   // this many drives in a row with an unreadable motion (a bad motion index) latch HandBodyFault (~1 s)
};
// The native velocity guard (hand_body_native.hpp) refuses anything above these; the drive stays under them.
inline constexpr double kHandWriteMaxSpeed=10.5,kHandWriteMaxSpin=20.5;
// The raw read-back of a body (record +0x30 translation; hknpMotion +0x40 velocity, +0x50 spin). Unfiltered: the manager judges garbage.
struct BodyMotion {Vec3 position{},velocity{},spin{};};
// yield: an authored animation owns the arm, the clamp yields (carry, physical hold), or tracking is lost: the body holds where it is.
// target_free: the hand clamp's last palm cast found the target free (the snap and every create need it). player_root / locomotion: for
// player_drift_um (the player's root moving with no locomotion input while this hand's body is pressed into the capsule).
// holding (B16 fix round 2): this hand holds something; B14's stuck snap and far snap are suppressed (the hold's lag release owns that case) and
// resume after the release.
struct DriveInput {unsigned side{};WorldTrackedPose target{};double held_kg{};double now{};bool yield{};
                   bool target_free{};std::optional<Vec3> player_root{};bool locomotion{};bool holding{};};
struct DriveStatus {double gap_m{};double speed{};bool capped{},contact_now{};std::uint64_t contact_frames{},snaps{},cost_us{},cost_max_us{},player_drift_um{};
                    double dt_s{};std::uint64_t drives{},drive_writes{},yields{},skipped{},snap_requests{},snap_blocked{},garbage{},second_drives{},root_jumps{},pressed_ticks{};
                    // fix round 1: thread CPU time of this side's drive (QueryThreadCycleTime; 0 when unavailable), and the positive control for
                    // the drift proof: ticks where the root moved under locomotion input, and the largest such root travel in one locomotion window
                    std::uint64_t cpu_us{},cpu_max_us{},root_moved_ticks{},root_travel_um{};
                    std::uint64_t held_ticks{};};   // B16: drive ticks while holding (snaps suppressed)
// Fix round 1 (proof d, controller ruling): per-tick drive cost (both hands summed) as wall time (QPC) and thread CPU time, a fixed wall
// histogram, the ticks over budget, and the phase split of the slowest tick (read-back, servo, write, the rest). hist edges in us:
// [0,10) [10,20) [20,50) [50,100) [100,150) [150,200) [200,300) [300,500) [500,1000) [1000,inf).
inline constexpr std::array<std::uint64_t,9> kDriveHistEdgesUs{{10,20,50,100,150,200,300,500,1000}};
struct DriveTiming {
    bool cpu_available{};double cycles_per_us{};
    std::uint64_t ticks{},over_wall{},over_cpu{},wall_max_us{},cpu_max_us{};
    std::array<std::uint64_t,kDriveHistEdgesUs.size()+1> hist{};
    std::uint64_t slow_wall_us{},slow_cpu_us{},slow_read_us{},slow_servo_us{},slow_write_us{},slow_other_us{};
};
inline constexpr std::uint64_t kDriveBudgetUs=200;

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
    std::function<bool(unsigned side,std::uintptr_t world,Vec3 at,std::uint32_t& handle)> create;   // R1 createBody in THIS tick's world; throws HandBodyEngineFault
    std::function<bool(unsigned side,std::uint32_t handle,Vec3 linear)> velocity;     // 0xc85dc0 (the wake write); throws HandBodyEngineFault
    std::function<DestroyOutcome(unsigned side,std::uint32_t handle)> destroy;        // direct 0xc8f6b0; throws HandBodyEngineFault
    std::function<std::optional<Vec3>(unsigned side,std::uint32_t handle)> position;  // nullopt: the record no longer holds our body; throws HandBodyUnreadable
    std::function<bool(unsigned side,const hand_body::CommandRecord&)> apply;         // a write; throws HandBodyEngineFault
    std::function<void(unsigned side)> forget;                                        // abandon / lost: mod-side bookkeeping only, NEVER an engine call
    std::function<bool()> at_g1_point;                                                // write() is allowed only here (the dispatcher's G1 tick); absent: allowed
    std::function<std::optional<BodyMotion>(unsigned side,std::uint32_t handle)> motion;  // B14 read-back; nullopt: not our body; throws HandBodyUnreadable
    std::function<std::optional<std::uint32_t>(unsigned side,std::uint32_t handle)> filter;  // B16: the body's filter word as the engine holds it; nullopt: unreadable
    std::function<std::uint64_t()> now_us;                                            // B14 cost clock (microseconds); absent: costs read 0
    std::function<std::optional<std::uint64_t>()> cpu_cycles;                         // fix round 1: this thread's CPU cycles (QueryThreadCycleTime); nullopt: unavailable
    double cycles_per_us{};                                                           // the calibration (cycles per microsecond); <= 0: CPU time unavailable
};

// Gate state the runtime passes each tick (defaults: open, for the fakes).
struct HandBodyGate {bool g1_pass{true};bool violated{};const char* why{""};};

struct HandBodyStatus {
    bool available{};BodyState state{};std::uint32_t handle{};std::uint64_t creates{},destroys{},faults{},refused_writes{};
    std::uint64_t abandons{},queued_destroys{},direct_destroys{},busy_waits{},wake_writes{},create_refusals{},writes{},releases{},lost{},unverified{};
    std::uint64_t misplaced_writes{},reentrant_writes{};
    std::uint32_t filter{};std::uint64_t filter_writes{},filter_refused{};   // B16: the body's filter word as last written (R1 default at create)
    std::uint64_t filter_stranded{};   // B16: a body we stopped tracking (abandoned, destroy unconfirmed or faulted) may still carry a stripped word
    std::uint64_t filter_readback_mismatches{};   // B16 fix round 2: filter writes the engine did not read back as written (retried, then the latch)
};

// The last read-back pose for the render thread: the dispatcher publishes with try_lock (never waits), readers copy under the lock.
class HandBodyPoseSlot {
public:
    bool publish(const std::optional<WorldTrackedPose>& pose) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {skipped_.fetch_add(1,std::memory_order_relaxed);return false;}
        pose_=pose;++seq_;return true;
    }
    std::optional<WorldTrackedPose> read() const {std::lock_guard<std::mutex> lock(mutex_);return pose_;}
    // B15: the pose with its publication serial (bumps on every accepted publish, one per game frame while the body is alive): the render
    // thread judges a stale pose (unchanged for more than two displayed frames) from it.
    std::pair<std::optional<WorldTrackedPose>,std::uint64_t> read_stamped() const {std::lock_guard<std::mutex> lock(mutex_);return {pose_,seq_};}
    std::uint64_t skipped() const {return skipped_.load(std::memory_order_relaxed);}
private:
    mutable std::mutex mutex_;std::optional<WorldTrackedPose> pose_;std::uint64_t seq_{};std::atomic<std::uint64_t> skipped_{};
};

class HandBody {
public:
    static constexpr unsigned sides=2;
    static constexpr std::uint64_t create_retry_ms=1000;   // after a refused create, wait before the next attempt (owner resolution is not free)
    static constexpr unsigned max_destroy_retries=240;     // a queued destroy refused this many times latches the fault
    static constexpr unsigned max_wake_tries=30;           // a refused wake write (busy, unreadable) is retried on the next ticks, this many times
    static constexpr unsigned max_filter_misses=5;         // B16 fix round 2: a filter word not read back as written this many ticks in a row latches
    explicit HandBody(HandBodyEngine engine,bool enabled=true):engine_(std::move(engine)),enabled_(enabled) {}

    // Dispatcher tick (the G1 point, idle or not): run the lifecycle per side, then create / destroy / drain the queue / read back.
    // spawn: where a new body is created (the calibrated palm, world space); no spawn, no create. gate: the runtime's G1/G2 state.
    void service(const std::array<LifecycleInput,sides>& per_side,const std::array<std::optional<Vec3>,sides>& spawn={},const HandBodyGate& gate={}) {
        const TickGuard guard(ticking_);   // an atomic flag, not a mutex: a re-entry from the same thread is a refusal, never UB
        if(!guard.owned) {reentries_.fetch_add(1,std::memory_order_relaxed);return;}
        ticks_.fetch_add(1,std::memory_order_relaxed);
        ++tick_index_;file_tick_timing();   // B14: one velocity write per side per tick; the drive cost is summed over the tick's two drives
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
            Side& s=side_[side];
            // Review rounds 1-2: the load generation bumps on loading EXIT (runtime). Loading START is a same-world loss of gameplay (logic != 2,
            // the generation unchanged) and destroys through the lifecycle, directly or queued. ANY generation change with a body alive or
            // destroying is an abandon (no engine call, a pending destroy cancelled): a world rebuilt at the same address allocates handles
            // deterministically, so the old full handle may name another body, which own_state cannot tell from ours.
            const BodyAction action=life_.update(side,li);
            switch(action) {
            case BodyAction::abandon:            // the world changed under a live body: no engine call at all, counted (last resort)
                s.pending_destroy=false;s.handle=0;s.attempts=0;note_filter_unconfirmed(side);reset_filter(side);
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
                else if(s.snap_pending && life_.state(side)==BodyState::ready) do_snap(side,world,busy,spawn[side],li.now_ms);
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
    std::pair<std::optional<WorldTrackedPose>,std::uint64_t> pose_stamped(unsigned side) const {
        return side<sides?poses_[side].read_stamped():std::pair<std::optional<WorldTrackedPose>,std::uint64_t>{std::nullopt,0};}
    // Dispatcher thread only. Refused (counted) unless the lifecycle allows writes to this body in this world and load generation, physics
    // is idle and the record names the body's own full handle.
    // Called outside the dispatcher's G1 tick, or from inside service() (an engine callback): refused and counted.
    bool write(unsigned side,const hand_body::CommandRecord& record) {
        if(side>=sides) return false;
        bool at_point=true;
        try {at_point=!engine_.at_g1_point || engine_.at_g1_point();} catch(...) {at_point=false;}
        if(!at_point) {count(side,&Counters::misplaced_writes);count(side,&Counters::refused_writes);return false;}
        const TickGuard guard(ticking_);
        if(!guard.owned) {count(side,&Counters::reentrant_writes);count(side,&Counters::refused_writes);return false;}
        std::uint32_t handle{};
        if(record.size<0x10 || record.size>record.bytes.size()) {count(side,&Counters::refused_writes);return false;}
        std::memcpy(&handle,record.bytes.data()+8,sizeof handle);
        // The allow-list (B13: velocity; B16: the filter word): a type-9 velocity record of its own size, or a type-0x1a filter record of its own
        // size carrying an allowed hand word. Anything else is refused before any read of the engine.
        std::uint16_t type{};std::memcpy(&type,record.bytes.data()+4,sizeof type);
        std::uint32_t word{};std::memcpy(&word,record.bytes.data()+0xc,sizeof word);
        const bool velocity_record=type==9 && record.size==0x30,filter_record=type==0x1a && record.size==0x20;
        if(!velocity_record && !filter_record) {count(side,&Counters::refused_writes);return false;}
        if(filter_record && !hand_filter_word_allowed(word)) {count(side,&Counters::filter_refused);count(side,&Counters::refused_writes);return false;}
        std::uintptr_t world{};
        try {world=engine_.world?engine_.world():0;} catch(...) {world=0;}
        bool busy=true;
        try {busy=engine_.busy?engine_.busy():false;} catch(...) {busy=true;}
        Side& s=side_[side];
        // B14: one velocity write per side per tick; B16: one filter write per side per tick, apart (a filter word never takes the drive's slot)
        std::uint64_t& slot=filter_record?s.filter_tick:s.written_tick;
        if(latched_ || !enabled_ || !world || world!=last_world_ || last_gen_!=s.gen || busy || s.pending_destroy || !life_.can_write(side,world,handle) ||
           !engine_.apply || slot==tick_index_) {count(side,&Counters::refused_writes);if(filter_record) count(side,&Counters::filter_refused);return false;}
        slot=tick_index_;
        try {
            if(!engine_.apply(side,record)) {count(side,&Counters::refused_writes);if(filter_record) count(side,&Counters::filter_refused);return false;}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);return false;}
        count(side,&Counters::writes);
        if(filter_record) {   // fix round 2 ruling: the word is what the engine reads back; a miss is retried (<= max_filter_misses ticks), then latched
            count(side,&Counters::filter_writes);
            std::optional<std::uint32_t> got=word;
            if(engine_.filter) {try {got=engine_.filter(side,handle);} catch(...) {got.reset();}}
            if(got && *got==word) {s.filter=word;s.filter_misses=0;counters_[side].filter.store(word,std::memory_order_release);return true;}
            count(side,&Counters::filter_readback_mismatches);
            if(got) {s.filter=*got;counters_[side].filter.store(*got,std::memory_order_release);}   // the status shows the word the body really carries
            if(++s.filter_misses>=max_filter_misses) latch(side,"filter word not read back as written",true);   // destroyed next tick; HandBodyFault
            return false;
        }
        return true;
    }
    // B16: give this side's body `word` (an allowed hand word) through write(): dispatcher thread, at the G1 point, after service(). false:
    // no body, already that word, or refused (counted); the caller asks again on a later tick.
    bool write_filter(unsigned side,std::uint32_t word) {
        if(side>=sides || life_.state(side)!=BodyState::ready || side_[side].filter==word) return false;
        return write(side,hand_body::encode_set_filter(side_[side].handle,word));
    }
    // The word this side's body carries as far as the manager knows (the R1 default from its creation until a filter write lands). Any thread.
    std::uint32_t filter_word(unsigned side) const {return side<sides?counters_[side].filter.load(std::memory_order_acquire):R1Recipe::filter;}
    // B14, dispatcher thread, after service() in the same G1 tick (the runtime's tick runs once per game frame): read the body back
    // (position, velocity, spin), run B9's servo_step on the real interval since this side's last drive, and write one velocity record.
    // At most one velocity write per side per service tick (a wake write that tick takes it). Garbage read-back latches HandBodyFault (no
    // further write; the next service tick destroys the body once). yield: zero velocity plus the gravity term (the body holds). A snap
    // request (servo: stuck; or the gap beyond far_snap_gap) is executed by the next idle service tick, only while the target is free.
    void drive(const DriveInput& in) {
        const unsigned side=in.side;
        if(side>=sides) return;
        bool at_point=true;
        try {at_point=!engine_.at_g1_point || engine_.at_g1_point();} catch(...) {at_point=false;}
        if(!at_point) {count(side,&Counters::misplaced_writes);return;}
        const TickGuard guard(ticking_);
        if(!guard.owned) {count(side,&Counters::reentrant_writes);return;}
        const std::uint64_t t0=clock_us();const auto c0=cpu_now();
        struct Cost {HandBody& hb;unsigned side;std::uint64_t t0;std::optional<std::uint64_t> c0;std::uint64_t read{},servo{},write{};   // phase end marks (0: not reached)
            ~Cost() {hb.note_cost(side,t0,c0,read,servo,write);}} cost{*this,side,t0,c0};
        count(side,&Counters::drives);
        Side& s=side_[side];
        const bool had_cmd=s.has_cmd;s.has_cmd=false;   // review P3-3: a command older than the last drive is never compared (metric only)
        if(latched_ || !enabled_ || life_.state(side)!=BodyState::ready || s.pending_destroy) {count(side,&Counters::skipped);return;}
        std::uintptr_t world{};
        try {world=engine_.world?engine_.world():0;} catch(...) {world=0;}
        bool busy=true;
        try {busy=engine_.busy?engine_.busy():false;} catch(...) {busy=true;}
        if(!world || world!=last_world_ || last_gen_!=s.gen || busy || !life_.can_write(side,world,s.handle) || !engine_.motion || !engine_.apply) {
            count(side,&Counters::skipped);return;
        }
        const bool second=s.drive_tick==tick_index_;s.drive_tick=tick_index_;
        if(second) count(side,&Counters::second_drives);   // every further drive call in one tick (the hook runs 7-11k/s)
        if(s.written_tick==tick_index_) {   // this tick's write is taken (the wake, or an earlier drive): none here
            if(!second && std::isfinite(in.now)) s.last_drive_s=in.now;   // the wake write is the last command: the next dt runs from here
            count(side,&Counters::skipped);return;
        }
        std::optional<BodyMotion> m;
        try {m=engine_.motion(side,s.handle);s.unreadable_motion=0;}
        catch(const HandBodyUnreadable&) {   // a page gone for a moment: no write this tick; a motion that never reads (bad index) latches
            count(side,&Counters::skipped);
            if(++s.unreadable_motion>=drive_cfg_.max_unreadable_motion) latch(side,"motion read-back unreadable for 1 s",true);
            return;
        }
        catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);return;}
        cost.read=clock_us();
        if(!m) {count(side,&Counters::skipped);return;}                                      // not our body: service()'s read-back handles it
        const auto& dc=drive_cfg_;
        if(!finite(m->position) || !finite(m->velocity) || !finite(m->spin) || length(m->position)>dc.garbage_position ||
           length(m->velocity)>dc.garbage_speed || length(m->spin)>dc.garbage_speed) {   // Review Focus 3: never reaches the servo or a record
            count(side,&Counters::garbage);latch(side,"read-back garbage (position/velocity/spin)",true);publish_pose(side,std::nullopt);return;
        }
        auto& c=counters_[side];
        const double gap=length(in.target.position-m->position);
        c.gap_m.store(std::isfinite(gap)?gap:-1,std::memory_order_relaxed);c.speed.store(length(m->velocity),std::memory_order_relaxed);
        // The real interval since this side's last drive (not the dispatcher's 7-11k/s interval): the first drive after a create only starts it.
        const double dt=in.now-s.last_drive_s;
        if(std::isfinite(in.now)) s.last_drive_s=in.now;
        if(!std::isfinite(dt) || !(dt>0)) {count(side,&Counters::skipped);return;}
        c.dt_s.store(dt,std::memory_order_relaxed);
        // contact without events (research §2.5): last tick's command achieved short by more than 25% along its own direction
        if(had_cmd) {
            const double want=length(s.last_cmd);   // B15: a cap-limited command (c.capped) is not judged
            if(!c.capped.load(std::memory_order_relaxed) && want>=dc.contact_min_speed && dot(m->velocity,s.last_cmd)/want<(1-dc.contact_shortfall)*want) {count(side,&Counters::contact_frames);++s.short_streak;}
            else s.short_streak=0;
        }
        else s.short_streak=0;
        c.contact_now.store(s.short_streak>=dc.contact_ticks,std::memory_order_relaxed);c.contact_ms.store(steady_ms(),std::memory_order_relaxed);   // B15: short of the command for two ticks in a row
        note_drift(side,in,m->position);
        Vec3 linear{},angular{};bool yielded=in.yield || !hand_servo_detail::finite_pose(in.target);
        if(yielded) {s.servo={};count(side,&Counters::yields);c.capped.store(false,std::memory_order_relaxed);}
        else {
            WorldTrackedPose body;body.position=m->position;body.basis=in.target.basis;   // a sphere: no orientation to servo, the spin only brakes
            // B16 fix round 2: while holding, the stuck timer never runs (target_blocked resets it) and no snap is asked for: the hold's lag
            // release owns a stuck hand; the rules resume once the hold ends
            const auto out=servo_step(s.servo,in.target,body,m->velocity,m->spin,in.held_kg,dt,std::numeric_limits<double>::quiet_NaN(),servo_cfg_,tuning_,!in.target_free || in.holding);
            c.capped.store(out.capped,std::memory_order_relaxed);
            if(out.valid) {linear=out.linear;angular=out.angular;}   // invalid (a gap past max_gap, a bad target): hold, and the far gap snaps
            if(in.holding) {s.snap_pending=false;count(side,&Counters::held_ticks);}
            else if(out.snap || (std::isfinite(gap) && gap>dc.far_snap_gap)) {
                if(in.target_free) {if(!s.snap_pending) count(side,&Counters::snap_requests);s.snap_pending=true;}
                else count(side,&Counters::snap_blocked);
            }
        }
        linear=hand_servo_detail::limited(linear,dc.max_write_speed);angular=hand_servo_detail::limited(angular,dc.max_write_spin);
        s.last_cmd=linear;
        // gravity compensation (Q4 option 3): the step takes g*dt off, the write adds it back (dt as the servo uses it)
        const Vec3 written=linear+Vec3{0,0,dc.gravity*std::min(dt,tuning_.dt_max)};
        if(!finite(written) || !finite(angular) || length(written)>kHandWriteMaxSpeed || length(angular)>kHandWriteMaxSpin) {
            count(side,&Counters::garbage);latch(side,"drive produced a non-finite or oversized command",true);return;
        }
        s.written_tick=tick_index_;   // attempted: never a second write this tick, whatever apply() says
        cost.servo=clock_us();
        bool ok=false;
        try {ok=engine_.apply(side,hand_body::encode_set_velocity(s.handle,written,angular));cost.write=clock_us();}
        catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);return;}
        catch(const std::invalid_argument&) {count(side,&Counters::garbage);latch(side,"drive record refused (non-finite)",true);return;}
        if(!ok) {count(side,&Counters::refused_writes);s.has_cmd=false;return;}
        s.has_cmd=!yielded;
        s.woken=true;count(side,&Counters::writes);count(side,&Counters::drive_writes);
    }
    DriveStatus drive_status(unsigned side) const {
        DriveStatus d;if(side>=sides) return d;
        const auto& c=counters_[side];
        d.gap_m=c.gap_m.load();d.speed=c.speed.load();d.capped=c.capped.load();d.contact_now=c.contact_now.load() && steady_ms()-c.contact_ms.load()<=contact_fresh_ms;d.contact_frames=c.contact_frames.load();d.snaps=c.snaps.load();
        d.cost_us=c.cost_us.load();d.cost_max_us=c.cost_max_us.load();d.player_drift_um=c.player_drift_um.load();d.dt_s=c.dt_s.load();
        d.drives=c.drives.load();d.drive_writes=c.drive_writes.load();d.yields=c.yields.load();d.skipped=c.skipped.load();
        d.snap_requests=c.snap_requests.load();d.snap_blocked=c.snap_blocked.load();d.garbage=c.garbage.load();d.second_drives=c.second_drives.load();d.root_jumps=c.root_jumps.load();d.pressed_ticks=c.pressed_ticks.load();
        d.cpu_us=c.cpu_us.load();d.cpu_max_us=c.cpu_max_us.load();d.root_moved_ticks=c.root_moved_ticks.load();d.root_travel_um=c.root_travel_um.load();d.held_ticks=c.held_ticks.load();
        return d;
    }
    // The largest drive cost of one tick, both hands summed (proof (d): <= 200 us), and the last tick's sum.
    std::uint64_t drive_cost_max_us() const {return drive_cost_max_us_.load();}
    std::uint64_t drive_cost_us() const {return drive_cost_us_.load();}
    DriveTiming drive_timing() const {
        DriveTiming t;t.cpu_available=cpu_available();t.cycles_per_us=engine_.cycles_per_us;
        t.ticks=timing_ticks_.load();t.over_wall=over_wall_.load();t.over_cpu=over_cpu_.load();t.wall_max_us=drive_cost_max_us_.load();t.cpu_max_us=cpu_tick_max_us_.load();
        for(std::size_t i=0;i<t.hist.size();++i) t.hist[i]=hist_[i].load();
        t.slow_wall_us=slow_[0].load();t.slow_cpu_us=slow_[1].load();t.slow_read_us=slow_[2].load();t.slow_servo_us=slow_[3].load();t.slow_write_us=slow_[4].load();t.slow_other_us=slow_[5].load();
        return t;
    }
    // The tick sums are filed into the histogram at the next service() (both drives are in by then); this files the last one (tests, stop).
    void file_drive_tick() {const TickGuard guard(ticking_);if(guard.owned) file_tick_timing();}
    HandBodyStatus status(unsigned side) const {
        HandBodyStatus st;if(side>=sides) return st;
        const auto& c=counters_[side];
        st.available=enabled_ && !latched_flag_.load(std::memory_order_acquire) && !HandBodyFault::tripped();
        st.state=static_cast<BodyState>(c.state.load(std::memory_order_acquire));st.handle=c.handle.load(std::memory_order_acquire);
        st.creates=c.creates.load();st.destroys=c.destroys.load();st.faults=c.faults.load();st.refused_writes=c.refused_writes.load();
        st.abandons=c.abandons.load();st.queued_destroys=c.queued_destroys.load();st.direct_destroys=c.direct_destroys.load();
        st.busy_waits=c.busy_waits.load();st.wake_writes=c.wake_writes.load();st.create_refusals=c.create_refusals.load();
        st.writes=c.writes.load();st.releases=c.releases.load();st.lost=c.lost.load();st.unverified=c.unverified.load();
        st.misplaced_writes=c.misplaced_writes.load();st.reentrant_writes=c.reentrant_writes.load();
        st.filter=c.filter.load(std::memory_order_acquire);st.filter_writes=c.filter_writes.load();st.filter_refused=c.filter_refused.load();st.filter_stranded=c.filter_stranded.load();st.filter_readback_mismatches=c.filter_readback_mismatches.load();
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
            wake_writes{},create_refusals{},writes{},releases{},lost{},unverified{},misplaced_writes{},reentrant_writes{},filter_writes{},filter_refused{},filter_stranded{},filter_readback_mismatches{};
        std::atomic<std::uint32_t> filter{R1Recipe::filter};   // B16: the body's filter word as last written (the R1 default from each creation)
        std::atomic<std::uint64_t> contact_frames{},snaps{},cost_us{},cost_max_us{},player_drift_um{},drives{},drive_writes{},yields{},skipped{},
            snap_requests{},snap_blocked{},garbage{},second_drives{},root_jumps{},pressed_ticks{},cpu_us{},cpu_max_us{},root_moved_ticks{},root_travel_um{},held_ticks{};
        std::atomic<double> gap_m{},speed{},dt_s{};std::atomic<bool> capped{},contact_now{};std::atomic<std::uint64_t> contact_ms{};
        std::atomic<int> state{};std::atomic<std::uint32_t> handle{};
    };
    struct TickGuard {
        std::atomic<bool>& flag;bool owned;
        explicit TickGuard(std::atomic<bool>& f):flag(f),owned(!f.exchange(true,std::memory_order_acq_rel)) {}
        ~TickGuard() {if(owned) flag.store(false,std::memory_order_release);}
        TickGuard(const TickGuard&)=delete;TickGuard& operator=(const TickGuard&)=delete;
    };
    static constexpr std::uint64_t contact_fresh_ms=50;   // contact_now older than this (the drive stopped judging) reads false
    static std::uint64_t steady_ms() {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    struct Side {std::uint32_t handle{},gen{};std::uintptr_t world{};bool pending_destroy{},woken{};unsigned attempts{},wake_tries{};std::uint64_t next_create_ms{};
        std::uint32_t filter{R1Recipe::filter};std::uint64_t filter_tick{~0ull};unsigned filter_misses{};   // B16: the word this body carries; one filter write per tick
        // B14 drive state (dispatcher thread only)
        HandServoState servo{};double last_drive_s{std::numeric_limits<double>::quiet_NaN()};std::uint64_t written_tick{~0ull};
        std::uint64_t drive_tick{~0ull};bool snap_pending{},has_cmd{};Vec3 last_cmd{};std::optional<Vec3> press_root,last_root,loco_start,loco_last;unsigned unreadable_motion{},short_streak{};};
    void count(unsigned side,std::atomic<std::uint64_t> Counters::* field) {(counters_[side].*field).fetch_add(1,std::memory_order_relaxed);}
    std::uint64_t clock_us() const {try {return engine_.now_us?engine_.now_us():0;} catch(...) {return 0;}}
    // CPU time is available only with a calibration and while every read succeeds (one failed read: unavailable for the session)
    bool cpu_available() const {return engine_.cpu_cycles && engine_.cycles_per_us>0 && !cpu_failed_.load(std::memory_order_relaxed);}
    std::optional<std::uint64_t> cpu_now() {
        if(!cpu_available()) return std::nullopt;
        std::optional<std::uint64_t> c;
        try {c=engine_.cpu_cycles();} catch(...) {c.reset();}
        if(!c) cpu_failed_.store(true,std::memory_order_relaxed);
        return c;
    }
    static void store_max(std::atomic<std::uint64_t>& a,std::uint64_t v) {if(v>a.load(std::memory_order_relaxed)) a.store(v,std::memory_order_relaxed);}
    void note_cost(unsigned side,std::uint64_t t0,const std::optional<std::uint64_t>& c0,std::uint64_t read,std::uint64_t servo,std::uint64_t write) {
        const std::uint64_t t1=clock_us();const std::uint64_t us=t1>=t0?t1-t0:0;
        auto& c=counters_[side];c.cost_us.store(us,std::memory_order_relaxed);store_max(c.cost_max_us,us);
        std::uint64_t cpu_us=0;
        if(c0) if(const auto c1=cpu_now(); c1 && *c1>=*c0) cpu_us=static_cast<std::uint64_t>(static_cast<double>(*c1-*c0)/engine_.cycles_per_us+.5);
        c.cpu_us.store(cpu_us,std::memory_order_relaxed);store_max(c.cpu_max_us,cpu_us);
        // phases of this drive (a mark of 0: that phase was not reached; the time then counts as "other")
        const auto span=[](std::uint64_t a,std::uint64_t b) {return a && b>=a?b-a:0;};
        const std::uint64_t r=read?span(t0,read):0,sv=read && servo?span(read,servo):0,w=servo && write?span(servo,write):0;
        tick_drive_cost_us_+=us;tick_cpu_us_+=cpu_us;tick_read_us_+=r;tick_servo_us_+=sv;tick_write_us_+=w;tick_drove_=true;
        drive_cost_us_.store(tick_drive_cost_us_,std::memory_order_relaxed);
        store_max(cpu_tick_max_us_,tick_cpu_us_);
        if(tick_drive_cost_us_>drive_cost_max_us_.load(std::memory_order_relaxed)) {   // the slowest tick so far (sums of both drives once both are in)
            drive_cost_max_us_.store(tick_drive_cost_us_,std::memory_order_relaxed);
            const std::uint64_t known=tick_read_us_+tick_servo_us_+tick_write_us_;
            slow_[0].store(tick_drive_cost_us_);slow_[1].store(tick_cpu_us_);slow_[2].store(tick_read_us_);slow_[3].store(tick_servo_us_);slow_[4].store(tick_write_us_);
            slow_[5].store(tick_drive_cost_us_>=known?tick_drive_cost_us_-known:0);
        }
    }
    // Called at the start of each service() tick for the previous tick's two drives.
    void file_tick_timing() {
        if(!tick_drove_) return;
        timing_ticks_.fetch_add(1,std::memory_order_relaxed);
        std::size_t bin=0;while(bin<kDriveHistEdgesUs.size() && tick_drive_cost_us_>=kDriveHistEdgesUs[bin]) ++bin;
        hist_[bin].fetch_add(1,std::memory_order_relaxed);
        if(tick_drive_cost_us_>kDriveBudgetUs) over_wall_.fetch_add(1,std::memory_order_relaxed);
        if(cpu_available() && tick_cpu_us_>kDriveBudgetUs) over_cpu_.fetch_add(1,std::memory_order_relaxed);
        tick_drove_=false;tick_drive_cost_us_=tick_cpu_us_=tick_read_us_=tick_servo_us_=tick_write_us_=0;
    }
    // player_drift_um: the largest displacement of the player root inside one window where this hand's body is pressed into the capsule
    // and there is no locomotion input. Reported, never clamped or acted on (the hand never moves the player: the filter word omits 6/7/8/18).
    void note_drift(unsigned side,const DriveInput& in,Vec3 body) {
        Side& s=side_[side];const auto& dc=drive_cfg_;
        // review P2-2 (positive control): the root must be seen to move under locomotion input, or a stale root would also read 0 drift
        if(in.locomotion && in.player_root && finite(*in.player_root)) {
            const Vec3 r=*in.player_root;
            if(s.loco_last && length(r-*s.loco_last)>1e-4 && length(r-*s.loco_last)<=dc.root_jump) count(side,&Counters::root_moved_ticks);
            if(!s.loco_start) s.loco_start=r;
            s.loco_last=r;
            const double um=length(r-*s.loco_start)*1e6;
            if(std::isfinite(um)) store_max(counters_[side].root_travel_um,static_cast<std::uint64_t>(um+.5));
        } else {s.loco_start.reset();s.loco_last.reset();}
        if(!in.player_root || !finite(*in.player_root) || in.locomotion) {s.press_root.reset();s.last_root.reset();return;}
        const Vec3 r=*in.player_root;const Vec3 d=body-r;
        const bool pressed=std::sqrt(d.x*d.x+d.y*d.y)<=dc.capsule_radius && d.z>=-.05 && d.z<=dc.capsule_height;
        if(!pressed) {s.press_root.reset();s.last_root.reset();return;}
        count(side,&Counters::pressed_ticks);   // proof (b) is not vacuous: the body really sat in the capsule with no locomotion input
        if(!s.press_root) {s.press_root=r;s.last_root=r;return;}
        if(s.last_root && length(r-*s.last_root)>dc.root_jump) {count(side,&Counters::root_jumps);s.press_root=r;s.last_root=r;return;}   // Blink, a teleport
        s.last_root=r;
        const double um=length(r-*s.press_root)*1e6;
        auto& c=counters_[side].player_drift_um;
        if(std::isfinite(um) && um>static_cast<double>(c.load(std::memory_order_relaxed))) c.store(static_cast<std::uint64_t>(um+.5),std::memory_order_relaxed);
    }
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
        try {ok=engine_.create && engine_.create(side,world,at,handle);}
        catch(const HandBodyEngineFault& f) {
            if(f.handle!=0xffffffffu) {life_.created(side,world,f.handle);s.handle=f.handle;s.world=world;s.gen=last_gen_;}   // tracked: its one destroy comes next tick
            latch(side,f.what(),true);return;
        }
        if(!ok) {count(side,&Counters::create_refusals);s.next_create_ms=now_ms+create_retry_ms;return;}
        life_.created(side,world,handle);s.handle=handle;s.world=world;s.gen=last_gen_;s.attempts=0;s.pending_destroy=false;s.woken=false;s.wake_tries=0;
        reset_filter(side);   // B16: a new body always starts with the R1 default word (createBody's cinfo carries it)
        reset_drive(side);
        count(side,&Counters::creates);
        // The one wake write, right after the creation (B11 run 1: createBody's activation is forced to 0 and the body hangs asleep).
        // Refused (busy at the re-read): retried on the next idle ticks, at most max_wake_tries.
        try {
            ++s.wake_tries;
            if(engine_.velocity && engine_.velocity(side,handle,Vec3{0,0,-R1Recipe::wake_speed})) {s.woken=true;count(side,&Counters::wake_writes);}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);}
        catch(const HandBodyUnreadable&) {}
        s.written_tick=tick_index_;   // the tick's one velocity write (attempted): the drive skips this tick
    }
    void wake(unsigned side) {
        Side& s=side_[side];++s.wake_tries;
        try {
            if(engine_.velocity && engine_.velocity(side,s.handle,Vec3{0,0,-R1Recipe::wake_speed})) {s.woken=true;count(side,&Counters::wake_writes);}
        } catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);}
        catch(const HandBodyUnreadable&) {}
        s.written_tick=tick_index_;
    }
    // B14 snap (controller ruling): there is no setBodyTransform encoder (B8), so a snap is destroy + recreate at the controller pose, on an
    // idle G1 tick, and only while the clamp's cast shows the target free (spawn is set only then). Busy: wait for the next idle tick. Blocked:
    // dropped (counted), the body keeps servoing and the servo asks again.
    void do_snap(unsigned side,std::uintptr_t world,bool busy,const std::optional<Vec3>& spawn,std::uint64_t now_ms) {
        Side& s=side_[side];
        if(busy) {count(side,&Counters::busy_waits);return;}
        s.snap_pending=false;
        if(!spawn || !finite(*spawn) || latched_ || shutdown_.load(std::memory_order_acquire)) {count(side,&Counters::snap_blocked);return;}
        do_destroy(side,world,false);
        if(life_.state(side)!=BodyState::absent || latched_) return;   // refused (retried as a plain destroy, then the normal create) or faulted
        const auto before=counters_[side].creates.load(std::memory_order_relaxed);
        do_create(side,world,*spawn,now_ms);
        if(counters_[side].creates.load(std::memory_order_relaxed)>before) count(side,&Counters::snaps);
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
        catch(const HandBodyEngineFault& f) {latch(side,f.what(),true);note_filter_unconfirmed(side);finish(side);return;}   // final: never retried; the engine keeps it excluded
        switch(r) {
        case DestroyOutcome::gone:
            count(side,&Counters::destroys);count(side,&Counters::direct_destroys);finish(side);return;
        case DestroyOutcome::absent:
            count(side,&Counters::lost);if(engine_.forget) engine_.forget(side);finish(side);return;
        case DestroyOutcome::unknown:   // the call returned cleanly; the slot could not be confirmed empty: never reused (runtime), counted
            count(side,&Counters::destroys);count(side,&Counters::direct_destroys);count(side,&Counters::unverified);note_filter_unconfirmed(side);finish(side);return;
        case DestroyOutcome::still_present:
            latch(side,"destroyBodies returned but the body is still there",true);note_filter_unconfirmed(side);finish(side);return;
        case DestroyOutcome::refused:
            if(++s.attempts>=max_destroy_retries) {latch(side,"destroy refused repeatedly",true);note_filter_unconfirmed(side);finish(side);return;}
            s.pending_destroy=true;return;
        }
    }
    void finish(unsigned side) {Side& s=side_[side];life_.destroyed(side);s.handle=0;s.attempts=0;s.pending_destroy=false;reset_filter(side);reset_drive(side);}
    void reset_filter(unsigned side) {side_[side].filter=R1Recipe::filter;side_[side].filter_misses=0;counters_[side].filter.store(R1Recipe::filter,std::memory_order_release);}
    // DeepSeek P3: the manager forgets a body that may still exist (abandoned with its world, a destroy it could not confirm): if that body carried a
    // stripped word, say so (status filter_stranded) instead of letting the status read the default.
    void note_filter_unconfirmed(unsigned side) {if(side_[side].filter!=R1Recipe::filter) count(side,&Counters::filter_stranded);}
    void reset_drive(unsigned side) {
        Side& s=side_[side];s.servo={};s.last_drive_s=std::numeric_limits<double>::quiet_NaN();s.snap_pending=false;s.has_cmd=false;s.press_root.reset();s.last_root.reset();s.loco_start.reset();s.loco_last.reset();s.unreadable_motion=0;s.short_streak=0;counters_[side].contact_now.store(false,std::memory_order_relaxed);
    }
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
        if(!finite(*p) || length(*p)>drive_cfg_.garbage_position) {   // B14 (Review Focus 3): garbage latches the hand body off; never published
            count(side,&Counters::garbage);latch(side,"read-back garbage (position)",true);publish_pose(side,std::nullopt);return;
        }
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
    std::atomic<bool> ticking_{};bool latched_{};std::atomic<bool> latched_flag_{},shutdown_{};
    HandServoConfig servo_cfg_{};GrabTuning tuning_{};HandDriveConfig drive_cfg_{};
    std::uint64_t tick_index_{},tick_drive_cost_us_{},tick_cpu_us_{},tick_read_us_{},tick_servo_us_{},tick_write_us_{};bool tick_drove_{};
    std::atomic<std::uint64_t> drive_cost_max_us_{},drive_cost_us_{},cpu_tick_max_us_{},timing_ticks_{},over_wall_{},over_cpu_{};
    std::array<std::atomic<std::uint64_t>,kDriveHistEdgesUs.size()+1> hist_{};std::array<std::atomic<std::uint64_t>,6> slow_{};std::atomic<bool> cpu_failed_{};
    std::uintptr_t last_world_{};std::uint32_t last_gen_{};
    mutable std::mutex why_mutex_;std::string why_;
    std::atomic<std::uint64_t> ticks_{},unreadable_ticks_{},reentries_{};
};

// The 0x89fb0 hook's entry into the production hand body (native_grab.hpp calls it after the original dispatcher when it is set). nullptr
// unless build/hand-body installed the runtime: the knob-off cost is one relaxed load.
inline std::atomic<void (*)(void* manager)> hand_body_dispatch{nullptr};

} // namespace d2vr
