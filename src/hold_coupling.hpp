#pragma once
// Plan B task B16: held things follow the hand body (Bonelab §1.2: the held object is a dynamic body hanging on the hand's joint, so it is
// pulled behind the hand; §2.2: the hand lags when the world stops it). Pure: no engine access; the render thread is the only caller.
//
// * Held things follow the DRAWN hand (fix round 1 ruling): the body only as far as B15's HandVisualSource draws it (body_weight: 1 while the
//   body is the settled source, i.e. in contact within 0.10/0.12 m of the clamp pose or agreeing within 0.02 m; ramped over B15's 0.15 s blend;
//   0 in free air). In free air every hold is exactly B4's / plan A's; a free-air throw keeps the controller's velocity.
// * Motor holds (a prop or a corpse limb driven by PhysicalGrab): the pose the grab pulls the object toward is the controller moved toward the
//   hand body by body_weight (the body's position, the controller's orientation: a sphere has none), so the held body trails a hand the world
//   stopped.
// * Release (final review P2-1 ruling, amending fix rounds 2-3): a coupled hold is let go only when the HELD OBJECT is stuck, never on the
//   hand body's own gap: a hand snagged on a crate or a guard's arm while the object moves freely must not drop it (the native item does not
//   even collide with the hand). A native hold: the object's request pushed out by the HeldClamp more than 0.35 m for 0.25 s while that gap is
//   not closing faster than 0.5 m/s (B9's stuck definition, on the object); this replaces B4's own drop rule while the body is in play. A motor
//   hold (a prop or a corpse limb): PhysicalGrab's own rule, which judges the held body against the (coupled) target it is pulled to (corpse
//   0.5 m for 0.25 s, prop 1.5 m), so no second release is made here. A stuck hand body whose object is free is B14's stuck snap's case (the
//   snap is allowed while a native hold's object is free: CouplingOutput::object_blocked).
// * Native holds (HoldObject keyframes the item toward the mod's target, spec deviation 4; the HeldClamp stays the collision mechanism): the
//   target is the request computed from the drawn grip (B15 already draws it at the body while it selects the body, blended), pushed out by the
//   HeldClamp's planes (HeldClamp::project): it never reads the body itself. The clamp
//   itself keeps sweeping and judging the controller's request (HeldClamp::update, B4's sweeps and planes; the drop is the stuck rule above): a
//   body-carried request resting against a wall would sweep nothing, its plane would lapse after 0.1 s and the body would cycle. The body's own
//   servo target is NOT the clamped object pose (the body would be fed its own pose plus the offset every frame: a runaway) and NOT the hand
//   pose of the clamped carried object (the body would be handed its own position: a stall). It is the clamped controller request expressed
//   as the hand pose that holds it (controller + clamped(controller request) - controller request): a function of the controller and the
//   world only, so there is no loop; in free air it is the controller palm exactly (B14's target), against a wall the hand pose whose object
//   just touches the wall, where the body, and then the object it carries, come to rest.
// * Fallback by construction: body_ready false (knob off, fault latch, creation failed, not yet created, a stale pose) returns today's pose
//   bit-exact and uses_body false; nothing else changes.
// * The hand never collides with what it holds, without the pair API (B16 ruling): a native hold's item sits on DEBRIS for the hold and the
//   hand's word omits DEBRIS (by construction); a motor-held body whose layer the hand mask includes (a corpse limb on NPC_BODY_RAGDOLL 14, a
//   prop on RIGID_BODY 3) makes HandFilterKeeper ask for the hand's word without that layer. The default word returns only through SafeRelease
//   (the released object no longer overlaps the hand body, or 0.5 s passed: counted unresolved), or with a new body (a destroy or a recreate
//   always starts from the R1 default word).
#include "dwell_timer.hpp"
#include "hand_body_commands.hpp"
#include "hand_servo.hpp"
#include "held_clamp.hpp"
#include "physical_grab.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace d2vr {

enum class HoldKind {motor_prop,corpse,native};
struct CouplingInput {HoldKind kind{};bool body_ready{};std::optional<WorldTrackedPose> body;WorldTrackedPose controller{};
    std::optional<WorldTrackedPose> clamped_native;double now{};
    // native only: the request re-based on the controller palm (HoldCoupling::controller_request) and HeldClamp::update's output for it
    std::optional<WorldTrackedPose> controller_request,clamped_controller;
    double body_weight{1.};};   // motor holds: B15's VisualOutput::body_weight (how much of the drawn hand is the body); 0: today's controller pose
