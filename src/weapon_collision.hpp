#pragma once
// Pure policy: the physical held weapon's blade. World queries are an input (see
// docs/weapon-collision-integration.md); nothing here touches the engine. The hand itself stops at surfaces in hand_clamp.hpp
// (physical hands phase 1): the blade sweeps from the grip the hand clamp drew.
#include "tracked_pose.hpp"
#include "sword_blade_axis.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr {

// One world-query result. normal points out of the surface toward free space.
struct CollisionHit {Vec3 point{},normal{};double fraction{};std::uintptr_t entity{};std::uint16_t material{0xffff};}; // material: physics material id (0xffff none)
struct CollisionEvent {Vec3 point{},normal{};double speed{};std::uintptr_t entity{};bool blade{};unsigned side{};std::uint16_t material{0xffff};};
struct CollisionHaptic {float amplitude{},seconds{};};
struct CollisionConfig {
    double max_shift=.45;         // clamp never drags the weapon farther than this from the tracked pose
    double release_speed=1.5;     // m/s smooth release once the controller backs out
    double hold_seconds=.12;      // contact hysteresis: no new event/haptic while within it
    double haptic_cooldown=.08;
    double speed_for_full=3.0;    // impact m/s mapped to amplitude 1
    double palm_radius=.06;       // update_with_palm only: the hand sphere of the pre-hand-clamp path (build/no-hand-clamp)
    // v1289 contact_step (seconds and metres, nothing per frame or per headset):
    double plane_seconds=.12;     // a blade contact plane lives this long after its last hit (one the drawn blade rests on: until a clear cast)
    double tau_seconds=.03;       // the critically damped follower's time constant (95% of a step in about 4.7 tau)
    double slack=.015;            // the drawn tip is never deeper than this past a held plane's standoff: that far it engages at once
    double stay_weight=.5;        // solve()'s cost: the weapon's move from the controller's + stay_weight * its move from the last drawn pose
    double push_from=.1,push_to=.2;   // the blade's |tangent| in a plane: below push_from the grip takes all of the straight-in depth, above push_to none
    double blade_skin=.01;        // the tip rests this far off a surface (legacy_step's 1 cm)
    double contact_margin=.01;    // a plane the drawn tip is this close to (past the skin) is one the blade rests on
    double follow_speed=1.5;      // m/s: the fastest the follower moves the drawn tip (legacy_step's release_speed)
    double guard_speed=2.;        // m/s: the fastest the slack guard moves the drawn tip beyond the follower (a far target - a new plane, a push - is a
                                  // quick glide, never a one-frame jump)
};
struct CollisionResult {
    Vec3 grip{};                  // clamped grip to render
    Vec3 shift{};                 // grip minus the grip passed in (a blade pushed straight into a surface)
    std::optional<Vec3> blade_dir;  // blade touching a surface at an angle: rotate the weapon about the grip to this axis
    bool contact{};               // clamped, easing out, or inside hysteresis hold
    std::optional<CollisionEvent> event;   // new contact only
    std::optional<CollisionHaptic> haptic;
};

// Collision returns the visible blade axis; the grip's blade axis is sword_blade_axis (reverse grip negates the controller's forward axis).
inline bool align_weapon_blade(WorldTrackedPose& pose,Vec3 blade_direction,bool reversed=false) noexcept {
    const double target_length=length(blade_direction);
    if(!finite(blade_direction) || !(target_length>1e-9))return false;
    for(const auto axis:pose.basis)if(!finite(axis) || std::abs(dot(axis,axis)-1)>1e-5)return false;
    if(std::abs(dot(pose.basis[0],pose.basis[1]))>1e-5 || std::abs(dot(pose.basis[0],pose.basis[2]))>1e-5 ||
       dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])<1-1e-5)return false;
    const auto from=sword_blade_axis(pose,reversed),to=blade_direction*(1/target_length);
    const auto turn=cross(from,to);const double sine=length(turn),cosine=std::clamp(dot(from,to),-1.,1.);
    if(sine<=1e-12 && cosine>=0)return true;
    const auto axis=sine>1e-12?turn*(1/sine):pose.basis[1];
    for(auto& vector:pose.basis)
        vector=vector*cosine+cross(axis,vector)*sine+axis*(dot(axis,vector)*(1-cosine));
    return true;
}

