#pragma once
// Solid hands (user 2026-10-14: "world not very reactive to hands ... move things on the table with hands"): an open or relaxed
// empty hand pushes loose dynamic props the way a kinematic body would, at any speed, from sphere proxies on the palm and every
// finger. Pure math; native_hand_contact.hpp reads the bodies and writes the velocities on the physics thread.
// Software contact model, not a hknp body: the engine's velocity setter (0xc85dc0) is only proven on bodies the game already
// made (dynamic, flags & 2), and creating a keyframed hknp body is untested. The prop's linear velocity is raised along the push
// direction to the hand's, like a kinematic body hitting it; it is never lowered, so friction and the solver still own the rest.
// Stability on a table: no downward push (the table holds the prop up), spin untouched by the push direction, a hand speed and a
// depth term that are both capped, heavy bodies refused. Game metres, Z up.
#include "finger_ik.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
struct ContactSphere {Vec3 centre{};double radius{};};
inline constexpr std::size_t hand_palm_spheres=4,hand_proxy_count=hand_palm_spheres+5*4;
using HandProxies=std::array<ContactSphere,hand_proxy_count>;
struct PalmSphere {double across{},normal{},wrist{},radius{};};
inline constexpr std::array<PalmSphere,hand_palm_spheres> hand_palm{{{-.0144,-.0009,-.0143,.0254},
    {.0360,-.0008,-.0044,.0306},{-.0112,.0074,.0191,.0287},{.0195,-.0176,-.0741,.0209}}};
inline constexpr double hand_knuckle_add=.0119,hand_joint_add=.0043,hand_thumb_joint_add=.0077,hand_thumb_base_radius=.0266,hand_tip_pull=1.2;

