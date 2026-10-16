#pragma once
#include "vr_gestures.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2vr {
// Pure combat/stealth policy for the sword hand (side 1). Consumes resolved pose and native-sensor
// state, emits RT/RB levels and a haptic; the caller stays the only thing that writes the pad.
// Coordinates are the same stage metres as GestureFrame (+Y up, head_fwd/head_right unit).
enum class SwingKind {none,slash,overhead,thrust};
struct CombatTarget {
    enum class Kind {none,ground,drop};
    Kind kind{};std::uint32_t spawn{};
    explicit operator bool() const noexcept {
        return (kind==Kind::ground || kind==Kind::drop) && spawn && spawn!=0xffffffffu && (spawn>>16)<0x3fff;
    }
    bool operator==(const CombatTarget& other) const noexcept {return kind==other.kind && spawn==other.spawn;}
    bool operator!=(const CombatTarget& other) const noexcept {return !(*this==other);}
};
enum class AttackTransition {none,waiting,coalesced,resumed,timeout_ordinary,cancelled_target,cancelled_context,dispatched};
struct AttackDiagnostics {
    std::uint64_t recognized{},waiting{},coalesced{},resumed{},timeout_ordinary{},cancelled_target{},cancelled_context{},dispatched{};
};
struct Haptic {float amp{},sec{};};  // game_xr->pulse(1,amp,sec)

struct CombatTuning {
    float blade_len=0.65f;                  // grip -> blade tip along aim, metres
    float arm_speed=0.8f;                   // tip must be this slow (head-relative) to re-arm a swing
    float start_speed=1.5f;                 // segment opens above this
    float slash_speed=2.8f,thrust_speed=1.7f;
    float min_travel=0.25f,min_travel_thrust=0.12f; // tip path length inside the segment
    float min_accel=12.f;                   // peak tip acceleration (m/s^2) inside the segment
    float min_hand_speed=0.9f;              // rejects wrist-only turns (tip fast, hand still): headset test had accidental attacks
    float min_hand_travel=0.15f,min_hand_travel_thrust=0.12f; // the HAND must move this far inside the segment
    float seg_max_ms=350,cooldown_ms=300;
    float thrust_along_aim=0.8f,thrust_along_fwd=0.55f,overhead_down=-0.55f;
    float max_head_speed=2.5f,max_yaw_dps=150.f; // walking/sprint, snap or smooth turn: swings suppressed
    float jump_reject=0.6f;                 // tip moved this far in one sample = tracking glitch, reset
    float kill_thrust_scale=0.8f;           // thrust threshold multiplier while a kill target is qualified
    // Native attack gating (docs/native-attack-gating.md): RT still held ~74-118 ms into a 1.7x slash turns it into a heavy-attack
    // charge with no hit frames, so the press is a short pulse; RB cancels a running slash and RT is ignored while Block is up, so
    // RB is let go before the press and stays off while the slash runs; a press during a running slash is not queued.
    float rt_hold_ms=45,rt_hold_thrust_ms=45,rearm_gap_ms=40;
    float rb_release_ms=45,press_interval_ms=340,block_after_press_ms=350;
    float parry_prior_speed=1.5f,parry_window_ms=250,parry_lookback_ms=300;
    float drop_lead_ms=600;                 // an attack this recent is released onto the drop target
    float guard_slash_scale=1.3f,guard_thrust_scale=1.6f; // in the block pose an attack needs this much more speed and hand
                                            // travel: parry moves (a small push toward the attacker read as a thrust) keep the block
                                            // (user, headset v447: "block should be very forgiving")
    float choke_fwd_min=0.2f,choke_fwd_max=0.7f,choke_below=0.4f,choke_above=0.1f,choke_grip=0.55f,choke_grace_ms=500;
};

struct CombatFrame {
    GV3 head_pos,head_fwd,head_right;
    GV3 pos,aim,vel;                        // sword hand grip position, blade direction, linear velocity
    float grip{},trigger{};                 // raw right grip / trigger
    bool tracked{},allowed{},equipped{};    // equipped: sword in hand (not carrying a prop)
    bool shadow_walk{};
    bool explicit_block{};
    CombatTarget current_target{},remembered_target{};
    bool kill{},choke{},drop{};             // native sensors qualified this frame (TakedownState)
    bool ev_hit{},ev_block{},ev_kill{};     // optional one-frame game events for haptics
    std::uint64_t now_ms{};
};

struct CombatDecision {
    bool rt{},rb{};                         // levels to OR into the pad
    SwingKind swing{};                      // set on the fire frame only
    bool stealth_kill{};                    // fire frame: thrust into a qualified kill target
    bool drop_assist{};                     // fire frame: held attack released onto a new drop target
    bool block{},parry{},choke{};           // diagnostic state
    Haptic haptic{};
    AttackTransition attack_transition{};
    bool attack_pending{};
    std::uint64_t attack_deadline{},scheduled_press{};
    CombatTarget attack_target{};
    AttackDiagnostics attacks{};
};

