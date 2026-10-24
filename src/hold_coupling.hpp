#pragma once
// Plan B task B16: held things follow the hand body (Bonelab §1.2: the held object is a dynamic body hanging on the hand's joint, so it is
// pulled behind the hand; §2.2: the hand lags when the world stops it). Pure: no engine access; the render thread is the only caller.
//
// * Motor holds (a prop or a corpse limb driven by PhysicalGrab): while the hand body is ready, the pose the grab pulls the object toward is the
//   hand body's (its position, the controller's orientation: a sphere has none) instead of the controller's, so the held body trails a hand the
//   world already stopped. Release: a corpse at 0.5 m of hand lag held for 0.25 s (plan A's corpse rule, PhysicalGrab's own numbers, the same
//   DwellTimer clock, X-3); a prop at 1.5 m (PhysicalGrab's prop rule, no dwell).
// * Native holds (HoldObject keyframes the item toward the mod's target, spec deviation 4; the HeldClamp stays the collision mechanism): the
//   target is the request carried by the body (the hold offset, request minus the grip it was computed from, so it comes from the controller
//   side and never from the body or last frame, applied at the body), pushed out by the HeldClamp's planes (HeldClamp::project). The clamp
//   itself keeps sweeping and judging the controller's request (HeldClamp::update, B4's sweeps, planes and 0.35 m / 0.25 s drop rule): a
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
    std::optional<WorldTrackedPose> controller_request,clamped_controller;};
struct CouplingOutput {WorldTrackedPose target{};bool uses_body{};double lag_m{};bool release{};
    std::optional<Vec3> body_target;};   // native, body in use: the hand body's servo target (none: B14's, the controller palm)

class HoldCoupling {
public:
    static constexpr double lag_release_m=.5,lag_release_s=.25;   // plan A's corpse rule (spec 266/671, ruling B-26), kept equal
    static constexpr double prop_release_m=1.5;                    // props keep PhysicalGrab's 1.5 m rule
    static constexpr double max_dt=.05;                            // a hitch never counts as more dwell than this (Review Focus 4)
    static_assert(lag_release_m==PhysicalGrab::lag_release_m && lag_release_s==PhysicalGrab::lag_release_s,"the corpse rule is plan A's");

    // A usable body pose: ready and finite.
    static bool usable(bool body_ready,const std::optional<WorldTrackedPose>& body) {return body_ready && body && finite(body->position);}
    // Native: the body-carried request. Without a usable body it is `request` itself (bit-exact); with one, the hold offset (request - grip,
    // from this frame's controller-side request, never from the body or a past frame) applied at the body: the object is carried by the hand body.
    static WorldTrackedPose native_request(const WorldTrackedPose& request,Vec3 grip,const std::optional<WorldTrackedPose>& body,bool body_ready) {
        if(!usable(body_ready,body) || !finite(grip)) return request;
        WorldTrackedPose out=request;out.position=body->position+(request.position-grip);
        return finite(out.position)?out:request;
    }
    // Native: the same request moved from the grip to the controller palm (what B4 would request with the hand exactly at the controller).
    static WorldTrackedPose controller_request(const WorldTrackedPose& request,Vec3 grip,Vec3 controller) {
        WorldTrackedPose out=request;out.position=request.position+(controller-grip);return out;
    }