struct HandContactConfig {
    double strength_kg=2.;     // a hand moves a prop of this mass at full speed; heavier ones proportionally less
    double heavy_kg=12.;       // a hand cannot push this much (a barrel, a crate)
    double max_speed=4.;       // m/s of the prop's driven speed for ordinary contact...
    double swat_hand_speed=1.2;// ...unless the hand itself is faster than this: a swat may reach swat_speed
    double swat_speed=6.;
    double still_speed=.08;    // m/s: a hand slower than this is resting, not pushing
    double approach_cos=.2;    // a moving hand pushes only through proxies it is moving into (cosine of the angle to its heading)
    double shallow=.03;        // m: a resting hand eases out of overlaps this shallow; a deeper one is a finger inside the prop, left alone
    // Gap control: a moving hand holds the prop at its leading edge. The prop is driven at the hand's horizontal velocity plus
    // depth_gain * (how far the hand has moved into it along its heading, beyond the deadband), plus the friction the prop
    // loses between writes (PropDrag), the two together at most depth_speed. A resting hand eases out of an overlap the same way.
    double depth_deadband=.003,depth_gain=10.,depth_speed=.3; // (the friction part is PropDrag's, the trim is learned from the gap)
    double engage_slack=.03;   // m: a prop the hand pushed within engage_ms is still pushed this far past the touch (sticky contact)
    std::uint64_t engage_ms=150;
    double lateral_damp=.5;    // share of the prop's horizontal speed across the hand's motion removed per write while it is driven
    // Uprighting: a driven prop whose near-vertical axis tilts is turned back with this angular velocity per radian of tilt, so
    // the base friction's tipping torque (a bottle pushed at its middle) cannot topple it; not when the hand pushes above its centre.
    // Above the prop's centre by upright_above the spinning fades out over upright_fade (a push at the neck may topple it); a spin
    // shorter than upright_min rad/s is not worth a write of its own.
    double upright_gain=40.,upright_max=3.,upright_cos=.85,upright_above=.04,upright_fade=.06,upright_min=.4;
    // A fresh contact is a ride, not a jump: the gap spring grows with the contact's age (ramp m/s per second) and, for ramp_time seconds,
    // one write adds at most write_step (plus the friction given back) to the prop's speed. A swat is exempt.
    double ramp=1.5,ramp_time=.2,write_step=.08;
    double min_gain=.02;       // m/s: a smaller change is not written (it would not wake a sleeping body either)
    // The world cast holds the drawn grip palm_radius off what its chest-to-hand sweep meets, a prop included, and the sweep can cross
    // the prop away from the hand (sim v525: 4-12 cm back, the hand drawn short of the bottle it pushes). The tracked hand pushes: for a
    // prop that cast hit (ContactInput::held) the proxies are moved back to it by ContactInput::lag; a hand held back by a wall pushes
    // nothing behind it. A shift beyond lag_limit is not believed.
    double lag_limit=.25;
    // A drawn hand moved by something that is not a prop of ours (a wall's or a table edge's clamp: ContactInput::lag set, held false, lag_known false) is not where
    // the player's hand is, and its proxies can sit inside the prop: sim v509 s2b/s2c, one onset frame with the drawn proxies 6 cm in and a write of 0.5 m/s for a hand
    // at 0.15. Past lag_trust (1 cm) of horizontal offset (the table's lift is vertical and stays trusted) nothing is read from it that frame.
    double lag_trust=.01;
    // Mass-aware pushes (C5): one write raises the prop's speed along the push by at most push_force_n * dt / mass, dt the seconds since this prop's last
    // write (ContactInput::dt; 0 = not capped). 200 N (controller ruling, Task 6 R2): a forward push runs mostly along the forearm (Bonelab's forearm
    // drive is +-320-450 N; its lateral range 90-112 N, research Q4), and the brief's 120 N starting value sat on a knife-edge in the push lab (the 2.5 m/s
    // free swat's meet latch flipped between 90 and 160 N). It applies on top of strength_kg and heavy_kg above, so a push only gets weaker than without it.
    double push_force_n=200.;
    // A push never leaves the prop faster along the push than the hand moves along it by more than launch_margin (m/s, times the time factor; 0.3, controller
    // ruling Task 6 R3: 0.2 broke the lab's pinned-bottle wobble, and 0.3 equals depth_speed, so today it binds only where the goal exceeds the hand + the
    // spring, e.g. a lift's vertical part, and it holds if the spring is ever raised): the cap above
    // limits the acceleration of one write, this one the speed it ends at. The hand cannot throw by pushing (2 m/s push lab: 2.68 m/s written before it). A
    // ride's write also carries the friction it gives back (ContactInput friction + trim): the engine takes that off before the prop moves (a rough table's
    // ride, tests/hand_contact_check.cpp "ride mu 2", needs it, and a write into a wall must still grow past PropDrag::strong to be told from friction).
    double launch_margin=.3;
};
struct HandContact {
    bool touching{};     // a proxy overlaps the prop
    bool driving{};      // the hand moves the prop this tick (sticky contact is renewed); push says the prop was actually sped up
    bool push{};
    bool turn{};         // the uprighting spin is worth a write even if the velocity is not (write velocity unchanged + spin)
    bool has_gap{};      // gap and error were measured (a moving hand): PropDrag::steer uses them
    Vec3 normal{};       // from the prop toward the hand (the proxies that push)
    double depth{};      // deepest real penetration of those proxies (m)
    double gap{};        // how far the hand has moved into the prop along its heading: the worst proxy's overlap / the cosine to its normal (m)
    double error{};      // gap - deadband: positive = the prop lags the hand, negative = it runs ahead (m)
    Vec3 velocity{};     // the prop's velocity after the push (the input when push is false)
    Vec3 spin{};         // the angular velocity (world, rad/s) to write with it: the uprighting, zero otherwise
    bool capped{};       // the dv cap (push_force_n * dt / mass) bound this write
    Vec3 outward{};      // how the (tracked) hand has to move to leave the prop: along the deepest proxy's normal by its overlap, whichever proxy and way the hand moves;
                         // set even when nothing is pushed (a buried resting hand, a blocked prop). The drawn hand meets the prop by it (native_hand_contact.hpp meet)
};
// What the caller knows beyond the shapes.
struct ContactInput {
    bool engaged{};      // the prop was driven within engage_ms
    Vec3 lag{};          // the tracked hand minus the drawn hand: the world cast held the drawn one back by this much
    bool held{};         // ... and that cast hit this prop: the lag is the tracked hand's reach into it (HandContactConfig::lag_limit)
    bool lag_known{};    // the lag is some other prop's (the cast held the hand at it, or the hand meets it): trusted as the drawn hand's offset, not a stranger's clamp
    double friction{};   // m/s to add: what the prop loses to friction in one physics step (PropDrag::compensation)
    double trim{};       // m/s to add or take off, learned from the gap (PropDrag::trim): what the friction estimate got wrong
    double age{};        // seconds since the prop's contact began (0 for a fresh one)
    bool blocked{};      // the prop has not followed its last writes (PropDrag::blocked): the hand is pressing it into something
    bool stalled{};      // its last three writes moved nothing (PropDrag::stalled): a resting hand's ease cannot overcome the friction
    std::array<Vec3,3> basis{{{1,0,0},{0,1,0},{0,0,1}}}; // the prop's axes in the world
    double dt{};         // seconds since this prop's last write (NativeHandContact::write_dt: clamped to [1/240, 1/30], 1/90 for a first write); 0: not capped (unit tests)
    double time_factor=1.; // the engine's time scale: multiplies the push, its launch cap and its dv cap. Not read from the engine in phase 1 (C13): always 1.
                           // Above 1 it counts as 1 (a push never gets stronger than today, deviation 10); not finite or not positive: no write
    // A rigid body (a weapon the player holds) is not a soft hand: its contact drives at the weapon's own speed from the first frame, with no
    // ramp (the ride the hands get, ramp m/s per second and one write's write_step) and no stalled ease. Without this, a weapon moved at
    // aiming or walking speed writes at most write_step (8 cm/s) a frame and a bottle only twitches - the v1026-v1030 gun report ("does not
    // collide at all"), while a sword swung at 3-5 m/s passed the swat threshold and shoved properly.
    bool rigid{};
};

