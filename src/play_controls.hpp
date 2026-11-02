#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "tracked_pose_lifetime.hpp"

namespace d2vr {
// One attack per slow-to-fast motion. Interrupted motion must settle before
// returning gameplay can arm another pulse.
class MotionSwordSwing {
    bool armed_{};
    std::uint64_t epoch_{},source_{},now_{},until_{},ready_{};
public:
    struct Result {bool active{},started{};std::uint64_t until{};};
    Result update(float speed,bool velocity_valid,bool allowed,std::uint64_t epoch,
                  std::uint64_t source,std::uint64_t now) noexcept {
        const bool continuous=epoch && epoch==epoch_ && now>=now_ && source>=source_;
        if(!continuous) {armed_=false;until_=ready_=0;source_=0;}
        epoch_=epoch;now_=now;
        const bool valid=epoch && velocity_valid && std::isfinite(speed) && speed>=0 && source &&
            now>=source && now-source<=tracked_pose_freshness_ms;
        if(!valid || !allowed) {armed_=false;until_=0;source_=source;return {};}
        bool started=false;
        if(source!=source_) {
            source_=source;
            if(speed<=.75f) armed_=true;
            else if(speed>2.f && armed_) {
                armed_=false;
                if(now>=ready_) {until_=now+250;ready_=now+300;started=true;}
            }
        }
        return {until_ && now<until_,started,until_};
    }
};
// Right-stick push down owns one native crouch pulse. Crouch is a native toggle,
// so a flick must fire exactly once per push: the pulse runs 120 ms and then
// stays spent until the stick returns past -0.3. A flick that began while the
// stick was disallowed is spent as well, so allowed returning under a held stick
// can never pulse late.
class CrouchFlick {
    enum class State {ready,active,spent};
    State state_{State::ready};
    std::uint64_t until_{};
public:
    bool update(float turn_y,bool allowed,std::uint64_t now) noexcept {
        if(state_==State::active) {
            if(!allowed || now>=until_) state_=State::spent;
            else return true;
        } else if(state_==State::ready && turn_y<=-.7f) {
            if(allowed) {until_=now+120;state_=State::active;return true;}
            state_=State::spent;
        }
        if(state_==State::spent && turn_y>=-.3f) state_=State::ready;
        return false;
    }
};
// v779 (user, headset v778: "jump is not A any more but the right stick pushed up; holding A opens the weapon wheel, mirrored with the left
// controller"), dual wield: the right stick pushed up holds the game's jump (A) while it stays up (held: swims up, climbs, as a held A). Up past
// .7 and more up than sideways (a snap turn never jumps), let go under .3; a push begun while not allowed waits for the centre, as the crouch.
class JumpStick {
    bool down_{},spent_{};
public:
    bool update(float turn_x,float turn_y,bool allowed) noexcept {
        if(down_) {if(!allowed || turn_y<.3f) {down_=false;spent_=turn_y>=.3f;} return down_;}
        if(spent_) {if(turn_y<.3f) spent_=false;return false;}
        if(turn_y>=.7f && turn_y>=std::fabs(turn_x)) {if(allowed) down_=true;else spent_=true;}
        return down_;
    }
};
// Physical B sheathes the weapons by holding the native Interact button (X) for
// hold_ms. Only a fresh B press while allowed, with no target and no held
// trigger, starts it; a tap is enough because the hold then runs to completion.
// A lost allowed, a target appearing or a trigger pressed cancels the hold at
// once, and the press stays claimed until B is released, so neither a cancelled
// nor a completed hold can be restarted by the same held B.
class HolsterHold {
    std::uint64_t hold_ms_{},until_{};
    bool down_{},active_{},claimed_{};
public:
    explicit HolsterHold(std::uint64_t hold_ms=1000) noexcept : hold_ms_(hold_ms) {}
    bool update(bool b,bool allowed,bool target_valid,bool trigger_held,std::uint64_t now) noexcept {
        const bool edge=b && !down_;
        down_=b;
        if(active_ && (!allowed || target_valid || trigger_held || now>=until_)) {active_=false;claimed_=true;}
        if(!b) claimed_=false;
        else if(edge && !active_ && !claimed_ && allowed && !target_valid && !trigger_held) {
            active_=true;until_=now+hold_ms_;
        }
        return active_;
    }
};
// Per-side guard that keeps a grab from firing the trigger by accident. A grip
// press (.55) arms that side for 400 ms: any trigger press (.55) inside the
// window, or at any time while the side already holds an object, is consumed
// until that trigger releases. A press that began before the arm is never
// upgraded mid-press, so blocking and then counter-attacking still works, and
// swing_blocked() lets the caller suppress the motion sword swing while grabbing.
class GripTriggerGuard {
    std::array<bool,2> grip_down_{},trig_down_{},grabbed_{},consume_{};
    std::array<std::uint64_t,2> grip_since_{};
public:
    std::array<bool,2> update(const std::array<float,2>& grip,const std::array<float,2>& trigger,
            const std::array<bool,2>& holding,std::uint64_t now) noexcept {
        for(unsigned side=0;side<2;++side) {
            if(!grip_down_[side]) {
                if(grip[side]>=.55f) {grip_down_[side]=true;grip_since_[side]=now;}
            } else if(grip[side]<=.35f) grip_down_[side]=false;
            if(!trig_down_[side]) {
                if(trigger[side]>=.55f) {
                    trig_down_[side]=true;
                    consume_[side]=grip_down_[side]
                        && (holding[side] || (now>=grip_since_[side] && now-grip_since_[side]<=400));
                }
            } else if(trigger[side]<=.35f) {trig_down_[side]=false;consume_[side]=false;}
        }
        grabbed_=holding;
        return consume_;
    }
    bool swing_blocked(int side,std::uint64_t now) const noexcept {
        if(side<0 || side>1) return false;
        const unsigned s=static_cast<unsigned>(side);
        return grip_down_[s] && (grabbed_[s] || (now>=grip_since_[s] && now-grip_since_[s]<=400));
    }
};
// Physical crouch: sitting or crouching in real life crouches the character. Edge-triggered so the
// stick crouch is never fought: dropping the head below `down` metres under the calibrated standing
// height pulses the native crouch toggle (B) when the game is not already crouched; rising above
// `up` stands up again only if this crouch was physical. A refused pulse (mid-air, ceiling) retries
// every second, at most three times. compensate() is true while the camera should be lifted back
// by the game's own crouch drop (physically low and natively crouched), so the view keeps the real
// head height.
class PhysicalCrouch {
    bool low_{},ours_{};std::uint64_t until_{},retry_{};unsigned tries_{};
    bool pulse(std::uint64_t now) noexcept {until_=now+120;retry_=now+1000;++tries_;return true;}
public:
    bool update(double drop,bool crouched,bool forced,bool allowed,std::uint64_t now,double down=.35,double up=.22) noexcept {
        if(now<until_) return allowed;
        if(!allowed) return false;
        const bool was=low_;
        if(!low_ && drop>down) low_=true;else if(low_ && drop<up) low_=false;
        if(low_ && !was) {tries_=0;ours_=true;if(!crouched) return pulse(now);return false;}
        if(!low_ && was) {tries_=0;if(ours_ && crouched && !forced) return pulse(now);ours_=false;return false;}
        if(low_ && ours_ && !crouched && now>=retry_ && tries_<3) return pulse(now);
        if(!low_ && ours_) {
            if(!crouched) ours_=false;
            else if(!forced && now>=retry_ && tries_<3) return pulse(now);
        }
        return false;
    }
    bool low() const noexcept {return low_;}
    bool compensate(bool crouched) const noexcept {return low_ && crouched;}
    void reset() noexcept {low_=ours_=false;until_=retry_=0;tries_=0;}
};
// Lift for the camera while physically crouched (PhysicalCrouch::compensate): follows the game's own
// crouch animation through a 600 ms window after the crouch state flips, measured on the native camera
// height along world up, then freezes at the measured drop (so stairs while crouch-walking do not leak
// into it). Standing up ramps it back to zero the same way.
class CrouchLift {
    bool active_{};double ref_{},drop_{.5},lift_{};std::uint64_t since_{};bool ramping_{};
public:
    double update(bool compensate,double camera_up,std::uint64_t now) noexcept {
        if(compensate!=active_) {
            active_=compensate;since_=now;ramping_=true;
            ref_=camera_up; // the standing height, or the crouched height when standing up
        }
        if(ramping_) {
            const double moved=compensate?ref_-camera_up:camera_up-ref_;
            if(compensate) lift_=std::clamp(moved,0.,.9);
            else lift_=std::clamp(drop_-moved,0.,drop_);
            if(now-since_>=600) {
                ramping_=false;
                if(compensate) {if(lift_>.15) drop_=lift_;lift_=drop_;} else lift_=0;
            }
        }
        return lift_;
    }
    double lift() const noexcept {return lift_;}
};
// Hand dial for the native quick-access wheel (after the Dishonored 1 VR weapon dial): while the wheel
// is open, the hand that holds it selects by moving toward a wedge. The panel frame (head right and
// world up) and the hand position are frozen when the wheel opens; beyond `dead` metres the direction
// is fed to the native wheel as a full stick deflection. A deliberate right-stick input wins, and a lost
// hand pose keeps the last direction instead of inventing one.
class WheelDial {
    bool open_{};std::array<double,3> origin_{},right_{};std::array<float,2> last_{};bool has_last_{};
public:
    // hand/head_right: tracked positions/vectors (y up); returns true and fills x,y while the dial points.
    bool update(bool wheel_open,bool tracked,const std::array<double,3>& hand,const std::array<double,3>& head_right,
                float& x,float& y,double dead=.035) noexcept {
        if(!wheel_open) {open_=false;has_last_=false;return false;}
        if(!open_) {
            if(!tracked) return false;
            open_=true;origin_=hand;
            const double h=std::sqrt(head_right[0]*head_right[0]+head_right[2]*head_right[2]);
            right_=h>1e-6?std::array<double,3>{head_right[0]/h,0,head_right[2]/h}:std::array<double,3>{1,0,0};
            return false;
        }
        if(tracked) {
            const double dx=hand[0]-origin_[0],dy=hand[1]-origin_[1],dz=hand[2]-origin_[2];
            const double u=dx*right_[0]+dz*right_[2],v=dy,len=std::sqrt(u*u+v*v);
            if(len<dead) {has_last_=false;return false;}
            last_={static_cast<float>(u/len),static_cast<float>(v/len)};has_last_=true;
        }
        if(!has_last_) return false;
        x=last_[0];y=last_[1];return true;
    }
};
}
