#pragma once
#include <optional>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
// Physical Quest X owns the native weapon/power wheel: the caller turns a held
// wheel() into a held native LB and equips on that LB release. Opens only on a
// fresh X edge while allowed; losing allowed cancels the press (wheel closes
// without equipping) and the press stays claimed until X is released, so a
// tracking/focus/menu interruption can never late-open the wheel.
// A positive hold_ms delays opening (the right-hand A wheel: a tap stays a jump); the button passes
// through natively until x_consumed() turns true.
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
// Both hands open the same wheel: left X at once, right A after a short hold. The first hand to open owns
// selection until its button is released; the other button is then ignored (never late-opens).
class WheelHands {
    XWheelOwner left_,right_{350};
public:
    void update(bool x,bool a,bool allowed,std::uint64_t now_ms) noexcept {
        left_.update(x,allowed && !right_.wheel(),now_ms);
        right_.update(a,allowed && !left_.wheel(),now_ms);
    }
    bool wheel() const noexcept {return left_.wheel() || right_.wheel();}
    int hand() const noexcept {return left_.wheel()?0:right_.wheel()?1:-1;}
    bool a_consumed() const noexcept {return right_.x_consumed();}
    // Native A (jump) is held back while it may become a wheel hold; a tap under 350 ms
    // jumps on release with an 80 ms pulse, so opening the wheel never jumps.
    bool a_passthrough(bool a,std::uint64_t now_ms) noexcept {
        if(a && !a_down_) a_since_=now_ms;
        if(!a && a_down_ && now_ms-a_since_<350 && !left_.wheel()) a_pulse_until_=now_ms+80;
        a_down_=a;return now_ms<a_pulse_until_;
    }
    // Cursor-hand trigger commit: closes the wheel like a button release (the button stays claimed until released).
    void commit() noexcept {left_.commit();right_.commit();}
    void reset() noexcept {left_.reset();right_.reset();a_down_=false;a_pulse_until_=0;}
private:
    bool a_down_{};std::uint64_t a_since_{},a_pulse_until_{};
};
// Wheel selection feed: the owning hand's opposite thumb stick (left-owned: right stick, as native;
// right-owned: left stick, since the right thumb holds A) wins over that hand's dial; the other stick is dropped.
inline void wheel_select(int owner,float& turn_x,float& turn_y,float& move_x,float& move_y,bool dial,float dx,float dy) noexcept {
    if(owner==1) {turn_x=move_x;turn_y=move_y;}
    move_x=move_y=0;
    if(dial && turn_x*turn_x+turn_y*turn_y<.0625f) {turn_x=dx;turn_y=dy;}
}
// Two-handed pointing cursor for the open wheel. Each hand's offset from where it was when the wheel
// opened (head-right/up plane) is its pointing direction. The hand that moves meaningfully becomes the
// cursor and keeps it until the other hand moves faster; inside `dead` metres the cursor is neutral.
// A fresh trigger press of the cursor hand (edge seen while open) commits. Stick override: wheel_select.
class WheelCursor {
    bool open_{},has_[2]{},down_[2]{};std::array<std::array<double,3>,2> origin_{},prev_{};
    std::array<double,3> right_{1,0,0};int active_{-1};
public:
    int cursor() const noexcept {return active_;}
    bool update(bool wheel_open,const std::array<bool,2>& tracked,const std::array<std::array<double,3>,2>& hand,
                const std::array<double,3>& head_right,float& x,float& y,double dead=.035,double move=.004) noexcept {
        if(!wheel_open) {open_=has_[0]=has_[1]=false;active_=-1;return false;}
        if(!open_) {
            open_=true;
            const double h=std::sqrt(head_right[0]*head_right[0]+head_right[2]*head_right[2]);
            right_=h>1e-6?std::array<double,3>{head_right[0]/h,0,head_right[2]/h}:std::array<double,3>{1,0,0};
        }
        double speed[2]{};
        for(int i=0;i<2;++i) {
            if(!tracked[i]) continue;
            if(!has_[i]) {has_[i]=true;origin_[i]=prev_[i]=hand[i];continue;}
            const auto& a=hand[i];const auto& b=prev_[i];
            speed[i]=std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));prev_[i]=a;
        }
        for(int i=0;i<2;++i) if(speed[i]>=move && (active_<0 || (i!=active_ && speed[i]>speed[active_]))) active_=i;
        if(active_<0 || !tracked[active_] || !has_[active_]) return false;
        const auto& h=hand[active_];const auto& o=origin_[active_];
        const double u=(h[0]-o[0])*right_[0]+(h[2]-o[2])*right_[2],v=h[1]-o[1],len=std::sqrt(u*u+v*v);
        if(len<dead) return false;
        x=static_cast<float>(u/len);y=static_cast<float>(v/len);return true;
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
    bool open_{},has_[2]{},down_[2]{},outside_{};V centre_{},right_{1,0,0},normal_{0,0,1},prev_[2]{};int active_{-1},filt_hand_{-1},sector_{-1};double fu_{},fv_{};
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
            normal_={-f[0],0,-f[2]};right_={-f[2],0,f[0]}; // forward x up
        }
        double u[2]{},v[2]{};bool ok[2]{};
        for(int i=0;i<2;++i) {
            if(!tracked[i]) continue;
            const double den=dot(dir[i],normal_);
            if(den>-1e-3) continue; // pointing away from the plane
            const V d{centre_[0]-pos[i][0],centre_[1]-pos[i][1],centre_[2]-pos[i][2]};
            const double t=dot(d,normal_)/den;if(t<=0) continue;
            const V r{pos[i][0]+dir[i][0]*t-centre_[0],pos[i][1]+dir[i][1]*t-centre_[1],pos[i][2]+dir[i][2]*t-centre_[2]};
            u[i]=dot(r,right_);v[i]=r[1];ok[i]=true;
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
        hit={centre_[0]+right_[0]*a*radius,centre_[1]+b*radius,centre_[2]+right_[2]*a*radius};dot_visible=true;
        if(len<dead) {sector_=-1;return false;}
        // Sector hysteresis: keep the held wedge until the pointer is > 8 deg past its edge; output its centre.
        constexpr double pi=3.14159265358979,wedge=pi/4;
        const double ang=std::atan2(b,a);
        if(sector_>=0) {
            double off=std::fmod(ang-sector_*wedge,2*pi);if(off>pi) off-=2*pi;if(off<-pi) off+=2*pi;
            if(std::abs(off)>wedge/2+8*pi/180) sector_=-1;
        }
        if(sector_<0) sector_=static_cast<int>(std::lround(ang/wedge))&7;
        dx=static_cast<float>(std::cos(sector_*wedge));dy=static_cast<float>(std::sin(sector_*wedge));return true;
    }
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
            std::uint64_t now_ms=0,bool query_ready=true) noexcept {
        Result out{};
        for(unsigned side=0;side<2;++side) {
            const float value=trigger[side];
            const bool mine=static_cast<int>(side)==active_hand;
            if(!down_[side]) {
                if(value>=.55f) {
                    down_[side]=true;since_[side]=now_ms;
                    role_[side]=wheel_open?Role::wheel
                        :(mine && query_ready && target_valid && allowed?Role::interact
                        :((!mine || !query_ready) && allowed && now_ms?Role::pending:Role::native));
                }
            } else if(value<=.35f) {down_[side]=false;role_[side]=Role::none;}
            if(role_[side]==Role::pending) {
                const auto waited=now_ms>=since_[side]?now_ms-since_[side]:0;
                if(!allowed) role_[side]=Role::drain;
                else if(mine && query_ready && waited>=60) role_[side]=target_valid?Role::interact:Role::native;
                else if(waited>=250) role_[side]=Role::native;
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
// A hand squeezing its grip at a point fixed to the head (user request: the journal at the left temple, the sword over
// the right shoulder). Point = head + left*l + up*u + forward*f; radius r (+3 cm hysteresis); mask: allowed hands.
// journal = one 150 ms pulse per squeeze; held = the squeeze that started there is still held. Consumed until release.
class TempleReach {
    using V=std::array<double,3>;
    double l_,u_,f_,r_;unsigned mask_;
    bool in_[2]{},latched_[2]{},down_[2]{};std::uint64_t until_{};
public:
    explicit TempleReach(double left=.09,double up=.02,double forward=0,double radius=.13,unsigned mask=3) noexcept : l_(left),u_(up),f_(forward),r_(radius),mask_(mask) {}
    struct Out {bool journal{},held{};int entered{-1};std::array<bool,2> consume{};};
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
            in_[i]=in;
            const bool pressed=grip[i]>=(down_[i]?.4f:.6f);
            if(in && pressed && !down_[i]) {latched_[i]=true;until_=now+150;}
            if(!pressed) latched_[i]=false;
            out.consume[i]=latched_[i];out.held|=latched_[i];down_[i]=pressed;
        }
        out.journal=now<until_;
        return out;
    }
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