// The prop's deceleration while the hand drives it (table friction), from the velocity a write left it with and the velocity found at
// the next service. The engine applies friction before it integrates a step, so a prop written at the hand's speed moves slower by
// deceleration x step: the write adds compensation() back. A write that friction ate whole counts as all of it (the next is stronger);
// one stronger than `strong` that still moved nothing means a wall or another prop: blocked().
class PropDrag {
    Vec3 written_{};double wrote_at_{},dv_{},dt_{},blocked_at_{},stalled_at_{},trim_{},steered_at_{};bool pending_{},ever_{};unsigned blocked_{},stalled_{};
public:
    static constexpr double step=1./60,limit=.4,window=.9,strong=.45,trim_gain=30.,trim_min=-.12,trim_max=.3,trim_zone=.02;
    void wrote(Vec3 velocity,double seconds) {
        if(seconds-wrote_at_>.5) {dv_=dt_=0;trim_=0;}
        written_=velocity;wrote_at_=seconds;pending_=length(velocity)>.03;ever_=true;
    }
    // Seconds since the last write (ContactInput::dt); 0 before the first one.
    double since_write(double seconds) const {return ever_?seconds-wrote_at_:0.;}
    void observe(Vec3 velocity,double seconds) {
        if(!pending_) return;
        pending_=false;
        const double dt=seconds-wrote_at_;if(!(dt>.001) || dt>.1) return;
        const double along=dot(written_,unit(written_)),now=dot(velocity,unit(written_));
        const bool stepped=std::abs(now-along)>1e-5; // false: no physics step since the write, nothing to learn about a stall
        if(stepped) {if(now<along*.3) {++stalled_;stalled_at_=seconds;} else stalled_=0;}
        if(stepped && now<along*.3 && along>=strong) {++blocked_;blocked_at_=seconds;trim_=0;return;} // a strong write did not move it: blocked, not friction
        if(stepped) blocked_=0;
        dv_=dv_*window+std::max(0.,along-std::max(now,0.));dt_=dt_*window+dt; // a weak write friction ate entirely counts in full: the next one is stronger
    }
    // The prop did not follow its last two writes (a wall, another prop): stop writing for 0.3 s, then try once more.
    bool blocked(double seconds) const {return blocked_>=2 && seconds-blocked_at_<.3;}
    // Three writes in a row moved nothing (a resting hand's gentle ease against static friction): stop writing for 0.25 s.
    bool stalled(double seconds) const {return stalled_>=3 && seconds-stalled_at_<.25;}
    // The trim: what the friction estimate (and an engine that takes only part of a step's friction before it moves the prop) gets wrong.
    // While the hand drives the prop, the gap error integrates: a prop that runs ahead (negative error) has the give-back reduced, one that
    // lags has it raised. Reset when the prop stops being driven or is blocked.
    void steer(const HandContact& r,double seconds) {
        if(!(r.driving && r.has_gap)) {steered_at_=0;return;}
        const double dt=seconds-steered_at_;
        const bool first=steered_at_==0;steered_at_=seconds;
        if(first || !(dt>0 && dt<.05)) return;
        if(r.error>trim_zone) return;                 // a deep overlap (a hand that started inside the prop) is the spring's, not a lag to integrate
        trim_=std::clamp(trim_+trim_gain*std::max(r.error,-trim_zone)*dt,trim_min,trim_max);
    }
    double trim() const {return trim_;}
    double decel() const {return dt_>0?std::min(dv_/dt_,20.):0.;}
    double compensation() const {return std::min(decel()*step,limit);}
};

