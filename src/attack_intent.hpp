#pragma once
// Plan G task 3: the physical attack only forwards intent. A fast drawn-sword swing, or a straight crouched stab, presses the game's own
// attack button; the engine then decides the outcome (on an unaware guard seen from behind the native attack is the assassination, on an
// aware guard it is an ordinary attack). Nothing here names a victim, damages anyone or starts a takedown.
#include "arm_ik.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
// hand_fwd is the direction the hand points (blade axis); a zero vector means "unknown", which can never read as a stab.
// hand_fwd / head_fwd: unit directions (a zero vector means unknown: no stab, no yaw guard).
struct AttackSample {double t_s{};Vec3 hand_pos{},head_pos{};bool tracked{};Vec3 hand_fwd{},head_fwd{};};

// Everything that must be true for a physical attack to press. Pure so one test pins each term.
struct AttackGate {
    bool owns{},allowed{},tracked{},explicit_block{},shadow_walk{};
    bool carrying{},grab_held{},left_power{},equip_transition{},ui_open{};
};
inline constexpr bool attack_allowed(const AttackGate& g) noexcept {
    return g.owns && g.allowed && g.tracked && !g.explicit_block && !g.shadow_walk &&
           !g.carrying && !g.grab_held && !g.left_power && !g.equip_transition && !g.ui_open;
}