// lag_m: the raw hand-body gap (status only, it releases nothing); object_gap_m: the held object's gap to its own target (native: the clamp's push of
// the controller's request); object_blocked: that gap is beyond the kind's threshold now (B14's snap waits for the release instead).
struct CouplingOutput {WorldTrackedPose target{};bool uses_body{};double lag_m{};bool release{};
    std::optional<Vec3> body_target;   // native, body in use: the hand body's servo target (none: B14's, the controller palm)
    double object_gap_m{};bool object_blocked{};};

class HoldCoupling {
public:
    static constexpr double lag_release_m=.5,lag_release_s=.25;   // plan A's corpse rule (spec 266/671, ruling B-26): PhysicalGrab's, on the held body
    static constexpr double prop_release_m=1.5;                    // props keep PhysicalGrab's 1.5 m rule (the held body to its target)
    static constexpr double max_dt=.05;                            // a hitch never counts as more dwell than this (Review Focus 4)
    static bool same_point(Vec3 a,Vec3 b) {return a.x==b.x && a.y==b.y && a.z==b.z;}
    // A usable body pose: ready and finite.
    static bool usable(bool body_ready,const std::optional<WorldTrackedPose>& body) {return body_ready && body && finite(body->position);}
    // Native: the same request moved from the grip to the controller palm (what B4 would request with the hand exactly at the controller).
    static WorldTrackedPose controller_request(const WorldTrackedPose& request,Vec3 grip,Vec3 controller) {
        WorldTrackedPose out=request;out.position=request.position+(controller-grip);return out;
    }

    // Final review P2-1: the native release is the HELD OBJECT stuck (its request pushed out by more than native_release_m for native_release_s,
    // the push not closing faster than stuck_closing_mps), 0.35 m / 0.25 s as spec 5A and HeldClamp; a motor hold is released by PhysicalGrab's
    // own object-lag rule (lag_release_m for lag_release_s for a corpse, prop_release_m for a prop, on the coupled target). No usable body: today's
    // behaviour, no release here.
    static constexpr double native_release_m=.35,native_release_s=.25;
    static constexpr double stuck_closing_mps=HandServoConfig{}.snap_closing_mps,closing_tau_s=.03;   // the push's closing rate, smoothed
    CouplingOutput update(unsigned side,const CouplingInput& in) {
        CouplingOutput out;out.target=in.controller;
        if(side>=2) return out;
        State& s=state_[side];
        double dt=s.has_time && std::isfinite(in.now)?in.now-s.time:0;
        if(!(dt>=0) || !std::isfinite(dt)) dt=0;
        dt=std::min(dt,max_dt);
        if(std::isfinite(in.now)) {s.time=in.now;s.has_time=true;}
        const bool use=usable(in.body_ready,in.body) && clamp_detail::finite_pose(in.controller);
        const double w=std::isfinite(in.body_weight)?std::clamp(in.body_weight,0.,1.):0.;
        if(in.kind==HoldKind::native && in.clamped_native && clamp_detail::finite_pose(*in.clamped_native)) out.target=*in.clamped_native;   // the clamp's output
        if(!use) {s.native.reset();s.has_push=false;s.push_closing=0;return out;}   // today's behaviour: the controller pose (or B4's clamp output), no release
        out.lag_m=length(in.controller.position-in.body->position);   // the raw hand lag (status only)
        if(in.kind==HoldKind::native) {
            out.uses_body=w>0;   // the object follows the body only as far as B15 draws it (couple_native passes the drawn target)
            Vec3 shift{};double push=0;   // the clamp's push of the controller's request: how far the world holds the object off its target
            if(in.controller_request && in.clamped_controller && finite(in.controller_request->position) && finite(in.clamped_controller->position)) {
                shift=in.clamped_controller->position-in.controller_request->position;push=length(shift);
            }
            if(!std::isfinite(push)) push=0;
            out.body_target=push>0?in.controller.position+shift:in.controller.position;   // free air: the controller palm itself, exactly
            if(s.has_push && dt>0) {const double rate=(s.push-push)/dt,a=std::min(1.,dt/(closing_tau_s+dt));s.push_closing+=(rate-s.push_closing)*a;}
            else if(!s.has_push) s.push_closing=0;
            s.push=push;s.has_push=true;
            const bool slow=!(s.push_closing>=stuck_closing_mps);   // the object is not coming back toward its target
            out.object_gap_m=push;out.object_blocked=push>native_release_m;
            out.release=s.native.update(out.object_blocked && slow,dt);
            return out;
        }
        s.native.reset();s.has_push=false;s.push_closing=0;
        if(w>0) {   // the orientation stays the controller's
            out.uses_body=true;
            out.target.position=w>=1?in.body->position:in.controller.position+(in.body->position-in.controller.position)*w;
        }
        return out;
    }
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    struct State {DwellTimer native{native_release_s};double time{};bool has_time{};
                  double push{},push_closing{};bool has_push{};};   // the native object's push and its closing rate (P2-1)
    std::array<State,2> state_{};
};
static_assert(HoldCoupling::native_release_m==HeldClampConfig{}.release_gap && HoldCoupling::native_release_s==HeldClampConfig{}.release_seconds,"spec 5A's numbers");