// Palm-frame proxies, posed by the per-finger curl share (finger_ik's convention: share of finger_full_degrees, joint by joint).
struct PalmBasis {Vec3 across{},normal{},wrist{};};
inline PalmBasis palm_basis(const rig::HandGeometry& hand) {
    const auto normal=hand.palm_in;
    const auto knuckles=hand.finger[1].pivot-hand.finger[4].pivot;
    const auto across=unit(knuckles-normal*dot(knuckles,normal));
    const auto middle=(hand.finger[1].pivot+hand.finger[2].pivot+hand.finger[3].pivot+hand.finger[4].pivot)*.25;
    const auto along=middle-normal*dot(middle,normal)-across*dot(middle,across);
    return {across,normal,unit(along)*-1.};
}
struct HandJointTurns {
    std::array<std::array<double,3>,5> joints{};
    explicit HandJointTurns(std::array<std::array<double,3>,5> value={}):joints(value) {}
    const std::array<double,3>& operator[](unsigned finger) const {return joints[finger];}
    std::array<double,3>& operator[](unsigned finger) {return joints[finger];}
};
inline HandProxies hand_proxies(const rig::HandGeometry& hand,const WorldTrackedPose& palm,const HandJointTurns& turns) {
    const auto world=[&](Vec3 p) {return palm.position+palm.basis[0]*p.x+palm.basis[1]*p.y+palm.basis[2]*p.z;};
    HandProxies out{};std::size_t n=0;
    const auto axes=palm_basis(hand);
    for(const auto& sphere:hand_palm) out[n++]={world(axes.across*sphere.across+axes.normal*sphere.normal+axes.wrist*sphere.wrist),sphere.radius};
    for(unsigned f=0;f<5;++f) {
        const auto& g=hand.finger[f];
        const double r=hand.thickness[f]+(f?hand_joint_add:hand_thumb_joint_add);
        const auto points=rig::finger_points(g,turns[f]);
        out[n++]={world(points[0]),f?hand.thickness[f]+hand_knuckle_add:hand_thumb_base_radius};
        for(unsigned segment=1;segment<points.size();++segment) {
            const auto centre=segment==3?points[segment]-unit(points[3]-points[2])*(r*hand_tip_pull):points[segment];
            out[n++]={world(centre),r};
        }
    }
    return out;
}
inline HandProxies hand_proxies(const rig::HandGeometry& hand,const WorldTrackedPose& palm,const std::array<float,5>& curl) {
    HandJointTurns turns{};
    for(unsigned finger=0;finger<5;++finger) {
        const double value=std::isfinite(curl[finger])?std::clamp<double>(curl[finger],0.,1.):0.;
        turns[finger]=rig::finger_turn_at(hand.finger[finger],finger,value);
    }
    return hand_proxies(hand,palm,turns);
}

