#pragma once
#include <chrono>
#include "tracked_pose.hpp"
#include "tracked_pose_lifetime.hpp"
#include "dwell_timer.hpp"
#include "grab_force_cap.hpp"
#include "release_reason.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
inline constexpr std::uint64_t grab_pose_freshness_ms=tracked_pose_freshness_ms;
// Samples closer than this share a clock quantum; their motion waits for the next spaced sample instead of being dropped.
inline constexpr std::uint64_t grab_velocity_min_ms=8;
// v1090: the precise clock of a grab's samples and writes (steady clock, s); the ms ticks stay for freshness and lifecycle.
inline double precise_s() noexcept {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline bool grab_pose_valid(const WorldTrackedPose& pose) {
    if(!finite(pose.position)) return false;
    for(unsigned i=0;i<3;++i) {
        if(!finite(pose.basis[i]) || std::abs(length(pose.basis[i])-1)>.02) return false;
        for(unsigned j=i+1;j<3;++j) if(std::abs(dot(pose.basis[i],pose.basis[j]))>.02) return false;
    }
    return dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])>.98;
}
inline Vec3 grab_local(const WorldTrackedPose& hand,Vec3 v) {
    return {dot(v,hand.basis[0]),dot(v,hand.basis[1]),dot(v,hand.basis[2])};
}
inline Vec3 grab_world(const WorldTrackedPose& hand,Vec3 v) {
    return hand.basis[0]*v.x+hand.basis[1]*v.y+hand.basis[2]*v.z;
}
inline Vec3 grab_limit(Vec3 v,double limit) {
    const auto n=length(v);return n>limit?v*(limit/n):v;
}
inline Vec3 grab_rotation_error(const WorldTrackedPose& object,const WorldTrackedPose& goal) {
    // Quaternion extraction preserves a correction at a half turn, where
    // summing crossed basis vectors incorrectly gives zero angular velocity.
    double r[3][3]{};
    for(unsigned i=0;i<3;++i) {
        const double a[3]{goal.basis[i].x,goal.basis[i].y,goal.basis[i].z};
        const double b[3]{object.basis[i].x,object.basis[i].y,object.basis[i].z};
        for(unsigned row=0;row<3;++row) for(unsigned col=0;col<3;++col) r[row][col]+=a[row]*b[col];
    }
    double q[4]{};const auto trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0) {
        const auto s=2*std::sqrt(1+trace);q[3]=s*.25;
        q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s;
    } else {
        unsigned i=0;if(r[1][1]>r[i][i]) i=1;if(r[2][2]>r[i][i]) i=2;
        const unsigned j=(i+1)%3,k=(i+2)%3;
        const auto s=2*std::sqrt(std::max(0.,1+r[i][i]-r[j][j]-r[k][k]));
        if(s<1e-9) return {};
        q[i]=s*.25;q[j]=(r[i][j]+r[j][i])/s;q[k]=(r[i][k]+r[k][i])/s;q[3]=(r[k][j]-r[j][k])/s;
    }
    if(q[3]<0) for(auto& v:q) v=-v;
    const Vec3 axis{q[0],q[1],q[2]};const auto n=length(axis);
    return n<1e-9?axis*2:axis*(2*std::atan2(n,q[3])/n);
}
struct GrabMotor {Vec3 linear{},angular{};bool release{};};
// Input for PhysicalGrab::corpse_motor. corpse=false is a prop: the force cap only, with motor()'s 1.5 m release and throw.
struct CorpseMotorInput {Vec3 body_velocity{},body_spin{};bool velocity_known{},corpse{true};
    bool state_changed{};   // caller saw the entity change state this tick; wins over an open fist (the release reason is state_changed)
    bool stick{};           // v1084: a corpse limb sticks like a prop (corpse_motor below); a prop always does (v1075)
    double object_kg{},inertia_kgm2{},share{1.},dt{};const GrabTuning* tuning{};
    Vec3 carrier{};   // v1075: the player's own velocity (the held thing's feed-forward rides on it, as motor()'s v1069 carrier)
    double now_s{};   // v1090: the motor's precise time (steady clock, s; 0 = off): the goal is led from the hand sample's time to now
    bool chain{};     // v1087: the held body carries others on its joints (a part of a severed limb): the sticking hold integrates its error (corpse_motor)
};
// What Task 10 needs per write: the command, the cap fraction (share of the commanded velocity change removed), the capped flag,
// the implied force, and the release reason (none while held).
// write=false: nothing to write this frame (invalid dt); the hold continues. A non-finite input or result releases with
// ReleaseReason::fault (a zero-velocity release; the caller writes nothing). cap_fraction is the share of the LINEAR velocity
// change removed by the cap (angular share only when the linear change is zero).
struct CorpseMotor {GrabMotor motor;double lag_m{};bool capped{};double cap_fraction{};double implied_force_n{};ReleaseReason reason{ReleaseReason::none};bool write{true};};
// The grasp point kept for the drawn hand to blend out after a release (A-12): weight falls 1 -> 0 over anchor_blend_s.
struct ReleasedAnchor {Vec3 position{};double weight{};std::uint64_t released_at{};};
// A velocity motor keeps the native body dynamic, so collision/gravity remain
// native and tracking loss cannot leave a body in a changed motion mode.
class PhysicalGrab {
    bool armed_=false,pressed_=false,held_=false,valid_sample_=false;
    std::uint64_t epoch_{},tick_{},velocity_tick_{};
    double velocity_at_s_{},sample_at_s_{};   // sample_at_s_: the newest sample's precise time (0 = unknown)   // v1090: the velocity baseline's precise time (s, steady clock; 0 = none: the tick clock)
    WorldTrackedPose previous_{},relative_{},last_goal_{};
    // Velocity has its own baseline so same-quantum samples keep their motion while previous_/tick_ track the newest pose.
    Vec3 velocity_{},velocity_origin_{};
    Vec3 goal_velocity_{},goal_origin_{};bool goal_ok_=false;   // v1238: the hold goal's velocity (sample's EMA; ok = a baseline from this hold)
    Vec3 goal_point(const WorldTrackedPose& hand) const {return hand.position+grab_world(hand,relative_.position);}
    // Hand-to-limb contact point in the object's own frame (snatch grab: the bind stores it, the first command is zero).
    Vec3 contact_local_{};
    DwellTimer lag_timer_{lag_release_s};
    ReleaseReason reason_=ReleaseReason::none;
    bool forced_release_=false,has_released_=false;
    std::uint64_t release_tick_{};
    Vec3 last_anchor_{};
    // Corpse hold stability (fix round 1): after a bind the hand-to-grasp offset slides to ~0 (the body comes to the palm); r0_/o0_ are the bind-time
    // hand-frame object origin and grasp offset, slide_s_ the time since. A blocked body slips instead of dropping (reseat_) unless legacy_lag_.
    Vec3 slide_r0_{},slide_o0_{};double slide_s_=0;bool slide_done_=true,legacy_lag_=false;
    double recovery_s_=0;   // time spent in stale-frame recovery (s, > 0 while recovering; 0 = normal play). Reset on bind and release.
    Vec3 chain_integral_{}; // v1087: the integrated hold error of a chained body (CorpseMotorInput::chain), m*s. Reset on bind.
    void note_release(ReleaseReason reason,bool forced) {
        recovery_s_=0;reason_=reason;forced_release_=forced;release_tick_=tick_;has_released_=true;
    }
public:
    // Corpse lag rule (Bonelab's hand lags a blocked body, then lets go): the grasp point trailing its target by more than
    // lag_release_m for lag_release_s releases; a forced release blocks a regrab for regrab_cooldown_s (a flapping grip).
    static constexpr double lag_release_m=.5,lag_release_s=.25,regrab_cooldown_s=.3,anchor_blend_s=.25;
    // Corpse slide and slip (fix round 1): anchor_slide_s to bring the grasp point to the palm; a lag over lag_release_m for lag_release_s re-seats the goal toward
    // the body at slip_mps while the hand is within slip_hand_m of the grasped bone (plus the hold's offset on it, H21); only a lag over lag_hard_release_m (or a hand beyond slip_hand_m) lets go.
    static constexpr double anchor_slide_s=.25,slip_mps=.15,slip_hand_m=.45,lag_hard_release_m=.9;
    // v1075/v1084 the sticking hold (corpse_motor): the closure gains (1/s) and the velocity limits of a hold that puts the thing on the hand every step
    static constexpr double stick_gain=40,stick_spin_gain=40,stick_mps=20,stick_rps=30;
    // v1087 a chained hold's integral: critical damping with stick_gain (1/s^2), integrated only within chain_window_m of the goal, its correction capped
    static constexpr double lead_max_s=.05;   // v1090: the most a goal is led (a stalled publication is not extrapolated further)
    static constexpr double chain_gain=stick_gain*stick_gain/4,chain_window_m=.05,chain_mps=3;
    void set_legacy_lag_release(bool on) noexcept {legacy_lag_=on;}   // build/legacy-corpse-lag-release: no slide, the hard 0.5 m / 0.25 s drop
    bool pressed() const {return pressed_;}
    // A squeeze the game owned at its press, handed to the physics hand mid-press (a yanked or woken body under it): the
    // grip already held latches on the next valid sample, as if pressed fresh.
    void arm() noexcept {armed_=true;}
    bool held() const {return held_;}
    // v1074: the hold itself - the held body's pose in the sampled hand's frame (target() = hand * relative). The draw places the thing with it.
    const WorldTrackedPose& relative() const noexcept {return relative_;}
    const Vec3& velocity() const {return velocity_;}
    const WorldTrackedPose& last_goal() const noexcept {return last_goal_;}   // v1249: the goal of the last corpse_motor write (led to its time)
    inline static bool goal_feed=true;   // v1238: feed the goal's velocity (false: the hand's, v1090; lab A/B and build/legacy-hand-feed)
    Vec3 feed_velocity() const {return goal_feed && goal_ok_?goal_velocity_:velocity_;}
    double recovery_s() const {return recovery_s_;}
#ifdef D2VR_TESTING
    void set_recovery_for_tests(double seconds) {recovery_s_=seconds;}
#endif
    // at_s (v1090, user v1089: carried severed parts unstable on the run): the sample's precise time (steady clock, s). The ms ticks are
    // GetTickCount's 15.6 ms quantum while hands arrive every ~11 ms, so a running hand's velocity (the motor's feed) swung +-40%; 0 keeps the tick clock.
    bool sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick,double at_s=0) {
        const bool valid_input=usable && epoch && tick && grab_pose_valid(hand) && std::isfinite(grip) && grip>=0 && grip<=1;
        if(!valid_input || epoch!=epoch_ || tick<tick_ || (tick_ && tick-tick_>grab_pose_freshness_ms)) {
            // A fresh released sample is sufficient to rearm at a new epoch;
            // held grips and unusable/reset samples never acquire on return.
            armed_=valid_input && epoch!=epoch_ && grip<.35f && (!tick_ || tick>=tick_);
            valid_sample_=false;pressed_=false;velocity_={};epoch_=epoch;tick_=tick;previous_=hand;velocity_origin_=hand.position;goal_ok_=false;velocity_tick_=tick;velocity_at_s_=at_s;return false;
        }
        const auto displacement=hand.position-previous_.position;
        // Check discontinuities even at duplicate or slow accepted timestamps,
        // before a released grip can convert old movement into a throw.
        // v1086 (user: "a held screwdriver or tray falls out of my hand on a snap turn although I hold the grip"): a snap turn carries the hand
        // round the player's pivot in one step (45 deg: 0.77 x its distance from the pivot), past this jump rule when the arm is out or the player
        // stands off his play-space centre. A HELD grip still squeezed continues from the new hand: the press and the hold stay, the jump is not
        // hand motion (no throw velocity from it). A real teleport (a blink) still ends the hold through the motor's 1.5 m lag rule.
        if(!(length(displacement)<=.5) && held_ && pressed_ && grip>=.35f) {velocity_={};tick_=tick;previous_=hand;velocity_origin_=hand.position;goal_ok_=false;velocity_tick_=tick;velocity_at_s_=at_s;sample_at_s_=at_s;valid_sample_=true;return true;}
        if(!(length(displacement)<=.5)) {valid_sample_=false;armed_=pressed_=false;velocity_={};tick_=tick;previous_=hand;velocity_origin_=hand.position;goal_ok_=false;velocity_tick_=tick;velocity_at_s_=at_s;return false;}
        // Samples inside one clock quantum leave velocity (and its baseline) alone; their motion enters the next spaced sample.
        const bool precise=at_s>0 && velocity_at_s_>0 && at_s>=velocity_at_s_;
        if(precise?at_s-velocity_at_s_>=.004:tick-velocity_tick_>=grab_velocity_min_ms) {
            const auto dt=precise?at_s-velocity_at_s_:double(tick-velocity_tick_)*.001;velocity_at_s_=at_s;
            const double weight=1-std::exp(-dt/.035);
            velocity_=velocity_*(1-weight)+grab_limit((hand.position-velocity_origin_)*(1/dt),12)*weight;
            velocity_origin_=hand.position;velocity_tick_=tick;
            // v1238: the goal's own velocity (the hold point hand * relative, not the hand origin): a turning hand or player swings the goal round
            // the hand, and the hand's velocity alone left ~omega*r/stick_gain of steady lag (8 mm at 2 rad/s and 15 cm); the same weights and limit
            const auto goal=goal_point(hand);
            // the hand's 35 ms average: the samples' clock is their arrival on the game thread, not the pose's time, and a shorter average passes
            // that jitter on (lab: +-2 ms arrival jitter at 6 m/s, 12 ms average 27 mm vs 15 mm)
            goal_velocity_=goal_ok_?goal_velocity_*(1-weight)+grab_limit((goal-goal_origin_)*(1/dt),12)*weight:velocity_;
            goal_origin_=goal;goal_ok_=held_;
        }
        if(grip<.35f) {armed_=true;pressed_=false;}
        else if(armed_ && grip>=.45f) pressed_=true;
        valid_sample_=true;tick_=tick;previous_=hand;sample_at_s_=at_s;return true;
    }
    // While the press waits for its owner (a native interactable's query, e.g. a corpse's carry, plus the obstruction check:
    // ~250 ms in sim v400), keep the sample fresh without touching the press latch. Otherwise the wait outlasts the freshness
    // window, the next sample resets, and a grip already held can never press.
    void keep_fresh(const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick) {
        if(!valid_sample_ || epoch!=epoch_ || tick<tick_ || tick-tick_>grab_pose_freshness_ms || !grab_pose_valid(hand) ||
           length(hand.position-previous_.position)>.5) return;
        tick_=tick;previous_=hand;velocity_origin_=hand.position;goal_ok_=false;velocity_tick_=tick;velocity_at_s_=0;
    }
    // B16: the hand this grab samples moved by a known step (a coupling to the hand body began or ended, the body snapped): continue from it
    // without reading the step as hand motion (no 0.5 m discontinuity release, no throw velocity from it); the press, the hold and the clock stay.
    void rebase(const WorldTrackedPose& hand) {
        if(!valid_sample_ || !grab_pose_valid(hand)) return;
        previous_=hand;velocity_origin_=hand.position;goal_ok_=false;velocity_at_s_=0;
    }
    bool bind(const WorldTrackedPose& object) {return bind(object,object.position);}
    bool bind(const WorldTrackedPose& object,Vec3 contact,double maximum_contact=.3) {
        // A hand clock that went backwards (stale rebase) keeps the cooldown running until it passes the release tick again.
        if(forced_release_ && (tick_<release_tick_ || double(tick_-release_tick_)*.001<regrab_cooldown_s)) return false;
        if(!valid_sample_ || !pressed_ || held_ || !grab_pose_valid(object) || !finite(contact) ||
           !std::isfinite(maximum_contact) || maximum_contact<0 || maximum_contact>.55 ||
           length(contact-previous_.position)>maximum_contact || length(contact-object.position)>3) return false;
        relative_.position=grab_local(previous_,object.position-previous_.position);
        for(unsigned i=0;i<3;++i) relative_.basis[i]=grab_local(previous_,object.basis[i]);
        contact_local_=grab_local(object,contact-object.position);
        slide_r0_=relative_.position;slide_o0_=relative_.position+grab_local(previous_,grab_world(object,contact_local_));slide_s_=0;slide_done_=legacy_lag_;
        recovery_s_=0;chain_integral_={};lag_timer_.reset();has_released_=false;forced_release_=false;reason_=ReleaseReason::none;last_anchor_=contact;
        second_.reset();goal_ok_=false;held_=true;return true;
    }
    // The grasped point: object origin plus the stored contact offset in the object's current frame (valid while held()).
    Vec3 anchor(const WorldTrackedPose& object) const {return object.position+grab_world(object,contact_local_);}
    ReleaseReason release_reason() const {return reason_;}
    // The last grasp point for the drawn hand to blend out over anchor_blend_s after a release; `tick` is the hand sample clock (ms).
    std::optional<ReleasedAnchor> released_anchor(std::uint64_t tick) const {
        if(!has_released_ || held_ || tick<release_tick_) return {};
        const auto age=double(tick-release_tick_)*.001;
        if(age>=anchor_blend_s) return {};
        return ReleasedAnchor{last_anchor_,1-age/anchor_blend_s,release_tick_};
    }
    WorldTrackedPose target() const {
        auto result=previous_;result.position=previous_.position+grab_world(previous_,relative_.position);
        for(unsigned i=0;i<3;++i) result.basis[i]=grab_world(previous_,relative_.basis[i]);
        if(second_) {
            // Two-handed hold (docs/NATIVE-HANDS-PLAN.md phase 2, HIGGS): the main hand keeps the position; the goal turns about the main hand
            // by the shortest arc that puts the second hand's grasp point on the line toward the second hand. Twist about that line stays the main hand's.
            const auto pivot=previous_.position;
            const auto have=result.position+grab_world(result,second_->local)-pivot,want=second_->hand-pivot;
            const auto axis=cross(have,want);const double s=length(axis),c=dot(have,want);
            if(length(have)>.05 && length(want)>.05 && s>1e-9) {
                const auto k=axis*(1/s);const double angle=std::atan2(s,c);
                const auto turn=[&](Vec3 v){return v*std::cos(angle)+cross(k,v)*std::sin(angle)+k*(dot(k,v)*(1-std::cos(angle)));};
                result.position=pivot+turn(result.position-pivot);
                for(auto& b:result.basis) b=turn(b);
            }
        }
        return result;
    }
    // The second hand on this held body: its grasp point (object-local, from its own grab's hand_in_object) and where that hand is now.
    struct SecondHand {Vec3 local{},hand{};};