class AttackIntent {
public:
    inline static constexpr double slash_mps=3.0,stab_mps=1.5,rearm_mps=1.0,glitch_mps=20.;
    inline static constexpr double cooldown_s=.3,min_dt_s=.004,max_gap_s=.25;
    inline static constexpr double stab_window_s=.4,stab_min_travel_m=.20,stab_min_straight=.75,stab_min_align=.5;
    inline static constexpr unsigned rearm_samples=2;
    inline static constexpr double max_head_mps=2.5,max_yaw_dps=150.,head_guard_s=.1;
    // Returns true on the one sample a press should start.
    // press_free: the caller's AttackPress can start now; without it nothing is spent (no cooldown, no count).
    bool update(const AttackSample& s,bool crouched,bool weapon_drawn,bool press_free=true) noexcept {
        if(!s.tracked || !std::isfinite(s.t_s) || !finite(s.hand_pos) || !finite(s.head_pos)) {restart();return false;}
        const Vec3 rel=s.hand_pos-s.head_pos; // the head's own motion is out of the signal; head turning leaves it unchanged
        if(!have_ || s.t_s<last_t_ || s.t_s-last_t_>max_gap_s) {restart();seed(s.t_s,rel,s);return false;}
        const double dt=s.t_s-last_t_;
        if(dt<min_dt_s) return false;
        const double raw=length(rel-last_rel_)/dt;
        if(raw>glitch_mps) {seed(s.t_s,rel,s);history_n_=0;return false;} // a tracking jump is not a movement: drop the sample, keep arming state
        // The head moving fast (a dodge, a lean, a turn) is not an attack, nor the 100 ms after it; the hand must re-arm afterwards.
        const double head_mps=length(s.head_pos-last_head_)/dt;
        double yaw_dps=0;
        if(length(s.head_fwd)>1e-6 && length(last_head_fwd_)>1e-6) {
            double c=dot(s.head_fwd,last_head_fwd_)/(length(s.head_fwd)*length(last_head_fwd_));
            c=c>1?1:c<-1?-1:c;yaw_dps=std::acos(c)*57.29578/dt;
        }
        if(head_mps>max_head_mps || yaw_dps>max_yaw_dps) guard_until_=s.t_s+head_guard_s;
        last_t_=s.t_s;last_rel_=rel;last_head_=s.head_pos;last_head_fwd_=s.head_fwd;
        speeds_[speed_i_++%3]=raw;if(speed_n_<3)++speed_n_;
        push_history(s.t_s,rel);
        if(speed_n_<3) return false;
        const double med=median3();
        speed_=med;
        if(med<rearm_mps) {if(slow_run_<rearm_samples)++slow_run_;} else slow_run_=0;
        if(slow_run_>=rearm_samples) armed_=true;
        if(s.t_s<guard_until_) {armed_=false;slow_run_=0;return false;}
        if(!press_free) return false;
        if(!armed_ || s.t_s<cooldown_until_ || !weapon_drawn) return false;
        bool slash=med>=slash_mps,stab=false;
        if(!slash && crouched && med>=stab_mps) stab=is_stab(s.hand_fwd);
        if(!slash && !stab) return false;
        armed_=false;slow_run_=0;cooldown_until_=s.t_s+cooldown_s;
        ++presses_;if(slash)++slash_;else ++stab_;
        return true;
    }
    // A sword contact on a guard the engine still calls unaware asks for the same press (the caller checked the contact speed and the
    // once-per-episode rule). A verified native offer bypasses gesture cooldown; the caller still owns a free exact-victim press.
    bool contact_press(double t_s,bool weapon_drawn,bool press_free=true,bool confirmed_native=false) noexcept {
        if(!weapon_drawn || !press_free || !std::isfinite(t_s) || (!confirmed_native && t_s<cooldown_until_)) return false;
        armed_=false;slow_run_=0;cooldown_until_=t_s+cooldown_s;++presses_;++contact_;
        return true;
    }
    std::uint64_t presses() const noexcept {return presses_;}
    std::uint64_t contacts() const noexcept {return contact_;}
    std::uint64_t slashes() const noexcept {return slash_;}
    std::uint64_t stabs() const noexcept {return stab_;}
    double speed() const noexcept {return speed_;}
private:
    static constexpr unsigned history_cap=64;
    bool have_{},armed_{};double last_t_{},cooldown_until_{},speed_{},guard_until_{};Vec3 last_rel_{},last_head_{},last_head_fwd_{};
    std::array<double,3> speeds_{};unsigned speed_i_{},speed_n_{},slow_run_{};
    std::array<double,history_cap> ht_{};std::array<Vec3,history_cap> hp_{};unsigned history_n_{},history_head_{};
    std::uint64_t presses_{},slash_{},stab_{},contact_{};
    void seed(double t,Vec3 rel,const AttackSample& s) noexcept {have_=true;last_t_=t;last_rel_=rel;last_head_=s.head_pos;last_head_fwd_=s.head_fwd;}
    void restart() noexcept {have_=false;armed_=false;speed_n_=speed_i_=slow_run_=0;history_n_=history_head_=0;speed_=0;}
    double median3() const noexcept {
        const double a=speeds_[0],b=speeds_[1],c=speeds_[2];
        return a<b?(b<c?b:(a<c?c:a)):(a<c?a:(b<c?c:b));
    }
    void push_history(double t,Vec3 rel) noexcept {
        ht_[history_head_]=t;hp_[history_head_]=rel;history_head_=(history_head_+1)%history_cap;
        if(history_n_<history_cap)++history_n_;
    }
    // Net travel, straightness and alignment over the last 400 ms of the relative path.
    bool is_stab(Vec3 fwd) const noexcept {
        const double flen=length(fwd);
        if(!(flen>1e-6) || history_n_<2) return false;
        const unsigned newest=(history_head_+history_cap-1)%history_cap;
        const double t_end=ht_[newest];
        double path=0;Vec3 first=hp_[newest],prev=first;
        for(unsigned k=0;k<history_n_;++k) { // newest to oldest, stopping outside the window
            const unsigned i=(newest+history_cap-k)%history_cap;
            if(t_end-ht_[i]>stab_window_s) break;
            if(k) path+=length(prev-hp_[i]);
            prev=hp_[i];first=hp_[i];
        }
        const Vec3 net=hp_[newest]-first;const double travel=length(net);
        if(travel<stab_min_travel_m || !(path>0) || travel/path<stab_min_straight) return false;
        return dot(net*(1./travel),fwd*(1./flen))>=stab_min_align;
    }
};

