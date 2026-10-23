#pragma once
// Plan B task B11: the isolated hand-body creation spike (dev knob build/hand-body-spike, sim only). Pure part: the engine seam, the gates
// (G1 thread/drain point, G2 owner) and the tick-driven spike itself. The real engine behind the seam is src/hand_body_spike_engine.hpp.
// Bonelab §2.1 makes the hand a physical body; before any of that is built, this spike answers one question: can the mod create a dynamic
// sphere in the hknp world, see it fall onto the floor, push it through the player's capsule without moving the player, and destroy it,
// without a crash? docs/native-hand-body-re.md (Route table, Gates for B11) is the authority for every engine fact used here.
// Routes: R1 (createBody with engine-shaped userdata) is the only creating route. R2 and R3 return false with the B6 reason, no engine call.
// Order of a session: G1 observation (count-only hooks) -> G1 verdict -> owner choice (G2 static part) -> create -> settle -> press -> hold
// -> destroy. A world change abandons (no engine call). A fault latches HandBodyFault and stops.
#include "arm_ik.hpp"
#include "hand_body_commands.hpp"
#include "hand_body_lifecycle.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace d2vr {

enum class SpikeRoute {R1,R2,R3};
inline const char* spike_route_name(SpikeRoute r) {return r==SpikeRoute::R1?"R1":r==SpikeRoute::R2?"R2":"R3";}
// The knob's content, read once: "R1", "R2" or "R3" (surrounding white space ignored). Anything else is refused.
inline std::optional<SpikeRoute> parse_spike_route(const std::string& text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    const auto last=text.find_last_not_of(" \t\r\n");
    const auto word=text.substr(first,last-first+1);
    if(word=="R1") return SpikeRoute::R1;
    if(word=="R2") return SpikeRoute::R2;
    if(word=="R3") return SpikeRoute::R3;
    return {};
}
// Statically impossible routes (docs/native-hand-body-re.md, Route table). nullptr: the route may create.
inline const char* spike_route_refusal(SpikeRoute r) {
    if(r==SpikeRoute::R2) return "R2 blocked (B6): spawned components never start (sim v407/v413), so a spawned prop never gets a body; "
                                 "calling CreatePhysics 0x808ea0 / start 0x809d80 by hand on an unstarted component is unproven and fatal without a PhysicsShape";
    if(r==SpikeRoute::R3) return "R3 dead (B6): createAttachedBody 0xc8e7c0 shares the target body's motion (no own mass, gravity or velocity); "
                                 "no engine motion follows the hand, and record 0x37 is printer-only";
    return nullptr;
}

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

// An engine call faulted (the real engine turns a failed guarded_call into this). The spike latches HandBodyFault and stops. handle: a body the
// faulting call may still have left in the world (a createBody that faulted after inserting its record); it then gets the one destroy attempt.
struct SpikeFault : std::runtime_error {
    explicit SpikeFault(const char* what,std::uint32_t body=0xffffffffu):std::runtime_error(what),handle(body) {}
    std::uint32_t handle;
};
// A read could not be made this tick (an unreadable pool, a page gone for a moment): not "the body is gone". The tick is skipped.
struct SpikeUnreadable {};

// The seam. create/destroy/position/flags/busy are the brief's interface; world, player, push and gameplay are added (the spike needs a
// world identity for the stale-world rule, the player root for the drift, a velocity write for the press, and gameplay for the lifecycle).
// Missing optional functions mean: world 1 (constant), no player, no push, gameplay true.
struct SpikeEngine {
    std::function<bool(SpikeRoute,Vec3 at,double radius,std::uint32_t& handle)> create;
    std::function<bool(std::uint32_t handle)> destroy;
    std::function<std::optional<Vec3>(std::uint32_t handle)> position;
    std::function<std::optional<std::uint32_t>(std::uint32_t handle)> flags;   // nullopt: the pool record no longer holds this handle
    std::function<bool()> busy;
    std::function<std::uintptr_t()> world;
    std::function<std::optional<Vec3>()> player;
    std::function<bool(std::uint32_t handle,Vec3 linear)> push;
    std::function<bool()> gameplay;
};

struct SpikeConfig {
    double radius=.05;            // 5 cm sphere
    double drop_height=.30;       // created this far above the floor
    double settle_min_s=.3,settle_window_s=.5,settle_eps_m=.002,settle_max_s=4;
    double floor_tol_m=.03;       // settle_z within this of floor + radius counts as resting on the floor
    double press_s=5,press_speed=.5;
    double life_s=20;             // the body is destroyed this long after its creation
};