// v1289 (user, headset and sim window 2026-11-13: "a held weapon touching the corner of an object or a wall sometimes thrashes hard from side to
// side"; lab tests/weapon_contact_lab.cpp). legacy_step's blade response is memoryless: each frame ONE hit - the closest along the controller's
// blade, answered a frame late - decides the drawn blade alone. At a concave corner that hit alternates between the two walls with tracking
// jitter, and the single-plane pivot slid the blade along the hit wall INTO the other one (lab: 185-258 mm tip jumps, 20-32 reversals/s; 16 cm
// inside the other wall when still); at a convex edge the cast's rounded normal turns toward the blade axis and crossed the pivot/push cut
// (|tangent| .15): the grip jumped 280 mm back in one frame, then the blade swung 48 degrees (524 mm tip jumps). contact_step instead:
//  - planes: a hit is held as a half-space for plane_seconds (a re-hit of the same surface refreshes it). The answered cast of the controller's
//    blade judges the planes held: one it passes through before its own hit (a convex edge's other face), or with no hit at all, is refuted; one
//    it reaches at or beyond its hit is shadowed and kept (a concave corner holds both walls). A plane the blade rests on does not age, and when a
//    hit refutes it it is contested: the blade stays on it (that hit's surface dropped) or takes the new surface, whichever solve() scores lower.
//  - slid casts: while the follower holds the blade off planes, every other cast is posted along the target blade instead of the controller's
//    (as the hand clamp's slid sweeps): the closest-hit cast along the controller's blade reports only the first wall, the slid one meets the
//    corner's second wall. Slid answers only add planes (where a thin face ends they would refute it every other frame).
//  - solve: candidates are the least rotations of the controller's blade that keep the tip out of every held plane (up to two crossing: a
//    corner), from the grip pushed back by a share of each plane's straight-in depth that grows smoothly as the blade turns toward its normal
//    (push_from..push_to of |tangent|; legacy: all or nothing at .15), and the controller's blade pushed out of every plane. The cheapest wins:
//    the weapon's move (tip plus grip) from the controller's plus stay_weight times its move from the last drawn pose (two equal corner
//    solutions, tipped up or down, keep the side the blade was drawn on; a blade resting on an edge does not pass through it until the
//    controller clearly wants the other side).
//  - follow: the drawn correction (rotation and push from the controller's pose) is critically damped toward the target (tau_seconds, exact for
//    any dt: no overshoot), its tip never faster than follow_speed; a pose deeper than slack past a held plane is brought out at guard_speed
//    more (a new surface or a push is a glide, never a one-frame jump). Settled with nothing to correct, the controller's pose passes through
//    exactly (free air is untouched, no latency).
// Cost: a mid-blade hit is a contact for up to three frames before the tip constraint holds it (legacy: one); the weapon reaches a far target
// at 1.5-3.5 m/s instead of in one frame. build/legacy-weapon-contact restores legacy_step (set_legacy). The build/no-hand-clamp palm path
// (update_with_palm) is always legacy_step.
struct WeaponContactStats {   // per hand since the probe started (status JSON weapon_collision.contact)
    std::uint64_t frames{};          // frames in contact (the response moved the weapon, or the contact/hold flag)
    std::uint64_t plane_changes{};   // in contact: the surface the response holds the weapon off changed (normal turned > 8 deg, appeared, went)
    std::uint64_t reversals{};       // in contact: two consecutive drawn tip steps over 5 mm pointing more than 90 deg apart
    double seconds{},damped_seconds{};   // time in contact; time contact_step's damped follower was engaged
    double max_jump_m{},max_turn_deg{};  // in contact: the largest frame-to-frame change of the drawn tip's offset from the tip the controller's blade
                                         // has at the grip passed in (the hand clamp's), and of the blade's rotation from the controller's: what the
                                         // blade response moved, not the controller or the hand clamp
};
class WeaponCollision {
    struct Plane {Vec3 point{},normal{};double age{};bool active{},refuted{},contested{};};
    static constexpr std::size_t plane_capacity=4;
    struct Side {Vec3 shift{},prev_grip{},prev_tip{};bool has_prev{},contact{};double since_hit=1e9,since_haptic=1e9;
        // contact_step
        std::array<Plane,plane_capacity> planes{};std::size_t plane_count{};
        Vec3 cast_from{},cast_to{};bool has_cast{};                // the segment the next answer describes (posted this frame)
        Vec3 rot{},rot_rate{},push{},push_rate{};bool engaged{};   // the follower: drawn blade = rotate(controller's, rot), grip + push
        Vec3 target_rot{},target_push{};bool last_cast_slid{};    // last frame's target; whether the segment posted last frame was the slid blade
        Vec3 last_dir{},last_tip{},last_grip{};bool has_last{};};   // last frame's drawn blade (its start, tip and axis)
    struct Trace {Vec3 offset{},corr{},tip{},step{},normal{};bool contact{},has_normal{},has{};};   // stats bookkeeping (survives reset)
    CollisionConfig cfg_;Side sides_[2];bool legacy_{};
    WeaponContactStats stats_[2]{};Trace trace_[2]{};
    static Vec3 clamp_len(Vec3 v,double m) {const double l=length(v);return l>m&&l>0?v*(m/l):v;}
public:
    explicit WeaponCollision(CollisionConfig c={}) : cfg_(c) {}
    void reset(unsigned side) {if(side<2)sides_[side]=Side{};}
    bool in_contact(unsigned side) const {return side<2&&sides_[side].contact;}
    void set_legacy(bool on) {legacy_=on;}   // build/legacy-weapon-contact: legacy_step for the hand-clamp path too
    bool legacy() const {return legacy_;}
    const WeaponContactStats& stats(unsigned side) const {return stats_[side&1u];}
    // The blade cast's sphere radius (the caller's slot-1 query): contact_step judges its planes with the sphere the engine swept.
    static double cast_radius(double blade_radius) {return std::max(.02,blade_radius);}
    // grip: the drawn grip (the hand clamp's); the blade sweeps from it. dir: unit blade axis; blade_length 0 for a bare hand
    // (nothing is swept: the shift eases out).
    // Blade only since physical hands Task 5: the chest-anchored palm sweep is gone (the hand clamp sweeps from the previous drawn hand).
    // blade_offset: segment start relative to the grip (a pistol barrel sits above and behind the fist); blade_radius:
    // the weapon's half thickness, already used as the sweep radius by the query, so a thick barrel stops at its surface.
    // query(Vec3 from,Vec3 to) -> std::optional<CollisionHit> (first solid along the segment).
    template<class Query> CollisionResult update(unsigned side,double dt,Vec3 grip,Vec3 dir,double blade_length,Query&& query,
                                                 Vec3 blade_offset={},double blade_radius=0) {
        return step(side,dt,nullptr,grip,dir,blade_length,query,blade_offset,blade_radius);
    }
    // The pre-hand-clamp path, kept for build/no-hand-clamp (a headset A/B): the palm sweeps anchor -> grip first (anchor: a point known
    // to be in free space, the chest) and stops palm_radius off the first surface near the hand; the blade sweeps from that grip.
    template<class Query> CollisionResult update_with_palm(unsigned side,double dt,Vec3 anchor,Vec3 grip,Vec3 dir,double blade_length,Query&& query,
                                                           Vec3 blade_offset={},double blade_radius=0) {
        return step(side,dt,&anchor,grip,dir,blade_length,query,blade_offset,blade_radius);
    }
private:
    template<class Query> CollisionResult step(unsigned side,double dt,const Vec3* anchor,Vec3 grip,Vec3 dir,double blade_length,Query& query,
                                               Vec3 blade_offset,double blade_radius) {
        std::optional<Vec3> normal;   // the surface the response holds the weapon off this frame (stats)
        const bool contact_path=!legacy_ && !anchor && side<2 && finite(grip) && finite(dir) && length(dir)>.5 && finite(blade_offset) &&
                                dt>0 && std::isfinite(dt) && std::isfinite(blade_length);
        const auto out=contact_path?contact_step(side,dt,grip,dir,blade_length,query,blade_offset,blade_radius,normal):
                                    legacy_step(side,dt,anchor,grip,dir,blade_length,query,blade_offset,blade_radius,normal);
        if(side<2) note_stats(side,dt,grip,dir,blade_length,blade_offset,out,normal);
        return out;
    }
    void note_stats(unsigned side,double dt,Vec3 grip,Vec3 dir,double blade_length,Vec3 blade_offset,const CollisionResult& out,
                    const std::optional<Vec3>& normal) {
        auto& st=stats_[side];auto& tr=trace_[side];
        if(!finite(grip) || !finite(dir) || !(length(dir)>.5) || !finite(blade_offset) || !(dt>0) || !std::isfinite(dt) || !std::isfinite(blade_length)) {tr=Trace{};return;}
        const bool contact=out.contact || out.blade_dir.has_value();
        const double reach=blade_length>0?blade_length:0.;
        const Vec3 raw=dir*(1/length(dir));
        const Vec3 drawn=out.blade_dir && length(*out.blade_dir)>1e-9?*out.blade_dir*(1/length(*out.blade_dir)):raw;
        const Vec3 offset=(out.grip-grip)+(drawn-raw)*reach,corr=rotation_between(raw,drawn),tip=out.grip+blade_offset+drawn*reach;
        Vec3 step{};
        if(contact) {
            ++st.frames;st.seconds+=dt;if(sides_[side].engaged) st.damped_seconds+=dt;
            if(tr.has && tr.contact) {
                st.max_jump_m=std::max(st.max_jump_m,length(offset-tr.offset));
                st.max_turn_deg=std::max(st.max_turn_deg,length(corr-tr.corr)*57.29577951308232);
                if(tr.has_normal!=normal.has_value() || (normal && dot(*normal,tr.normal)<.99)) ++st.plane_changes;
                step=tip-tr.tip;
                if(length(step)>.005 && length(tr.step)>.005 && dot(step,tr.step)<0) ++st.reversals;
            }
        }
        tr.offset=offset;tr.corr=corr;tr.tip=tip;tr.step=step;tr.contact=contact;tr.has=true;tr.has_normal=normal.has_value();if(normal) tr.normal=*normal;
    }
    // The rotation vector (axis * angle) taking unit a to unit b by the least rotation; a and b opposite: half a turn about some perpendicular.
    static Vec3 rotation_between(Vec3 a,Vec3 b) {
        const Vec3 axis=cross(a,b);const double sine=length(axis),cosine=std::clamp(dot(a,b),-1.,1.);
        if(sine>1e-12) return axis*(std::atan2(sine,cosine)/sine);
        if(cosine>0) return {};
        const Vec3 side=cross(a,std::abs(a.x)<.9?Vec3{1,0,0}:Vec3{0,1,0});
        return side*(3.14159265358979323846/length(side));
    }
    static Vec3 rotated(Vec3 v,Vec3 rot) {
        const double angle=length(rot);if(!(angle>0)) return v;
        const Vec3 k=rot*(1/angle);
        return v*std::cos(angle)+cross(k,v)*std::sin(angle)+k*(dot(k,v)*(1-std::cos(angle)));
    }
    // Where along from->to a sphere of `radius` first touches the plane (point, unit normal toward free space): 0 when it starts touching.
    static std::optional<double> crossing(Vec3 from,Vec3 to,Vec3 point,Vec3 normal,double radius) {
        const double s0=dot(from-point,normal)-radius,s1=dot(to-point,normal)-radius;
        if(s0<=0) return 0.;
        if(s1>=0) return std::nullopt;
        return s0/(s0-s1);
    }
    static double smoothstep(double a,double b,double x) {const double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);}
    // The unit directions where the least rotation from d0 keeps the tip out of the planes, the grip fixed (dot(d, n[k]) >= c[k]): the nearest
    // point of each plane's circle, and the two crossings of each pair of circles (a corner: tipped up or down along it). Unfiltered: the
    // caller keeps the ones that satisfy every plane.
    template<class Each> static void boundary_directions(Vec3 d0,const Vec3* n,const double* c,std::size_t m,Vec3 last_dir,Each&& each) {
        for(std::size_t i=0;i<m;++i) {
            if(c[i]>1) continue;
            const double ci=std::max(c[i],-1.);
            Vec3 tangent=d0-n[i]*dot(d0,n[i]);
            if(length(tangent)<1e-6) tangent=last_dir-n[i]*dot(last_dir,n[i]);   // the blade along the normal: the side it was drawn on
            if(length(tangent)>=1e-6) each(n[i]*ci+tangent*(std::sqrt(std::max(0.,1-ci*ci))/length(tangent)));
            for(std::size_t j=i+1;j<m;++j) {
                if(c[j]>1) continue;
                const double k=dot(n[i],n[j]),det=1-k*k;
                if(!(det>1e-9)) continue;
                const double a=(ci-k*c[j])/det,b=(c[j]-k*ci)/det;
                const Vec3 u=n[i]*a+n[j]*b;const double rest=1-dot(u,u);
                if(rest<0) continue;
                const Vec3 w=cross(n[i],n[j])*(std::sqrt(rest)/std::sqrt(det));
                each(u+w);each(u-w);
            }
        }
    }
    struct Target {Vec3 push{},dir{};double cost{};};
    // The constraint a plane puts on the blade: its tip (`at` metres along it) at least `off` off the plane (legacy_step's: 1 cm plus the weapon's
    // radius).
    struct Row {Vec3 n,p;double at,off;};
    Row row(const Plane& q,double reach,double radius) const {return {q.normal,q.point,reach,radius+cfg_.blade_skin};}
    static double gap(const Row& r,Vec3 g0,Vec3 push,Vec3 dir) {return dot(g0+push+dir*r.at-r.p,r.n)-r.off;}
    // The rows of the planes in `use` that the blade's own start is in front of (one it is behind: a face the blade is past).
    std::size_t rows(const Side& s,const std::array<bool,plane_capacity>& use,Vec3 g0,double reach,double radius,std::array<Row,plane_capacity>& out) const {
        std::size_t m=0;
        for(std::size_t i=0;i<s.plane_count;++i) if(use[i] && dot(g0-s.planes[i].point,s.planes[i].normal)>0) out[m++]=row(s.planes[i],reach,radius);
        return m;
    }
    // The drawn blade the planes in `use` allow (g0: the blade's start on the drawn grip; d0: the controller's unit blade axis). Candidates: the
    // controller's blade when it is out of them all; else every least rotation (boundary_directions) from the grip pushed back by the planes'
    // straight-in share, and the controller's blade pushed out of all of them. The one that moves the weapon least wins: cost = D(candidate,
    // controller's) + stay_weight * D(candidate, last drawn), D = the tip's distance plus the grip's (so a push, which moves the hand off the
    // controller, costs twice its length; an equal pair - tipped up or down along a corner - keeps the side it was drawn on until the controller
    // clearly wants the other).
    Target solve(const Side& s,const std::array<bool,plane_capacity>& use,Vec3 g0,Vec3 d0,double reach,double radius) const {
        std::array<Row,plane_capacity> r{};const std::size_t m=reach>0?rows(s,use,g0,reach,radius,r):0;
        const Vec3 raw_tip=g0+d0*reach;
        const auto cost=[&](Vec3 push,Vec3 dir) {
            const Vec3 grip=g0+push,tip=grip+dir*reach;
            double c=length(tip-raw_tip)+length(push);
            if(s.has_last) c+=cfg_.stay_weight*(length(tip-s.last_tip)+length(grip-s.last_grip));
            return c;
        };
        const auto feasible=[&](Vec3 push,Vec3 dir,double tolerance) {
            for(std::size_t k=0;k<m;++k) if(gap(r[k],g0,push,dir)<-tolerance) return false;
            return true;
        };
        if(feasible({},d0,1e-9)) return Target{{},d0,cost({},d0)};
        std::optional<Target> best;
        const auto consider=[&](Vec3 push,Vec3 dir,double tolerance) {
            const double l=length(dir);
            if(!finite(push) || !finite(dir) || !(l>.5)) return;
            dir=dir*(1/l);
            if(!feasible(push,dir,tolerance)) return;
            const double c=cost(push,dir);
            if(!best || c<best->cost) best=Target{push,dir,c};
        };
        // The straight-in share: each plane the controller's blade is past pushes the grip back by its depth times a share that falls smoothly
        // from 1 (blade along the normal) to 0 (|tangent| push_to and more).
        Vec3 push{};
        for(std::size_t k=0;k<m;++k) {
            const double depth=-gap(r[k],g0,{},d0);
            if(!(depth>0)) continue;
            push=push+r[k].n*(depth*(1-smoothstep(cfg_.push_from,cfg_.push_to,length(d0-r[k].n*dot(d0,r[k].n)))));
        }
        push=clamp_len(push,cfg_.max_shift);
        std::array<Vec3,plane_capacity> n{};std::array<double,plane_capacity> c{};
        for(std::size_t k=0;k<m;++k) {n[k]=r[k].n;c[k]=(r[k].off-dot(g0+push-r[k].p,r[k].n))/r[k].at;}
        const Vec3 last_dir=s.has_last?s.last_dir:d0;
        boundary_directions(d0,n.data(),c.data(),m,last_dir,[&](Vec3 d) {consider(push,d,1e-9);});
        // The controller's blade pushed out of every plane (projected pass after pass; a corner converges in a few).
        Vec3 x{};
        for(int pass=0;pass<32;++pass) for(std::size_t k=0;k<m;++k) {const double g=gap(r[k],g0,x,d0);if(g<0) x=x-r[k].n*g;}
        const Vec3 full=clamp_len(x,cfg_.max_shift);
        consider(full,d0,1e-4);
        return best?*best:Target{full,d0,cost(full,d0)};   // nothing keeps the blade out (planes in conflict, the push capped): the push as far as it goes
    }
    // A hit on a held surface (parallel normal, within 1 cm) refreshes it; else a new plane, in place of the oldest one the blade does not rest on
    // when full (else the oldest). Its index.
    static std::size_t add_plane(Side& s,Vec3 point,Vec3 normal) {
        for(std::size_t i=0;i<s.plane_count;++i) {
            auto& q=s.planes[i];
            if(dot(q.normal,normal)>.99 && std::abs(dot(point-q.point,q.normal))<.01) {q.point=point;q.normal=normal;q.age=0;return i;}
        }
        std::size_t slot=s.plane_count;
        if(slot>=plane_capacity) {
            slot=0;
            for(std::size_t i=1;i<plane_capacity;++i) {
                const auto& a=s.planes[i];const auto& b=s.planes[slot];
                if((b.active || b.contested) && !(a.active || a.contested)) {slot=i;continue;}
                if((a.active || a.contested)==(b.active || b.contested) && a.age>b.age) slot=i;
            }
        } else ++s.plane_count;
        s.planes[slot]=Plane{point,normal,0,false,false,false};
        return slot;
    }
    template<class Keep> static void keep_planes(Side& s,Keep&& keep) {
        std::size_t kept=0;
        for(std::size_t i=0;i<s.plane_count;++i) if(keep(s.planes[i])) s.planes[kept++]=s.planes[i];
        s.plane_count=kept;
    }
    template<class Query> CollisionResult contact_step(unsigned side,double dt,Vec3 grip,Vec3 dir,double blade_length,Query& query,Vec3 blade_offset,
                                                       double blade_radius,std::optional<Vec3>& normal) {
        CollisionResult out{};out.grip=grip;
        auto& s=sides_[side];
        const double reach=blade_length>0?blade_length:0.;
        const Vec3 d0=dir*(1/length(dir)),g0=grip+blade_offset,tip=g0+dir*blade_length;
        // Events and haptics: legacy_step's rule (what the controller's blade meets).
        Vec3 vtip{};
        if(s.has_prev) vtip=(tip-s.prev_tip)*(1/dt);
        s.prev_grip=grip;s.prev_tip=tip;s.has_prev=true;
        s.since_hit+=dt;s.since_haptic+=dt;
        std::optional<CollisionHit> hit,best;double speed=0;
        // The cast posted this frame (its answer comes back next frame): the controller's blade; while the follower holds the blade off planes,
        // every other one is the slid blade instead - last frame's target on this frame's controller - which meets the next surface along the
        // drawn blade directly (a corner's second wall: the closest-hit cast along the controller's blade reports only the first; the hand clamp's
        // slid sweeps, hand_clamp.hpp, do the same).
        Vec3 cast_a=g0,cast_b=tip;bool slid=false;
        if(reach>0 && s.engaged && s.plane_count && !s.last_cast_slid && length(s.target_rot)+length(s.target_push)>0) {
            cast_a=g0+s.target_push;cast_b=cast_a+rotated(d0,s.target_rot)*reach;slid=true;
        }
        if(reach>0) {
            hit=query(cast_a,cast_b);
            if(hit) {const double pen=dot(hit->point-tip,hit->normal)+blade_radius;if(pen>0) {best=hit;speed=std::max(0.,-dot(vtip,hit->normal));}}
        }
        if(best) {
            if(!s.contact) {
                out.event=CollisionEvent{best->point,best->normal,speed,best->entity,true,side,best->material};
                if(s.since_haptic>=cfg_.haptic_cooldown) {
                    out.haptic=CollisionHaptic{static_cast<float>(std::clamp(.2+.8*speed/cfg_.speed_for_full,.2,1.)),
                        static_cast<float>(std::clamp(.03+.02*speed,.03,.12))};
                    s.since_haptic=0;
                }
            }
            s.contact=true;s.since_hit=0;
        } else if(s.since_hit>cfg_.hold_seconds) s.contact=false;
        // Fold in the answer: it describes the segment posted last frame (s.cast_from -> s.cast_to).
        std::optional<std::size_t> fresh;bool contest=false;
        if(!(reach>0)) {s.plane_count=0;s.has_cast=false;}
        else {
            const double rc=cast_radius(blade_radius);
            for(std::size_t i=0;i<s.plane_count;++i) {auto& q=s.planes[i];q.refuted=q.contested=false;if(!q.active) q.age+=dt;}
            const bool hit_ok=hit && finite(hit->point) && finite(hit->normal) && length(hit->normal)>.5;
            const Vec3 hn=hit_ok?hit->normal*(1/length(hit->normal)):Vec3{};
            // Only the controller's blade judges: a slid cast passes the planes it slides on by design, and where a thin face ends (a shelf's
            // 2 cm front) its clear answer said nothing about the face and refuted it every other frame (lab: 40 reversals/s). It only adds.
            if(s.has_cast && !s.last_cast_slid) {
                const Vec3 span_vector=s.cast_to-s.cast_from;const double span=length(span_vector);
                double at=1;
                if(hit_ok) {
                    if(const auto t=crossing(s.cast_from,s.cast_to,hit->point,hn,rc)) at=*t;
                    else at=span>1e-9?std::clamp(dot(hit->point-s.cast_from,span_vector)/(span*span),0.,1.):0.;
                }
                for(std::size_t i=0;i<s.plane_count;++i) {
                    auto& q=s.planes[i];
                    const auto t=crossing(s.cast_from,s.cast_to,q.point,q.normal,rc);
                    if(!t) continue;                                  // the cast never reached its region: no news
                    if(!hit_ok) q.refuted=true;                       // a clear cast passed through it
                    else if((at-*t)*span>.002) {if(q.active) {q.contested=true;contest=true;} else q.refuted=true;}   // passed through it before the hit
                }
            }
            keep_planes(s,[&](const Plane& q) {return !q.refuted && (q.age<=cfg_.plane_seconds || q.active || q.contested);});
            if(hit_ok) fresh=add_plane(s,hit->point,hn);
            if(fresh && s.planes[*fresh].contested) {s.planes[*fresh].contested=false;contest=false;for(std::size_t i=0;i<s.plane_count;++i) contest=contest || s.planes[i].contested;}
            s.cast_from=cast_a;s.cast_to=cast_b;s.has_cast=true;s.last_cast_slid=slid;
        }
        // The target: every plane held but the contested ones; or, contested, the blade staying on what it rests on (that hit's surface dropped)
        // when that costs less (solve's cost: the weapon's move from the controller's plus stay_weight times its move from the last drawn).
        std::array<bool,plane_capacity> use{};
        for(std::size_t i=0;i<s.plane_count;++i) use[i]=!s.planes[i].contested;
        Target target=solve(s,use,g0,d0,reach,blade_radius);
        if(contest) {
            std::array<bool,plane_capacity> stay{};
            for(std::size_t i=0;i<s.plane_count;++i) stay[i]=!fresh || i!=*fresh;
            const Target kept=solve(s,stay,g0,d0,reach,blade_radius);
            const bool keep_old=kept.cost<=target.cost;
            if(keep_old) {target=kept;use=stay;}
            keep_planes(s,[&,i=std::size_t{0}](const Plane& q) mutable {const bool drop=keep_old?(fresh && i==*fresh):q.contested;++i;return !drop;});
            for(std::size_t i=0;i<s.plane_count;++i) {s.planes[i].contested=false;use[i]=true;}
        }
        // The surface the response holds the weapon off (stats): the one the controller's tip is deepest past.
        {double deepest=0;
         for(std::size_t i=0;i<s.plane_count;++i) {const auto& q=s.planes[i];
             const double depth=-gap(row(q,reach,blade_radius),g0,{},d0);
             if(use[i] && reach>0 && dot(g0-q.point,q.normal)>0 && depth>deepest) {deepest=depth;normal=q.normal;}}}
        // Follow: critically damped toward the target's correction, exact for this dt (x(t) = target + (c1 + c2 t) e^-wt).
        const Vec3 rot_target=rotation_between(d0,target.dir),push_target=target.push;
        s.target_rot=rot_target;s.target_push=push_target;
        const bool settled_target=length(rot_target)==0 && length(push_target)==0;
        if(s.engaged || !settled_target) {
            const double w=1/cfg_.tau_seconds,e=std::exp(-w*dt);
            const auto follow=[&](Vec3& x,Vec3& v,Vec3 goal) {const Vec3 c1=x-goal,c2=v+c1*w;x=goal+(c1+c2*dt)*e;v=(v-c2*(w*dt))*e;};
            const Vec3 rot_before=s.rot,push_before=s.push;
            follow(s.rot,s.rot_rate,rot_target);follow(s.push,s.push_rate,push_target);
            // The follower's tip moves at most follow_speed (a far target - a new surface, a push - is a steady glide, then the damped approach).
            if(reach>0 || length(s.push-push_before)>0) {
                const double travel=length((s.push-push_before)+(rotated(d0,s.rot)-rotated(d0,rot_before))*reach),limit=cfg_.follow_speed*dt;
                if(travel>limit) {const double k=limit/travel;s.rot=rot_before+(s.rot-rot_before)*k;s.push=push_before+(s.push-push_before)*k;s.rot_rate=s.rot_rate*k;s.push_rate=s.push_rate*k;}
            }
            s.engaged=true;
            // Deeper than slack past a held plane: brought toward the target at guard_speed more this frame (then from rest: no overshoot).
            std::array<Row,plane_capacity> r{};const std::size_t m=reach>0?rows(s,use,g0,reach,blade_radius,r):0;
            const auto depth=[&](Vec3 rot,Vec3 push) {
                const Vec3 d=rotated(d0,rot);double worst=-1e9;
                for(std::size_t k=0;k<m;++k) worst=std::max(worst,-gap(r[k],g0,push,d));
                return worst;
            };
            if(reach>0 && depth(s.rot,s.push)>cfg_.slack) {
                double lo=0,hi=1;
                for(int k=0;k<16;++k) {const double mid=(lo+hi)*.5;
                    if(depth(s.rot+(rot_target-s.rot)*mid,s.push+(push_target-s.push)*mid)>cfg_.slack) lo=mid;else hi=mid;}
                // At most guard_speed of tip travel this frame (the rest next frame, from rest: no overshoot).
                const auto tip_at=[&](double u) {return g0+s.push+(push_target-s.push)*u+rotated(d0,s.rot+(rot_target-s.rot)*u)*reach;};
                const double travel=length(tip_at(hi)-tip_at(0)),limit=cfg_.guard_speed*dt;
                if(travel>limit) hi*=limit/travel;
                s.rot=s.rot+(rot_target-s.rot)*hi;s.push=s.push+(push_target-s.push)*hi;s.rot_rate={};s.push_rate={};
            }
            s.push=clamp_len(s.push,cfg_.max_shift);
            if(settled_target && length(s.rot)<1e-6 && length(s.push)<1e-6 && length(s.rot_rate)<1e-4 && length(s.push_rate)<1e-4) {
                s.rot={};s.rot_rate={};s.push={};s.push_rate={};s.engaged=false;
            }
        }
        const Vec3 drawn=s.engaged?rotated(d0,s.rot):d0;
        if(s.engaged) {
            out.shift=s.push;out.grip=grip+s.push;
            if(reach>0 && length(s.rot)>0) out.blade_dir=drawn;
        }
        out.contact=s.contact || s.engaged;
        // The planes the blade rests on: the target's tip within contact_margin of their standoff (the target, not the damped pose, which lags it).
        // They do not age; only a clear cast or a lost contest takes them.
        const Vec3 drawn_tip=g0+(s.engaged?s.push:Vec3{})+drawn*reach;
        for(std::size_t i=0;i<s.plane_count;++i) {auto& q=s.planes[i];
            q.active=reach>0 && dot(g0-q.point,q.normal)>0 && gap(row(q,reach,blade_radius),g0,target.push,target.dir)<=cfg_.contact_margin;}
        s.last_dir=drawn;s.last_tip=drawn_tip;s.last_grip=g0+(s.engaged?s.push:Vec3{});s.has_last=true;
        return out;
    }
    template<class Query> CollisionResult legacy_step(unsigned side,double dt,const Vec3* anchor,Vec3 grip,Vec3 dir,double blade_length,Query& query,
                                                      Vec3 blade_offset,double blade_radius,std::optional<Vec3>& normal) {
        CollisionResult out{};out.grip=grip;
        if(side>1||(anchor && !finite(*anchor))||!finite(grip)||!finite(dir)||!finite(blade_offset)||!(dt>0)||!std::isfinite(dt)||!std::isfinite(blade_length)) {if(side<2)reset(side);return out;}
        auto& s=sides_[side];
        const Vec3 tip=grip+blade_offset+dir*blade_length;
        Vec3 vgrip{},vtip{};
        if(s.has_prev) {vgrip=(grip-s.prev_grip)*(1/dt);vtip=(tip-s.prev_tip)*(1/dt);}
        s.prev_grip=grip;s.prev_tip=tip;s.has_prev=true;
        s.since_hit+=dt;s.since_haptic+=dt;
        Vec3 target{};std::optional<CollisionHit> best;bool blade=false;double speed=0;
        // Palm (update_with_palm only): sweep anchor->grip; stop one radius off the surface.
        if(anchor) if(const auto h=query(*anchor,grip)) {
            const double pen=dot(h->point-grip,h->normal)+cfg_.palm_radius;
            // Only a surface at the hand pushes it. A hit far up the sweep means the body anchor is inside geometry
            // (crouching under the real head height): pushing the hand then made the sword jump out of it.
            if(pen>0 && length(h->point-grip)<.25) {target=h->normal*pen;best=h;speed=std::max(0.,-dot(vgrip,h->normal));}
        }
        // Blade: sweep from the (palm-corrected) grip to the tip; push out by tip penetration.
        if(blade_length>0) {
            const Vec3 g=grip+target+blade_offset,t=g+dir*blade_length;
            if(const auto h=query(g,t)) {
                const double pen=dot(h->point-t,h->normal)+blade_radius;
                if(pen>0) {
                    best=h;blade=true;speed=std::max(0.,-dot(vtip,h->normal));
                    // Slide: the hand stays where it is drawn and the blade pivots about the grip until its tip rests
                    // 1 cm off the surface. Shifting the whole hand by the tip depth made the sword jump out of the hand.
                    const double c=-(dot(g-h->point,h->normal)-.01-blade_radius)/blade_length;
                    const Vec3 tangent=dir-h->normal*dot(dir,h->normal);const double tl=length(tangent);
                    if(c>-1 && c<1 && tl>.15) out.blade_dir=tangent*(std::sqrt(1-c*c)/tl)+h->normal*c;
                    else target=target+h->normal*pen; // straight into the surface: push back
                }
            }
        }
        target=clamp_len(target,cfg_.max_shift);
        if(best) {
            if(!s.contact) {
                out.event=CollisionEvent{best->point,best->normal,speed,best->entity,blade,side,best->material};
                if(s.since_haptic>=cfg_.haptic_cooldown) {
                    out.haptic=CollisionHaptic{static_cast<float>(std::clamp(.2+.8*speed/cfg_.speed_for_full,.2,1.)),
                        static_cast<float>(std::clamp(.03+.02*speed,.03,.12))};
                    s.since_haptic=0;
                }
            }
            s.contact=true;s.since_hit=0;
            // Engage instantly (never render inside the wall); ease only when the push shrinks.
            if(length(target)>=length(s.shift)) s.shift=target;
            else s.shift=s.shift+clamp_len(target-s.shift,cfg_.release_speed*dt);
        } else {
            if(s.since_hit>cfg_.hold_seconds) s.contact=false;
            const double l=length(s.shift),step=cfg_.release_speed*dt;
            s.shift=l<=step?Vec3{}:s.shift*((l-step)/l);
        }
        out.shift=s.shift;out.grip=grip+s.shift;out.contact=s.contact||length(s.shift)>1e-4;
        if(best) normal=best->normal;
        return out;
    }
};

// Adapter for a binary native test (e.g. the 0x95550 occlusion wrapper: true = blocked). Bisects
// the segment. Normal is approximated as the reversed segment direction (UNVERIFIED for slanted
// surfaces; prefer a real normal from 0x6c6760 when available).
template<class Blocked> std::optional<CollisionHit> bisect_segment(Vec3 from,Vec3 to,Blocked&& blocked,unsigned iterations=6) {
    if(!blocked(from,to)) return std::nullopt;
    const Vec3 d=to-from;const double l=length(d);
    if(!(l>1e-9)) return std::nullopt;
    double lo=0,hi=1;
    for(unsigned i=0;i<iterations;++i) {const double m=(lo+hi)/2;if(blocked(from,from+d*m))hi=m;else lo=m;}
    return CollisionHit{from+d*hi,d*(-1/l),hi,0};
}

}  // namespace d2vr
