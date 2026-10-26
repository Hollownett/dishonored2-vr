#pragma once
#include "grab_force_cap.hpp"
#include "selection_policy.hpp"
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
    bool native_corpse{};};   // Ruling V1: the native Use target is a corpse (dead or unconscious body): grip never issues its carry Use   // fast_native (fix round 1, I2): the left hand is in the hip-draw zone or a gun is drawn: a native action that must not wait the 150 ms
inline constexpr std::uint64_t grip_alone_fresh_wait_ms=100;   // the existing 100 ms intent window, never longer (spec G3)
// Rules, in order:
//  1. an occupied hand, a choke that owns the squeeze or an offered takedown: use (today's path);
//  2. the physical pick counts only within grip_alone_gate_m (0.10 m: the hand is on it; a right-hand block next to a table still blocks);
//  3. no fresh native verdict yet and under 100 ms since the edge: pending;
//  4. (a corpse only; a living NPC's own native target is Use) the native target IS the physical pick's entity (a corpse's own carry): grab. Else both scored with the same probe, the lower cost wins: loot or usable -> use, prop, bone or living (ruling H6) -> grab; a tie goes to Use;
//     a native target beyond the palm's 0.20 m pickup gate (a gaze Use) still wins over nothing;
//  5. nothing in reach (ruling H1): pending while the grab buffer runs (grab_buffer_ms from the edge, 0.10 m gate), then use (today's role).
inline GripAloneChoice decide_grip_alone(const GripAloneInput& in,const GrabTuning& tuning,double hover_radius) noexcept {
    if(in.occupied || in.choke_owns || in.takedown_offered || in.fast_native) return GripAloneChoice::use;
    const bool physical=in.physical && (in.physical->kind==PalmKind::prop || in.physical->kind==PalmKind::bone || in.physical->kind==PalmKind::living) &&
        std::isfinite(in.physical->distance) && in.physical->distance>=0 && in.physical->distance<=tuning.grip_alone_gate_m && std::isfinite(in.physical->cost);
    // A corpse's native carry target must not swallow a nearer prop. All grip-only
    // physical picks use the same contact gate; shoulder carry still needs its torso hold.
    if(in.native_corpse) {
        const std::uint64_t waited=in.now_ms>=in.since_ms?in.now_ms-in.since_ms:0;
        if(physical && (in.physical->kind!=PalmKind::living || in.native_fresh)) return GripAloneChoice::grab;
        if(!in.native_fresh && waited<grip_alone_fresh_wait_ms) return GripAloneChoice::pending;
        return tuning.grab_buffer_ms>0 && static_cast<double>(waited)<tuning.grab_buffer_ms?GripAloneChoice::pending:GripAloneChoice::none;
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
    if(physical && in.native && in.physical->kind==PalmKind::bone && in.physical->entity && in.native->id==in.physical->entity) return GripAloneChoice::grab;
    if(physical && in.native && in.physical->kind==PalmKind::living && in.physical->entity && in.native->id==in.physical->entity) return GripAloneChoice::use;
    if(physical && (!native_cost || in.physical->distance<*native_cost)) return GripAloneChoice::grab;
    if(in.native) return GripAloneChoice::use;
    if(tuning.grab_buffer_ms>0 && static_cast<double>(elapsed)<tuning.grab_buffer_ms) return GripAloneChoice::pending;
    return GripAloneChoice::use;
}
// Item 4 (v613, the user's decision 2026-10-25): a corpse goes onto the shoulder (the game's native body carry) only when the player holds a TORSO
// bone and keeps the grip for corpse_carry_hold_ms. Ragdoll bones (grab_mass.hpp): 0 pelvis, 1 spine, 2 chest, 3 neck, 4 head, 5.. limbs. The torso is the
// pelvis (the body's centre bone, where a person grabs to lift), the spine and the chest; the neck (collar), the head and every limb stay the physical
// drag. A quick torso grab (released or shorter than 0.5 s) is still the hand drag. build/legacy-corpse-carry (legacy) restores v612: never hands off.
inline constexpr std::uint64_t corpse_carry_hold_ms=500;
inline constexpr bool torso_bone(int bone) noexcept {return bone>=0 && bone<=2;}
// native_is_corpse: this hand's native Use target is the held corpse itself (its own carry); occupied: the game already carries or holds something.
struct CarryHandoffInput {bool corpse_hold{},grip_held{},legacy{},native_is_corpse{},occupied{};int bone{-1};std::uint64_t held_ms{};};
inline bool carry_handoff(const CarryHandoffInput& in) noexcept {
    return !in.legacy && in.corpse_hold && in.grip_held && in.native_is_corpse && !in.occupied && torso_bone(in.bone) && in.held_ms>=corpse_carry_hold_ms;
}
} // namespace d2vr