    CouplingOutput update(unsigned side,const CouplingInput& in) {
        CouplingOutput out;out.target=in.controller;
        if(side>=2) return out;
        State& s=state_[side];
        double dt=s.has_time && std::isfinite(in.now)?in.now-s.time:0;
        if(!(dt>=0) || !std::isfinite(dt)) dt=0;
        dt=std::min(dt,max_dt);
        if(std::isfinite(in.now)) {s.time=in.now;s.has_time=true;}
        const bool use=usable(in.body_ready,in.body) && clamp_detail::finite_pose(in.controller);
        if(in.kind==HoldKind::native) {
            if(in.clamped_native && clamp_detail::finite_pose(*in.clamped_native)) out.target=*in.clamped_native;   // B4: the clamp's output
            if(!use) {s.lag.reset();return out;}
            out.uses_body=true;
            out.lag_m=length(in.controller.position-in.body->position);
            double gap=0;Vec3 shift{};   // the clamp's push of the controller's request
            if(in.controller_request && in.clamped_controller && finite(in.controller_request->position) && finite(in.clamped_controller->position)) {
                shift=in.clamped_controller->position-in.controller_request->position;gap=length(shift);
            }
            out.body_target=gap>0?in.controller.position+shift:in.controller.position;   // free air: the controller palm itself, exactly
            return out;   // release: HeldClamp::should_release() on the controller's request (B4's rule, unchanged)
        }
        if(!use) {s.lag.reset();return out;}   // today's behaviour: the controller pose, untouched
        out.uses_body=true;
        out.target.position=in.body->position;                    // the orientation stays the controller's
        out.lag_m=length(in.controller.position-in.body->position);
        if(in.kind==HoldKind::corpse) out.release=s.lag.update(out.lag_m>lag_release_m,dt);
        else {s.lag.reset();out.release=out.lag_m>prop_release_m;}
        return out;
    }
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    struct State {DwellTimer lag{lag_release_s};double time{};bool has_time{};};
    std::array<State,2> state_{};
};

// One native-hold frame with the hand body in use, after `clamp.update(controller_request)` gave `clamped_controller` (dxgi_probe.cpp's
// publish_held_object and the checks run exactly this): the body-carried request pushed out by the same planes is the target, and the body's
// servo target is the clamped controller request as a hand pose. request: today's (B4) request, computed from `grip`.
inline CouplingOutput couple_native(HoldCoupling& coupling,unsigned side,const HeldClamp& clamp,const std::optional<HeldProxies>& proxies,
                                    const WorldTrackedPose& request,Vec3 grip,const WorldTrackedPose& controller,const std::optional<WorldTrackedPose>& body,
                                    bool body_ready,const WorldTrackedPose& controller_request,const WorldTrackedPose& clamped_controller,double now) {
    CouplingInput in{};in.kind=HoldKind::native;in.body_ready=body_ready;in.body=body;in.controller=controller;in.now=now;
    in.controller_request=controller_request;in.clamped_controller=clamped_controller;
    WorldTrackedPose carried=HoldCoupling::native_request(request,grip,body,body_ready);
    if(const auto pushed=clamp.project(carried,proxies)) carried.position=*pushed;
    in.clamped_native=HoldCoupling::usable(body_ready,body)?carried:clamped_controller;
    return coupling.update(side,in);
}

