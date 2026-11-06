#pragma once
// Native-hands "feel" layer (docs/NATIVE-HANDS-PLAN.md, phase 1): contact haptics, weight slowdown, speed-gated hits.
// Pure math, no engine reads; formulas follow HIGGS (docs/research-bonelab-alyx-hands.md section 3.5). Cost: a few flops per event.
#include <algorithm>
#include <cmath>

namespace d2vr {

enum class HandFeelEvent {touch,hit,grab,release};
struct HapticPulse {float amplitude{};float seconds{};};

// Haptic strength grows with contact speed and mass^0.6 (HIGGS). Below 0.2 m/s a touch is silent (no buzz while resting on things).
inline HapticPulse hand_haptic(HandFeelEvent e,double speed_mps,double kg) noexcept {
    const double mass=std::pow(std::clamp(kg,.1,100.),.6)/std::pow(10.,.6);          // 1 at 10 kg
    switch(e) {
    case HandFeelEvent::touch: if(!(speed_mps>=.2)) return {};
        return {static_cast<float>(std::clamp(.08+.12*speed_mps*mass,.08,.6)),.010f};
    case HandFeelEvent::hit:   return {static_cast<float>(std::clamp(.35+.15*speed_mps*mass,.35,1.)),.030f};
    case HandFeelEvent::grab:  return {static_cast<float>(std::clamp(.25+.25*mass,.25,.8)),.040f};
    case HandFeelEvent::release: return {.12f,.015f};
    }
    return {};
}

// Walk speed scale while holding `held_kg` in total (both hands). HIGGS: 0.675 % per kg, at most 75% slower.
// Light things (< 3 kg: bottles, limbs, weapons) do not slow the player.
inline float held_move_scale(double held_kg) noexcept {
    if(!(held_kg>3.)) return 1.f;
    return static_cast<float>(1.-std::min(.75,.00675*(held_kg-3.)));
}

// One punch is one event: hand speed above 2.5 m/s fires, then the hand is quiet for 0.3 s.
// Returns 0 when nothing fires, else a 0..1 strength (2.5 m/s -> 0, 8 m/s -> 1) for the impulse and the melee damage scale.
struct HandHitGate {
    double quiet_until{-1.};
    static constexpr double min_speed=2.5,full_speed=8.,cooldown=.3;
    float fire(double speed_mps,double now) noexcept {
        if(!(speed_mps>=min_speed) || now<quiet_until) return 0.f;
        quiet_until=now+cooldown;
        return static_cast<float>(std::clamp((speed_mps-min_speed)/(full_speed-min_speed),.05,1.));
    }
};

}  // namespace d2vr