// One native-hold frame with the hand body ready, after `clamp.update(controller_request)` gave `clamped_controller` (dxgi_probe.cpp's
// publish_held_object and the checks run exactly this). drawn (B15 draws the body, weight > 0): the target is the drawn-grip request pushed out
// by the same planes, and controller_request is the request moved to the palm; not drawn: the target is clamped_controller, B4's own output for
// B4's own request (controller_request is then the request itself). The body's servo target is the clamped request as a hand pose either way.
inline CouplingOutput couple_native(HoldCoupling& coupling,unsigned side,const HeldClamp& clamp,const std::optional<HeldProxies>& proxies,
                                    const WorldTrackedPose& request,const WorldTrackedPose& controller,const std::optional<WorldTrackedPose>& body,
                                    bool body_ready,bool drawn,const WorldTrackedPose& controller_request,const WorldTrackedPose& clamped_controller,double now) {
    CouplingInput in{};in.kind=HoldKind::native;in.body_ready=body_ready;in.body=body;in.controller=controller;in.now=now;in.body_weight=drawn?1.:0.;
    in.controller_request=controller_request;in.clamped_controller=clamped_controller;in.clamped_native=clamped_controller;
    if(drawn && HoldCoupling::usable(body_ready,body)) {   // the object follows the drawn hand, never the raw body
        WorldTrackedPose carried=request;
        if(const auto pushed=clamp.project(carried,proxies)) carried.position=*pushed;
        in.clamped_native=carried;
    }
    return coupling.update(side,in);
}

