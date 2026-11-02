#pragma once
// Dual wield visuals (docs/research-dual-wield.md): while the hands are mirrored, a weapon the engine draws on one arm is shown by a copy
// at the other controller. Pure math, game world (Z up). A Pose is a position plus the world directions of the frame's forward, left and up
// axes (WorldTrackedPose). The offset of a weapon in its hand's frame is measured live, mirrored across the hand's left axis, and composed
// with the other hand.
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace d2vr::held_copy {
using Pose=WorldTrackedPose;
inline bool pose_finite(const Pose& p) noexcept {return finite(p.position) && finite(p.basis[0]) && finite(p.basis[1]) && finite(p.basis[2]);}
// Finite, unit, orthogonal and right handed: what the engine's frames are, and the only thing worth teleporting to.
inline bool valid(const Pose& p) noexcept {
    if(!pose_finite(p) || std::abs(p.position.x)>1e7 || std::abs(p.position.y)>1e7 || std::abs(p.position.z)>1e7) return false;
    for(unsigned i=0;i<3;++i) {
        if(std::abs(length(p.basis[i])-1)>.02) return false;
        for(unsigned j=0;j<i;++j) if(std::abs(dot(p.basis[i],p.basis[j]))>.02) return false;
    }
    return dot(cross(p.basis[0],p.basis[1]),p.basis[2])>.98;
}
// A frame as the game stores it: origin[3] and axis rows (forward, left, up), as entity+0x300/+0x30c or model+0x1860/+0x186c.
inline Pose from_rows(const float* origin,const float* rows) noexcept {
    return {{origin[0],origin[1],origin[2]},{Vec3{rows[0],rows[1],rows[2]},Vec3{rows[3],rows[4],rows[5]},Vec3{rows[6],rows[7],rows[8]}}};
}
// `world` expressed in `frame` (position and basis vectors as frame-local coordinates).
inline Pose to_local(const Pose& frame,const Pose& world) noexcept {
    const auto local=[&](Vec3 v) {return Vec3{dot(v,frame.basis[0]),dot(v,frame.basis[1]),dot(v,frame.basis[2])};};
    return {local(world.position-frame.position),{local(world.basis[0]),local(world.basis[1]),local(world.basis[2])}};
}
inline Pose to_world(const Pose& frame,const Pose& local) noexcept {
    const auto world=[&](Vec3 v) {return frame.basis[0]*v.x+frame.basis[1]*v.y+frame.basis[2]*v.z;};
    return {frame.position+world(local.position),{world(local.basis[0]),world(local.basis[1]),world(local.basis[2])}};
}
// The same grip on the other hand: reflect the local left (Y) axis. Position y is negated; R' = S R S, S = diag(1,-1,1), so each basis
// vector has its y negated and the one that stands for left is negated too (det stays +1).
inline Pose mirror(const Pose& local) noexcept {
    const auto flip=[](Vec3 v,double s) {return Vec3{v.x*s,-v.y*s,v.z*s};};
    return {{local.position.x,-local.position.y,local.position.z},{flip(local.basis[0],1),flip(local.basis[1],-1),flip(local.basis[2],1)}};
}
// A grip measured on `stored_side` as seen from `side`.
inline Pose offset_for(const Pose& stored,unsigned stored_side,unsigned side) noexcept {return stored_side==side?stored:mirror(stored);}
inline Pose orthonormalized(const Pose& p) noexcept {
    Pose r{p.position,{}};
    r.basis[0]=unit(p.basis[0]);
    r.basis[1]=unit(p.basis[1]-r.basis[0]*dot(p.basis[1],r.basis[0]));
    r.basis[2]=cross(r.basis[0],r.basis[1]);
    return r;
}
inline Pose blend(const Pose& a,const Pose& b,double t) noexcept {
    Pose r{a.position+(b.position-a.position)*t,{}};
    for(unsigned i=0;i<3;++i) r.basis[i]=a.basis[i]+(b.basis[i]-a.basis[i])*t;
    return orthonormalized(r);
}
inline double angle(const Pose& a,const Pose& b) noexcept {
    const double trace=dot(a.basis[0],b.basis[0])+dot(a.basis[1],b.basis[1])+dot(a.basis[2],b.basis[2]);
    return std::acos(std::clamp((trace-1)/2,-1.,1.));
}
// How fast a hand moved between two feeds (m/s, rad/s): an offset is only sampled while the hand is nearly still, because the weapon
// frame (game thread) and the hand pose (render thread) are not from the same instant.
struct Motion {double linear{},angular{};};
inline Motion motion(const Pose& a,const Pose& b,double seconds) noexcept {
    if(!(seconds>1e-4)) return {1e9,1e9};
    return {length(b.position-a.position)/seconds,angle(a,b)/seconds};
}
// The weapon's offset in its own hand's frame. Samples are blended; a sample that disagrees with the blend restarts it (a new item, or the
// draw clip still moving it). ready() after `needed` consecutive agreeing samples: from then on the offset is held.
struct OffsetFilter {
    Pose value{};bool have{};unsigned stable{};
    static constexpr double blend_t=.2,agree_m=.02,agree_rad=.17;
    void reset() noexcept {*this={};}
    bool add(const Pose& sample) noexcept {
        if(!valid(sample)) return false; // NaN or a half-written frame never enters the average
        if(!have || length(sample.position-value.position)>agree_m || angle(sample,value)>agree_rad) {value=sample;have=true;stable=1;return true;}
        value=blend(value,sample,blend_t);++stable;return true;
    }
    bool ready(unsigned needed=12) const noexcept {return have && stable>=needed && valid(value);}
};
// Where the copy goes: the other hand's drawn pose composed with the mirrored offset. Empty when anything is not finite.
inline bool target(const Pose& other_hand,const Pose& offset,Pose& out) noexcept {
    if(!valid(other_hand) || !valid(offset)) return false;
    out=to_world(other_hand,mirror(offset));
    return pose_finite(out);
}

// ---- Held copies of mod-owned items and gun copies thrown into the world (docs/dual-wield-two-hands-plan.md, row A) ----
// What a def is, by its name (guard defs included: elite_gun.def, the guard sword .def), case-insensitive.
enum class Kind : unsigned {other,pistol,crossbow,sword};
inline bool name_has(std::string_view name,std::string_view part) noexcept {
    if(part.size()>name.size()) return false;
    for(std::size_t i=0;i+part.size()<=name.size();++i) {
        std::size_t k=0;
        while(k<part.size() && std::tolower(static_cast<unsigned char>(name[i+k]))==part[k]) ++k;
        if(k==part.size()) return true;
    }
    return false;
}
inline Kind kind_of(std::string_view def) noexcept {
    if(name_has(def,"crossbow")) return Kind::crossbow;
    if(name_has(def,"sword") || name_has(def,"blade") || name_has(def,"cutlass")) return Kind::sword;
    if(name_has(def,"gun") || name_has(def,"pistol")) return Kind::pistol;
    return Kind::other;
}
// A weapon's frame in its hand's frame when nothing was measured. The hand frame is WorldTrackedPose (forward, left, up) and the blade of a sword
// lies along its forward axis (sword_blade_axis); a gun's barrel is its model's row 0 (the barrel aim reads model+0x186c), so every kind sits with
// its forward axis along the hand's, at the palm. ponytail: identity rotation and zero position for every kind. The authored hold is measured live
// from the same def when it is natively equipped (NativeWeaponHands::service_measure); this is only the fallback for defs with no native twin
// (guard weapons, a second pistol before the first measurement). Tune in the headset: grip_constant is the single place.
// v767 (sim on the user's save): the identity put a copy's barrel along the fist tube, muzzle up (headset v761/v762 "muzzle up" for copies). Pistol and
// crossbow now carry the native left-hand holds measured live (status dual_wield.guns.held.kinds); `side` = the hand they were measured on (0 = left).
struct GripConstant {Pose offset;unsigned side;};
inline GripConstant grip_constant(Kind k) noexcept {
    switch(k) { // measured v767 (left hand, chapter-4 save): the barrel ~52 degrees below the hand's forward, the grip in the palm
    case Kind::pistol: return {{{.0298,-.0370,-.0245},{Vec3{.6134,.0849,-.7852},Vec3{.0105,.9932,.1156},Vec3{.7897,-.0792,.6084}}},0};
    case Kind::crossbow: return {{{.0324,-.0379,-.0243},{Vec3{.5909,.0170,-.8065},Vec3{-.0587,.9980,-.0220},Vec3{.8046,.0603,.5908}}},0};
    default: return {{{0,0,0},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}},0};
    }
}
// v773: a measured hold of a gun is plausible when its barrel (row 0) lies within 45 degrees of the kind's constant (mirrored for the other
// side). Other kinds have no constant to compare with: always plausible.
inline bool plausible_grip(Kind k,const Pose& measured,unsigned side) noexcept {
    if(k!=Kind::pistol && k!=Kind::crossbow) return true;
    const auto c=grip_constant(k);const auto want=side==c.side?c.offset:mirror(c.offset);
    const auto a=measured.basis[0],b=want.basis[0];const double la=length(a),lb=length(b);
    return la>1e-6 && lb>1e-6 && dot(a,b)/(la*lb)>=std::cos(45*3.14159265358979323846/180);
}
// Where a copy goes: the hand's drawn pose composed with the item's grip offset. `native_side` = the side the offset was measured on; if the copy
// is shown at the other hand the offset is mirrored across the hand's left axis (same as target()). Empty when anything is not finite.
inline bool place(const Pose& hand,const Pose& offset,bool mirrored,Pose& out) noexcept {
    if(!valid(hand) || !valid(offset)) return false;
    out=to_world(hand,mirrored?mirror(offset):offset);
    return pose_finite(out);
}
// v776 (user, headset v775: "the weapon lags behind when I start running, especially with dual wield"): a copy is teleported at Stage0, in the game
// frame's think window, BEFORE the frame's physics step moves the player: the body frame (model+0x1860) is written by the post-physics attach pass
// (0x755030 -> 0x753c10), which carries the arms with it. The root a copy reads at Stage0 is the previous frame's and the copy is drawn one frame of
// the player's motion behind the arm (v*dt: 10 cm at 6 m/s and 60 fps, 6.7 cm at 90 fps; v771 carried the copy to the root it read, not to where that
// root is about to be). The step to expect is the last one: the root's displacement between two consecutive services, while those are one frame apart
// (a pause, a hitch or a load gives none). Exact at a steady speed; a*dt^2 off while accelerating (3 mm at 23 m/s^2 and 90 fps, lab: weapon_follow_lab_check).
// v779 (user, headset v778: "the weapon chosen second still jitters when running"; the headset ran 70 frames/s whose frame-to-frame time changed
// by 1.3 ms median, 5.7 ms p99: the last step alone missed by 1-3 cm every frame at a run): the last displacement is scaled by THIS step's game dt
// over the dt of the step that made it (the engine's GAME clock dt read at those services; 0 or out of range = unknown, unscaled). Exact at a
// steady speed whatever the frame pacing; a*dt^2 while accelerating.
// v816 (user, headset v815: "the doubling of the guns while I move forward"; pace p50 18 ms, p95 45 ms): a service gap over 50 ms dropped the step, so on every
// long frame the copy sat a whole frame of the player's motion behind the hand. The game clock dt (known below .1 s) already says one sim step: the wall gap may be .1 s.
inline Vec3 root_step(const Vec3& previous,const Vec3& now,double seconds,double dt_last=0,double dt_now=0,double max_gap_s=.1,double max_m=.5) noexcept {
    const auto d=now-previous;
    if(!(seconds>1e-4 && seconds<=max_gap_s && finite(d) && length(d)<=max_m)) return {};
    const auto known=[](double t) {return t>1e-4 && t<.1;};
    const auto step=known(dt_last) && known(dt_now)?d*(dt_now/dt_last):d;
    return length(step)<=max_m?step:Vec3{};
}
// v816 (user, headset v815: "when I turn, the hands are carried over after the turn"): a copy was carried by the root's translation only, so every stick
// turn left it one frame of the turn behind the hand (the arms are carried by the whole root, FullBodyDriver::anchored). The turn to expect is the last one,
// as root_step: `previous`/`now` = the heading the camera is placed with (root yaw minus NativeTurning's head-follow, which BodyAnchor takes off), radians.
// A smooth turn only: a step faster than max_rate or bigger than max_rad is a snap (or a teleport) and is not repeated (a snap at Stage0 is not known yet:
// the copy lags that one frame). ponytail: rate/size heuristic, ask NativeTurning for the mode if a smooth turn ever exceeds it.
inline double yaw_step(double previous,double now,double seconds,double dt_last=0,double dt_now=0,double max_gap_s=.1,double max_rate=6,double max_rad=.3) noexcept {
    const double d=std::remainder(now-previous,6.283185307179586);
    const auto known=[](double t) {return t>1e-4 && t<.1;};
    if(!(seconds>1e-4 && seconds<=max_gap_s && std::isfinite(d) && std::abs(d)<=max_rad && std::abs(d)<=max_rate*(known(dt_last)?dt_last:seconds))) return 0;
    return known(dt_last) && known(dt_now)?d*(dt_now/dt_last):d;
}
// The game's yaw of a frame (twist about +Z of the rotation whose columns are the frame's forward and left rows), whatever its axis convention.
inline double frame_yaw(const Pose& f) noexcept {return std::atan2(f.basis[0].y-f.basis[1].x,f.basis[0].x+f.basis[1].y);}
// `p` moved as the player is: turned by `yaw` about +Z around `pivot` (the root origin) and moved by `move`, the last step, which turns with the
// player too (the stick walks in the player's frame: unturned, a 45 ms step at 3 m/s and 90 deg/s missed by 9.5 mm, lab held_copy_turn_check).
inline Pose carry(const Pose& p,const Vec3& pivot,const Vec3& move,double yaw) noexcept {
    const double c=std::cos(yaw),s=std::sin(yaw);const auto r=[&](Vec3 v) {return Vec3{c*v.x-s*v.y,s*v.x+c*v.y,v.z};};
    return {pivot+r(move)+r(p.position-pivot),{r(p.basis[0]),r(p.basis[1]),r(p.basis[2])}};
}
// A dropped copy lying in the world goes back to the hip after this long, or when the player walked this far from it (as the sword's recall).
// ponytail: 30 s / 12 m, tune in the headset.
inline constexpr double lying_return_s=30,lying_return_m=12;
inline bool should_return(double age_s,double distance_m) noexcept {return age_s>lying_return_s || distance_m>lying_return_m;}
// The hand's reach for a pickup: distance from the hand to the lying copy's origin.
inline bool in_reach(Vec3 hand,Vec3 gun,double radius=.45) noexcept {return finite(hand) && finite(gun) && length(gun-hand)<=radius;}
// The index of the present copy nearest the hand (within max_m), or -1.
inline int nearest(const Vec3* pos,const bool* present,std::size_t n,Vec3 hand,double max_m=1e9) noexcept {
    int best=-1;double best_d=max_m;
    for(std::size_t i=0;i<n;++i) if(present[i] && finite(pos[i]) && finite(hand)) {const double d=length(pos[i]-hand);if(d<=best_d) {best_d=d;best=static_cast<int>(i);}}
    return best;
}
} // namespace d2vr::held_copy