struct SpikeReport {
    SpikeRoute route{};bool created{},collided_with_floor{},destroyed{},faulted{};double settle_z{},player_drift_m{};std::uint64_t ticks{};
    // Extras for the run log.
    bool refused{},abandoned{},destroy_attempted{},settled{},stop_requested{};
    std::uint32_t handle{};std::optional<std::uint32_t> flags_after_destroy;
    double floor_z{std::numeric_limits<double>::quiet_NaN()},created_at_s{-1},destroyed_at_s{-1};
    std::uint64_t busy_waits{},pushes{},create_calls{},destroy_calls{},destroy_refusals{},unreadable_ticks{};
    std::string reason;
};

// One spike, tick-driven: the real engine ticks it once per game frame from the G1-approved point; run_spike ticks it in a loop for fakes.
// Every engine call goes through the B10 lifecycle (side 0): create only when idle, writes only while can_write, abandon on a world change.
class SpikeRunner {
public:
    enum class Stage {create,settle,press,hold,done};
    SpikeRunner(SpikeRoute route,Vec3 at,double floor_z,SpikeConfig config={}):at_(at),config_(config) {report_.route=route;report_.floor_z=floor_z;}
    Stage stage() const {return stage_;}
    bool done() const {return stage_==Stage::done;}
    const SpikeReport& report() const {return report_;}
    // End early: the body is destroyed at the next allowed tick (session end, a gate violated after creation).
    void request_stop(const char* why) {if(!stop_) {stop_=true;report_.stop_requested=true;if(why && report_.reason.empty()) report_.reason=why;}}
    // Returns true while the spike still runs.
    bool tick(SpikeEngine& e,double now_s) {
        if(stage_==Stage::done) return false;
        ++report_.ticks;
        if(const char* refusal=spike_route_refusal(report_.route)) {finish_refused(refusal);return false;}
        if(!finite(at_)) {finish_refused("spawn point not finite");return false;}
        const std::uintptr_t w=e.world?e.world():1;
        const bool busy=e.busy?e.busy():false;
        const bool life_over=report_.created && now_s-report_.created_at_s>=config_.life_s;
        LifecycleInput in;
        in.world=w;in.gameplay=e.gameplay?e.gameplay():true;in.focused=in.tracked=true;in.physics_busy=busy;
        in.enabled=!stop_ && !life_over;in.fault=report_.faulted;in.now_ms=static_cast<std::uint64_t>(std::max(0.,now_s)*1000);
        switch(life_.update(0,in)) {
        case BodyAction::abandon:            // the body died with its world: no engine call at all
            report_.abandoned=true;if(report_.reason.empty()) report_.reason="world changed: abandoned";
            stage_=Stage::done;return false;
        case BodyAction::create:
            if(stage_!=Stage::create) {stage_=Stage::done;return false;}   // single shot: never a second body
            do_create(e,w,now_s);return stage_!=Stage::done;
        case BodyAction::destroy:
            do_destroy(e,w,now_s);return stage_!=Stage::done;   // a refused destroy stays pending
        case BodyAction::destroy_queued:     // physics busy: no direct destroy and no queued destroy record (B8): retry at an idle tick
            pending_destroy_=true;++report_.busy_waits;return true;
        case BodyAction::none:
            break;
        }
        if(pending_destroy_) {if(!busy) do_destroy(e,w,now_s);else ++report_.busy_waits;return stage_!=Stage::done;}
        if(stage_==Stage::create) {
            if(busy) ++report_.busy_waits;
            if(stop_) {finish_refused(report_.reason.empty()?"stopped before creation":report_.reason.c_str());return false;}
            return true;
        }
        if(life_.state(0)!=BodyState::ready) return true;
        advance(e,w,now_s);
        return stage_!=Stage::done;
    }
private:
    void finish_refused(const std::string& why) {report_.refused=true;if(report_.reason.empty()) report_.reason=why;stage_=Stage::done;}
    // A fault latches HandBodyFault and stops the spike. A body that exists is not touched again except by the lifecycle's own destroy on
    // the next tick (in.fault makes it destroy; only when idle, only in its own world, once): no body may outlive the session.
    void fault(const char* what) {
        report_.faulted=true;HandBodyFault::trip();
        if(report_.reason.empty()) report_.reason=what;
        if(!report_.created) stage_=Stage::done;
    }
    void do_create(SpikeEngine& e,std::uintptr_t w,double now_s) {
        std::uint32_t handle=0;bool ok=false;++report_.create_calls;
        try {ok=e.create && e.create(report_.route,at_,config_.radius,handle);}
        catch(const SpikeFault& f) {
            if(f.handle!=0xffffffffu) {   // the record exists: track it so it gets its one destroy attempt
                life_.created(0,w,f.handle);created_world_=w;report_.created=true;report_.handle=f.handle;report_.created_at_s=now_s;stage_=Stage::hold;
            }
            fault("create faulted");return;
        }
        if(!ok) {finish_refused("create refused");return;}
        life_.created(0,w,handle);created_world_=w;
        report_.created=true;report_.handle=handle;report_.created_at_s=now_s;
        stage_=Stage::settle;settle_ref_t_=now_s;settle_ref_z_=std::numeric_limits<double>::quiet_NaN();
    }
    void do_destroy(SpikeEngine& e,std::uintptr_t w,double now_s) {
        pending_destroy_=false;
        if(!report_.created || w!=created_world()) {stage_=Stage::done;return;}   // a world change was handled by abandon; never destroy elsewhere
        report_.destroy_attempted=true;++report_.destroy_calls;
        bool ok=false;
        try {ok=e.destroy && e.destroy(report_.handle);}
        catch(const SpikeFault&) {   // final: no retry; the body's state is unknown and is reported so (the engine keeps it excluded)
            report_.faulted=true;HandBodyFault::trip();if(report_.reason.empty()) report_.reason="destroy faulted";
            report_.destroyed_at_s=now_s;life_.destroyed(0);stage_=Stage::done;return;
        }
        std::optional<std::uint32_t> after;bool known=true;
        try {after=e.flags?e.flags(report_.handle):std::nullopt;}
        catch(const SpikeFault&) {report_.faulted=true;HandBodyFault::trip();known=false;}
        catch(const SpikeUnreadable&) {known=false;}
        if(!ok) {   // refused without an engine call (busy, unreadable, not ours any more)
            ++report_.destroy_refusals;
            if(known && !after) {report_.abandoned=true;if(report_.reason.empty()) report_.reason="body record gone before the destroy: abandoned";}
            else if(!report_.faulted && report_.destroy_refusals<240) {pending_destroy_=true;return;}   // retry at the next idle tick
            else if(report_.reason.empty()) report_.reason="destroy refused repeatedly";
            report_.destroyed_at_s=now_s;life_.destroyed(0);stage_=Stage::done;return;
        }
        report_.flags_after_destroy=after;
        report_.destroyed=known && (!after || *after==0);
        report_.destroyed_at_s=now_s;
        life_.destroyed(0);
        stage_=Stage::done;
    }
    std::uintptr_t created_world() const {return created_world_;}
    void advance(SpikeEngine& e,std::uintptr_t w,double now_s) {
        if(report_.faulted || !life_.can_write(0,w,report_.handle)) return;
        std::optional<Vec3> p;
        try {p=e.position?e.position(report_.handle):std::nullopt;}
        catch(const SpikeFault&) {fault("position read faulted");return;}
        catch(const SpikeUnreadable&) {++report_.unreadable_ticks;return;}
        if(!p || !finite(*p)) {   // the record no longer holds our body: treat it as gone with its world (no engine call)
            report_.abandoned=true;if(report_.reason.empty()) report_.reason="body record lost: abandoned";
            life_.destroyed(0);stage_=Stage::done;return;
        }
        const double t=now_s-report_.created_at_s;
        if(stage_==Stage::settle) {
            if(!std::isfinite(settle_ref_z_) || std::abs(p->z-settle_ref_z_)>config_.settle_eps_m) {settle_ref_z_=p->z;settle_ref_t_=now_s;}
            const bool still=t>=config_.settle_min_s && now_s-settle_ref_t_>=config_.settle_window_s;
            if(still || t>=config_.settle_max_s) {
                report_.settled=still;report_.settle_z=p->z;
                report_.collided_with_floor=still && std::isfinite(report_.floor_z) &&
                    std::abs(p->z-(report_.floor_z+config_.radius))<=config_.floor_tol_m;
                stage_=Stage::press;press_start_=now_s;
                try {player_start_=e.player?e.player():std::nullopt;} catch(const SpikeFault&) {fault("player read faulted");return;} catch(const SpikeUnreadable&) {player_start_.reset();}
            }
            return;
        }
        if(stage_==Stage::press) {
            std::optional<Vec3> pl;
            try {pl=e.player?e.player():std::nullopt;} catch(const SpikeFault&) {fault("player read faulted");return;} catch(const SpikeUnreadable&) {++report_.unreadable_ticks;}
            if(pl && player_start_ && finite(*pl)) report_.player_drift_m=std::max(report_.player_drift_m,length(*pl-*player_start_));
            const bool over=now_s-press_start_>=config_.press_s;
            Vec3 v{};
            if(!over && player_start_) {
                const Vec3 to{player_start_->x-p->x,player_start_->y-p->y,0};
                const double d=length(to);
                if(d>1e-3) v=to*(config_.press_speed/d);
            }
            if(e.push && life_.can_write(0,w,report_.handle)) {
                try {if(e.push(report_.handle,v)) ++report_.pushes;} catch(const SpikeFault&) {fault("push faulted");return;} catch(const SpikeUnreadable&) {++report_.unreadable_ticks;}
            }
            if(over) stage_=Stage::hold;
            return;
        }
        // hold: nothing to do; the lifecycle destroys at life_s
    }
    Vec3 at_{};SpikeConfig config_{};SpikeReport report_{};HandBodyLifecycle life_{};Stage stage_{Stage::create};
    bool stop_{},pending_destroy_{};std::uintptr_t created_world_{};
    double settle_ref_t_{},settle_ref_z_{std::numeric_limits<double>::quiet_NaN()},press_start_{};std::optional<Vec3> player_start_;
};

