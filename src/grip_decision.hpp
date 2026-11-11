#pragma once
#include "grab_force_cap.hpp"
#include "selection_policy.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
namespace d2vr {
// G3 (plan Task 6, the user's decision 2026-10-25): grip ALONE grabs physical props and corpses (Bonelab / Alyx: "when the controller reports
// grabbed"); grip on loot or a usable (a door, a lever) still does the game's Use. At the grip edge the hand's native Use target and its physical
// pick are compared by actual hand distance, and the nearer eligible one wins. pending: keep deciding (GripIntent waits).
enum class GripAloneChoice {pending,use,grab,none};   // none (V1): nothing - neither the game's Use nor a grab (a corpse with no bone in reach)
// native: this hand's live native Use target (NativeInteractionAim::target_bounds). physical: its physical lane's pick (NativeGrab::focus_score,
// raw cost from the same palm probe). native_fresh: the stage-0 verdict for this hand is current, target OR none (ruling H2: query_ready).
// since_ms: the grip edge (GripIntent::decision_since). choke_owns / takedown_offered: chokes and takedowns keep priority. occupied: the hand
// carries or holds something (native carry, a held object, a physical hold).
struct GripAloneInput {std::optional<PalmTarget> native;std::optional<FocusScore> physical;PalmProbe probe;
    bool native_fresh{};std::uint64_t since_ms{},now_ms{};bool choke_owns{},takedown_offered{},occupied{};
    bool fast_native{};
    bool native_corpse{};
    bool native_carry{};
    bool native_limb{};};   // v1088: the native target is a severed limb (arkSeveredLimb): never the game's carry (below). v1087: the native target is a native carryable (arkCpntPickable_Simple: a bottle, a severed limb), kind loot by takeable(), whose Use is only the game's HoldObject carry   // Ruling V1: the native Use target is a corpse (dead or unconscious body): grip never issues its carry Use   // fast_native (fix round 1, I2): the left hand is in the hip-draw zone or a gun is drawn: a native action that must not wait the 150 ms
inline constexpr std::uint64_t grip_alone_fresh_wait_ms=100;   // the existing 100 ms intent window, never longer (spec G3)
// Rules, in order:
//  1. an occupied hand, a choke that owns the squeeze or an offered takedown: use (today's path);
//  2. the physical pick counts only within grip_alone_gate_m (0.10 m: the hand is on it; a right-hand block next to a table still blocks);
//  3. no fresh native verdict yet and under 100 ms since the edge: pending;
//  4. (a corpse only; a living NPC's own native target is Use) the native target IS the physical pick's entity (a corpse's own carry): grab. Else both scored with the same probe, the lower cost wins: loot or usable -> use, prop, bone or living (ruling H6) -> grab; a tie goes to Use;
//     a native target beyond the palm's 0.20 m pickup gate (a gaze Use) still wins over nothing;
//  5. nothing in reach (ruling H1): pending while the grab buffer runs (grab_buffer_ms from the edge, 0.10 m gate), then use (today's role).
inline GripAloneChoice decide_grip_alone(const GripAloneInput& in,const GrabTuning& tuning,double hover_radius) noexcept {
    // v1088 (user, v1087 headset: "not fixed ... make it hold like it holds bottles and glasses, the hand in full contact with the limb"): a severed limb
    // is never the game's carry - its HoldObject floats the limb a whole-limb cylinder (r ~0.27 m) off the palm, the "sphere at a distance" of v1085-v1087.
    // The grab takes it when its own pick is that limb within limb_reach_m, or the other prop or corpse bone it picked within the usual gates (as the cost
    // rule below would); else the press waits for a pick (the grab buffer) and then does nothing.
    // (v1087 log: every right-hand press on the limb had no physical pick - the palm pick was not scored while a Use target existed - so Use took it.)
    if(in.native_limb) {
        if(in.occupied || in.choke_owns || in.takedown_offered) return GripAloneChoice::none;
        if(in.native && in.physical && std::isfinite(in.physical->distance) && in.physical->distance>=0 && std::isfinite(in.physical->cost)) {
            const auto& f=*in.physical;
            if(f.kind==PalmKind::prop && f.entity==in.native->id && f.distance<=limb_reach_m) return GripAloneChoice::grab;
            if((f.kind==PalmKind::prop && f.distance<=grab_acquire_distance) || (f.kind==PalmKind::bone && f.distance<=tuning.body_gate_m)) return GripAloneChoice::grab;
        }
        const std::uint64_t waited=in.now_ms>=in.since_ms?in.now_ms-in.since_ms:0;
        return tuning.grab_buffer_ms>0 && static_cast<double>(waited)<tuning.grab_buffer_ms?GripAloneChoice::pending:GripAloneChoice::none;
    }
    // v1145 (user, headset v1144: "bodies can still only be grabbed with grip + trigger"; 35 grip-alone presses chose Use with a corpse bone 22-25 cm
    // from the palm - the left hand's fast_native (a gun drawn / the hip) or a held takedown offer; the Use on a body is then blocked (v637), so the grip
    // did nothing while the fist, which skips this decision, grabbed): a corpse bone in reach is grabbed whatever the gun or a takedown offer says.
    // An occupied hand and the choke that owns this squeeze still win.
    if(!in.occupied && !in.choke_owns && (in.takedown_offered || in.fast_native) && in.physical && in.physical->kind==PalmKind::bone &&
       std::isfinite(in.physical->distance) && in.physical->distance>=0 && in.physical->distance<=tuning.body_gate_m && std::isfinite(in.physical->cost)) return GripAloneChoice::grab;
    if(in.occupied || in.choke_owns || in.takedown_offered || in.fast_native) return GripAloneChoice::use;
    const bool physical=in.physical && (in.physical->kind==PalmKind::prop || in.physical->kind==PalmKind::bone || in.physical->kind==PalmKind::living) &&
        std::isfinite(in.physical->distance) && in.physical->distance>=0 && in.physical->distance<=(in.physical->kind==PalmKind::prop?grab_acquire_distance:in.physical->kind==PalmKind::bone?tuning.body_gate_m:tuning.grip_alone_gate_m)   /* v1141 (user feedback 2026-11-11: "grabbing an enemy and a lying body are different controls"): a lying body's bone reaches body_gate_m (living keeps the hand-on-him gate) */ && std::isfinite(in.physical->cost);
    // A corpse's native carry target must not swallow a nearer prop. All grip-only
    // props use the hand sphere; bones keep their contact gate; shoulder carry still needs its torso hold.
    if(in.native_corpse) {
        const std::uint64_t waited=in.now_ms>=in.since_ms?in.now_ms-in.since_ms:0;
        if(physical && (in.physical->kind!=PalmKind::living || in.native_fresh)) return GripAloneChoice::grab;
        if(!in.native_fresh && waited<grip_alone_fresh_wait_ms) return GripAloneChoice::pending;
        // v1146 (user, headset v1145 on their own save: "two bodies near me: grip can't pick them up, grip + trigger can"): 280+ presses on such a body
        // (its native carry the hand's target, no ragdoll part in the palm's pick at the edge) ended in none = drain: no grab for the rest of the squeeze.
        // The fist is a physical request whose lane keeps searching (its grab buffer, then each frame) as the hand closes on the body; the game's
        // carry Use is no way in (the v637 body guard blocks every Use on a body, fist or grip: sim v1146 body_use_blocked 24). So grip alone on a
        // body becomes the same physical request once its own buffer has found nothing: same hand, same body, same result as grip + trigger.
        return tuning.grab_buffer_ms>0 && static_cast<double>(waited)<tuning.grab_buffer_ms?GripAloneChoice::pending:GripAloneChoice::grab;
    }
    const std::uint64_t elapsed=in.now_ms>=in.since_ms?in.now_ms-in.since_ms:0;
    if(!in.native_fresh && elapsed<grip_alone_fresh_wait_ms) return GripAloneChoice::pending;
    // I1: a living NPC's native targets (choke, talk, knock-out prompt) are on HIM and win Use; his hold waits for a current verdict (ruling H2).
    if(physical && in.physical->kind==PalmKind::living && !in.native_fresh)
        return tuning.grab_buffer_ms>0 && static_cast<double>(elapsed)<tuning.grab_buffer_ms?GripAloneChoice::pending:GripAloneChoice::use;
    std::optional<double> native_cost;
    if(in.native) if(const auto s=palm_cost(in.probe,*in.native,hover_radius,grab_acquire_distance)) native_cost=s->cost;
    // A corpse whose OWN native Use (the game's carry) is the native target: its whole-body box holds the palm, so it would always win on
    // cost; the user's decision is that grip alone grabs corpses, so the physical hold of that same entity wins (sim, Track G acceptance).
    // Only a corpse (PalmKind::bone): a living NPC's native target (choke, talk, knock-out) is Use (I1).
    // v1141 (user feedback 2026-11-11): also when that native target is the body's Loot - the trigger loots, the grip grabs.
    if(physical && in.native && in.physical->kind==PalmKind::bone && in.physical->entity && in.native->id==in.physical->entity) return GripAloneChoice::grab;
    // v1141 (user feedback 2026-11-11: "grabbing an enemy and a lying body are different controls"): the v674 rule (a body offering Loot is looted by the grip) is gone:
    // the TRIGGER owns a body's Loot like Pickpocket (NpcOffer::loot), the grip always grabs the body.
    if(physical && in.native && in.physical->kind==PalmKind::living && in.physical->entity && in.native->id==in.physical->entity) return GripAloneChoice::use;
    // v1086 (user: "severed limbs and other props should follow the same pattern as bottles: in my hand, not floating like normal game use"):
    // a prop the physical grab can hold that is ALSO the game's Use target (a severed limb: its HoldObject drives a jointed body softly) is
    // grabbed - the sticking hold - whatever the two costs. Loot stays Use; bottles and glasses are no physical candidates (their Use stays).
    // v1087 (user, v1086 headset: "I still hold the leg at a distance with a sphere"): a severed limb's Use target IS kind loot - takeable() calls every
    // native carryable loot - so the v1086 rule never matched (v1086 log: 7 presses with native = physical = the limb, native_kind 0, Use chosen). A
    // carryable's Use is only the game's carry, which the grab replaces; real loot (taken into the inventory) stays Use.
    if(physical && in.native && (in.native->kind!=PalmKind::loot || in.native_carry) && in.physical->kind==PalmKind::prop && in.physical->entity && in.native->id==in.physical->entity)
        return GripAloneChoice::grab;
    if(physical && (!native_cost || in.physical->distance<*native_cost)) return GripAloneChoice::grab;
    if(in.native) return GripAloneChoice::use;
    if(tuning.grab_buffer_ms>0 && static_cast<double>(elapsed)<tuning.grab_buffer_ms) return GripAloneChoice::pending;
    return GripAloneChoice::use;
}
// A body put on the shoulder by hand stays there, held by physics (user, headset v627: "take it with one or two hands and throw it onto the
// shoulder", then the user's choice of a physical shoulder: the game will not take a body held up in the air into its own carry, sim v632).
// A hand that has held any bone of a corpse for corpse_dock_hold_ms and lets go in a shoulder zone, the body near it (its grasp point within
// corpse_dock_lag_m of the hand), docks it: the hold stays, pulled to where the hand let go, fixed to the player's heading (anchored), and the
// hand is free. A fresh squeeze of that hand at the shoulder takes the body back into the hand (resumed; the squeeze is the dock's alone:
// consumes). The other hand holding the same body, or any release of the hold (it was pulled away, the lag rule, a fault), ends the dock.
// v1019 (user, 2026-11-05): the dock is also the trigger point for the game's own body carry, so a body placed on the shoulder acts
// like the native carry (Sokolov out of the Jindosh mansion). shoulder_carry.hpp presses the game's Use while the placing hand's native
// target is still this body; when the game takes it, the dock yields (native_carry -> take) and the game's CarryCorpse owns the body.
// The physical dock remains the fallback when the game does not take it. The native handoff can be turned off with build/no-shoulder-native.
// Replaces v613's torso held 0.5 s, which turned a walking torso drag into the game's carry.
inline constexpr std::uint64_t corpse_dock_hold_ms=250;
inline constexpr double corpse_dock_lag_m=.6;
// v1067 (user: "make it easy to put a corpse on a shoulder"): a body held in a shoulder zone for corpse_dock_dwell_ms docks too, the grip still
// held (no let-go needed), and a body lifted with both hands docks (the other hand's hold at the dock does not end it; a NEW one does).
inline constexpr std::uint64_t corpse_dock_dwell_ms=120;   // v1069 (user: "even easier"): 200 -> 120
// native_carry (v1019, user 2026-11-05): the game now carries a body (arkPlayerStateCarryCorpse). The placement handed it to the
// game through shoulder_carry.hpp, so the dock's own hold ends at once (take): the game binds the body to the player's spine and
// any physical motor write from here would fight it.
struct ShoulderDockIn {bool corpse_hold{};std::uintptr_t entity{};std::uint64_t held_ms{};double lag_m{1e9};bool in_zone{};float grip{};bool other_holds{};bool native_carry{};std::uint64_t now{};};
class ShoulderDock {
public:
    struct Out {bool anchored{},docked{},resumed{},take{};};
    Out update(unsigned side,const ShoulderDockIn& in) noexcept {
        Out o{};if(side>=2) return o;
        auto& s=side_[side];
        const bool down=in.grip>=(s.down?.35f:.55f);
        if(s.anchored) {
            if(in.native_carry) {s.anchored=false;s.entity=0;o.take=true;}   // the game took the body (the native shoulder now)
            else if(!in.corpse_hold || in.entity!=s.entity || (in.other_holds && !s.other_at_dock)) s.anchored=false;
            else if(down && !s.down && in.in_zone) {s.anchored=false;s.consumed=true;o.resumed=true;}
        } else {
            const bool ready=in.corpse_hold && in.entity && in.in_zone && in.held_ms>=corpse_dock_hold_ms && std::isfinite(in.lag_m) && in.lag_m<=corpse_dock_lag_m;
            if(!(ready && down && in.now)) s.zone_since=0;else if(!s.zone_since) s.zone_since=in.now;
            const bool dwelt=s.zone_since && in.now-s.zone_since>=corpse_dock_dwell_ms;
            if(ready && ((s.down && !down) || dwelt)) {s.anchored=true;s.entity=in.entity;s.other_at_dock=in.other_holds;s.zone_since=0;o.docked=true;}
        }
        if(!in.other_holds) s.other_at_dock=false;
        if(!down) s.consumed=false;
        s.down=down;o.anchored=s.anchored;return o;
    }
    bool anchored(unsigned side) const noexcept {return side<2 && side_[side].anchored;}
    bool consumes(unsigned side) const noexcept {return side<2 && side_[side].consumed;}
    void reset(unsigned side) noexcept {if(side<2) side_[side]=Side{};}
private:
    struct Side {bool down{},anchored{},consumed{},other_at_dock{};std::uintptr_t entity{};std::uint64_t zone_since{};};
    std::array<Side,2> side_{};
};
// The shoulder zones, in tracking space: a sphere of `radius` at head + side*left + up*up + forward*forward on either side (the eye centre and
// the head's axes; -Z forward, +Y up). Clear of the face (a hand holding something up in front of the eyes is not at a shoulder) and inside the
// sword holster over the right shoulder (TempleReach scabbard: left -.17, up -.10, forward -.10, r .16), which takes only a squeeze that STARTS there.
// v1069 (user: "even easier to put a corpse on the shoulder"): radius .24 -> .30, and the face is kept out by its own sphere (face) round the eyes.
struct ShoulderZone {double left=.18,up=-.18,forward=-.03,radius=.30,face=.16;};
inline bool at_shoulder(const std::array<double,3>& head,const std::array<double,3>& forward,const std::array<double,3>& up,const std::array<double,3>& hand,
                        const ShoulderZone& z={}) noexcept {
    using V=std::array<double,3>;
    const auto unit=[](V v) {const double l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);return l>1e-6?V{v[0]/l,v[1]/l,v[2]/l}:V{};};
    const V f=unit(forward),u=unit(up);
    const V left=unit({u[1]*f[2]-u[2]*f[1],u[2]*f[0]-u[0]*f[2],u[0]*f[1]-u[1]*f[0]});   // up x forward = left (right-handed, -Z forward)
    {double e2=0;for(unsigned i=0;i<3;++i) e2+=(hand[i]-head[i])*(hand[i]-head[i]);if(!(e2>=z.face*z.face)) return false;}   // at the face (or NaN): never
    for(const double side:{1.,-1.}) {
        double d2=0;
        for(unsigned i=0;i<3;++i) {const double c=head[i]+left[i]*side*z.left+u[i]*z.up+f[i]*z.forward;d2+=(hand[i]-c)*(hand[i]-c);}
        if(std::isfinite(d2) && d2<z.radius*z.radius) return true;
    }
    return false;
}
// v651 (user, headset v642: the free left hand could not pick up bottles or limbs with the sword in the right): the held sword's Block (RB) locks the
// arm body part and drops the native Use, so Block steps aside while the left grip is (or may become) a Use. Not for a gripped gun or a choke (own RB).
inline bool block_yields_to_left_use(bool left_grip,bool gun_held,bool choke_rb,bool use_or_pending) noexcept {return left_grip && !gun_held && !choke_rb && use_or_pending;}
} // namespace d2vr
