#pragma once
// Track T item A: a grenade leaves the throwing hand with the hand's own motion. Pure state (no engine, no clock): the probe feeds a
// per-frame hand sample (world position and velocity RELATIVE to the player, game metres / m/s) and the trigger; the native Throw
// hook asks take() when the game finally launches (about 0.3 s after the release: the throw animation), and gets
// gain x (peak hand speed over the 0.10 s before the release) + the player's own velocity, capped at the game's own throw speed.
// No valid release in the last 1.5 s, or no sample in that window: ok=false and the native throw is untouched.
// Ideas (gain/window/cap, release-time capture) follow the measured facts in .superpowers/sdd/2026-10-25-small-objects-fingers/
// mines-grenades-research.md; no code is shared with that GPL project.
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
struct ThrowGestureParams {
    double gain=1.5,window_ms=100,cap_mps=17,stale_ms=1500,teleport_mps=12;
    float hold_on=.55f,hold_off=.35f;
};
struct ThrowGestureResult {bool ok{};std::array<double,3> velocity{},spawn{};double hand_speed{};std::uint64_t delay_ms{};}; // delay: release -> the game's launch
// How hard a gadget hand squeezes (user, headset v671: "squeeze a fist, then throw like a normal grenade"): with the grip held it is
// the fist (both), so opening the hand lets go; without it, the trigger alone (the old way).
inline float gadget_squeeze(float grip,float trigger) noexcept {return grip>.55f?(grip<trigger?grip:trigger):trigger;}
// v688 (user, headset v686: the grenade left late): a squeeze that began as a fist ends when EITHER finger opens. Stateless,
// a grip falling under .55 handed the squeeze to the index finger alone, so a lagging index held the throw back.
// After an opened fist the squeeze stays released until the index lets go too (or the fist closes hard again): a resting index
// must not cook the next gadget.
class GadgetFist {
    bool fist_{},wait_{};
public:
    float operator()(float grip,float trigger) noexcept {
        const float both=grip<trigger?grip:trigger;
        if(wait_) {
            if(gadget_squeeze(grip,trigger)<.35f || (grip>.8f && trigger>=.55f)) wait_=false;
            else return 0.f;
        }
        if(fist_) {
            if(both<.35f) {fist_=false;wait_=true;}
            return both;
        }
        if(grip>.55f && trigger>=.55f) {fist_=true;return both;}
        return gadget_squeeze(grip,trigger);
    }
};
// Dual wield (v771, user, headset v770): a grenade or mine is held by the grip, as any weapon. The trigger pressed while holding arms it: a
// grenade then aims natively (the game's left trigger held) and is thrown when the grip opens; a mine is placed (a short native trigger tap,
// aimed at the palm by NativeGadgetAim). A grip let go unarmed drops it (NativeGadgetDrop: put down or tossed, unarmed).
// v779 (user, headset v778: "a mine from the wheel places badly, only after I toss it to the other hand"; the journal: 25 trigger presses with
// the grip open, none placed): a mine is placed by the trigger with or without the grip, except in the frame the grip closes (a catch).
class GadgetGrip {
    bool held_{},armed_{},trig_down_{},present_{},mode_pending_{},mode_hold_{};std::uint64_t tap_until_{},mode_since_{};
public:
    // Task 7 (user: "mine throw should attach mine, like in the flat game"): a mine let go unarmed while the hand moves faster than mine_throw_min_mps
    // is not dropped (the mod's own loot-pickup flight never attaches): throw_place starts the native placement tap (the probe aims it along the throw).
    static constexpr double mine_throw_min_mps=1.5;
    // Task 6 (DOTO Hook Mine, user: hold the use button to switch lethal / non-lethal, tap to throw). The flat game (R7, controller decl
    // m_statesMapping): ACTION_POWER_WEAPON down enters ChangeWeaponState; released before its hold time (decl+0x90 = 0.3 s) the state ends and the
    // release throws (PLACE_WEAPON_AFAR); held 0.3 s the mode flips and the button is consumed (no throw). So with mode_capable a mine's trigger is
    // passed through: the game's LT follows the trigger from the press (at least 80 ms, so a flick registers) and the game decides. mode_hold marks the
    // game's switch point (mode_hold_ms, for haptics/status), mode_switched the release after it. The grip opening releases LT and does nothing else.
    struct Out {bool native_trigger{},drop{},held{},armed{},throw_place{},mode_hold{},mode_switched{};};
    Out update(bool gadget,bool grenade,float grip,float trigger,std::uint64_t now_ms,double hand_speed_mps=0,bool mode_capable=false,std::uint64_t mode_hold_ms=300) noexcept {
        Out o{};
        if(!gadget) {held_=armed_=trig_down_=present_=mode_pending_=mode_hold_=false;tap_until_=0;return o;}
        const bool was=held_;held_=grip>=(held_?.35f:.55f);
        const bool trig=trigger>=(trig_down_?.35f:.55f),edge=trig && !trig_down_ && present_;trig_down_=trig;present_=true; // a trigger already down when it arrives presses nothing
        if(edge) {if(!grenade) {if(was || !held_) {if(mode_capable) {mode_pending_=true;mode_since_=now_ms;} else tap_until_=now_ms+150;}} else if(was && held_) armed_=true;} // a resting index at the grab arms nothing
        if(grenade) {
            o.native_trigger=armed_ && held_;   // the grip opening releases the game's trigger: the throw
            if(was && !held_) {o.drop=!armed_;armed_=false;}
        } else {
            if(!mode_capable) mode_pending_=mode_hold_=false;
            if(mode_pending_) {   // the press, passed through (mode_pending_ = LT down for the game)
                const auto age=now_ms-mode_since_;
                if(was && !held_) {mode_pending_=mode_hold_=false;o.held=held_;o.armed=armed_;return o;}   // the grip opened: LT released, the game's own release
                if(!trig && age>=80) {                                                                     // released (after the 80 ms minimum)
                    mode_pending_=false;o.mode_switched=mode_hold_;mode_hold_=false;o.held=held_;o.armed=armed_;return o;
                }
                if(trig && age>=mode_hold_ms) mode_hold_=true;                                             // the game's switch point
                o.native_trigger=true;o.mode_hold=mode_hold_;o.held=held_;o.armed=armed_;return o;
            }
            if(was && !held_ && now_ms>=tap_until_) {
                if(hand_speed_mps>mine_throw_min_mps) {o.throw_place=true;tap_until_=now_ms+150;} else o.drop=true;
            }
            o.native_trigger=now_ms<tap_until_;
        }
        o.held=held_;o.armed=armed_;return o;
    }
};
class ThrowGesture {
    static constexpr unsigned history=32;
    struct Sample {std::uint64_t t;std::array<double,3> v;};
    ThrowGestureParams p_;
    std::array<Sample,history> ring_{};unsigned head_{},count_{};
    std::array<double,3> pos_{};bool have_pos_{};std::uint64_t pos_t_{};
    bool held_{},released_{};std::uint64_t release_t_{};std::array<double,3> peak_{};bool peak_ok_{};
    static double norm(const std::array<double,3>& v) noexcept {return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
    static bool finite(const std::array<double,3>& v) noexcept {return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);}
public:
    explicit ThrowGesture(const ThrowGestureParams& p={}) : p_(p) {}
    void reset() noexcept {head_=count_=0;held_=released_=false;peak_ok_=false;have_pos_=false;}
    // velocity relative to the player; samples over the teleport speed (a blink, a snap turn) restart the history.
    void note_hand(std::uint64_t now,const std::array<double,3>& pos,const std::array<double,3>& velocity) noexcept {
        if(!finite(pos) || !finite(velocity)) {count_=0;have_pos_=false;return;}
        pos_=pos;have_pos_=true;pos_t_=now;
        if(norm(velocity)>p_.teleport_mps) {count_=0;return;}
        ring_[head_]={now,velocity};head_=(head_+1)%history;if(count_<history) ++count_;
    }
    // The trigger: a release (held -> below hold_off) captures the peak over the window before it. A new squeeze discards an unused one.
    void note_trigger(std::uint64_t now,float trigger) noexcept {
        if(!held_ && trigger>=p_.hold_on) {held_=true;released_=false;}
        else if(held_ && trigger<p_.hold_off) {
            held_=false;released_=true;release_t_=now;peak_ok_=false;peak_={};double best=-1;
            for(unsigned i=0;i<count_;++i) {
                const auto& s=ring_[(head_+history-1-i)%history];
                if(now<s.t || now-s.t>p_.window_ms) continue;
                const double n=norm(s.v);if(n>best) {best=n;peak_=s.v;peak_ok_=true;}
            }
        }
    }
    bool pending(std::uint64_t now) const noexcept {return released_ && now>=release_t_ && now-release_t_<=p_.stale_ms;}
    // The game launches the grenade now. Consumes the release.
    ThrowGestureResult take(std::uint64_t now,const std::array<double,3>& player_velocity) noexcept {
        ThrowGestureResult out{};
        if(!pending(now) || !peak_ok_ || !have_pos_ || now<pos_t_ || now-pos_t_>250 || !finite(player_velocity)) return out;
        released_=false;out.delay_ms=now-release_t_;
        out.hand_speed=norm(peak_);
        for(unsigned i=0;i<3;++i) out.velocity[i]=peak_[i]*p_.gain+player_velocity[i];
        const double n=norm(out.velocity);
        if(n>p_.cap_mps) for(double& c:out.velocity) c*=p_.cap_mps/n;
        out.spawn=pos_;out.ok=true;return out;
    }
};
} // namespace d2vr
