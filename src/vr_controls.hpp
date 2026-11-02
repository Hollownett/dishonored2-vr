#pragma once
#include <optional>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "grip_decision.hpp"

namespace d2vr {
// Physical Quest X owns the native weapon/power wheel: the caller turns a held
// wheel() into a held native LB and equips on that LB release. Opens only on a
// fresh X edge while allowed; losing allowed cancels the press (wheel closes
// without equipping) and the press stays claimed until X is released, so a
// tracking/focus/menu interruption can never late-open the wheel.
// A positive hold_ms delays opening (unused since v779: the right-hand A wheel opens at once, the jump is on the
// right stick); the button passes through natively until x_consumed() turns true.
class XWheelOwner {
    bool down_{},claimed_{},open_{},ignored_{};std::uint64_t since_{},hold_ms_{};
public:
    explicit XWheelOwner(std::uint64_t hold_ms=0) noexcept : hold_ms_(hold_ms) {}
    void update(bool physical_x,bool allowed,std::uint64_t now_ms=0) noexcept {
        if(!physical_x) {down_=claimed_=open_=ignored_=false;return;}
        if(!down_) {down_=true;since_=now_ms;ignored_=!allowed;}
        else if(!allowed) {open_=false;ignored_=true;}
        if(!ignored_ && !claimed_ && now_ms-since_>=hold_ms_) {claimed_=open_=true;}
    }
    bool wheel() const noexcept {return open_;}
    bool x_consumed() const noexcept {return claimed_;}
    void reset() noexcept {down_=claimed_=open_=ignored_=false;}
    void commit() noexcept {if(open_) {open_=false;ignored_=true;}}
};
// Both hands open the same wheel, each at once: left X, right A (dual wield; v779, user, headset v778: "holding A opens the wheel, mirrored
// with the left controller"; was B after a 350 ms hold). The first hand to open owns selection until its button is released; the other
// button is then ignored (never late-opens).
class WheelHands {
    XWheelOwner left_,right_;
public:
    void update(bool x,bool a,bool allowed,std::uint64_t now_ms) noexcept {
        left_.update(x,allowed && !right_.wheel(),now_ms);
        right_.update(a,allowed && !left_.wheel(),now_ms);
    }
    bool wheel() const noexcept {return left_.wheel() || right_.wheel();}
    int hand() const noexcept {return left_.wheel()?0:right_.wheel()?1:-1;}
    bool a_consumed() const noexcept {return right_.x_consumed();}
    // Cursor-hand trigger commit: closes the wheel like a button release (the button stays claimed until released).
    void commit() noexcept {left_.commit();right_.commit();}
    void reset() noexcept {left_.reset();right_.reset();}
};
// Wheel selection feed: the owning hand's opposite thumb stick (left-owned: right stick, as native; right-owned: left stick, since the
// right thumb holds the button); the other stick is dropped. The hand's dial wins unless the stick is used on purpose (v771, headset v770:
// a hand's choice was sometimes lost): left-owned, the right stick only past .6 (a resting thumb no longer steals it); right-owned, the left
// stick is locomotion and selects only while the hand has not moved (dial off).
// hand_only (dual wield's hand wheel; v772, user: "the hand moved toward the item, not the sticks"): no stick ever selects; until the hand
// moves (dial off) nothing is selected.
inline void wheel_select(int owner,float& turn_x,float& turn_y,float& move_x,float& move_y,bool dial,float dx,float dy,bool hand_only=false) noexcept {
    if(hand_only) {move_x=move_y=0;turn_x=dial?dx:0.f;turn_y=dial?dy:0.f;return;}
    if(owner==1) {turn_x=move_x;turn_y=move_y;}
    move_x=move_y=0;
    if(dial && (owner==1 || turn_x*turn_x+turn_y*turn_y<.36f)) {turn_x=dx;turn_y=dy;}
}
// Two-handed pointing cursor for the open wheel. Each hand's offset from where it was when the wheel
// opened (head-right/up plane) is its pointing direction. The hand that moves meaningfully becomes the
// cursor and keeps it until the other hand moves faster; inside `dead` metres the cursor is neutral.
// A fresh trigger press of the cursor hand (edge seen while open) commits. Stick override: wheel_select.
// v776 (user, headset v775: turning the hand changed nothing): the dual-wield wheel no longer uses this position cursor, its hand POINTS
// (WheelPointer::update_plane); nothing in the game calls it any more, it stays with its lab.
class WheelCursor {
    bool open_{},has_[2]{},down_[2]{};std::array<std::array<double,3>,2> origin_{},prev_{};
    std::array<double,3> right_{1,0,0},up_{0,1,0};int active_{-1};std::array<float,2> last_{};bool have_last_{};
public:
    int cursor() const noexcept {return active_;}
    // plane_up: the drawn ring's up axis (head_right is then its right axis), so a hand moving along the ring moves the selection the
    // same way (v762, headset v761: seen from above, world up compressed the ring's bottom). Without it: head-right made level, world up.
    bool update(bool wheel_open,const std::array<bool,2>& tracked,const std::array<std::array<double,3>,2>& hand,
                const std::array<double,3>& head_right,float& x,float& y,double dead=.035,double move=.004,
                const std::array<double,3>* plane_up=nullptr) noexcept {
        if(!wheel_open) {open_=has_[0]=has_[1]=have_last_=false;active_=-1;return false;}
        if(!open_) {
            open_=true;
            const auto unit=[](const std::array<double,3>& v,const std::array<double,3>& fallback) {
                const double l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);return l>1e-6?std::array<double,3>{v[0]/l,v[1]/l,v[2]/l}:fallback;};
            if(plane_up) {right_=unit(head_right,{1,0,0});up_=unit(*plane_up,{0,1,0});}
            else {right_=unit({head_right[0],0,head_right[2]},{1,0,0});up_={0,1,0};}
        }
        double speed[2]{};
        for(int i=0;i<2;++i) {
            if(!tracked[i]) continue;
            if(!has_[i]) {has_[i]=true;origin_[i]=prev_[i]=hand[i];continue;}
            const auto& a=hand[i];const auto& b=prev_[i];
            speed[i]=std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));prev_[i]=a;
        }
        for(int i=0;i<2;++i) if(speed[i]>=move && (active_<0 || (i!=active_ && speed[i]>speed[active_]))) active_=i;
        if(active_<0 && tracked[0]!=tracked[1]) active_=tracked[0]?0:1; // a lone hand (dual wield: the wheel's own) needs no fast first move
        if(active_<0 || !tracked[active_] || !has_[active_]) return false;
        const auto& h=hand[active_];const auto& o=origin_[active_];const std::array<double,3> d{h[0]-o[0],h[1]-o[1],h[2]-o[2]};
        const double u=d[0]*right_[0]+d[1]*right_[1]+d[2]*right_[2],v=d[0]*up_[0]+d[1]*up_[1]+d[2]*up_[2],len=std::sqrt(u*u+v*v);
        // Back inside the dead zone the last choice holds (v763, headset v762: a hand drifting back while the button let go committed nothing).
        if(len<dead) {if(!have_last_) return false;x=last_[0];y=last_[1];return true;}
        x=static_cast<float>(u/len);y=static_cast<float>(v/len);last_={x,y};have_last_=true;return true;
    }
    // True once on a fresh trigger press of the cursor hand while the wheel is open.
    bool commit(bool wheel_open,const std::array<float,2>& trigger) noexcept {
        bool out=false;
        for(int i=0;i<2;++i) {
            const bool now=trigger[i]>=(down_[i]?.35f:.55f);
            out|=wheel_open && now && !down_[i] && i==active_;down_[i]=now;
        }
        return out;
    }
};
// Laser-pointer cursor for the open wheel. At open a virtual plane is fixed `distance` m ahead of the head,
// facing it; each tracked hand's aim ray (pos + dir) hits it, and the hit offset / `radius` is the stick
// vector (clamped to the unit circle). The cursor hand is the one whose hit moved most (first hand: `prefer`).
// radius is a constant tuned by eye: the HUD wheel is shrunk (~0.65) about the screen centre, so it spans
// roughly 0.25 m at 1 m; adjust if the dot and wedge disagree. Trigger commit as WheelCursor::commit.
class WheelPointer {
    using V=std::array<double,3>;
    bool open_{},has_[2]{},down_[2]{},outside_{};V centre_{},right_{1,0,0},up_{0,1,0},normal_{0,0,1},prev_[2]{};int active_{-1},filt_hand_{-1},sector_{-1};double fu_{},fv_{};
    static double dot(const V& a,const V& b) noexcept {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
public:
    int cursor() const noexcept {return active_;}
    // Filtered cursor on the wheel plane, metres from the quad centre (u right, v up); valid while a hand hits it.
    std::optional<std::array<double,2>> plane() const noexcept {return open_ && active_>=0 && has_[active_]?std::optional<std::array<double,2>>{{fu_,fv_}}:std::nullopt;}
    // dx/dy: stick vector; hit: world point for the dot (unclamped in the plane, limited to 1.8 radius: reaches the potion slots at the quad edge).
    // Returns false while no hand hits; the dot is still valid when inside `dead` (fraction of radius, stick neutral).
    bool update(bool wheel_open,const std::array<bool,2>& tracked,const std::array<V,2>& pos,const std::array<V,2>& dir,
                const V& head,const V& head_forward,int prefer,float& dx,float& dy,V& hit,bool& dot_visible,
                double distance=1.0,double radius=.25,double dead=.2,double move=.004) noexcept {
        dot_visible=false;
        if(!wheel_open) {open_=has_[0]=has_[1]=outside_=false;active_=filt_hand_=sector_=-1;return false;}
        if(!open_) {
            open_=true;active_=prefer;
            const double h=std::sqrt(head_forward[0]*head_forward[0]+head_forward[2]*head_forward[2]);
            const V f=h>1e-6?V{head_forward[0]/h,0,head_forward[2]/h}:V{0,0,-1}; // yaw only: the wheel stays upright
            centre_={head[0]+f[0]*distance,head[1],head[2]+f[2]*distance};
            normal_={-f[0],0,-f[2]};right_={-f[2],0,f[0]};up_={0,1,0}; // forward x up
        }
        return solve(tracked,pos,dir,dx,dy,hit,dot_visible,radius,dead,move);
    }
    // Dual wield: the same cursor on a plane given every frame (the wheel ring drawn in front of a hand). centre/normal/right/up:
    // the quad's centre and unit axes in the rays' space; radius: the ring in metres. Same filtering, hand choice and sectors as update().
    // v776: the caller tracks only the hand that opened the wheel (so the other can never take the cursor); its own ray points at the ring.
    // continuous: dx/dy are the pointed direction itself, not the 8 wedge centres (the native ring holds as many items as the player owns, evenly
    // spaced: 13 on the user's save, so 8 pointer positions reach 5 of them); sector() still holds the wedge, 8 deg hysteresis, for haptics and logs.
    bool update_plane(bool wheel_open,const std::array<bool,2>& tracked,const std::array<V,2>& pos,const std::array<V,2>& dir,
                const V& centre,const V& normal,const V& right,const V& up,int prefer,float& dx,float& dy,V& hit,bool& dot_visible,
                double radius,bool continuous=false,double dead=.2,double move=.004) noexcept {
        dot_visible=false;
        if(!wheel_open) {open_=has_[0]=has_[1]=outside_=false;active_=filt_hand_=sector_=-1;return false;}
        if(!open_) {open_=true;active_=prefer;}
        centre_=centre;normal_=normal;right_=right;up_=up;
        return solve(tracked,pos,dir,dx,dy,hit,dot_visible,radius,dead,move,continuous);
    }
    int sector() const noexcept {return sector_;} // the wedge (0 right, 2 up, 4 left, 6 down) the cursor is in; -1 in the dead zone
private:
    bool solve(const std::array<bool,2>& tracked,const std::array<V,2>& pos,const std::array<V,2>& dir,float& dx,float& dy,V& hit,bool& dot_visible,
               double radius,double dead,double move,bool continuous=false) noexcept {
        double u[2]{},v[2]{};bool ok[2]{};
        for(int i=0;i<2;++i) {
            if(!tracked[i]) continue;
            const double den=dot(dir[i],normal_);
            if(den>-1e-3) continue; // pointing away from the plane
            const V d{centre_[0]-pos[i][0],centre_[1]-pos[i][1],centre_[2]-pos[i][2]};
            const double t=dot(d,normal_)/den;if(t<=0) continue;
            const V r{pos[i][0]+dir[i][0]*t-centre_[0],pos[i][1]+dir[i][1]*t-centre_[1],pos[i][2]+dir[i][2]*t-centre_[2]};
            u[i]=dot(r,right_);v[i]=dot(r,up_);ok[i]=true;
        }
        double speed[2]{};
        for(int i=0;i<2;++i) {
            if(!ok[i]) {has_[i]=false;continue;}
            const V p{u[i],v[i],0};
            if(has_[i]) speed[i]=std::hypot(p[0]-prev_[i][0],p[1]-prev_[i][1]);
            prev_[i]=p;has_[i]=true;
        }
        for(int i=0;i<2;++i) if(speed[i]>=move && (active_<0 || !ok[active_] || (i!=active_ && speed[i]>speed[active_]))) active_=i;
        if(active_<0 || !ok[active_]) {if(ok[0]||ok[1]) active_=ok[0]?0:1;else return false;}
        // Speed-adaptive smoothing (One-Euro style, per call): tremor under ~4 mm is heavily damped, moves over ~4 cm pass raw.
        if(filt_hand_!=active_) {filt_hand_=active_;fu_=u[active_];fv_=v[active_];}
        else {
            const double d=std::hypot(u[active_]-fu_,v[active_]-fv_),al=std::clamp((d-.004)/.04,.08,1.);
            fu_+=(u[active_]-fu_)*al;fv_+=(v[active_]-fv_)*al;
        }
        double a=fu_/radius,b=fv_/radius;double len=std::hypot(a,b);
        outside_=len>1.3; // past the ring: potion slots and Y, clicked through the native pointer
        if(len>1.8) {a*=1.8/len;b*=1.8/len;len=1.8;}
        hit={centre_[0]+right_[0]*a*radius+up_[0]*b*radius,centre_[1]+right_[1]*a*radius+up_[1]*b*radius,centre_[2]+right_[2]*a*radius+up_[2]*b*radius};dot_visible=true;
        if(len<dead) {sector_=-1;return false;}
        // Sector hysteresis: keep the held wedge until the pointer is > 8 deg past its edge; output its centre.
        constexpr double pi=3.14159265358979,wedge=pi/4;
        const double ang=std::atan2(b,a);
        if(sector_>=0) {
            double off=std::fmod(ang-sector_*wedge,2*pi);if(off>pi) off-=2*pi;if(off<-pi) off+=2*pi;
            if(std::abs(off)>wedge/2+8*pi/180) sector_=-1;
        }
        if(sector_<0) sector_=static_cast<int>(std::lround(ang/wedge))&7;
        if(continuous) {dx=static_cast<float>(a/len);dy=static_cast<float>(b/len);return true;}
        dx=static_cast<float>(std::cos(sector_*wedge));dy=static_cast<float>(std::sin(sector_*wedge));return true;
    }
public:
    // True once on a fresh trigger press of the cursor hand while the wheel is open.
    bool commit(bool wheel_open,const std::array<float,2>& trigger) noexcept {
        bool out=false;
        for(int i=0;i<2;++i) {
            const bool now=trigger[i]>=(down_[i]?.35f:.55f);
            out|=wheel_open && now && !down_[i] && !outside_;down_[i]=now; // either trigger; ring only (elixirs/hide are click zones)
        }
        return out;
    }
};
// Per-press trigger ownership. Each side picks a role at its press edge and
// keeps it until that trigger releases; no role depends on later state except
// the allowed cancellation of an interact press. An owner created or reset
// while a trigger is already held consumes it until it releases, so a
// re-created owner can never fire an interact for a press it did not see.
class InteractOwner {
    enum class Role {none,wheel,interact,native,drain,pending};
    std::array<Role,2> role_{Role::drain,Role::drain};
    std::array<bool,2> down_{true,true};
    std::array<std::uint64_t,2> since_{};
public:
    struct Result {bool interact;std::array<bool,2> consume_trigger;};
    // An unready hand's fresh press waits, consumed, for its locked query and
    // 60 ms settling time. After 250 ms it falls back to native input.
    Result update(const std::array<float,2>& trigger,int active_hand,bool target_valid,bool wheel_open,bool allowed,
            std::uint64_t now_ms=0,bool query_ready=true,bool wait_for_target=false) noexcept {
        Result out{};
        for(unsigned side=0;side<2;++side) {
            const float value=trigger[side];
            const bool mine=static_cast<int>(side)==active_hand;
            if(!down_[side]) {
                if(value>=.55f) {
                    down_[side]=true;since_[side]=now_ms;
                    role_[side]=wheel_open?Role::wheel
                        :(mine && query_ready && target_valid && allowed?Role::interact
                        :((!mine || !query_ready || wait_for_target) && allowed && now_ms?Role::pending:Role::native));
                }
            } else if(value<=.35f) {down_[side]=false;role_[side]=Role::none;}
            if(role_[side]==Role::pending) {
                const auto waited=now_ms>=since_[side]?now_ms-since_[side]:0;
                if(!allowed) role_[side]=Role::drain;
                else if(mine && query_ready && waited>=60 && (target_valid || !wait_for_target)) role_[side]=target_valid?Role::interact:Role::native;
                else if(waited>=250 && !wait_for_target) role_[side]=Role::native;
            }
            if(role_[side]==Role::interact && !allowed) role_[side]=Role::drain;
            out.interact|=role_[side]==Role::interact;
            out.consume_trigger[side]=role_[side]==Role::wheel || role_[side]==Role::interact || role_[side]==Role::drain ||
                role_[side]==Role::pending;
        }
        return out;
    }
    void reset() noexcept {role_={Role::drain,Role::drain};down_={true,true};}
};
// One free hand, two intents (user, headset 2026-10-14): the grip alone does what the game proposes (Use: doors, keyholes,
// loot, talk, carry, choke); a full fist (grip + trigger) grabs physically (props, bottles, ashtrays, corpses, a yank). A grip
// press waits `window` ms for the trigger; a trigger pulled first waits as long for the grip and is held back meanwhile, so a
// fist never casts or fires. The decision lasts until both are let go. A lone tap shorter than the window still happens: a grip
// tap as a 250 ms Use, a trigger tap as a 60 ms pull. A hand holding a weapon (free=false) passes its trigger untouched.
inline bool released_gaze_yields(int locked_hand,int gaze_hand,bool gaze_live,
    const std::array<float,2>& grips,const std::array<float,2>& uses,const std::array<bool,2>& fresh_use,
    const std::array<bool,2>& claimants,bool occupied) noexcept {
    if(locked_hand<0 || locked_hand>1 || gaze_hand!=locked_hand || !gaze_live || occupied) return false;
    const auto previous=static_cast<unsigned>(locked_hand),next=previous^1u;
    return grips[previous]<=.35f && uses[previous]<=.35f && grips[next]>=.45f && fresh_use[next] && claimants[next];
}
class OfferedChokeGrip {
    enum class Role {ready,other,choke,drain};
    Role role_=Role::drain;
    std::uint32_t victim_{};
    std::uint64_t epoch_{};
public:
    struct Result {bool consume_use{},rb{};};
    std::uint32_t victim() const noexcept {return role_==Role::choke?victim_:0;}
    void cancel() noexcept {role_=Role::drain;victim_=0;}
    Result update(bool use,bool started,bool held,bool allowed,std::uint64_t epoch,
        std::uint32_t offered_victim,std::uint32_t active_victim,bool occupied,bool trigger_held=false) noexcept {
        if(epoch_!=epoch || !allowed || !epoch) {role_=Role::drain;victim_=0;}
        epoch_=epoch;
        if(!use && !held && !trigger_held) {role_=Role::ready;victim_=0;return {};}
        if(role_==Role::ready && use && started) {
            if(allowed && epoch && held && !occupied && offered_victim) {role_=Role::choke;victim_=offered_victim;}
            else role_=Role::other;
        }
        if(role_!=Role::choke) return {role_==Role::drain,false};
        if(!held || !use || occupied || (offered_victim!=victim_ && active_victim!=victim_)) {
            role_=Role::drain;victim_=0;return {true,false};
        }
        return {true,true};
    }
};
// Which hand's grip is the choke (v639: the free one): the sword's own hand, or its other one while the sword holds the first and no gun the other.
inline constexpr unsigned choke_hand(unsigned sword_hand,bool sword_owns,bool gun_owns) noexcept {return sword_owns && !gun_owns?sword_hand^1u:sword_hand;}
// A squeeze that holds a weapon (the sword's hold-to-hold fist, the gun hand's gun or gadget, a mod-owned copy) or that another gesture took (holster,
// temple, shoulder dock) is spoken for: a choke offer that appears while it is squeezed is no press. v698/v793 let a squeezed choke-hand grip take
// every offer; with a gun in the other hand the sword hand is the choke hand (above), so the blade's own squeeze chose each unaware guard before the
// stab reached him (user, headset v891: "a choke instead of the stealth kill"). The control thread passes this as OfferedChokeGrip's `occupied`.
inline constexpr bool grip_spoken_for(unsigned hand,unsigned sword_hand,bool sword_owns,bool gun_hand_holds,bool copy_owns,bool taken) noexcept {
    return copy_owns || taken || (hand==sword_hand?sword_owns:gun_hand_holds);
}
class TalkYOwner {
public:
    struct Target {
        std::uintptr_t component{};int generation{-1};std::uint32_t spawn{};std::uintptr_t owner{};
        bool operator==(const Target& other) const noexcept {
            return component==other.component && generation==other.generation && spawn==other.spawn && owner==other.owner;
        }
        explicit operator bool() const noexcept {return component && generation!=-1 && spawn;}
    };
    struct Result {bool consume_y{},interact{},owned{};};
private:
    bool down_=true,claimed_{},cancelled_{};
    Target target_{};std::uint64_t epoch_{},until_{};
public:
    bool owns(Target target) const noexcept {return claimed_ && !cancelled_ && target==target_;}
    void cancel() noexcept {cancelled_=true;until_=0;}
    Result update(bool pressed,bool allowed,std::uint64_t epoch,std::uint64_t now,Target target) noexcept {
        if(epoch_!=epoch || !allowed || !epoch) {cancelled_=true;until_=0;}
        epoch_=epoch;
        if(!pressed && now>=until_) {down_=claimed_=cancelled_=false;target_={};return {};}
        if(pressed && !down_) {
            down_=true;
            if(!cancelled_ && allowed && epoch && target) {claimed_=true;target_=target;until_=now+160;}
        }
        if(claimed_ && (!(target==target_) || !allowed || !epoch)) {cancelled_=true;until_=0;}
        return {claimed_ || cancelled_,claimed_ && !cancelled_ && allowed && now<until_,claimed_};
    }
};
class GripIntent {
public:
    enum class Kind {none,deciding,use,fist,trigger,drain,grab,carry};   // grab (G3, plan Task 6): a grip alone that grabs what the palm is on
    struct Out {bool use{},fist{},pass_trigger{true},tap_trigger{},use_started{},fist_started{},pending_use{},grab{},grab_started{},drop_started{};};
private:
    Kind kind_{Kind::none};bool g_{},t_{},grip_first_{},carry_confirmed_{};std::uint64_t since_{},use_until_{},tap_until_{},carry_since_{},drop_until_{};
public:
    static constexpr std::uint64_t carry_use_timeout_ms=1500;   // v613 item 4: the carry's Use press is held at most this long (sim: query lock + press took ~0.55 s; 500 ms was too short) if the game never starts the carry
    Kind kind() const noexcept {return kind_;}
    // The grip edge of the press being decided, for decide_grip_alone (the press that starts on this frame: now).
    std::uint64_t decision_since(std::uint64_t now) const noexcept {return kind_==Kind::deciding?since_:now;}
    // use_idle: the Use has nothing to act on (no game target for this hand). A trigger then joining within `late` ms after the
    // window still makes the fist (user's headset log 2026-10-14: 12 of 96 fists had the trigger more than 200 ms after the grip).
    // A grip with the trigger finger still off the trigger (< .05, headset traces v555-v562: 97% of fists show it moved one 47 ms
    // sample after the grip, 1% of grip-alone presses) commits to Use after `quick` ms instead of the full window.
    // one_list (G3, plan Task 6; build/no-grip-alone-grab clears it): a grip alone is decided by `choice` (decide_grip_alone, computed by the caller
    // for this hand each frame) instead of the 40 ms quick Use: use -> Use, grab -> Kind::grab, pending -> keep deciding, until `pending_max`
    // (at least the window; the caller's grab buffer may ask longer, ruling H1), then Use. A trigger joining while deciding is still a fist.
    Out update(float grip,float trigger,std::uint64_t now,bool free,std::uint64_t window=100,bool use_idle=false,std::uint64_t late=400,std::uint64_t quick=40,
               GripAloneChoice choice=GripAloneChoice::use,bool one_list=false,std::uint64_t pending_max=0,bool handoff=false,bool carry_active=false) noexcept {
        // handoff: the physical corpse hold may become the native carry this frame (the caller checked torso bone, 0.5 s, the native Use target IS this corpse, nothing carried).
        // carry_active: a native BODY carry is running (global: the game carries a body on the right shoulder whichever hand began it); the toggle Use drops it.
        const bool g=grip>=(g_?.35f:.45f),t=trigger>=(t_?.35f:.55f),edge=(g && !g_) || (t && !t_);
        g_=g;t_=t;Out out{};
        if(drop_until_) {if(carry_active) {out.drop_started=true;drop_until_=0;} else if(now>=drop_until_) drop_until_=0;}   // released inside the state lag: the drop waits for the carry to show
        if(!free) {kind_=g || t?Kind::drain:Kind::none;use_until_=tap_until_=0;return out;}
        if(kind_==Kind::none && edge) {kind_=Kind::deciding;since_=now;grip_first_=g;}
        if(kind_==Kind::deciding) {
            if(g && t) {kind_=Kind::fist;out.fist_started=true;}
            else if(!g && !t) { // tapped and let go inside the window
                kind_=Kind::none;
                if(grip_first_) {use_until_=now+250;out.use_started=true;} else tap_until_=now+60;
            }
            else if(one_list && grip_first_ && g) {
                if(choice==GripAloneChoice::grab) {kind_=Kind::grab;out.grab_started=true;}
                else if(choice==GripAloneChoice::none) kind_=Kind::drain;   // V1: a corpse with no bone in reach: no Use, no grab, until the grip is released
                else if(choice==GripAloneChoice::use || now-since_>=std::max(window,pending_max)) {kind_=Kind::use;out.use_started=true;}
            }
            else if(now-since_>=window || (grip_first_ && g && trigger<.05f && now-since_>=quick)) {kind_=g?Kind::use:Kind::trigger;out.use_started=g;}
        }
        else if(kind_==Kind::carry && !g) {   // v613 item 4: letting go of a shoulder carry drops the body (the native Use is a toggle: only a confirmed carry is toggled off)
            kind_=Kind::drain;
            if(carry_active) out.drop_started=true;
            else if(!carry_confirmed_ && now-carry_since_<carry_use_timeout_ms) drop_until_=carry_since_+carry_use_timeout_ms;
        }
        else if(kind_==Kind::carry) {if(carry_active) carry_confirmed_=true;}
        else if(kind_==Kind::grab && g && handoff && !carry_active) {kind_=Kind::carry;out.use_started=true;carry_since_=now;carry_confirmed_=false;}   // v613 item 4: a torso hold of 0.5 s hands off to the native carry
        else if(kind_!=Kind::none && !g && !t) kind_=Kind::none;
        else if(kind_==Kind::use && g && t && use_idle && now-since_<window+late) {kind_=Kind::fist;out.fist_started=true;}
        else if(kind_==Kind::use && !g) kind_=Kind::drain;      // a trigger still held after the Use is not a cast
        else if(kind_==Kind::grab && !g) kind_=Kind::drain;     // nor after a grab
        else if(kind_==Kind::trigger && !t) kind_=Kind::drain;  // nor a grip still held after a cast a Use
        out.pending_use=kind_==Kind::deciding && grip_first_ && g;
        out.use=kind_==Kind::use || (kind_==Kind::carry && !carry_confirmed_ && now-carry_since_<carry_use_timeout_ms) || now<use_until_;
        out.fist=kind_==Kind::fist;
        out.grab=kind_==Kind::grab;
        out.tap_trigger=now<tap_until_;
        out.pass_trigger=kind_==Kind::trigger || kind_==Kind::none || out.tap_trigger;
        return out;
    }
};
// Face buttons while the native wheel is open: A/B/Y reach the pad (native quick actions) but only for presses
// that began after the wheel opened; anything held at open or at close stays masked until released, so a jump
// held through the close never re-fires. X is the wheel hold and is never passed here.
// Quick Access Wheel art in the movie's 1920x1080 stage, measured from the native wheel capture: the selection ring,
// the health (B) and mana (A) elixirs on the left and "Hide/Show all (hold Y)" at the top right.
enum class WheelZone {none,ring,health,mana,hide};
inline WheelZone wheel_zone(double x,double y) noexcept {
    if(std::hypot(x-960,y-562)<400) return WheelZone::ring;
    if(std::hypot(x-397,y-89)<90) return WheelZone::health;
    if(std::hypot(x-142,y-415)<90) return WheelZone::mana;
    if(x>1440 && x<1920 && y>20 && y<160) return WheelZone::hide;
    return WheelZone::none;
}
// Cursor on the wheel quad (metres from its centre, quad `width` metres wide showing the whole W x H capture) -> stage.
// The movie fits its stage into the capture by the limiting axis and centres it.
inline std::optional<std::array<double,2>> wheel_stage_point(double u,double v,double width,unsigned w,unsigned h) noexcept {
    if(!w || !h || !(width>0)) return {};
    const double s=std::min(w/1920.,h/1080.),tx=(w-1920*s)*.5,ty=(h-1080*s)*.5,height=width*h/w;
    const double px=(.5+u/width)*w,py=(.5-v/height)*h;
    return std::array<double,2>{(px-tx)/s,(py-ty)/s};
}
// Metres per stage pixel on that quad (ring radius 326 stage px -> the pointer's sector radius).
inline double wheel_metres_per_stage(double width,unsigned w,unsigned h) noexcept {
    return w && h?std::min(w/1920.,h/1080.)*width/w:0.;
}
// v776: the cursor (metres from the quad centre, WheelPointer::plane) is on an elixir or "Hide/Show all": a click zone, so the hand chooses no ring item there
// (the dual-wield wheel feeds no wedge and no game pointer, which a trigger would otherwise click as that item).
inline bool wheel_on_button(double u,double v,double width,unsigned w,unsigned h) noexcept {
    const auto st=wheel_stage_point(u,v,width,w,h);if(!st) return false;
    const auto z=wheel_zone((*st)[0],(*st)[1]);return z==WheelZone::health || z==WheelZone::mana || z==WheelZone::hide;
}
// A hand squeezing its grip at a point fixed to the head (user request: the journal at the left temple, the sword over
// the right shoulder). Point = head + left*l + up*u + forward*f; radius r (+3 cm hysteresis); mask: allowed hands.
// journal = one 150 ms pulse per squeeze; held = the squeeze that started there is still held. Consumed until release.
class TempleReach {
    using V=std::array<double,3>;
    double l_,u_,f_,r_;unsigned mask_;
    bool in_[2]{},latched_[2]{},down_[2]{};std::uint64_t until_{};
public:
    explicit TempleReach(double left=.09,double up=.02,double forward=0,double radius=.13,unsigned mask=3) noexcept : l_(left),u_(up),f_(forward),r_(radius),mask_(mask) {}
    struct Out {bool journal{},held{};int entered{-1};std::array<bool,2> consume{},in{};};
    // head: eye centre; forward/up: head axes (any length).
    Out update(const V& head,const V& forward,const V& up,const std::array<std::optional<V>,2>& hands,const std::array<float,2>& grip,bool gameplay,std::uint64_t now) noexcept {
        Out out{};
        const auto unit=[](V v){const double l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);return l>1e-6?V{v[0]/l,v[1]/l,v[2]/l}:V{};};
        const V f=unit(forward),u=unit(up);
        const V left=unit({u[1]*f[2]-u[2]*f[1],u[2]*f[0]-u[0]*f[2],u[0]*f[1]-u[1]*f[0]}); // up x forward = left (right-handed, -Z forward)
        const V temple{head[0]+left[0]*l_+u[0]*u_+f[0]*f_,head[1]+left[1]*l_+u[1]*u_+f[1]*f_,head[2]+left[2]*l_+u[2]*u_+f[2]*f_};
        for(int i=0;i<2;++i) {
            bool in=false;
            if(gameplay && hands[i] && (mask_>>i&1u)) {const V d{(*hands[i])[0]-temple[0],(*hands[i])[1]-temple[1],(*hands[i])[2]-temple[2]};
                in=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])<(in_[i]?r_+.03:r_);}
            if(in && !in_[i]) out.entered=i;
            in_[i]=in;out.in[i]=in;
            const bool pressed=grip[i]>=(down_[i]?.4f:.6f);
            if(in && pressed && !down_[i]) {latched_[i]=true;until_=now+150;}
            if(!pressed) latched_[i]=false;
            out.consume[i]=latched_[i];out.held|=latched_[i];down_[i]=pressed;
        }
        out.journal=now<until_;
        return out;
    }
};
// Full-grip sword (user 2026-10-13, Bonelab style): the sword is in the right hand only while the grip is squeezed.
// Squeeze at the shoulder holster: draw it (or call back a dropped one). Release there: sheathe. Release anywhere else: drop
// it as a physical sword; squeeze at the dropped sword: pick it up. How the hand takes the hilt decides normal or reverse
// grip (no button): fist pointing down at the shoulder, or against the lying blade, is reverse. A sword the game draws by
// itself (after a mantle, a carried object) without the grip is put away again; one drawn while its copy lies in the world
// calls the copy back.
class SwordGrip {
public:
    enum class Action {none,draw,sheathe,drop,pickup,recall,stow};
    struct In {
        float grip{};bool gameplay{},zone{},drawn{},busy{};
        bool use_target{}; // the game offers this hand something to use (an item, a note, a door): the squeeze is that Use
        bool copy{},copy_reach{};double copy_age_s{},copy_distance{};
        double draw_dot{},pickup_dot{}; // hand forward . up (at the shoulder), hand forward . the lying blade
        std::uint64_t now{};
    };
    struct Out {Action action{};bool reversed{},owns{};};
    Out update(const In& in) noexcept {
        Out out{};
        const bool pressed=in.grip>=(down_?.4f:.6f),edge=pressed && !down_,released=!pressed && down_;down_=pressed;
        if(pending_ && (in.drawn || in.now-pending_>1500)) pending_=0;
        if(!in.drawn) {drawn_since_=0;sheathe_sent_=0;dropped_=0;} else if(!drawn_since_) drawn_since_=in.now;
        if(in.copy && !in.drawn) settled_=true;else if(!in.copy) settled_=false;
        if(!in.gameplay) {if(!pressed) held_=false;out.reversed=reversed_;return out;} // a menu or the wheel never drops it
        if(edge && !in.busy) {
            if(in.copy && (in.zone || (in.copy_reach && !in.use_target))) {out.action=Action::pickup;
                const double dot=in.zone?in.draw_dot:in.pickup_dot;if(std::abs(dot)>pickup_dead_band) reversed_=dot<0; // a hand across the blade keeps the last grip
                held_=true;pending_=in.now;}
            else if(in.zone && !in.drawn) {out.action=Action::draw;reversed_=in.draw_dot<-.2;held_=true;pending_=in.now;}
        } else if(released && held_) {
            held_=false;
            if(in.drawn || pending_) {out.action=in.zone?Action::sheathe:Action::drop;pending_=0;sheathe_sent_=in.now;if(!in.zone) dropped_=in.now;} // leaving the hand
        }
        // The game cancels an instant unequip that meets a running equip clip (sim v406): stow again until the hand is empty.
        if(out.action==Action::none && dropped_ && in.drawn && !held_ && !pressed && in.now-dropped_>=300 && in.now-stow_sent_>=300) {
            if(in.now-dropped_>3000) dropped_=0;else {out.action=Action::stow;stow_sent_=in.now;}}
        // The hand closed on a sword in it: a squeeze that starts on the drawn sword. A grip already held when the game drew it by itself
        // (a corpse held through the end of a carry, headset v627: 16 drops for 11 draws and pickups) is not taking the sword.
        // v694 (user, headset v693: "the sword is in the hand only when I take it from my back or from the floor"): a squeeze never takes a
        // sword the game drew by itself (the game's right draws are vetoed natively, NativeManualEquipRuntime); one that slipped through is put away.
        if(out.action==Action::none && !pending_ && in.now-recall_sent_>1000) {
            // ponytail: 30 s / 12 m return to the holster; tune in the headset
            if(in.copy && ((settled_ && in.drawn) || in.copy_age_s>30 || in.copy_distance>12)) {out.action=Action::recall;recall_sent_=in.now;}
        }
        // User 2026-11-02 ("after climbing I lose what was in my hands"): a mantle (or another scene, busy) takes the sword out of a hand that
        // holds it and the game's own restore is vetoed (veto_right_draw_locked). A hand still squeezing gets it back, once, after the scene.
        if(in.drawn && held_) had_=true;
        if(!held_) had_=false;
        if(out.action==Action::none && had_ && pressed && !in.drawn && !pending_ && !in.busy && !in.copy) {
            if(!lost_) lost_=in.now+1;
            else if(in.now+1-lost_>=restore_ms) {out.action=Action::draw;pending_=in.now;had_=false;lost_=0;}
        } else lost_=0;
        if(out.action==Action::none && in.drawn && !held_ && !pending_ && !in.busy && in.now-drawn_since_>150 &&
           (!sheathe_sent_ || in.now-sheathe_sent_>1500)) {out.action=Action::sheathe;sheathe_sent_=in.now;}
        out.reversed=reversed_;out.owns=held_ || pending_ || in.zone || (!in.use_target && in.copy && in.copy_reach);
        return out;
    }
    bool held() const noexcept {return held_;}
    static constexpr double pickup_dead_band=.35; // |dot| a pickup needs before it changes the grip; `reversed_` changes only on a draw or pickup edge
    static constexpr std::uint64_t restore_ms=300; // ponytail: the game's own restore lands first if it can; tune in the headset
private:
    bool down_{},held_{},reversed_{},settled_{},had_{};std::uint64_t pending_{},drawn_since_{},sheathe_sent_{},recall_sent_{},dropped_{},stow_sent_{},lost_{};
};
// Full-grip pistol/crossbow (user 2026-10-13): the left hand holds the gun only while its grip is squeezed. Let go anywhere
// and it goes back to the left hip (no physical pistol prop exists to drop). Squeeze at the hip with the left hand empty:
// draw the gun the hip holds (the last gun drawn). A gun the wheel or the game puts in the hand stays until the grip has
// held it once.
// Dual wield (docs/dual-wield-two-hands-plan.md, row A; In::drop_enabled, off = everything above exactly): letting go of a drawn gun away from
// the hip drops/throws it (Action::drop, NativeWeaponHands::Op::gun_drop); at the hip it still holsters. While a dropped native gun lies in the
// world (In::copy) a squeeze at the hip, or with the empty hand within reach of it (In::copy_reach), picks it up (Action::pickup, the draw's
// 100 ms and authorisation) instead of drawing from the hip. Action values are appended: gun_actions[] needs five entries.
class GunGrip {
public:
    enum class Action {none,draw,holster,drop,pickup};
    struct In {float grip{},trigger{};bool gameplay{},zone{},drawn{},empty{},loaded{},busy{},object_claim{},draw_authorized{};std::uint64_t now{},release_owner{};
        bool drop_enabled{},copy{},copy_reach{};};
    struct Out {Action action{};bool owns{},reserved{},expected{};};   // expected: the draw is the pickup's hip fallback (expect)
    void reject_draw() noexcept {held_=false;pending_=0;candidate_=false;rejected_=down_;release_pending_=false;held_owner_=0;expect_since_=0;}
    // v962 (user, headset v951: "the sword in one hand, I pick up a pistol or crossbow with the free hand and it goes to the hip instead of staying in the free hand"):
    // a gun the extras loop took natively from the floor (the wheel's equip) is expected in the hand within a moment. When it is not there after expect_ms while the hand
    // still squeezes (it stayed in the holster), the hand draws it from the hip like a squeeze at the holster; a hand that let go, or a gun that landed, ends the wait.
    void expect(std::uint64_t now) noexcept {expect_since_=now?now:1;}
    static constexpr std::uint64_t expect_ms=350,expect_give_up_ms=3000;
    Out update(const In& in) noexcept {
        Out out{};
        const bool pressed=in.grip>=(down_?.4f:.6f),edge=pressed && !down_,released=!pressed && down_;down_=pressed;
        if(pending_) {
            if(in.drawn) pending_=0;
            else if(in.now>=pending_ && in.now-pending_>1500) reject_draw();
        }
        if(!pressed) rejected_=false;
        if(!in.gameplay) {candidate_=false;if(!pressed && held_) {release_pending_=true;held_=false;}return out;}
        if(release_pending_) {
            if(pressed || (!in.drawn && !pending_)) {release_pending_=false;held_owner_=0;}
            else if(!in.busy) {
                if(held_owner_ && in.release_owner==held_owner_ && !in.object_claim)out.action=Action::holster;
                release_pending_=false;held_owner_=0;pending_=0;out.owns=in.drawn;return out;
            }
        }
        if(released && held_ && in.busy) {release_pending_=true;held_=false;}
        const bool pick=in.drop_enabled && in.copy && (in.zone || in.copy_reach); // a dropped gun lies in the world: the squeeze takes it back
        const bool eligible=!in.busy && !in.object_claim && in.trigger<=.35f && in.empty && (pick || (in.zone && in.loaded));
        if(edge) {candidate_=eligible;candidate_since_=in.now;}
        if(!pressed || !eligible) candidate_=false;
        if(candidate_ && in.draw_authorized && in.now>=candidate_since_ && in.now-candidate_since_>=100) {
            out.action=pick?Action::pickup:Action::draw;held_=true;pending_=in.now;candidate_=false;
        }
        else if(released && held_) {held_=false;held_owner_=0;if(in.drawn || pending_) {
            // away from the hip a gun in the hand leaves it (dual wield); a draw still pending, or the hip itself, holsters
            if(in.drop_enabled && in.drawn && !in.zone) {out.action=Action::drop;dropped_=in.now;}
            else out.action=Action::holster;
            pending_=0;}}
        // the gun the engine still reports drawn for a moment after a drop is not taken back by a new squeeze
        if(pressed && in.drawn && !in.busy && !rejected_ && out.action==Action::none && (!dropped_ || in.now-dropped_>=500)) {if(!held_)held_owner_=0;held_=true;}
        if(held_ && pressed && in.drawn && in.release_owner)held_owner_=in.release_owner;
        // As SwordGrip: a gun the game took out of a squeezing hand (a mantle: busy, the authored camera) is drawn again from the hip after the scene.
        if(in.drawn && held_) had_=true;
        if(!held_) had_=false;
        if(out.action==Action::none && had_ && pressed && !in.drawn && !pending_ && !in.busy && in.empty && in.loaded && !in.object_claim && !in.copy) {
            if(!lost_) lost_=in.now+1;
            else if(in.now+1-lost_>=SwordGrip::restore_ms) {out.action=Action::draw;pending_=in.now;had_=false;lost_=0;}
        } else lost_=0;
        if(expect_since_) {
            if(!pressed || in.drawn || in.now<expect_since_ || in.now-expect_since_>expect_give_up_ms) expect_since_=0;
            else if(in.now-expect_since_>=expect_ms && out.action==Action::none && !pending_ && !in.busy && in.empty && in.loaded && !in.object_claim) {
                out.action=Action::draw;out.expected=true;pending_=in.now;held_=true;expect_since_=0;
            }
        }
        out.owns=held_ || pending_ || in.drawn;out.reserved=candidate_;
        return out;
    }
    bool held() const noexcept {return held_;}
private:
    bool down_{},held_{},candidate_{},rejected_{},release_pending_{},had_{};std::uint64_t pending_{},candidate_since_{},held_owner_{},dropped_{},lost_{},expect_since_{};
};
class WheelFaceButtons {
    unsigned blocked_{};bool wheel_{};
public:
    struct Out {bool a{},b{},y{},active{};};
    Out update(bool a,bool b,bool y,bool wheel) noexcept {
        const unsigned raw=(a?1u:0u)|(b?2u:0u)|(y?4u:0u);
        if(wheel!=wheel_) {wheel_=wheel;blocked_=raw;}
        blocked_&=raw;
        const unsigned pass=wheel?raw&~blocked_:0u;
        return {(pass&1)!=0,(pass&2)!=0,(pass&4)!=0,wheel||blocked_!=0};
    }
};
} // namespace d2vr