// One spike in a fixed-step loop (1/60 s) until it ends or seconds + 10 s pass. seconds is the body's life.
inline SpikeReport run_spike(SpikeEngine& e,SpikeRoute route,Vec3 at,double floor_z,double seconds) {
    SpikeConfig config;config.life_s=seconds;
    SpikeRunner runner(route,at,floor_z,config);
    const double dt=1./60;
    for(double t=0;t<=seconds+10;t+=dt) if(!runner.tick(e,t)) break;
    if(!runner.done()) runner.request_stop("loop budget spent");
    return runner.report();
}

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
// and at every step entry (which closes the 0x7e7e10 writer for this run). This is the in-run G1 of the B11 ruling; the doc's full G1 (five
// 120 s sessions with a level load and a save/load) is not claimed by it.
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
// After the pass the same counters must not grow (checked every tick; a violation stops the spike, which destroys at the next allowed tick).
inline bool g1_still_holds(const G1Stats& at_pass,const G1Stats& now) {
    return now.dispatch_off_frame_thread==at_pass.dispatch_off_frame_thread && now.dispatch_overlap==at_pass.dispatch_overlap &&
           now.step_busy==at_pass.step_busy && now.frame_threads==at_pass.frame_threads && now.dispatch_busy==at_pass.dispatch_busy;
}

