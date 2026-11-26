#pragma once
// Hand/weapon vs hand/weapon contact (dual wield, blade vs other hand, gun vs sword, hand vs hand). Pure maths, no engine queries.
// Each side = up to 3 capsules in world space: [0] the hand/fist, [1] the held weapon, and the forearm (has_forearm), which
// only the OTHER side's hand/weapon collide with. Cost per update: up to 8 capsule pairs
// (segment-segment closest point, ~60 flops each), no allocation, noexcept. Only the deepest cross-side pair is resolved.
// FEED UNCORRECTED POSES every frame (raw/clamped hands, not the previous output): the correction is a pure display offset,
// so the hysteresis below holds a resting crossed pair steady instead of chasing its own output.
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace d2vr {
struct HwcCapsule { Vec3 a,b; double radius{}; };
struct HwcSide {
    bool tracked{};                 // false: untracked / not drawn -> no contact, no correction
    HwcCapsule part[2]{};           // [0] hand (a==b is fine), [1] weapon
    bool has_weapon{};
    bool weapon_is_blade{};         // sword/knife: eligible for a clash
    Vec3 vel[2]{};                  // m/s of each part
    bool has_forearm{};             // the other side's hand/weapon must not pass through this forearm
    HwcCapsule forearm{};           // taken as still (no velocity): a forearm pair moves only the hand/weapon side
};
struct HwcEvent { Vec3 point; Vec3 normal; double rel_speed{}; bool clash{}; };   // normal points side 0 -> side 1
struct HwcResult {
    Vec3 correction[2]{};           // add to side 0 / side 1 hand (and its weapon); each <= max_correction
    bool contact{};
    bool forearm{};                 // the resolved pair involved a forearm (the hand/weapon side takes the whole correction)
    double depth{};
    std::optional<HwcEvent> event;  // first frame of a contact only (re-armed after rearm_seconds)
};

class HandWeaponContact {
public:
    static constexpr double max_correction=.05, rearm_seconds=.08, hysteresis=.001, clash_speed=1.5;
    HwcResult update(const HwcSide (&s)[2],double dt) noexcept {
        HwcResult out;
        if(!finite_side(s[0]) || !finite_side(s[1]) || !std::isfinite(dt)) return out;   // NaN frame: ignored, state kept
        cooldown_=std::max(0.,cooldown_-std::max(0.,dt));
        if(!s[0].tracked || !s[1].tracked) {in_contact_=false;return out;}
        double best=0;int bi=-1,bj=-1;Vec3 p0{},p1{};HwcCapsule ca{},cb{};
        // part index: 0 hand, 1 weapon, 2 forearm. Forearm-vs-forearm is not a pair; every other side-0 x side-1 combo is.
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
            if((i==2&&j==2) || !has_part(s[0],i) || !has_part(s[1],j)) continue;
            const HwcCapsule& A=part_of(s[0],i);const HwcCapsule& B=part_of(s[1],j);
            Vec3 c0,c1;closest(A,B,c0,c1);
            const double pen=A.radius+B.radius-length(c1-c0);
            if(pen>best) {best=pen;bi=i;bj=j;p0=c0;p1=c1;ca=A;cb=B;}
        }
        if(bi<0) {in_contact_=false;return out;}
        const bool fa=bi==2||bj==2;   // a forearm pair
        if(in_contact_ && std::fabs(best-last_depth_)<hysteresis) {   // resting: keep last correction
            out.correction[0]=last_[0];out.correction[1]=last_[1];out.contact=true;out.forearm=last_forearm_;out.depth=last_depth_;return out;
        }
        Vec3 n=p1-p0;const double d=length(n);
        n=d>1e-9?n*(1./d):fallback_normal(ca,cb);
        // The faster (swinging) side yields less; both still -> 50/50. A forearm pair: the hand/weapon side takes all of it.
        const double v0=length(vel_of(s[0],bi)),v1=length(vel_of(s[1],bj));
        const double w0=bj==2?1.:bi==2?0.:(v1+.5)/(v0+v1+1.);
        out.correction[0]=cap(n*(-best*w0));out.correction[1]=cap(n*(best*(1.-w0)));
        out.contact=true;out.forearm=fa;out.depth=best;
        last_[0]=out.correction[0];last_[1]=out.correction[1];last_depth_=best;last_forearm_=fa;
        if(!in_contact_ && cooldown_<=0) {
            const double rel=length(vel_of(s[0],bi)-vel_of(s[1],bj));
            out.event=HwcEvent{(p0+p1)*.5,n,rel,s[0].weapon_is_blade && s[1].weapon_is_blade && bi==1 && bj==1 && rel>=clash_speed};
            cooldown_=rearm_seconds;
        }
        in_contact_=true;
        return out;
    }
    void reset() noexcept {*this=HandWeaponContact{};}
private:
    bool in_contact_{},last_forearm_{};double cooldown_{},last_depth_{};Vec3 last_[2]{};
    static bool has_part(const HwcSide& s,int k) noexcept {return k==0||(k==1&&s.has_weapon)||(k==2&&s.has_forearm);}
    static const HwcCapsule& part_of(const HwcSide& s,int k) noexcept {return k==2?s.forearm:s.part[k];}
    static Vec3 vel_of(const HwcSide& s,int k) noexcept {return k==2?Vec3{}:s.vel[k];}
    static bool finite_side(const HwcSide& s) noexcept {
        for(int i=0;i<2;++i) if(!finite(s.part[i].a)||!finite(s.part[i].b)||!finite(s.vel[i])||!std::isfinite(s.part[i].radius)) return false;
        if(s.has_forearm && (!finite(s.forearm.a)||!finite(s.forearm.b)||!std::isfinite(s.forearm.radius))) return false;
        return true;
    }
    static Vec3 cap(Vec3 v) noexcept {const double l=length(v);return l>max_correction?v*(max_correction/l):v;}
    static Vec3 fallback_normal(const HwcCapsule& a,const HwcCapsule& b) noexcept {   // exactly intersecting axes
        Vec3 n=cross(a.b-a.a,b.b-b.a);double l=length(n);
        if(l<1e-9) {n=cross(a.b-a.a,Vec3{0,0,1});l=length(n);}
        if(l<1e-9) {n=Vec3{1,0,0};l=1;}
        return n*(1./l);
    }
    // Closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9); parallel-safe.
    static void closest(const HwcCapsule& A,const HwcCapsule& B,Vec3& c1,Vec3& c2) noexcept {
        const Vec3 d1=A.b-A.a,d2=B.b-B.a,r=A.a-B.a;
        const double a=dot(d1,d1),e=dot(d2,d2),f=dot(d2,r);double s=0,t=0;
        if(a<=1e-12 && e<=1e-12) {}
        else if(a<=1e-12) t=std::clamp(f/e,0.,1.);
        else {
            const double c=dot(d1,r);
            if(e<=1e-12) s=std::clamp(-c/a,0.,1.);
            else {
                const double b=dot(d1,d2),den=a*e-b*b;
                s=den>1e-12*a*e?std::clamp((b*f-c*e)/den,0.,1.):0.;   // parallel: any s, pick the start
                t=(b*s+f)/e;
                if(t<0) {t=0;s=std::clamp(-c/a,0.,1.);} else if(t>1) {t=1;s=std::clamp((b-c)/a,0.,1.);}
            }
        }
        c1=A.a+d1*s;c2=B.a+d2*t;
    }
};
}