// World velocity of a tracked point from timestamped samples (seconds): ~25 ms smoothing (70 ms below 0.4 m/s, where a slow hand's
// stepwise samples would otherwise beat), restarts on a gap or a jump (snap turn,
// teleport, tracking loss: more than 12 m/s or 0.35 m between samples). Two samples closer than 8 ms are one: the baseline stays, so the next one spans both.
class HandMotion {
    Vec3 last_{},velocity_{};double time_{};bool valid_{};unsigned quiet_{},surge_{};
public:
    static constexpr double tau=.025,tau_slow=.07,tau_fast=.008,gap=.15,jump=.35,limit=12.,merge=.008;
    void reset() {valid_=false;velocity_={};quiet_=surge_=0;}
    Vec3 velocity() const {return valid_?velocity_:Vec3{};}
    void sample(Vec3 position,double seconds) {
        if(!finite(position) || !std::isfinite(seconds)) {reset();return;}
        if(!valid_ || seconds<time_ || seconds-time_>gap || length(position-last_)>jump) {last_=position;time_=seconds;valid_=true;velocity_={};quiet_=surge_=0;return;}
        const double dt=seconds-time_;if(dt<merge) return; // closer samples are one: a 4 ms gap would turn a stepped hand's 0.4 cm step into 1 m/s
        // Faster than any hand moves: a snap turn or teleport carried the hand, not an arm. No velocity, no swat.
        if(length(position-last_)>limit*dt) {last_=position;time_=seconds;velocity_={};quiet_=surge_=0;return;}
        const Vec3 v=(position-last_)*(1/dt);
        const double now=length(v),before=length(velocity_);
        quiet_=now<.03?quiet_+1:0;surge_=now>2*before+.3?surge_+1:0;
        // A hand clearly speeding up for two samples in a row is followed at once (a swat's first contact would read half its speed;
        // one spike is a stepped hand's step, not a swat), one that has been still for 4 samples (44 ms) lets go at once; otherwise the
        // slow smoothing keeps a stepwise hand from beating.
        const double constant=surge_>=2 || (quiet_>=4 && before>.05)?tau_fast:before<.4?tau_slow:tau;
        const double w=1-std::exp(-dt/constant);
        velocity_=velocity_*(1-w)+v*w;last_=position;time_=seconds;
    }
};