// ---- G2: owner. (a) the player's userdata owner, (b) a mod-spawned prop's rigid-body component.
enum class SpikeOwner {none,player_a,prop_b};
inline const char* spike_owner_name(SpikeOwner o) {return o==SpikeOwner::player_a?"a_player":o==SpikeOwner::prop_b?"b_prop":"none";}
struct OwnerEvidence {
    bool a_resolved{},a_userdata_is_player{},a_entity_is_player{},a_type_decoded{},a_type_in_range{};   // the B7 owner chain, read live
    bool b_available{};const char* b_reason{""};
};
struct OwnerDecision {SpikeOwner owner{};const char* reason{""};};
inline bool owner_a_static_ok(const OwnerEvidence& e) {
    return e.a_resolved && e.a_userdata_is_player && e.a_entity_is_player && e.a_type_decoded && !e.a_type_in_range;
}
inline OwnerDecision choose_owner(const OwnerEvidence& e) {
    if(owner_a_static_ok(e)) return {SpikeOwner::player_a,"a: arkPlayerUserData owner, owner+0x10 is the player, typenum outside the live wake range"};
    if(e.b_available) return {SpikeOwner::prop_b,"b: owner (a) failed its static G2 checks"};
    return {SpikeOwner::none,"NO-GO: owner (a) failed its static G2 checks and owner (b) is unavailable"};
}
// The runtime half of G2 for a probe body: 0x853220 calls with the hand's owner and 0x8527a0 calls for the hand's handle, counted from creation.
struct G2Counts {std::uint64_t wake_owner{},exit_hand{};};
inline bool g2_rejects(const G2Counts& since_create) {return since_create.wake_owner || since_create.exit_hand;}
inline OwnerDecision owner_after_g2_reject(SpikeOwner rejected,const OwnerEvidence& e) {
    if(rejected==SpikeOwner::player_a && e.b_available) return {SpikeOwner::prop_b,"b: G2 rejected (a) at run time"};
    return {SpikeOwner::none,"NO-GO: G2 rejected the owner at run time and no fallback owner is available"};
}