// ---- the hand body's filter word while it holds something (B16 ruling: no pair API, no 0x91b50 / record 0x3a)
// The release check after a hold that took a layer out of the mask: does the hand body still overlap the released object? answered false: the
// object could not be read this frame (SafeRelease waits); answered with no hit: clear (or the object is gone).
struct HandOverlap {bool answered{};std::optional<ClampHit> hit;double depth{};};
struct FilterKeeperInput {
    double now{};
    bool body_ready{};                    // a hand body exists on this side (no body: nothing to keep)
    std::uint64_t body_generation{};      // the side's create count: a new body (a snap, a recreate) starts with the R1 default word
    std::optional<unsigned> held_layer;   // the layer of what this hand holds through the body now (none: nothing, or not coupled)
    std::uintptr_t held_entity{};bool held_corpse{};   // what is held (each released object gets its own release check)
    bool pose_fresh{true};   // review P3-1: the body's pose is current; while it is not, the release checks are frozen (their clocks stop)
};
// Counts per update (the status sums them): swaps = stripping holds begun, restores = releases SafeRelease ended (unresolved: after 0.5 s
// still overlapping), body_resets = releases ended because the body went or was recreated (it carries the default word).
struct FilterKeeperOutput {std::uint32_t word{};bool stripped{},releasing{},overlap{};unsigned swapped{},restored{},unresolved{},recreated{};};
class HandFilterKeeper {
public:
    static constexpr std::uint32_t default_word=hand_body::hand_filter_word(false);
    static constexpr std::uint32_t default_mask=hand_body::word_mask(default_word);
    static constexpr std::uint32_t world_mask=hand_body::layer_bit(hand_body::layer_world_low)|hand_body::layer_bit(hand_body::layer_world_low_high)|
        hand_body::layer_bit(hand_body::layer_world_high)|hand_body::layer_bit(hand_body::layer_mover);
    static constexpr std::size_t max_releases=4;   // objects let go whose release check is still running, per side
    // A layer the hand would collide with that is not the world's (the world stays solid whatever the hand holds).
    static constexpr bool strippable(unsigned layer) {return layer<27 && (default_mask&hand_body::layer_bit(layer))!=0 && (world_mask&hand_body::layer_bit(layer))==0;}
    static constexpr std::uint32_t holding_word(unsigned layer) {
        return strippable(layer)?hand_body::filter_word(hand_body::layer_debris,default_mask&~hand_body::layer_bit(layer)):default_word;
    }
    // The only words a hand body may be given: its own DEBRIS layer, a mask inside the default one that keeps every world layer.
    static constexpr bool allowed(std::uint32_t word) {
        return hand_body::word_layer(word)==hand_body::layer_debris && (hand_body::word_mask(word)&~default_mask)==0 && (hand_body::word_mask(word)&world_mask)==world_mask;
    }
    // overlap(entity, corpse) -> HandOverlap: the release check of one released object (called only for objects whose release is running).
    // The word is the default mask without the held layer and without the layer of every object still in its release check: a layer returns
    // only when the last object of that layer cleared its own SafeRelease (or the body went: a new body is born with the default word).
    template<class Overlap> FilterKeeperOutput update(unsigned side,const FilterKeeperInput& in,Overlap overlap) {
        FilterKeeperOutput out;out.word=default_word;
        if(side>=2) return out;
        State& s=state_[side];
        if(!in.body_ready) {out.recreated=s.count+(s.holding?1u:0u);s=State{};return out;}   // no body: its word went with it
        if(s.has_gen && in.body_generation!=s.gen && s.count) {out.recreated=static_cast<unsigned>(s.count);s.count=0;}   // a new body: born with the default
        s.gen=in.body_generation;s.has_gen=true;
        const bool hold=in.held_layer && strippable(*in.held_layer);
        if(s.holding && (!hold || in.held_entity!=s.held.entity)) {   // the hold ended (or moved to another object): its release check starts
            if(s.count==max_releases) {++out.unresolved;++out.restored;drop(s,0);}   // no room: the oldest check gives up (counted unresolved)
            s.rel[s.count]=s.held;s.rel[s.count].safe.reset();++s.count;s.holding=false;
        }
        if(hold && !s.holding) {
            bool resumed=false;   // a regrab of an object still in its release check continues that entry (no new swap)
            for(std::size_t i=0;i<s.count;++i) if(s.rel[i].entity==in.held_entity) {s.held=s.rel[i];drop(s,i);resumed=true;break;}
            if(!resumed) {s.held=Entry{};s.held.entity=in.held_entity;s.held.corpse=in.held_corpse;++out.swapped;}
            s.holding=true;
        }
        if(hold) s.held.layer=*in.held_layer;
        // review P3-1: with no current hand pose there is nothing to check against: the running checks are frozen (no answer, no time passes),
        // so a hitch never times a restore out while the hand may still be inside the object
        if(!in.pose_fresh) {if(!s.frozen) {s.frozen=true;s.frozen_at=in.now;}}
        else if(s.frozen) {if(std::isfinite(in.now-s.frozen_at) && in.now>s.frozen_at) s.frozen_total+=in.now-s.frozen_at;s.frozen=false;}
        for(std::size_t i=0;i<s.count && !s.frozen;) {
            Entry& e=s.rel[i];
            const HandOverlap ov=overlap(e.entity,e.corpse);
            out.overlap=out.overlap || ov.hit.has_value();
            SafeReleaseInput r{};r.now=in.now-s.frozen_total;r.released=true;r.overlap=ov.hit;r.sweep_answered=ov.answered;r.depth=ov.depth;
            const auto o=e.safe.update(r);
            if(o.restore_layer) {++out.restored;if(o.unresolved) ++out.unresolved;drop(s,i);} else ++i;
        }
        std::uint32_t mask=default_mask;
        if(s.holding) mask&=hand_body::word_mask(holding_word(s.held.layer));
        for(std::size_t i=0;i<s.count;++i) mask&=hand_body::word_mask(holding_word(s.rel[i].layer));
        out.word=mask==default_mask?default_word:hand_body::filter_word(hand_body::layer_debris,mask);
        out.stripped=out.word!=default_word;out.releasing=s.count>0;
        return out;
    }
    bool releasing(unsigned side) const {return side<2 && state_[side].count>0;}
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    struct Entry {std::uintptr_t entity{};bool corpse{};unsigned layer{};SafeRelease safe{};};
    struct State {Entry held{};bool holding{};std::array<Entry,max_releases> rel{};std::size_t count{};std::uint64_t gen{};bool has_gen{};
                  bool frozen{};double frozen_at{},frozen_total{};};
    static void drop(State& s,std::size_t i) {for(std::size_t k=i+1;k<s.count;++k) s.rel[k-1]=s.rel[k];--s.count;s.rel[s.count]=Entry{};}
    std::array<State,2> state_{};
};
static_assert(HandFilterKeeper::default_word==0x000885e4u,"the R1 word");
static_assert(HandFilterKeeper::holding_word(hand_body::layer_npc_body_ragdoll)==0x000085e4u,"a held corpse: the mask without layer 14");
static_assert(HandFilterKeeper::holding_word(hand_body::layer_debris)==HandFilterKeeper::default_word,"DEBRIS is never in the mask");
static_assert(!HandFilterKeeper::strippable(hand_body::layer_world_low) && !HandFilterKeeper::strippable(hand_body::layer_mover),"the world stays solid");
static_assert(HandFilterKeeper::allowed(HandFilterKeeper::default_word) && HandFilterKeeper::allowed(HandFilterKeeper::holding_word(hand_body::layer_rigid_body)),"");

