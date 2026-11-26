#pragma once
// Arm-vs-world collision policy (pure maths, no engine code). The mod sweeps the forearm and upper arm of the arm
// the IK would draw, keeps the hits as half-space planes, and the elbow is then swivelled about the shoulder-wrist
// axis so that it clears every plane. World Z is up, metres, seconds.
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr {

struct ArmCollisionConfig {
    double forearm_radius=.045, upper_radius=.055, forearm_skip=.05, upper_skip=.04, plane_seconds=.15,
           same_normal_deg=15, same_offset=.03, edge_fraction=.5, skin=.005, max_swivel_deg=110, max_inward=.10,
           engage_rate=12, release_tau=.08, release_rate=4, violation_engage=.01, jump=.35;
};
struct ArmPlane {Vec3 point{},normal{};double radius{},last_hit{};};   // normal unit, toward free space
struct ArmGeometry {Vec3 shoulder{},elbow{},wrist{};bool valid{};};    // the NATURAL elbow, world
struct ArmCast {Vec3 from{},to{};double radius{};};
struct ArmHit {Vec3 point{},normal{};bool initial_overlap{},normal_valid{true},solid{true};};
struct ArmCollisionStats {std::uint64_t casts{},hits{},planes_made{},refreshed{},expired{},edge_discards{},overlaps{},resets{};};

class ArmCollision {
public:
    static constexpr std::size_t max_planes=4;
    explicit ArmCollision(ArmCollisionConfig c={}) : cfg_(c) {}

    // [0] forearm from wrist+skip toward the elbow, [1] upper arm from shoulder+skip toward the elbow.
    std::array<std::optional<ArmCast>,2> casts(const ArmGeometry& g) const {
        std::array<std::optional<ArmCast>,2> out;
        if(!g.valid||!finite(g.shoulder)||!finite(g.elbow)||!finite(g.wrist)) return out;
        make_cast(g.wrist,g.elbow,cfg_.forearm_skip,cfg_.forearm_radius,out[0]);
        make_cast(g.shoulder,g.elbow,cfg_.upper_skip,cfg_.upper_radius,out[1]);
        return out;
    }

    void observe(unsigned side,const ArmGeometry& cast_geometry,const std::array<std::optional<ArmHit>,2>& hits,double now,unsigned answered=0x3u) { // answered: bit i = cast i was answered (counted in stats.casts)
        if(side>1||!std::isfinite(now)) return;
        const ArmGeometry& g=cast_geometry;
        if(!g.valid||!finite(g.shoulder)||!finite(g.elbow)||!finite(g.wrist)) return;
        Side& s=side_[side];
        if(s.has_prev&&(length(g.wrist-s.prev_wrist)>cfg_.jump||length(g.shoulder-s.prev_shoulder)>cfg_.jump)) reset(side);
        s.has_prev=true;s.prev_wrist=g.wrist;s.prev_shoulder=g.shoulder;
        // drop expired planes
        for(std::size_t i=0;i<s.count;) {
            if(now-s.p[i].last_hit>cfg_.plane_seconds) {remove(s,i);++s.stats.expired;} else ++i;
        }
        const auto cs=casts(g);
        const double cos_same=std::cos(cfg_.same_normal_deg*3.14159265358979323846/180);
        for(std::size_t i=0;i<2;++i) {
            if(cs[i] && ((answered>>i)&1u)) ++s.stats.casts;
            if(!hits[i]) continue;
            const ArmHit& h=*hits[i];
            ++s.stats.hits;
            if(h.initial_overlap) {++s.stats.overlaps;continue;}
            if(!h.solid||!h.normal_valid||!finite(h.point)||!finite(h.normal)) continue;
            const double nl=length(h.normal);
            if(!(nl>1e-9)) continue;
            ArmPlane np;np.point=h.point;np.normal=h.normal*(1.0/nl);
            np.radius=i==0?cfg_.forearm_radius:cfg_.upper_radius;np.last_hit=now;
            const Vec3 free_end=i==0?g.wrist:g.shoulder;
            if(dot(free_end-np.point,np.normal)<cfg_.edge_fraction*np.radius) {++s.stats.edge_discards;continue;}
            bool merged=false;
            for(std::size_t k=0;k<s.count;++k) {
                ArmPlane& e=s.p[k];
                if(dot(e.normal,np.normal)>=cos_same&&std::fabs(dot(np.point-e.point,e.normal))<=cfg_.same_offset) {
                    e=np;++s.stats.refreshed;merged=true;break;
                }
            }
            if(merged) continue;
            if(s.count==max_planes) {   // the oldest goes
                std::size_t o=0;
                for(std::size_t k=1;k<s.count;++k) if(s.p[k].last_hit<s.p[o].last_hit) o=k;
                remove(s,o);
            }
            s.p[s.count++]=np;++s.stats.planes_made;
        }
    }

    std::size_t planes(unsigned side,std::array<ArmPlane,max_planes>& out,double now) const {
        if(side>1) return 0;
        const Side& s=side_[side];
        std::size_t n=0;
        for(std::size_t i=0;i<s.count;++i)
            if(now-s.p[i].last_hit<=cfg_.plane_seconds) out[n++]=s.p[i];
        return n;
    }

    void reset(unsigned side) {
        if(side>1) return;
        Side& s=side_[side];
        s.count=0;s.has_prev=false;++s.stats.resets;
    }
    void reset() {reset(0);reset(1);}
    const ArmCollisionStats& stats(unsigned side) const {return side_[side>1?1:side].stats;}
    const ArmCollisionConfig& config() const {return cfg_;}

private:
    struct Side {
        std::array<ArmPlane,max_planes> p{};
        std::size_t count{};
        bool has_prev{};
        Vec3 prev_wrist{},prev_shoulder{};
        ArmCollisionStats stats{};
    };
    static void remove(Side& s,std::size_t i) {
        for(std::size_t k=i;k+1<s.count;++k) s.p[k]=s.p[k+1];
        --s.count;
    }
    static void make_cast(Vec3 root,Vec3 elbow,double skip,double radius,std::optional<ArmCast>& out) {
        const Vec3 d=elbow-root;const double len=length(d);
        if(!std::isfinite(len)||len<skip+.02) return;
        out=ArmCast{root+d*(skip/len),elbow,radius};
    }
    ArmCollisionConfig cfg_;
    Side side_[2]{};
};

// Rotate the natural elbow about the shoulder-wrist axis (right-hand rule about unit(wrist-shoulder)).
inline Vec3 swivel_elbow(Vec3 shoulder,Vec3 wrist,Vec3 natural_elbow,double theta) {
    if(!finite(shoulder)||!finite(wrist)||!finite(natural_elbow)||!std::isfinite(theta)) return natural_elbow;
    const Vec3 d=wrist-shoulder;const double l=length(d);
    if(!(l>1e-9)||!std::isfinite(l)) return natural_elbow;
    const Vec3 a=d*(1.0/l);const Vec3 v=natural_elbow-shoulder;
    const double c=std::cos(theta),s=std::sin(theta);
    return shoulder+v*c+cross(a,v)*s+a*(dot(a,v)*(1-c));
}

// Max over planes of radius+skin-dot(elbow-point,normal), clamped to >= 0.
inline double swivel_violation(Vec3 shoulder,Vec3 wrist,Vec3 elbow,const ArmPlane* planes,std::size_t n,double skin) {
    (void)shoulder;(void)wrist;
    double worst=0;
    if(!planes||!finite(elbow)) return 0;
    for(std::size_t i=0;i<n;++i) {
        const double v=planes[i].radius+skin-dot(elbow-planes[i].point,planes[i].normal);
        if(std::isfinite(v)&&v>worst) worst=v;
    }
    return worst;
}

struct SwivelResult {double theta{},residual{};bool feasible{true},constrained{};};

// prefer_theta is the follower's current swivel. When it is beyond 5 degrees and a feasible candidate of the same sign exists,
// only those are considered (switching to the mirror solution would sweep the drawn elbow through the obstacle).
// An infeasible solve ranks candidates by worst_violation + 0.03*|theta| so it never swings far for a tiny gain;
// residual is the chosen candidate's plain worst violation.
inline SwivelResult solve_swivel(Vec3 shoulder,Vec3 wrist,Vec3 natural_elbow,Vec3 outward,const ArmPlane* planes,std::size_t n,
                                 const ArmCollisionConfig& cfg,double prefer_theta=0) {
    SwivelResult res;
    res.constrained=planes!=nullptr&&n>0;
    const double len=length(wrist-shoulder);
    if(!res.constrained||!finite(shoulder)||!finite(wrist)||!finite(natural_elbow)||!finite(outward)||
       !std::isfinite(len)||len<1e-6) {
        res.constrained=false;return res;
    }
    const double pi=3.14159265358979323846,deg=pi/180;
    const double maxt=std::max(0.0,cfg.max_swivel_deg)*deg;
    const double penalty=0.03;   // metres per radian, infeasible ranking only
    struct Eval {double theta,cost;};   // cost = worst violation (plane or inward limit)
    auto eval=[&](double th)->Eval {
        const Vec3 e=swivel_elbow(shoulder,wrist,natural_elbow,th);
        const double v=swivel_violation(shoulder,wrist,e,planes,n,cfg.skin);
        const double inward=-cfg.max_inward-dot(e-natural_elbow,outward);
        return {th,std::max(v,std::max(0.0,inward))};
    };
    const double eps=1e-9;
    auto better=[&](const Eval& a,const Eval& b) {
        const bool af=a.cost<=eps,bf=b.cost<=eps;
        if(af&&bf) return std::fabs(a.theta)<std::fabs(b.theta);
        if(af!=bf) return af;
        const double ra=a.cost+penalty*std::fabs(a.theta),rb=b.cost+penalty*std::fabs(b.theta);
        if(ra<rb-eps) return true;
        if(ra>rb+eps) return false;
        return std::fabs(a.theta)<std::fabs(b.theta);
    };
    const double step=5*deg;
    const int k=static_cast<int>(std::floor(maxt/step+1e-9));
    // sg==0: all thetas (theta 0 always included); sg=+-1: only thetas of that sign
    auto search=[&](int sg,bool& have)->Eval {
        Eval best{0,0};have=false;
        auto consider=[&](double th) {
            if(sg!=0&&sg*th<=0) return;
            const Eval e=eval(th);
            if(!have||better(e,best)) {best=e;have=true;}
        };
        if(sg==0) consider(0.0);
        for(int i=1;i<=k;++i) {consider(i*step);consider(-i*step);}
        if(maxt-k*step>1e-9) {consider(maxt);consider(-maxt);}
        if(have) {
            const double centre=best.theta;
            for(int i=-5;i<=5;++i) consider(std::clamp(centre+i*deg,-maxt,maxt));
        }
        return best;
    };
    bool have=false;Eval best{0,0};
    if(std::isfinite(prefer_theta)&&std::fabs(prefer_theta)>5*deg) {
        bool h=false;const Eval e=search(prefer_theta>0?1:-1,h);
        if(h&&e.cost<=eps) {best=e;have=true;}
    }
    if(!have) best=search(0,have);
    res.theta=best.theta;res.feasible=best.cost<=eps;
    res.residual=res.feasible?0.0:best.cost;
    return res;
}

// Engaging (target further out, or still violating) is fast. Releasing is a first-order exponential ease (not critically
// damped), rate-limited to release_rate.
inline double follow_swivel(double current,double target,double current_violation,double dt,const ArmCollisionConfig& cfg) {
    if(!std::isfinite(current)) current=0;
    if(!std::isfinite(target)||!std::isfinite(dt)||dt<=0) return current;
    const double diff=target-current;
    double step;double limit;
    if(std::fabs(target)>std::fabs(current)||(std::isfinite(current_violation)&&current_violation>cfg.violation_engage)) {
        step=diff;limit=cfg.engage_rate*dt;
    } else {
        const double a=cfg.release_tau>1e-9?1-std::exp(-dt/cfg.release_tau):1.0;
        step=diff*a;limit=cfg.release_rate*dt;
    }
    step=std::clamp(step,-limit,limit);
    return current+step;
}

} // namespace d2vr