// ---- The session: phase ordering. No engine call before G1 has passed and an owner is chosen.
struct SessionInput {
    double now_s{};G1Stats g1;OwnerEvidence owner;G2Counts g2;   // g2: cumulative counts for the chosen owner / the created handle
    std::optional<G2Counts> g2_at_create;                          // the counters taken by the engine just before its createBody call
    std::optional<Vec3> player_root;double floor_z{std::numeric_limits<double>::quiet_NaN()};std::optional<Vec3> forward;
};
class SpikeSession {
public:
    enum class Phase {observe,owner,run,done};
    explicit SpikeSession(SpikeRoute route,double g1_min_s=8,SpikeConfig config={}):route_(route),g1_min_s_(g1_min_s),config_(config) {}
    Phase phase() const {return phase_;}
    SpikeOwner owner() const {return owner_;}
    const G1Verdict& g1() const {return g1_;}
    const G1Stats& g1_at_pass() const {return g1_pass_;}
    const std::optional<SpikeRunner>& runner() const {return runner_;}
    const std::string& outcome() const {return outcome_;}
    bool g2_rejected() const {return g2_rejected_;}
    const G2Counts& g2_since_create() const {return g2_since_;}
    void request_stop(const char* why) {
        stop_=true;
        if(runner_) runner_->request_stop(why);
        else if(phase_!=Phase::done) {outcome_=why?why:"stopped";phase_=Phase::done;}
    }
    bool tick(SpikeEngine& e,const SessionInput& in) {
        if(phase_==Phase::done) return false;
        if(const char* refusal=spike_route_refusal(route_)) {outcome_=refusal;phase_=Phase::done;return false;}
        if(phase_==Phase::observe) {
            g1_=judge_g1(in.g1,g1_min_s_);
            if(!g1_.decided) return true;
            if(!g1_.pass) {outcome_=std::string("G1 failed: ")+g1_.reason+" (no create)";phase_=Phase::done;return false;}
            g1_pass_=in.g1;phase_=Phase::owner;
        }
        if(phase_==Phase::owner) {
            if(!g1_still_holds(g1_pass_,in.g1)) {outcome_="G1 violated before creation (no create)";phase_=Phase::done;return false;}
            const auto d=choose_owner(in.owner);owner_reason_=d.reason;
            if(d.owner==SpikeOwner::none) {outcome_=d.reason;phase_=Phase::done;return false;}
            if(!in.player_root || !finite(*in.player_root) || !std::isfinite(in.floor_z)) return true;   // wait for the player
            owner_=d.owner;
            Vec3 f=in.forward && finite(*in.forward)?Vec3{in.forward->x,in.forward->y,0}:Vec3{1,0,0};
            if(length(f)<1e-3) f={1,0,0};
            f=unit(f);
            const Vec3 at{in.player_root->x+f.x*.45,in.player_root->y+f.y*.45,in.floor_z+config_.drop_height+config_.radius};
            runner_.emplace(route_,at,in.floor_z,config_);
            if(stop_) runner_->request_stop("stopped");
            phase_=Phase::run;
        }
        // run
        if(!g1_still_holds(g1_pass_,in.g1)) runner_->request_stop("G1 violated after the pass");
        if(runner_->report().created) {
            if(!counting_) {counting_=true;g2_base_=in.g2_at_create.value_or(in.g2);}
            g2_since_={in.g2.wake_owner-g2_base_.wake_owner,in.g2.exit_hand-g2_base_.exit_hand};
            if(!g2_rejected_ && g2_rejects(g2_since_)) {
                g2_rejected_=true;runner_->request_stop("G2 rejected the owner at run time");
                const auto next=owner_after_g2_reject(owner_,in.owner);owner_reason_=next.reason;
            }
        }
        const bool running=runner_->tick(e,in.now_s);
        if(!running) {
            const auto& r=runner_->report();
            outcome_=r.reason.empty()?(r.destroyed?"spike complete":"spike ended"):r.reason;
            if(g2_rejected_) outcome_+=std::string("; ")+owner_reason_;
            phase_=Phase::done;
        }
        return running;
    }
    const char* owner_reason() const {return owner_reason_;}
private:
    SpikeRoute route_;double g1_min_s_;SpikeConfig config_;Phase phase_{Phase::observe};G1Verdict g1_{};G1Stats g1_pass_{};
    SpikeOwner owner_{SpikeOwner::none};const char* owner_reason_{""};std::optional<SpikeRunner> runner_;std::string outcome_;
    bool stop_{},counting_{},g2_rejected_{};G2Counts g2_base_{},g2_since_{};
};

// The 0x89fb0 hook's entry into the spike (native_grab.hpp calls it after the original dispatcher when it is set). nullptr unless the knob
// installed the spike: the knob-off cost is one relaxed load.
inline std::atomic<void (*)(void* manager)> hand_body_spike_dispatch{nullptr};

} // namespace d2vr