// Surface normal of the shape's distance field at p (points out of the shape); zero where it is undefined (a capsule's axis).
inline Vec3 shape_normal(const rig::HeldShape& shape,Vec3 p) {
    constexpr double h=.002;
    const Vec3 g{rig::shape_distance(shape,p+Vec3{h,0,0})-rig::shape_distance(shape,p-Vec3{h,0,0}),
                 rig::shape_distance(shape,p+Vec3{0,h,0})-rig::shape_distance(shape,p-Vec3{0,h,0}),
                 rig::shape_distance(shape,p+Vec3{0,0,h})-rig::shape_distance(shape,p-Vec3{0,0,h})};
    const double n=length(g);
    return n>1e-9?g*(1/n):Vec3{};
}
// What a prop is, for a hand's touch: the cylinder/box that explains its drawn bounds (finger_ik classify_held), its capsule
// shortened so the rounded ends do not overhang the cylinder it was fitted to.
inline rig::HeldShape prop_shape(const NativeRigidBody& body) {
    const WorldTrackedPose none{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    auto shape=rig::classify_held(body.pose,body.bounds,-1,none);
    if(shape.half.x>0 && shape.radius>0) shape.half.x=std::max(0.,shape.half.x-shape.radius);
    return shape;
}
// Havok's hkHalf is the upper 16 bits of a float. hknpMotion +0x26 is taken to be m_inverseInertia[3], the inverse mass (UNVERIFIED
// live: static layout only); a prop's mass is that if it is within a factor 12 of what its volume suggests (350 kg/m3), else the
// volume's, so a wrong layout cannot make every prop weigh nothing or a ton.
inline double prop_mass(std::uint16_t inverse_mass_half,const NativeGrabBounds& bounds) {
    const auto e=bounds.maximum-bounds.minimum;
    const double estimate=std::clamp(std::max(e.x,.02)*std::max(e.y,.02)*std::max(e.z,.02)*350.,.05,2000.);
    const std::uint32_t bits=static_cast<std::uint32_t>(inverse_mass_half)<<16;float inverse;std::memcpy(&inverse,&bits,4);
    if(std::isfinite(inverse) && inverse>1e-4f) {const double m=1./inverse;if(m>=estimate/12 && m<=estimate*12) return m;}
    return estimate;
}

// One hand against one prop. hand_velocity: the tracked hand's world velocity; body_velocity: the prop's current one.
// A hand that moves across a prop carries it along: the prop is driven to the hand's own horizontal velocity, collinear with the hand's
// motion (it never squirts off the contact normal), plus the gap control above, so it stays at the hand's leading edge. A prop on a
// table is never pushed down or up unless the hand is under it; a prop touched within engage_ms (in.engaged) is still driven while it is
// within engage_slack of the hand, so contact survives pose jitter. Only the speed along the drive is raised, never lowered.
// Which proxies drive: a moving hand drives through the proxies it moves into (a fingertip that the grip-only world cast left beyond the
// prop's far side is behind its surface, not pushing it back); a resting hand eases out of shallow overlaps only.
inline HandContact hand_contact(const HandProxies& proxies,const rig::HeldShape& shape,Vec3 hand_velocity,Vec3 body_velocity,double mass,
        const HandContactConfig& cfg={},const ContactInput& in={}) {
    HandContact out{};out.velocity=body_velocity;
    if(!finite(hand_velocity) || !finite(body_velocity)) return out;
    if(!std::isfinite(in.time_factor) || !(in.time_factor>0) || !std::isfinite(in.dt) || !(in.dt>=0)) return out; // a bad time input writes nothing
    if(!in.held && !in.lag_known && length(Vec3{in.lag.x,in.lag.y,0})>cfg.lag_trust) return out; // moved by something else: its proxies say nothing about this prop
    const double speed=length(hand_velocity);const bool moving=speed>=cfg.still_speed;
    const Vec3 heading=moving?hand_velocity*(1/speed):Vec3{};
    const Vec3 flat{hand_velocity.x,hand_velocity.y,0};const double flat_speed=length(flat);
    const Vec3 across=flat_speed>=cfg.still_speed?flat*(1/flat_speed):Vec3{}; // the hand's horizontal heading
    const double slack=in.engaged?cfg.engage_slack:0.;
    const double lag_size=length(in.lag);
    const Vec3 offset=in.held && lag_size<=cfg.lag_limit?in.lag:Vec3{}; // where the player's hand is, when the cast held the drawn one at this prop
    const auto support=[&](Vec3 d) { // how far the shape reaches from its centre along d
        return std::abs(shape.half.x*dot(shape.frame.basis[0],d))+std::abs(shape.half.y*dot(shape.frame.basis[1],d))+std::abs(shape.half.z*dot(shape.frame.basis[2],d))+shape.radius;};
    Vec3 sum{};double depth=0,gap=-1e9,weight=0,hit_z=0,deepest=0;bool pushing=false,buried=false;Vec3 deepest_n{};
    const double reach=length(shape.half)+shape.radius+slack; // a proxy touching the shape is within reach + its radius of the shape's centre
    for(const auto& s:proxies) {
        const Vec3 at=s.centre+offset;
        const auto from=at-shape.frame.position;
        if(dot(from,from)>(reach+s.radius)*(reach+s.radius)) continue;
        const double d=rig::shape_distance(shape,at);
        const double pen=s.radius-d;
        if(!(pen+slack>0)) continue;
        auto n=shape_normal(shape,at);
        if(length(n)<.5) {n=at-shape.frame.position;if(length(n)<1e-6) continue;n=unit(n);}
        if(pen>0) out.touching=true;
        if(pen>deepest) {deepest=pen;deepest_n=n;}
        if(!moving && pen>cfg.shallow) buried=true; // a resting hand through the prop (fingers inside, palm beside): its shallow overlaps are not a push direction
        if(moving?dot(n,heading)>-cfg.approach_cos:pen>cfg.shallow) continue;
        const double w=pen+slack;
        sum=sum+n*w;depth=std::max(depth,pen);pushing=true;weight+=w;hit_z+=w*at.z;
        gap=std::max(gap,pen/std::max(-dot(n,across),.35)); // the overlap measured along the hand's heading, not along the surface normal
    }
    out.outward=deepest_n*deepest;
    if(!pushing || buried || length(sum)<1e-9 || !(mass>0) || mass>=cfg.heavy_kg) return out;
    out.normal=unit(sum);out.depth=std::max(depth,0.);hit_z/=weight;
    out.driving=moving || out.depth>cfg.depth_deadband;
    // Uprighting: the base friction tips a prop pushed at its middle; turn its most vertical axis back toward the world's. Computed first:
    // every path that keeps the contact carries it, and it is written even when the velocity needs no change.
    if(cfg.upright_gain>0) {
        unsigned best=0;for(unsigned i=1;i<3;++i) if(std::abs(in.basis[i].z)>std::abs(in.basis[best].z)) best=i;
        const double fade=1-std::clamp((hit_z-(shape.frame.position.z+cfg.upright_above))/cfg.upright_fade,0.,1.);
        if(std::abs(in.basis[best].z)>=cfg.upright_cos && fade>0) {
            const Vec3 up=in.basis[best]*(in.basis[best].z<0?-1.:1.);
            out.spin=cross(up,Vec3{0,0,1})*(cfg.upright_gain*fade);
            const double turn=length(out.spin);if(turn>cfg.upright_max) out.spin=out.spin*(cfg.upright_max/turn);
            out.turn=(out.driving || in.engaged) && length(out.spin)>cfg.upright_min; // also while the prop coasts on after the hand has stopped pushing (engage_ms)
        }
    }
    if(in.blocked) {out.turn=false;return out;} // pinned: keep the contact (sticky), write nothing
    Vec3 away=out.normal*-1; // into the prop
    // Horizontal, unless the hand is under the prop (below its lowest point): a prop on a table is held up by it.
    const double base_z=shape.frame.position.z-support({0,0,1});
    const bool lifting=away.z>=.5 && proxies[0].centre.z+offset.z<base_z+.03;
    if(!lifting) {away.z=0;const double l=length(away);away=l<.25?Vec3{}:away*(1/l);}
    const double gain=in.rigid?1.:std::min(1.,cfg.strength_kg/mass); // a rigid weapon shoves what its force cap allows, not what a soft hand does
    const bool swat=speed>cfg.swat_hand_speed || in.rigid; // a rigid body (a held weapon) strikes at any speed: ContactInput::rigid
    Vec3 goal{};
    if(length(across)>0) { // moving across: ride the hand's leading edge
        out.gap=gap;out.has_gap=true;
        out.error=gap-cfg.depth_deadband;
        const double trim=swat?0.:in.trim,room=std::min(cfg.depth_speed,cfg.ramp*in.age); // what a fresh contact may add on top of the friction
        const double extra=std::max(std::min(std::max(0.,out.error)*cfg.depth_gain+std::max(trim,0.),room)+std::min(trim,0.)+(swat?0.:in.friction),-.1);
        goal=across*std::max(0.,flat_speed+extra);
        if(lifting) goal.z=std::max(hand_velocity.z,0.);
    } else { // resting, or moving straight up or down: ease out of the overlap
        if(in.stalled) return out;
        const double ease=out.depth>cfg.depth_deadband?std::min((out.depth-cfg.depth_deadband)*cfg.depth_gain,cfg.depth_speed):0.;
        goal=away*ease;
        if(lifting && moving) goal.z+=std::max(hand_velocity.z,0.);
    }
    const double time_factor=std::min(in.time_factor,1.); // [0, 1]: slow motion weakens a push, nothing strengthens it
    goal=goal*gain*time_factor;
    const double cap=swat?cfg.swat_speed:cfg.max_speed,size=length(goal);
    if(size<1e-9) return out;
    if(size>cap) goal=goal*(cap/size);
    const Vec3 direction=unit(goal);
    double target=length(goal);const double current=dot(body_velocity,direction);
    // A fresh contact adds a little per write: the first one is a ride, not a shove (what the friction costs is given on top).
    if(!swat && in.age<cfg.ramp_time) target=std::min(target,std::max(current,0.)+cfg.write_step+std::max(0.,in.friction+in.trim));
    // Never faster than the hand moves along the push + launch_margin, once the engine has taken the step's friction off (what a ride's write gives back
    // for it, as the ramp above). In game time.
    target=std::min(target,(std::max(0.,dot(hand_velocity,direction))+cfg.launch_margin+(swat?0.:std::max(0.,in.friction+in.trim)))*time_factor);
    if(in.dt>0) { // mass-aware: one write adds at most F dt / m, dt in game time (the time factor's share of the real seconds)
        const double cap_target=std::max(current,0.)+cfg.push_force_n*time_factor*in.dt/mass;
        if(target>cap_target) {target=cap_target;out.capped=true;}
    }
    if(target-current<cfg.min_gain) {out.capped=false;return out;} // nothing written: the cap bound no write
    out.velocity=body_velocity+direction*(target-current);out.push=true;
    if(length(across)>0 && cfg.lateral_damp>0) { // what the prop does across the hand's motion follows the hand
        const Vec3 side=out.velocity-across*dot(out.velocity,across);Vec3 flat_side{side.x,side.y,0};
        out.velocity=out.velocity-flat_side*cfg.lateral_damp;
    }
    return out;
}
// Callers that predate ContactInput.
inline HandContact hand_contact(const HandProxies& proxies,const rig::HeldShape& shape,Vec3 hand_velocity,Vec3 body_velocity,double mass,
        const HandContactConfig& cfg,bool engaged) {
    ContactInput in;in.engaged=engaged;return hand_contact(proxies,shape,hand_velocity,body_velocity,mass,cfg,in);
}
} // namespace d2vr
