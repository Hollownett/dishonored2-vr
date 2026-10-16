#pragma once
// Pure policy: physical held weapon / hand. World queries are an input (see
// docs/weapon-collision-integration.md); nothing here touches the engine.
#include "tracked_pose.hpp"
#include <cstdint>
#include <optional>

namespace d2vr {

// One world-query result. normal points out of the surface toward free space.
struct CollisionHit {Vec3 point{},normal{};double fraction{};std::uintptr_t entity{};std::uint16_t material{0xffff};}; // material: physics material id (0xffff none)
struct CollisionEvent {Vec3 point{},normal{};double speed{};std::uintptr_t entity{};bool blade{};unsigned side{};std::uint16_t material{0xffff};};
struct CollisionHaptic {float amplitude{},seconds{};};
struct CollisionConfig {
    double palm_radius=.06;       // hand sphere
    double max_shift=.45;         // clamp never drags the weapon farther than this from the tracked pose
    double release_speed=1.5;     // m/s smooth release once the controller backs out
    double hold_seconds=.12;      // contact hysteresis: no new event/haptic while within it
    double haptic_cooldown=.08;
    double speed_for_full=3.0;    // impact m/s mapped to amplitude 1
};
struct CollisionResult {
    Vec3 grip{};                  // clamped grip to render
    Vec3 shift{};                 // grip minus raw grip (apply the same shift to the wrist target for hands)
    std::optional<Vec3> blade_dir;  // blade touching a surface at an angle: rotate the weapon about the grip to this axis
    bool contact{};               // clamped, easing out, or inside hysteresis hold
    std::optional<CollisionEvent> event;   // new contact only
    std::optional<CollisionHaptic> haptic;
};

// Collision returns the visible blade axis; reverse grip negates the controller's forward axis.
inline bool align_weapon_blade(WorldTrackedPose& pose,Vec3 blade_direction,bool reversed=false) noexcept {
    const double target_length=length(blade_direction);
    if(!finite(blade_direction) || !(target_length>1e-9))return false;
    for(const auto axis:pose.basis)if(!finite(axis) || std::abs(dot(axis,axis)-1)>1e-5)return false;
    if(std::abs(dot(pose.basis[0],pose.basis[1]))>1e-5 || std::abs(dot(pose.basis[0],pose.basis[2]))>1e-5 ||
       dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])<1-1e-5)return false;
    const auto from=pose.basis[0]*(reversed?-1.:1.),to=blade_direction*(1/target_length);
    const auto turn=cross(from,to);const double sine=length(turn),cosine=std::clamp(dot(from,to),-1.,1.);
    if(sine<=1e-12 && cosine>=0)return true;
    const auto axis=sine>1e-12?turn*(1/sine):pose.basis[1];
    for(auto& vector:pose.basis)
        vector=vector*cosine+cross(axis,vector)*sine+axis*(dot(axis,vector)*(1-cosine));
    return true;
}

class WeaponCollision {
    struct Side {Vec3 shift{},prev_grip{},prev_tip{};bool has_prev{},contact{};double since_hit=1e9,since_haptic=1e9;};
    CollisionConfig cfg_;Side sides_[2];
    static Vec3 clamp_len(Vec3 v,double m) {const double l=length(v);return l>m&&l>0?v*(m/l):v;}
public:
    explicit WeaponCollision(CollisionConfig c={}) : cfg_(c) {}
    void reset(unsigned side) {if(side<2)sides_[side]=Side{};}
    bool in_contact(unsigned side) const {return side<2&&sides_[side].contact;}
    // anchor: a point known to be in free space (shoulder/chest); the palm sweep starts there.
    // dir: unit blade axis; blade_length 0 for a bare hand or ranged weapon.
    // blade_offset: segment start relative to the grip (a pistol barrel sits above and behind the fist); blade_radius:
    // the weapon's half thickness, already used as the sweep radius by the query, so a thick barrel stops at its surface.
    // query(Vec3 from,Vec3 to) -> std::optional<CollisionHit> (first solid along the segment).
    template<class Query> CollisionResult update(unsigned side,double dt,Vec3 anchor,Vec3 grip,Vec3 dir,double blade_length,Query&& query,
                                                 Vec3 blade_offset={},double blade_radius=0) {
        CollisionResult out{};out.grip=grip;
        if(side>1||!finite(anchor)||!finite(grip)||!finite(dir)||!(dt>0)||!std::isfinite(dt)||!std::isfinite(blade_length)) {if(side<2)reset(side);return out;}
        auto& s=sides_[side];
        const Vec3 tip=grip+blade_offset+dir*blade_length;
        Vec3 vgrip{},vtip{};
        if(s.has_prev) {vgrip=(grip-s.prev_grip)*(1/dt);vtip=(tip-s.prev_tip)*(1/dt);}
        s.prev_grip=grip;s.prev_tip=tip;s.has_prev=true;
        s.since_hit+=dt;s.since_haptic+=dt;
        Vec3 target{};std::optional<CollisionHit> best;bool blade=false;double speed=0;
        // Palm: sweep anchor->grip; stop one radius off the surface.
        if(const auto h=query(anchor,grip)) {
            const double pen=dot(h->point-grip,h->normal)+cfg_.palm_radius;
            // Only a surface at the hand pushes it. A hit far up the sweep means the body anchor is inside geometry
            // (crouching under the real head height): pushing the hand then made the sword jump out of it.
            if(pen>0 && length(h->point-grip)<.25) {target=h->normal*pen;best=h;speed=std::max(0.,-dot(vgrip,h->normal));}
        }
        // Blade: sweep from the palm-corrected grip to the tip; push out by tip penetration.
        if(blade_length>0) {
            const Vec3 g=grip+target+blade_offset,t=g+dir*blade_length;
            if(const auto h=query(g,t)) {
                const double pen=dot(h->point-t,h->normal)+blade_radius;
                if(pen>0) {
                    best=h;blade=true;speed=std::max(0.,-dot(vtip,h->normal));
                    // Slide: the hand stays on the controller and the blade pivots about the grip until its tip rests
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