private:
    std::optional<SecondHand> second_;
public:
    void second_hand(std::optional<SecondHand> h) noexcept {second_=h && finite(h->local) && finite(h->hand)?h:std::nullopt;}
    bool two_handed() const noexcept {return second_.has_value();}
    // This grab's hand origin in the held object's frame (a bound grab).
    Vec3 hand_in_object() const {const auto v=relative_.position*-1.;return {dot(v,relative_.basis[0]),dot(v,relative_.basis[1]),dot(v,relative_.basis[2])};}
    // The other hand let go: this grab now drives the body from where the hand and the body are now (no jump to its old bind relation).
    // v1062 seat: move the held thing by `world` (m) relative to the hand, from now on (the drawn hand lay in it: finger_ik.hpp held_standoff).
    void nudge(Vec3 world) {
        if(!held_ || !valid_sample_ || !finite(world)) return;
        const auto v=grab_local(previous_,world);relative_.position=relative_.position+v;slide_r0_=slide_r0_+v;goal_origin_=goal_origin_+world;   // v1238: a seat step is no goal motion
    }
    void reseat(const WorldTrackedPose& object) {
        if(!held_ || !valid_sample_ || !grab_pose_valid(object)) return;
        relative_.position=grab_local(previous_,object.position-previous_.position);
        for(unsigned i=0;i<3;++i) relative_.basis[i]=grab_local(previous_,object.basis[i]);
        goal_ok_=false;
    }
    // `reason`: open_fist only for a grip-open, lag for a blocked-carry release, lifecycle for a handoff or tracking loss (default).
    // Bookkeeping only happens when a hold actually ends, so releasing an idle lane keeps the cooldown, reason and anchor.
    // forced (B16: a coupled corpse let go for hand lag): the regrab cooldown runs, as this class's own lag release does.
    GrabMotor release(bool throw_velocity,ReleaseReason reason=ReleaseReason::lifecycle,bool forced=false) {
        if(held_) note_release(reason,forced);
        held_=false;return {throw_velocity && valid_sample_?velocity_:Vec3{}, {},true};
    }
    GrabMotor release_sample(const WorldTrackedPose& hand,float grip,bool usable,std::uint64_t epoch,std::uint64_t tick,bool throw_velocity,ReleaseReason reason=ReleaseReason::lifecycle) {
        // Native grip release can arrive before the next motor callback.
        const bool accepted=sample(hand,grip,usable,epoch,tick);
        return release(throw_velocity && accepted,reason);
    }
    // carrier (v1069, user: "walking with a prop, the hand and the part lag behind and sync again"): the player's own velocity. The target rides on
    // the player, so that part of the feed never fades with the sample's age (the hand servo's Fix B); only the hand's motion relative to the player does.
    std::optional<GrabMotor> motor(const WorldTrackedPose& object,bool usable,std::uint64_t now=0,std::uint64_t source_tick=0,Vec3 carrier={}) {
        if(!held_) return {};
        if(usable && valid_sample_ && grab_pose_valid(object)) last_anchor_=anchor(object);
        if(!usable || !valid_sample_ || !pressed_ || !grab_pose_valid(object)) {const bool open=usable && valid_sample_ && !pressed_;return release(open,open?ReleaseReason::open_fist:ReleaseReason::lifecycle);}
        const auto goal=target();const auto error=goal.position-object.position;
        if(length(error)>1.5) return release(valid_sample_ && pressed_,ReleaseReason::lag); // fast swing outran the motor: throw, do not dead-drop // Wall blockage or an ownership discontinuity.
        const auto spin=grab_rotation_error(object,goal);
        const auto source=source_tick?std::min(source_tick,tick_):tick_;
        const auto age=now>=source?now-source:0;
        if(!finite(carrier) || !(length(carrier)<=10)) carrier={};
        const auto feed=now?carrier+(now>=source?(velocity_-carrier)*std::exp(-double(age>32?age-32:0)*.001/.035):Vec3{}):velocity_;
        return GrabMotor{grab_limit(error*18+feed,10),grab_limit(spin*12,5),false};
    }
    std::optional<CorpseMotor> corpse_motor(const WorldTrackedPose& object,bool usable,std::uint64_t now,std::uint64_t source_tick,const CorpseMotorInput& in) {
        if(!held_) return {};
        const bool hand_ok=usable && valid_sample_ && grab_pose_valid(object);
        if(hand_ok) last_anchor_=anchor(object);
        auto finish=[&](ReleaseReason reason,bool forced,bool throw_velocity,double lag) {
            // A corpse release writes no velocity; a prop release is today's release(throw).
            CorpseMotor out;out.lag_m=lag;out.reason=reason;
            if(in.corpse) {held_=false;out.motor=GrabMotor{{},{},true};}
            else out.motor=release(throw_velocity,reason);   // props keep their old behaviour: no cooldown
            if(in.corpse) note_release(reason,forced);
            return out;
        };
        if(in.state_changed) return finish(ReleaseReason::state_changed,false,false,0);
        if(!hand_ok || !pressed_) {
            const bool open=usable && valid_sample_ && !pressed_;
            return finish(open?ReleaseReason::open_fist:ReleaseReason::lifecycle,false,open,0);
        }
        const GrabTuning fallback;const auto& tuning=in.tuning?*in.tuning:fallback;
        // Non-finite inputs never reach the engine: release as a fault (no velocity).
        if(!finite(in.body_velocity) || !finite(in.body_spin) || !std::isfinite(in.share) || !std::isfinite(in.object_kg) || !std::isfinite(in.inertia_kgm2))
            return finish(ReleaseReason::fault,false,false,0);
        // One validated, clamped dt feeds both the lag timer and the cap; an unusable dt writes nothing this frame.
        if(!(in.dt>0) || !std::isfinite(in.dt)) {CorpseMotor idle;idle.write=false;return idle;}
        const double dt=std::min(in.dt,tuning.dt_max);
        if(in.corpse && !legacy_lag_ && !slide_done_) {   // the body comes to the palm: the grasp offset (hand frame) shrinks linearly to 0
            relative_.position=slide_r0_-slide_o0_*(slide_s_/anchor_slide_s);   // the first frame after the bind stays at the stored offset (a zero command)
            if(slide_s_>=anchor_slide_s) slide_done_=true;else slide_s_=std::min(anchor_slide_s,slide_s_+dt);
        }
        // v1090 (user, headset v1089: carried parts unstable on the run): the goal is where the hand is NOW, not at its sample: the newest hand is 0-25 ms
        // old at a game frame (90 Hz publications, ~81 fps frames), so at 6 m/s the goal trailed 0-15 cm by a different amount every frame (the lab
        // tests/held_run_jitter_check.cpp). Led by the hand's own velocity over that age (at most lead_max_s); off without precise times.
        const double lead=in.now_s>0 && sample_at_s_>0 && in.now_s>=sample_at_s_?std::min(in.now_s-sample_at_s_,lead_max_s):0.;
        const Vec3 hand_feed=feed_velocity();   // v1238: the goal's own velocity
        const auto led=[&]{auto g=target();g.position=g.position+hand_feed*lead;return g;};
        auto goal=led();auto error=goal.position-object.position;
        auto lag=length(anchor(goal)-anchor(object));
        if(in.corpse) {
            if(lag_timer_.update(lag>lag_release_m,dt)) {
                // A blocked body (a floor, a wall under the goal) slips instead of dropping: re-seat the goal toward the body while the hand is still on it.
                const bool slip=!legacy_lag_ && lag<=lag_hard_release_m && length(previous_.position-object.position)<=slip_hand_m+length(contact_local_);   // H21: measured from the GRASPED bone (its origin, allowing for where on it the hand holds)
                if(!slip) return finish(ReleaseReason::lag,true,false,lag);
                const auto toward=anchor(object)-anchor(goal);const auto n=length(toward);
                if(n>1e-9) relative_.position=relative_.position+grab_local(previous_,toward*(std::min(slip_mps*dt,n)/n));
                goal=led();error=goal.position-object.position;lag=length(anchor(goal)-anchor(object));
            }
        }
        else if(length(error)>1.5) return finish(ReleaseReason::lag,false,pressed_,lag);
        const auto spin=grab_rotation_error(object,goal);
        const auto source=source_tick?std::min(source_tick,tick_):tick_;
        const auto age=now>=source?now-source:0;
        // v1075 (user: "you just carry a thing and it is simply in your hand: nothing twitches, nothing moves"): a held prop is driven like a
        // carried weapon, not a weight. Its feed rides on the player's own velocity (v1069's carrier, which only the uncapped motor had), it closes
        // its gap at stick_gain (40/s since v1084; 35/s in v1075; 18/s before), and it is not force-capped. A held prop lagged up to v/18 behind a running hand (~0.3 m at 5 m/s once the
        // hand sample's feed had faded) and a heavy one more. Corpse limbs too since v1084 (in.stick), unless build/legacy-weighted-hold.
        const bool stick=!in.corpse || in.stick;   // v1075: a held prop; v1084: a corpse limb too (build/legacy-weighted-hold: the capped law for it)
        const Vec3 carry=stick && finite(in.carrier) && length(in.carrier)<=10?in.carrier:Vec3{};
        const auto fade=now>=source?std::exp(-double(age>32?age-32:0)*.001/.035):0.;
        const auto feed=!now?hand_feed:carry+(hand_feed-carry)*fade;
        // Stale frame (a game-thread hitch: raw dt beyond dt_max): the hand kept moving while no motor ran, so `error` holds the whole stall.
        // Closing it at the normal gain would command a jump (0.4 m x 18 = 7 m/s) and whip the chain; scale the error term by dt_max/dt so the
        // catch-up speed a stall can ask for stays what a normal frame could (smooth catch-up, as the hand lags and then closes in Bonelab).
        const double catch_up=in.dt>tuning.dt_max?tuning.dt_max/in.dt:1.;
        // The stale frame also opens a recovery window: the NORMAL frames that follow still see the leftover lag (a 0.5 s stall leaves ~0.3 m) and
        // would close it at the full gain (~6 m/s), whipping the chain. While recovering, the error-closure term alone is capped at catch_up_mps
        // (the hand's own feed velocity is not); recovery ends when the lag is under 5 cm or the window runs out. Outside recovery the law is unchanged.
        // The cap RAMPS (catch_up_mps + catch_up_ramp_mps2 * t) and recovery ends when it reaches the normal 10 m/s limit or the lag is under 5 cm and the rotation error under 0.1 rad, so no frame
        // jumps back to full gain. Only a hitch (raw dt > stale_dt_s) enters it: sustained low fps keeps just the per-frame dt_max/dt scaling above.
        // An active ramp is never re-armed by a further stale frame (it keeps ramping, so a run of slow frames still ends it); only an idle one arms.
        if(recovery_s_>0) recovery_s_+=in.dt;
        else if(in.dt>tuning.stale_dt_s) recovery_s_=1e-9;
        // v1084 stick (user, v1083 headset: "use the same path for all physical props and corpses as for bottles and glasses ... I grab it, it
        // sticks to my hand"): the game's HoldObject puts a bottle on its target every step; the weighted hold (18/s through a 90-300 N force cap)
        // made a corpse's limb trail and float behind the hand, as it did a prop before v1075. A sticking hold closes the gap in about one physics
        // step (stick_gain: 2/3 of it a 60 Hz step, stable up to 50 ms steps) with no force cap; the grasp slide, the slip, the lag release, the
        // stale-frame recovery and the runaway rule stay. build/legacy-weighted-hold puts corpse limbs back on the capped Bonelab law.
        auto closure=error*((stick?stick_gain:18)*catch_up);auto spin_closure=spin*(stick?stick_spin_gain:12);
        // v1087 (lab tests/severed_limb_hold_check.cpp: a foot carrying the rest of a severed leg sagged 40 mm, a hand body under its arm 31 mm): the held
        // part of a severed limb carries the others on its joints, and the proportional hold leaves a steady error of that weight's pull / stick_gain. It
        // integrates its own error while it is near the goal (a snap turn's or a blocked step's error is no load), so the part stays in the hand.
        if(stick && in.chain) {
            if(length(error)<chain_window_m) chain_integral_=grab_limit(chain_integral_+error*dt,chain_mps/chain_gain);
            closure=closure+chain_integral_*chain_gain;
        }
        if(recovery_s_>0) {
            const double cap=tuning.catch_up_mps+tuning.catch_up_ramp_mps2*recovery_s_,wcap=cap*.5;   // angular: half the linear cap (5 rad/s vs 10 m/s)
            const double c=length(closure),w=length(spin_closure);
            // Ends when the cap has reached the normal limit, or when both uncapped closures are already inside the caps and the body has caught up
            // (lag under 5 cm, rotation under 0.1 rad): on that frame the capped and the normal law agree, so nothing steps.
            if(cap>=10 || (c<=cap && w<=wcap && lag<.05 && length(spin)<.1)) recovery_s_=0;
            else {if(c>cap) closure=closure*(cap/c);if(w>wcap) spin_closure=spin_closure*(wcap/w);}
        }
        const auto want_v=grab_limit(closure+feed,stick?stick_mps:10),want_w=grab_limit(spin_closure,stick?stick_rps:5);
        // Unknown body velocity: step from rest (the caller counts the fallback).
        const Vec3 v0=in.velocity_known?in.body_velocity:Vec3{},w0=in.velocity_known?in.body_spin:Vec3{};
        const auto step=stick?CappedStep{want_v,want_w,false,false,0.,0.}:cap_velocity_step(v0,w0,want_v,want_w,previous_.basis[0],in.object_kg,in.inertia_kgm2,dt,tuning,in.share);
        if(!finite(step.linear) || !finite(step.angular)) return finish(ReleaseReason::fault,false,false,lag);
        const auto asked=length(want_v-v0)>1e-12?length(want_v-v0):length(want_w-w0),
            given=length(want_v-v0)>1e-12?length(step.linear-v0):length(step.angular-w0);
        last_goal_=goal;   // v1249: the led goal of this write (held_render_pose.hpp draws the engine's render frame there)
        CorpseMotor out;
        out.motor=GrabMotor{step.linear,step.angular,false};out.lag_m=lag;
        out.capped=step.linear_capped||step.angular_capped;
        out.cap_fraction=asked>1e-12?std::clamp(1-given/asked,0.,1.):0;
        out.implied_force_n=step.implied_force_n;
        return out;
    }
};
} // namespace d2vr