// ---- the hand body's filter word while it holds something (B16 ruling: no pair API, no 0x91b50 / record 0x3a)
struct FilterKeeperInput {
    double now{};
    bool body_ready{};                    // a hand body exists on this side (no body: nothing to keep)
    std::uint64_t body_generation{};      // the side's create count: a new body (a snap, a recreate) starts with the R1 default word
    std::optional<unsigned> held_layer;   // the layer of what this hand holds through the body now (none: nothing, or not coupled)
    // The release check after a hold that took a layer out of the mask: does the hand body still overlap the released object?
    bool overlap_answered{};std::optional<ClampHit> overlap;double depth{};
};
struct FilterKeeperOutput {std::uint32_t word{};bool stripped{},releasing{},swapped{},restored{},unresolved{},recreated{};};
class HandFilterKeeper {
public:
    static constexpr std::uint32_t default_word=hand_body::hand_filter_word(false);
    static constexpr std::uint32_t default_mask=hand_body::word_mask(default_word);
    static constexpr std::uint32_t world_mask=hand_body::layer_bit(hand_body::layer_world_low)|hand_body::layer_bit(hand_body::layer_world_low_high)|
        hand_body::layer_bit(hand_body::layer_world_high)|hand_body::layer_bit(hand_body::layer_mover);
    // A layer the hand would collide with that is not the world's (the world stays solid whatever the hand holds).
    static constexpr bool strippable(unsigned layer) {return layer<27 && (default_mask&hand_body::layer_bit(layer))!=0 && (world_mask&hand_body::layer_bit(layer))==0;}
    static constexpr std::uint32_t holding_word(unsigned layer) {
        return strippable(layer)?hand_body::filter_word(hand_body::layer_debris,default_mask&~hand_body::layer_bit(layer)):default_word;
    }
    // The only words a hand body may be given: its own DEBRIS layer, a mask inside the default one that keeps every world layer.
    static constexpr bool allowed(std::uint32_t word) {
        return hand_body::word_layer(word)==hand_body::layer_debris && (hand_body::word_mask(word)&~default_mask)==0 && (hand_body::word_mask(word)&world_mask)==world_mask;
    }
    FilterKeeperOutput update(unsigned side,const FilterKeeperInput& in) {
        FilterKeeperOutput out;out.word=default_word;
        if(side>=2) return out;
        State& s=state_[side];
        if(!in.body_ready) {out.recreated=s.word!=default_word;s=State{};return out;}   // no body: its word went with it; a new one starts at the default
        if(s.has_gen && in.body_generation!=s.gen && s.phase==Phase::releasing) {   // a new body after the hold: it is born with the default word
            s.phase=Phase::idle;s.word=default_word;s.safe.reset();out.recreated=true;
        }
        s.gen=in.body_generation;s.has_gen=true;
        const bool hold=in.held_layer && strippable(*in.held_layer);
        if(hold) {
            out.swapped=s.phase==Phase::idle;   // default -> stripped (a regrab while releasing keeps the stripped word: no new swap)
            s.phase=Phase::holding;s.word=holding_word(*in.held_layer);s.safe.reset();
        } else if(s.phase==Phase::holding) {s.phase=Phase::releasing;s.safe.reset();}   // the hold ended: SafeRelease decides when the default returns
        if(s.phase==Phase::releasing) {
            SafeReleaseInput r{};r.now=in.now;r.released=true;r.overlap=in.overlap;r.sweep_answered=in.overlap_answered;r.depth=in.depth;
            const auto o=s.safe.update(r);
            if(o.restore_layer) {s.phase=Phase::idle;s.word=default_word;s.safe.reset();out.restored=true;out.unresolved=o.unresolved;}
        }
        out.word=s.word;out.stripped=s.word!=default_word;out.releasing=s.phase==Phase::releasing;
        return out;
    }
    bool releasing(unsigned side) const {return side<2 && state_[side].phase==Phase::releasing;}
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    enum class Phase {idle,holding,releasing};
    struct State {Phase phase{Phase::idle};std::uint32_t word{default_word};SafeRelease safe{};std::uint64_t gen{};bool has_gen{};};
    std::array<State,2> state_{};
};
static_assert(HandFilterKeeper::default_word==0x000885e4u,"the R1 word");
static_assert(HandFilterKeeper::holding_word(hand_body::layer_npc_body_ragdoll)==0x000085e4u,"a held corpse: the mask without layer 14");
static_assert(HandFilterKeeper::holding_word(hand_body::layer_debris)==HandFilterKeeper::default_word,"DEBRIS is never in the mask");
static_assert(!HandFilterKeeper::strippable(hand_body::layer_world_low) && !HandFilterKeeper::strippable(hand_body::layer_mover),"the world stays solid");
static_assert(HandFilterKeeper::allowed(HandFilterKeeper::default_word) && HandFilterKeeper::allowed(HandFilterKeeper::holding_word(hand_body::layer_rigid_body)),"");

// The hand body overlaps a released object: its sphere (radius + margin) reaches the object's bounds (a corpse: any of its 17 limb spheres).
// Pure geometry over what the caller read; the answer feeds HandFilterKeeper's SafeRelease.
struct HandOverlap {bool answered{};std::optional<ClampHit> hit;double depth{};};
inline HandOverlap hand_overlap(Vec3 hand,double hand_radius,const NativeRigidBody* bodies,std::size_t count,double margin=.01) {
    HandOverlap out;out.answered=true;
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
    void note_filter(unsigned side,const FilterKeeperOutput& f,bool overlap) {
        if(side>=2) return;
        auto& c=c_[side];c.word.store(f.word,std::memory_order_relaxed);
        if(f.swapped) c.swaps.fetch_add(1,std::memory_order_relaxed);
        if(f.restored) c.restores.fetch_add(1,std::memory_order_relaxed);
        if(f.unresolved) c.unresolved.fetch_add(1,std::memory_order_relaxed);
        if(f.recreated) c.recreated.fetch_add(1,std::memory_order_relaxed);
        if(f.releasing && overlap) c.overlap_frames.fetch_add(1,std::memory_order_relaxed);
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