// The hand body overlaps a released object: its sphere (radius + margin) reaches the object's bounds (a corpse: any of its 17 limb spheres).
// Pure geometry over what the caller read; the answer feeds HandFilterKeeper's SafeRelease. Nothing read (count 0) is no answer: the caller
// answers "clear" itself only when it knows the object is gone.
inline HandOverlap hand_overlap(Vec3 hand,double hand_radius,const NativeRigidBody* bodies,std::size_t count,double margin=.01) {
    HandOverlap out;out.answered=count>0;
    if(!finite(hand)) {out.answered=false;return out;}
    for(std::size_t i=0;i<count;++i) {
        const auto contact=bodies[i].contact(hand);
        if(!finite(contact)) continue;
        const double d=length(hand-contact);const double depth=hand_radius+margin-d;
        if(depth>0 && (!out.hit || depth>out.depth)) {
            ClampHit h;h.point=contact;h.normal=d>1e-6?(hand-contact)*(1/d):Vec3{0,0,1};h.entity=bodies[i].entity;h.body=BodyClass::dynamic;
            out.hit=h;out.depth=depth;
        }
    }
    return out;
}

// Counters for the status (render thread writes, status thread reads).
class HoldCouplingStats {
public:
    void note(unsigned side,HoldKind kind,const CouplingOutput& o) {
        if(side>=2) return;
        auto& c=c_[side];c.frames.fetch_add(1,std::memory_order_relaxed);
        (kind==HoldKind::native?c.native:kind==HoldKind::corpse?c.corpse:c.motor).fetch_add(1,std::memory_order_relaxed);
        if(o.uses_body) c.uses_body.fetch_add(1,std::memory_order_relaxed);
        if(o.release) c.releases.fetch_add(1,std::memory_order_relaxed);
        c.lag_m.store(o.lag_m,std::memory_order_relaxed);
    }
    void note_filter(unsigned side,const FilterKeeperOutput& f) {
        if(side>=2) return;
        auto& c=c_[side];c.word.store(f.word,std::memory_order_relaxed);
        c.swaps.fetch_add(f.swapped,std::memory_order_relaxed);c.restores.fetch_add(f.restored,std::memory_order_relaxed);
        c.unresolved.fetch_add(f.unresolved,std::memory_order_relaxed);c.recreated.fetch_add(f.recreated,std::memory_order_relaxed);
        if(f.overlap) c.overlap_frames.fetch_add(1,std::memory_order_relaxed);
    }
    std::string json() const {
        std::ostringstream o;o<<"{\"sides\":[";
        for(unsigned i=0;i<2;++i) {const auto& c=c_[i];
            o<<(i?",":"")<<"{\"frames\":"<<c.frames.load()<<",\"uses_body\":"<<c.uses_body.load()<<",\"motor\":"<<c.motor.load()<<",\"corpse\":"<<c.corpse.load()
             <<",\"native\":"<<c.native.load()<<",\"releases\":"<<c.releases.load()<<",\"lag_m\":"<<c.lag_m.load()<<",\"word\":"<<c.word.load()
             <<",\"swaps\":"<<c.swaps.load()<<",\"restores\":"<<c.restores.load()<<",\"unresolved\":"<<c.unresolved.load()<<",\"body_resets\":"<<c.recreated.load()
             <<",\"overlap_frames\":"<<c.overlap_frames.load()<<'}';}
        o<<"],\"default_word\":"<<HandFilterKeeper::default_word<<'}';return o.str();
    }
private:
    struct C {std::atomic<std::uint64_t> frames{},uses_body{},motor{},corpse{},native{},releases{},swaps{},restores{},unresolved{},recreated{},overlap_frames{};
              std::atomic<double> lag_m{};std::atomic<std::uint32_t> word{HandFilterKeeper::default_word};};
    std::array<C,2> c_{};
};

}  // namespace d2vr