class VrCombat {
    CombatTuning t_;
    SwordBlockPose block_;
    GV3 tip_{},tip_v_{},prev_head_{},prev_fwd_{},seg_start_{},head_v_{},hand_prev_{};
    std::uint64_t last_ms_{},seg_ms_{},cool_until_{},rt_until_{},rearm_until_{};
    std::uint64_t press_ms_{},parry_until_{},choke_seen_{},peak_ms_{},press_at_{};bool rb_was_{};
    float prev_s_{},peak_acc_{},travel_{},yaw_dps_{},peak_speed_{},hand_travel_{},tip_speed_{};
    bool have_{},armed_{},in_seg_{},drop_was_{},trig_was_{},block_was_{},choke_on_{},shadow_walk_{};
    enum class Pending {none,waiting,target_scheduled,ordinary_scheduled};
    Pending pending_{};CombatTarget pending_target_{};std::uint64_t pending_deadline_{},dispatched_press_{};
    AttackDiagnostics attacks_{};
    void cancel_attack() noexcept {pending_=Pending::none;pending_target_={};pending_deadline_=0;rt_until_=rearm_until_=0;press_at_=dispatched_press_;}
    void reset_swing() {armed_=in_seg_=false;travel_=peak_acc_=0;}
public:
    explicit VrCombat(const CombatTuning& t={}) noexcept : t_(t) {}
    void reset() noexcept {
        auto diagnostics=attacks_;if(pending_!=Pending::none)++diagnostics.cancelled_context;
        *this=VrCombat(t_);attacks_=diagnostics;
    }
    const CombatTuning& tuning() const noexcept {return t_;}
    float tip_speed() const noexcept {return tip_speed_;} // last head-relative blade tip speed (m/s)

