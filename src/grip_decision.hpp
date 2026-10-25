#pragma once
#include "grab_force_cap.hpp"
#include "selection_policy.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
namespace d2vr {
// G3 (plan Task 6, the user's decision 2026-10-25): grip ALONE grabs physical props and corpses (Bonelab / Alyx: "when the controller reports
// grabbed"); grip on loot or a usable (a door, a lever) still does the game's Use. At the grip edge the hand's native Use target and its physical
// pick are scored in ONE list with the same palm cost, and the cheaper one wins. pending: keep deciding (GripIntent waits).
enum class GripAloneChoice {pending,use,grab};
// native: this hand's live native Use target (NativeInteractionAim::target_bounds). physical: its physical lane's pick (NativeGrab::focus_score,
// raw cost from the same palm probe). native_fresh: the stage-0 verdict for this hand is current, target OR none (ruling H2: query_ready).
// since_ms: the grip edge (GripIntent::decision_since). choke_owns / takedown_offered: chokes and takedowns keep priority. occupied: the hand
// carries or holds something (native carry, a held object, a physical hold).
struct GripAloneInput {std::optional<PalmTarget> native;std::optional<FocusScore> physical;PalmProbe probe;
    bool native_fresh{};std::uint64_t since_ms{},now_ms{};bool choke_owns{},takedown_offered{},occupied{};};
inline constexpr std::uint64_t grip_alone_fresh_wait_ms=100;   // the existing 100 ms intent window, never longer (spec G3)
// Rules, in order:
//  1. an occupied hand, a choke that owns the squeeze or an offered takedown: use (today's path);
//  2. the physical pick counts only within grip_alone_gate_m (0.12 m: the hand is on it; a right-hand block next to a table still blocks);
//  3. no fresh native verdict yet and under 100 ms since the edge: pending;
//  4. both scored with the same probe, the lower cost wins: loot or usable -> use, prop, bone or living (ruling H6) -> grab; a tie goes to Use;
//     a native target beyond the palm's 0.45 m gate (a gaze Use) still wins over nothing;
//  5. nothing in reach (ruling H1): pending while the grab buffer runs (grab_buffer_ms from the edge, 0.12 m gate), then use (today's role).
inline GripAloneChoice decide_grip_alone(const GripAloneInput& in,const GrabTuning& tuning,double hover_radius) noexcept {
    if(in.occupied || in.choke_owns || in.takedown_offered) return GripAloneChoice::use;
    const bool physical=in.physical && (in.physical->kind==PalmKind::prop || in.physical->kind==PalmKind::bone || in.physical->kind==PalmKind::living) &&
        std::isfinite(in.physical->distance) && in.physical->distance>=0 && in.physical->distance<=tuning.grip_alone_gate_m && std::isfinite(in.physical->cost);
    const std::uint64_t elapsed=in.now_ms>=in.since_ms?in.now_ms-in.since_ms:0;
    if(!in.native_fresh && elapsed<grip_alone_fresh_wait_ms) return GripAloneChoice::pending;
    std::optional<double> native_cost;
    if(in.native) if(const auto s=palm_cost(in.probe,*in.native,hover_radius,grab_acquire_distance)) native_cost=s->cost;
    if(physical && (!native_cost || in.physical->cost<*native_cost)) return GripAloneChoice::grab;
    if(in.native) return GripAloneChoice::use;
    if(tuning.grab_buffer_ms>0 && static_cast<double>(elapsed)<tuning.grab_buffer_ms) return GripAloneChoice::pending;
    return GripAloneChoice::use;
}
} // namespace d2vr
