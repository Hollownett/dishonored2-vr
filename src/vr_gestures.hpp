#pragma once
#include <cmath>
#include <cstdint>

namespace d2vr {
// Minimal 3-vector for the pure gesture policy. Local so this header stays free
// of engine/OpenXR types; all coordinates are OpenXR stage/local metres, +Y up,
// -Z forward of the head as captured at calibration.
struct GV3 { float x,y,z; };
inline float dot(const GV3& a,const GV3& b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline GV3 sub(const GV3& a,const GV3& b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float len(const GV3& a) noexcept {return std::sqrt(dot(a,a));}
inline GV3 norm(const GV3& a) noexcept {
    const float l=len(a);
    return l>1e-6f?GV3{a.x/l,a.y/l,a.z/l}:GV3{0,0,0};
}

// One frame of already-resolved per-hand state. side 0 is the left (power/pistol)
// hand, side 1 the right (sword) hand; `allowed` is the caller's gameplay gate
// (no menu, wheel or cinematic) and target_valid marks a native highlighted grab.
struct GestureFrame {
    GV3 head_pos,head_fwd,head_right;
    GV3 pos[2],aim[2],up[2],vel[2];
    float grip[2],trigger[2];
    bool tracked[2];
    bool allowed;
    bool sword_equipped;
    bool target_valid[2];
    bool takedown_available;
    std::uint64_t now_ms;
};

// Scalar-speed edge detector with CrouchFlick hysteresis: fire once on the high
// threshold, stay spent until speed drops below the low threshold, and spend a
// motion that reaches the high threshold while disallowed so it can never fire
// late when allowed returns under a moving hand.
class SpeedFlick {
    float hi_,lo_;
    bool spent_{};
public:
    explicit SpeedFlick(float hi=0.9f,float lo=0.35f) noexcept : hi_(hi),lo_(lo) {}
    bool update(float speed,bool allowed) noexcept {
        if(speed<=lo_) {spent_=false;return false;}
        if(speed>=hi_) {if(spent_) return false;spent_=true;return allowed;}
        return false;
    }
    void spend() noexcept {spent_=true;}
};

// Anchor dwell: proximity to an anchor plus an orientation cone and stillness for
// dwell_ms, pulsing once and then staying latched until the hand leaves the wider
// leave radius, so a hand parked on the anchor toggles exactly once per approach.
class PoseHold {
    float radius_,leave_,still_,cone_cos_;
    std::uint64_t dwell_,since_{};
    bool latched_{};
public:
    PoseHold(float radius,float leave,std::uint64_t dwell,float still,float cone_cos=-2.f) noexcept
        : radius_(radius),leave_(leave),still_(still),cone_cos_(cone_cos),dwell_(dwell) {}
    bool update(float dist,float align,float speed,bool allowed,std::uint64_t now) noexcept {
        if(dist>leave_) {latched_=false;since_=0;return false;}
        if(!allowed || dist>radius_ || align<cone_cos_ || speed>=still_) {since_=0;return false;}
        if(latched_) return false;
        if(!since_) since_=now;
        if(now-since_>=dwell_) {latched_=true;since_=0;return true;}
        return false;
    }
    bool latched() const noexcept {return latched_;}
};

// Hand-to-hand closure: pulse when the separation falls to dist at a closing
// speed of at least close, then re-arm only after the hands separate again and
// the cooldown has elapsed, so a held clap cannot machine-gun.
class ConvergeDiverge {
    float dist_,close_;
    std::uint64_t cooldown_,until_{};
    bool armed_{true};
public:
    ConvergeDiverge(float dist,float close,std::uint64_t cooldown) noexcept
        : dist_(dist),close_(close),cooldown_(cooldown) {}
    bool update(float separation,float closing,bool allowed,std::uint64_t now) noexcept {
        if(separation>dist_*1.6f) armed_=true;
        if(!allowed) return false;
        if(armed_ && separation<=dist_ && closing>=close_ && now>=until_) {
            armed_=false;until_=now+cooldown_;return true;
        }
        return false;
    }
};

// Sword block pose: the blade held roughly horizontal across the body (aim off
// world up, aim strongly along +-head_right), in front of the chest and face,
// hand slow. Enter after 80 ms dwell, leave after 150 ms out of pose, with the
// angle limits loosened by 0.1 while on; !allowed, !equipped or !tracked is an
// immediate off and clears the dwell.
class SwordBlockPose {
    std::uint64_t in_since_{},out_since_{};
    bool on_{},in_dwell_{},out_dwell_{};
public:
    bool update(const GV3& head_pos,const GV3& head_fwd,const GV3& head_right,
            const GV3& pos,const GV3& aim,float speed,bool equipped,bool allowed,bool tracked,
            std::uint64_t now) noexcept {
        if(!allowed || !equipped || !tracked) {
            on_=false;in_since_=0;out_since_=0;in_dwell_=false;out_dwell_=false;return false;
        }
        // The sword held in front of the body in any orientation blocks (user, headset v349: "block any attack just by
        // holding the sword in front"); a hand at the side or a fast swing does not.
        // Moving or still (user, headset v353: "any blade movement in front is a block"); the attack press itself
        // releases it (VrCombat: rb only while no rt).
        (void)aim;(void)speed;
        const GV3 d=sub(pos,head_pos);
        const float fwd=dot(d,head_fwd);
        const bool inpose=std::fabs(dot(d,head_right))<=0.6f
            && fwd>=0.05f && fwd<=0.85f
            && pos.y>=head_pos.y-0.8f && pos.y<=head_pos.y+0.5f;
        if(inpose) {
            out_dwell_=false;out_since_=0;
            if(on_) return true;
            if(!in_dwell_) {in_dwell_=true;in_since_=now;}
            if(now-in_since_>=80) {on_=true;in_dwell_=false;in_since_=0;return true;}
            return false;
        }
        in_dwell_=false;in_since_=0;
        if(!on_) return false;
        if(!out_dwell_) {out_dwell_=true;out_since_=now;}
        if(now-out_since_>=150) {on_=false;out_dwell_=false;out_since_=0;return false;}
        return true;
    }
    bool held() const noexcept {return on_;}
};

// Throat-slash gesture takedown: the right hand, already in front of the head,
// swipes hard sideways (mostly horizontal, mostly along +-head_right) for at
// least two consecutive frames inside a 200 ms window while the native takedown
// prompt is up. One pulse per motion, then spent until the hand stops for 150 ms;
// an 800 ms cooldown and the CrouchFlick rule spend a motion that began while the
// prompt, `allowed` or tracking was absent.
class ThroatSlash {
    std::uint64_t fast_since_{},slow_since_{},cooldown_until_{};
    int count_{};
    bool spent_{};
public:
    bool update(const GV3& head_pos,const GV3& head_fwd,const GV3& head_right,
            const GV3& pos,const GV3& vel,bool available,bool allowed,bool tracked,
            std::uint64_t now) noexcept {
        const float sp=len(vel);
        if(sp<0.5f) {
            if(!slow_since_) slow_since_=now;
            if(now-slow_since_>=150) {spent_=false;count_=0;fast_since_=0;}
        } else slow_since_=0;
        const bool arm=allowed && available && tracked;
        if(sp>=1.4f && !arm) spent_=true;
        if(!arm || spent_) {fast_since_=0;count_=0;return false;}
        const float horiz=std::sqrt(vel.x*vel.x+vel.z*vel.z);
        const GV3 d=sub(pos,head_pos);
        const float fwd=dot(d,head_fwd);
        const GV3 dir=sp>1e-6f?GV3{vel.x/sp,vel.y/sp,vel.z/sp}:GV3{0,0,0};
        const bool fast=sp>=1.4f && fwd>0.f && fwd<=0.75f
            && std::fabs(vel.y)<0.5f*horiz && std::fabs(dot(dir,head_right))>0.7f;
        if(!fast) {count_=0;return false;}
        if(count_==0) fast_since_=now;
        else if(now-fast_since_>200) {fast_since_=now;count_=1;}
        ++count_;
        if(count_>=2 && now>=cooldown_until_) {
            spent_=true;count_=0;cooldown_until_=now+800;return true;
        }
        return false;
    }
};

enum class PowerGesture {none,cast_push,cast_yoink,temple_toggle,clap};

// Left-hand power recognition, composed from the primitives. Push is an open
// palm driven forward over >=12 cm inside 300 ms from near the head; the Blink
// yoink is a flick toward the head only while LT has been held 150 ms; the temple
// toggle is a PoseHold on the left temple; the clap is a ConvergeDiverge of both
// hands. Every pulse is suppressed while disallowed, untracked or over a native
// grab target, and a 350 ms global cooldown separates any two pulses. Motions
// that start disallowed are spent, never queued.
class PowerGestures {
    PoseHold temple_;
    ConvergeDiverge clap_;
    std::uint64_t cooldown_until_{},push_since_{},trig_since_{};
    GV3 push_start_{};
    bool push_active_{},push_spent_{},trig_held_{},yoink_spent_{};
public:
    PowerGestures() noexcept : temple_(0.14f,0.25f,300,0.3f),clap_(0.10f,0.8f,600) {}
    PowerGesture update(const GestureFrame& f) noexcept {
        const GV3& pos=f.pos[0];
        const float grip=f.grip[0];
        const float head_dist=len(sub(pos,f.head_pos));
        const float fspeed=dot(f.vel[0],f.head_fwd);
        const bool gate=!f.allowed || !f.tracked[0] || f.target_valid[0];

        if(f.trigger[0]>=0.6f) {if(!trig_held_) {trig_held_=true;trig_since_=f.now_ms;}}
        else trig_held_=false;

        bool fired_push=false;
        if(push_active_) {
            const float travel=dot(sub(pos,push_start_),f.head_fwd);
            if(grip>=0.3f || (f.now_ms-push_since_>300 && travel<0.12f)) push_active_=false;
            else if(travel>=0.12f) {push_active_=false;push_spent_=true;fired_push=true;}
        }
        if(!push_active_ && !push_spent_ && grip<0.3f && fspeed>=1.3f && head_dist<=0.45f) {
            if(gate) push_spent_=true;
            else {push_active_=true;push_start_=pos;push_since_=f.now_ms;}
        }
        if(push_spent_ && fspeed<0.5f) push_spent_=false;

        bool fired_yoink=false;
        {
            const float tspeed=dot(f.vel[0],norm(sub(f.head_pos,pos)));
            if(tspeed<0.4f) yoink_spent_=false;
            if(tspeed>=0.9f && trig_held_ && f.now_ms-trig_since_>=150) {
                if(!yoink_spent_) {yoink_spent_=true;fired_yoink=true;}
            }
        }

        GV3 anchor=f.head_pos;
        anchor.x-=f.head_right.x*0.09f;anchor.y-=f.head_right.y*0.09f;anchor.z-=f.head_right.z*0.09f;
        anchor.y+=0.04f;
        const bool fired_temple=temple_.update(len(sub(pos,anchor)),1.f,len(f.vel[0]),
            f.allowed && f.tracked[0],f.now_ms);

        const GV3 d01=sub(f.pos[0],f.pos[1]);
        const float sep=len(d01);
        const float closing=-dot(sub(f.vel[0],f.vel[1]),norm(d01));
        const bool fired_clap=clap_.update(sep,closing,f.allowed && f.tracked[0] && f.tracked[1],f.now_ms);

        const PowerGesture out=fired_push?PowerGesture::cast_push
            :fired_yoink?PowerGesture::cast_yoink
            :fired_temple?PowerGesture::temple_toggle
            :fired_clap?PowerGesture::clap
            :PowerGesture::none;
        if(out==PowerGesture::none) return out;
        if(gate || f.now_ms<cooldown_until_) return PowerGesture::none;
        cooldown_until_=f.now_ms+350;
        return out;
    }
};

// Aggregate dispatcher: sword block on the right hand, throat-slash takedown on
// the right hand, power gestures on the left. Pure policy, no side effects beyond
// its own latches, so the caller stays the only thing that talks to the game.
struct GestureOut { bool block_held; bool takedown_pulse; PowerGesture power; };

class GestureSet {
    SwordBlockPose block_;
    ThroatSlash takedown_;
    PowerGestures powers_;
public:
    GestureOut update(const GestureFrame& f) noexcept {
        GestureOut o{};
        o.block_held=block_.update(f.head_pos,f.head_fwd,f.head_right,
            f.pos[1],f.aim[1],len(f.vel[1]),f.sword_equipped,f.allowed,f.tracked[1],f.now_ms);
        o.takedown_pulse=takedown_.update(f.head_pos,f.head_fwd,f.head_right,
            f.pos[1],f.vel[1],f.takedown_available,f.allowed,f.tracked[1],f.now_ms);
        o.power=powers_.update(f);
        return o;
    }
};
} // namespace d2vr
