#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace d2vr {
// G6 (plan Task 14, the user's decision 2026-10-25): an awake guard or civilian held by the collar or an arm. Pure policy: the hand seats on the
// grasp bone (a keyframed body: its position is read, it is NEVER written), a hard pull ragdolls him (today's yank), a tether lets go.
// Bonelab §1.2: the held body hangs on the physical hand; here the hand hangs on him (he is animation-driven).
struct LivingBone {std::uintptr_t entity{};std::uint32_t entity_id{};int bone{};Vec3 position{};double radius{};};
// controller: the raw controller (C9); bone: the grasp bone's position this tick; hand_velocity: the controller's (m/s); grip: the squeeze;
// alive: he is still living (!(npc+0xd1 & 0x28)) and the bone read succeeded; epoch: the pose epoch.
struct LivingTick {Vec3 controller{},bone{};Vec3 hand_velocity{};float grip{};double dt{};bool alive{};std::uint64_t epoch{};};
// anchor: where the drawn hand seats (the bone's surface toward the controller); pull_step: a horizontal teleport step toward the controller
// (only with pull_enabled); release: the hold ends (the hand eases back to the controller); hard_pull: the hold ends and the yank runs on him.
struct LivingStep {Vec3 anchor{};std::optional<Vec3> pull_step;bool release{},hard_pull{};};
inline constexpr double living_tether_m=.35,living_tether_s=.2,living_hard_pull_mps=2.2,living_pull_gap_m=.06,living_pull_mps=.5;   // .5 m/s: 5 cm per 100 ms
inline constexpr float living_release_grip=.35f;
class LivingHold {
    LivingBone grasp_;std::uint64_t epoch_{};bool pull_enabled_{};double beyond_s_{};bool ended_{};
public:
    LivingHold(LivingBone grasp,Vec3 controller,std::uint64_t epoch,bool pull_enabled):grasp_(grasp),epoch_(epoch),pull_enabled_(pull_enabled) {(void)controller;}
    const LivingBone& grasp() const noexcept {return grasp_;}
    bool ended() const noexcept {return ended_;}
    LivingStep tick(const LivingTick& t) {
        LivingStep out;out.anchor=grasp_.position;
        if(ended_) {out.release=true;return out;}
        // a dead or unreadable target, a new epoch or an open hand: let go, no step (nothing is ever written to him here)
        if(!t.alive || t.epoch!=epoch_ || !std::isfinite(t.grip) || t.grip<living_release_grip || !finite(t.controller) || !finite(t.bone) ||
           !finite(t.hand_velocity) || !std::isfinite(t.dt) || t.dt<0) {ended_=true;out.release=true;return out;}
        grasp_.position=t.bone;
        const Vec3 gap=t.controller-t.bone;const double distance=length(gap);
        const Vec3 away=distance>1e-9?gap*(1/distance):Vec3{};
        out.anchor=distance>grasp_.radius && grasp_.radius>0?t.bone+away*grasp_.radius:t.controller;   // on his surface, toward the hand
        // the hard pull: the hand moving away from him faster than today's yank threshold (strictly above 2.2 m/s)
        if(distance>1e-9 && dot(t.hand_velocity,away)>living_hard_pull_mps) {ended_=true;out.hard_pull=out.release=true;return out;}
        // the tether: the controller more than 0.35 m from the grasp point for 0.2 s
        if(distance>living_tether_m) beyond_s_+=t.dt;else beyond_s_=0;
        if(beyond_s_>=living_tether_s) {ended_=true;out.release=true;return out;}
        // the pull (only when Task 13's verdict allows it): horizontal, at most 5 cm per 100 ms, never closer than the 6 cm gap
        if(pull_enabled_) {
            const Vec3 flat{gap.x,gap.y,0};const double h=length(flat);
            if(h>living_pull_gap_m) {
                const double step=std::min(h-living_pull_gap_m,living_pull_mps*t.dt);
                if(step>0) out.pull_step=flat*(step/h);
            }
        }
        return out;
    }
};
} // namespace d2vr