    CombatDecision update(const CombatFrame& f) noexcept {
        CombatDecision d{};
        const std::uint64_t now=f.now_ms;
        if(f.shadow_walk!=shadow_walk_) {if(pending_!=Pending::none)d.attack_transition=AttackTransition::cancelled_context;reset();shadow_walk_=f.shadow_walk;}
        const bool live=f.allowed && f.tracked && (f.equipped || f.shadow_walk);
        if(!live) {block_.update(f.head_pos,f.head_fwd,f.head_right,f.pos,f.aim,0,false,false,false,now);reset_swing();have_=false;tip_speed_=0;
            block_was_=choke_on_=drop_was_=rb_was_=false;
            if(pending_!=Pending::none) {++attacks_.cancelled_context;d.attack_transition=AttackTransition::cancelled_context;}
            cancel_attack();trig_was_=f.trigger>=.55f;d.attacks=attacks_;return d;}

        // --- finite-difference tip velocity (GetTickCount64 steps ~15 ms: skip sub-8 ms samples) ---
        const GV3 tip{f.pos.x+f.aim.x*t_.blade_len,f.pos.y+f.aim.y*t_.blade_len,f.pos.z+f.aim.z*t_.blade_len};
        SwingKind kind=SwingKind::none;
        float rel_speed=0,hand_speed=len(f.vel);
        if(!have_) {tip_=tip;hand_prev_=f.pos;prev_head_=f.head_pos;prev_fwd_=f.head_fwd;last_ms_=now;have_=true;tip_v_=head_v_={0,0,0};prev_s_=0;}
        else if(now>=last_ms_+8) {
            const float dt=(now-last_ms_)*1e-3f;
            const GV3 step=sub(tip,tip_);
            if(len(step)>t_.jump_reject) {reset_swing();tip_v_={0,0,0};prev_s_=0;}
            else {
                const GV3 raw{step.x/dt,step.y/dt,step.z/dt};
                tip_v_={tip_v_.x*.4f+raw.x*.6f,tip_v_.y*.4f+raw.y*.6f,tip_v_.z*.4f+raw.z*.6f};
                const GV3 hs=sub(f.head_pos,prev_head_);
                head_v_={hs.x/dt,hs.y/dt,hs.z/dt};
                const float c=std::clamp(dot(f.head_fwd,prev_fwd_),-1.f,1.f);
                yaw_dps_=std::acos(c)*57.2958f/dt;
                const GV3 rel=sub(tip_v_,head_v_);
                rel_speed=len(rel);tip_speed_=rel_speed;
                const bool guard=len(head_v_)>t_.max_head_speed || yaw_dps_>t_.max_yaw_dps;
                const float acc=(rel_speed-prev_s_)/dt;
                if(guard || rel_speed<=t_.arm_speed) {if(rel_speed<=t_.arm_speed && !guard) armed_=true;in_seg_=false;travel_=peak_acc_=0;}
                else if(!in_seg_ && armed_ && rel_speed>=t_.start_speed && hand_speed>=t_.min_hand_speed) {
                    in_seg_=true;seg_start_=tip_;seg_ms_=now;travel_=0;peak_acc_=0;hand_travel_=0;
                }
                if(in_seg_) {
                    travel_+=len(step);hand_travel_+=len(sub(f.pos,hand_prev_));peak_acc_=std::max(peak_acc_,acc);
                    if(now-seg_ms_>t_.seg_max_ms) {in_seg_=false;armed_=false;}   // slow drifting arc: spent until it settles
                    else {
                        const GV3 dir=norm(rel);
                        GV3 flat{f.head_fwd.x,0,f.head_fwd.z};flat=norm(flat);
                        const bool along=dot(dir,f.aim)>=t_.thrust_along_aim && dot(dir,flat)>=t_.thrust_along_fwd;
                        const float gs=block_.held()?t_.guard_slash_scale:1.f,gt=block_.held()?t_.guard_thrust_scale:1.f;
                        const float tscale=(f.kill?t_.kill_thrust_scale:1.f)*gt;
                        const bool thrust=along && rel_speed>=t_.thrust_speed*tscale && travel_>=t_.min_travel_thrust && hand_travel_>=t_.min_hand_travel_thrust*gt;
                        const bool big=rel_speed>=t_.slash_speed*gs && travel_>=t_.min_travel && hand_travel_>=t_.min_hand_travel*gs;
                        if((thrust || big) && peak_acc_>=t_.min_accel && hand_speed>=t_.min_hand_speed && now>=cool_until_) {
                            kind=thrust?SwingKind::thrust:dir.y<=t_.overhead_down?SwingKind::overhead:SwingKind::slash;
                            in_seg_=false;armed_=false;cool_until_=now+static_cast<std::uint64_t>(t_.cooldown_ms);
                        }
                    }
                }
                prev_s_=rel_speed;
            }
            tip_=tip;hand_prev_=f.pos;prev_head_=f.head_pos;prev_fwd_=f.head_fwd;last_ms_=now;
        }

        // --- attack (RT) ---
        const bool trig=f.trigger>=.55f;
        if(trig && !trig_was_) press_ms_=now;
        trig_was_=trig;
        auto fire=[&](std::uint64_t hold,std::uint64_t release=0) {
            std::uint64_t start=now;
            if(rb_was_) start=now+static_cast<std::uint64_t>(t_.rb_release_ms);                    // Block must have exited
            start=std::max(start,now+release);
            if(press_at_) start=std::max(start,press_at_+static_cast<std::uint64_t>(t_.press_interval_ms)); // the running slash ignores presses
            if(now<rt_until_) start=std::max(start,rt_until_+static_cast<std::uint64_t>(t_.rearm_gap_ms)); // a new edge
            rearm_until_=start;rt_until_=start+hold;press_at_=start;
        };
        bool cancelled_target{};
        if(pending_!=Pending::none &&
           ((f.current_target && f.current_target!=pending_target_) ||
            (pending_==Pending::waiting && f.remembered_target && f.remembered_target!=pending_target_) ||
            (pending_==Pending::target_scheduled && f.current_target!=pending_target_))) {
            ++attacks_.cancelled_target;d.attack_transition=AttackTransition::cancelled_target;cancel_attack();cancelled_target=true;
        }
        if(pending_==Pending::waiting) {
            if(now>=pending_deadline_) {
                fire(static_cast<std::uint64_t>(t_.rt_hold_ms),static_cast<std::uint64_t>(t_.rb_release_ms));pending_=Pending::ordinary_scheduled;
                ++attacks_.timeout_ordinary;d.attack_transition=AttackTransition::timeout_ordinary;
            } else if(f.current_target==pending_target_) {
                fire(50,static_cast<std::uint64_t>(t_.rb_release_ms));pending_=Pending::target_scheduled;
                ++attacks_.resumed;d.attack_transition=AttackTransition::resumed;
            }
        }
        if(kind!=SwingKind::none) {
            d.swing=kind;press_ms_=now;++attacks_.recognized;
            d.stealth_kill=kind==SwingKind::thrust && f.kill;
            if(pending_!=Pending::none) {++attacks_.coalesced;if(d.attack_transition==AttackTransition::none)d.attack_transition=AttackTransition::coalesced;}
            else if(!cancelled_target) {
                if(!f.shadow_walk && !f.current_target && f.remembered_target) {
                    pending_=Pending::waiting;pending_target_=f.remembered_target;pending_deadline_=now+500;
                    ++attacks_.waiting;d.attack_transition=AttackTransition::waiting;
                } else {
                    fire(static_cast<std::uint64_t>(kind==SwingKind::thrust?t_.rt_hold_thrust_ms:t_.rt_hold_ms));
                    pending_target_=f.shadow_walk?CombatTarget{}:f.current_target;
                    pending_=pending_target_?Pending::target_scheduled:Pending::ordinary_scheduled;
                }
            }
            const float k=std::clamp((rel_speed-t_.start_speed)/4.f,0.f,1.f);
            d.haptic={.25f+.5f*k,.04f};
        }
        // Drop assist: attack pressed shortly before the native drop sensor qualified is released onto it,
        // preceded by the re-arm gap so the game sees a fresh press while the target exists.
        if(f.drop && !drop_was_ && press_ms_ && now-press_ms_<=static_cast<std::uint64_t>(t_.drop_lead_ms) && kind==SwingKind::none &&
           pending_==Pending::none && !cancelled_target) {
            d.drop_assist=true;fire(static_cast<std::uint64_t>(t_.rt_hold_thrust_ms),static_cast<std::uint64_t>(t_.rearm_gap_ms));
            pending_target_=f.shadow_walk?CombatTarget{}:f.current_target;
            pending_=pending_target_?Pending::target_scheduled:Pending::ordinary_scheduled;
        }
        drop_was_=f.drop;
        d.rt=pending_!=Pending::waiting && now<rt_until_ && now>=rearm_until_;

        // --- block (RB) with parry ---
        if(hand_speed>=peak_speed_ || now-peak_ms_>static_cast<std::uint64_t>(t_.parry_lookback_ms)) {peak_speed_=hand_speed;peak_ms_=now;}
        const bool blk=block_.update(f.head_pos,f.head_fwd,f.head_right,f.pos,f.aim,hand_speed,true,!f.shadow_walk,true,now);
        if(blk && !block_was_) {
            if(now-peak_ms_<=static_cast<std::uint64_t>(t_.parry_lookback_ms) && peak_speed_>=t_.parry_prior_speed)
                parry_until_=now+static_cast<std::uint64_t>(t_.parry_window_ms);
            d.haptic=stronger(d.haptic,{.2f,.03f});
        }
        block_was_=blk;
        d.block=blk;d.parry=blk && now<parry_until_;

        // --- choke (RB): reach forward at neck/head height + grip while the native choke sensor is qualified ---
        const GV3 rel=sub(f.pos,f.head_pos);
        const float fwd=dot(rel,f.head_fwd);
        const bool reach=fwd>=t_.choke_fwd_min && fwd<=t_.choke_fwd_max && f.pos.y>=f.head_pos.y-t_.choke_below
            && f.pos.y<=f.head_pos.y+t_.choke_above && hand_speed<1.5f;
        if(f.choke) choke_seen_=now;
        const bool sensor=!f.shadow_walk && (f.choke || (choke_on_ && now-choke_seen_<=static_cast<std::uint64_t>(t_.choke_grace_ms)));
        if(!choke_on_) choke_on_=!f.shadow_walk && f.choke && reach && f.grip>=t_.choke_grip;
        else if(!sensor || f.grip<.3f) choke_on_=false;
        d.choke=choke_on_;
        if(choke_on_) {
            if(pending_!=Pending::none) {++attacks_.cancelled_context;d.attack_transition=AttackTransition::cancelled_context;}
            cancel_attack();d.rt=false;
        }
        const bool slashing=press_at_ && now<press_at_+static_cast<std::uint64_t>(t_.block_after_press_ms);
        d.rb=((blk || (!f.shadow_walk && f.explicit_block)) && !f.choke && !d.rt && !slashing && pending_!=Pending::waiting) || choke_on_;rb_was_=d.rb;
        if(d.rt && pending_!=Pending::none) {
            d.attack_target=pending_target_;
            ++attacks_.dispatched;dispatched_press_=press_at_;d.attack_transition=AttackTransition::dispatched;pending_=Pending::none;pending_target_={};pending_deadline_=0;
        }

        // --- haptics from events (strongest wins) ---
        if(f.ev_hit) d.haptic=stronger(d.haptic,{.6f,.06f});
        if(f.ev_block) d.haptic=stronger(d.haptic,d.parry?Haptic{1.f,.1f}:Haptic{.8f,.08f});
        if(f.ev_kill || d.stealth_kill) d.haptic=stronger(d.haptic,{1.f,.18f});
        if(choke_on_) d.haptic=stronger(d.haptic,{.15f,.03f});
        d.attack_pending=pending_!=Pending::none;if(d.attack_transition!=AttackTransition::dispatched)d.attack_target=pending_target_;d.attack_deadline=pending_deadline_;
        d.scheduled_press=press_at_;d.attacks=attacks_;return d;
    }
private:
    static Haptic stronger(Haptic a,Haptic b) noexcept {return b.amp>a.amp?b:a;}
};
} // namespace d2vr