// The native attack is ignored while Block (RB) is held, and Block cancels a running slash. So a press first lets Block go for
// lead_ms, then holds the attack for min_hold_ms and keeps holding until the game has polled the pad at least twice since the attack
// went high (so the edge is never swallowed between two polls), capped at max_ms from the start. Block stays released while the
// press runs and for tail_ms after it. polls is a running count of the game's pad reads.
class AttackPress {
public:
    inline static constexpr std::uint64_t lead_ms=45,min_hold_ms=120,max_ms=500,tail_ms=350;
    inline static constexpr std::uint64_t min_polls=2;
    bool active() const noexcept {return active_;}
    // hold_ms: 0 = the ordinary 120 ms press. A charged sword strike passes a longer hold (the game turns a held attack
    // into its heavy attack; v963).
    bool start(std::uint64_t now_ms,std::uint64_t polls,std::uint64_t hold_ms=0) noexcept {
        (void)polls;
        if(active_) {++dropped_;return false;}
        active_=true;rt_=false;rt_started_=false;since_=now_ms;hold_ms_=hold_ms;end_ms_=0;++starts_;return true;
    }
    // Advance the state and return whether the attack button is held this frame.
    bool held(std::uint64_t now_ms,std::uint64_t polls) noexcept {
        if(!active_) return false;
        const std::uint64_t age=now_ms>=since_?now_ms-since_:max_ms;
        if(age>=max_ms) {finish(now_ms);return false;}
        if(age<lead_ms) return false; // Block is let go first
        if(!rt_started_) {rt_started_=true;rt_at_=now_ms;polls_at_rt_=polls;}
        const std::uint64_t hold=hold_ms_?hold_ms_:min_hold_ms;
        if(now_ms-rt_at_>=hold && polls-polls_at_rt_>=min_polls) {finish(now_ms);return false;}
        rt_=true;return true;
    }
    // Block (RB) must stay off while a press runs and shortly after.
    bool block_released(std::uint64_t now_ms) const noexcept {
        return active_ || (end_ms_ && now_ms>=end_ms_ && now_ms-end_ms_<tail_ms);
    }
    void reset() noexcept {active_=rt_=rt_started_=false;end_ms_=0;hold_ms_=0;}
    std::uint64_t starts() const noexcept {return starts_;}
    std::uint64_t dropped() const noexcept {return dropped_;}
private:
    bool active_{},rt_{},rt_started_{};std::uint64_t since_{},rt_at_{},polls_at_rt_{},end_ms_{},starts_{},dropped_{},hold_ms_{};
    void finish(std::uint64_t now_ms) noexcept {active_=rt_=false;end_ms_=now_ms?now_ms:1;}
};

// Whether the engine honoured a press: its assassination state (player FSM 13) was seen within window_ms after the press.
// Without any observation inside the window the press stays "not honoured" (an ordinary attack on an aware guard is that).
class AttackHonour {
public:
    inline static constexpr std::uint64_t window_ms=600;
    void press(std::uint64_t now_ms) noexcept {
        settle(now_ms,false);
        if(pending_) ++not_honoured_; // a newer press supersedes one the engine never took up
        pending_=true;at_=now_ms;seen_=false;
    }
    void observe(std::uint64_t now_ms,bool takedown_active) noexcept {
        settle(now_ms,takedown_active);
    }
    std::uint64_t honoured() const noexcept {return honoured_;}
    std::uint64_t not_honoured() const noexcept {return not_honoured_;}
private:
    bool pending_{},seen_{};std::uint64_t at_{},honoured_{},not_honoured_{};
    void settle(std::uint64_t now_ms,bool active) noexcept {
        if(!pending_) return;
        if(now_ms>=at_ && now_ms-at_<=window_ms && active) seen_=true;
        if(seen_) {++honoured_;pending_=false;}
        else if(now_ms<at_ || now_ms-at_>window_ms) {++not_honoured_;pending_=false;}
    }
};
}
