#pragma once
#include "finger_ik.hpp"
#include <array>
#include <cstdint>
#include <optional>

// G5 (plan Task 12): the free hand's probe answers, per probe slot (the palm cast and one per finger), as the shapes the fingers rest on. A slot's
// shape lives while its probe keeps answering: a hit is live for finger_probe_hit_life_ms after the slot's latest answer, and it stays through one
// missed probe (stickiness, ruling H10: it survives >= 2 probe cycles after the last hit, counted in probe cycles, not frames, so a frame hitch
// does not drop a surface); the second miss in a row drops it (a hand that lifts off is free within two cycles, never a stuck open hand). Bonelab
// disables its finger box on overlap (CheckFingerEnable, study 355); here the probes find the surface instead. Pure; one per hand.
namespace d2vr::rig {
inline constexpr std::uint64_t finger_probe_hit_life_ms=150;
inline constexpr unsigned finger_probe_cache_slots=8,finger_probe_sticky_misses=2;
class FingerProbeCache {
public:
    // A probe of this slot answered: its hit as a shape, or nothing (a miss, or a hit that makes no shape).
    void probed(unsigned slot,std::optional<HeldShape> hit,std::uint64_t now) {
        if(slot>=finger_probe_cache_slots) return;
        auto& s=slots_[slot];s.answered=now;
        if(hit) {s.has=true;s.shape=*hit;s.misses=0;}
        else if(s.has && ++s.misses>=finger_probe_sticky_misses) s.has=false;
    }
    // The live shapes (in slot order), how many.
    unsigned shapes(std::uint64_t now,std::array<HeldShape,8>& out) const {
        unsigned n=0;
        for(const auto& s:slots_) if(s.has && (now<=s.answered || now-s.answered<=finger_probe_hit_life_ms) && n<out.size()) out[n++]=s.shape;
        return n;
    }
    void clear() {slots_={};}
private:
    struct Slot {bool has{};HeldShape shape{};std::uint64_t answered{};unsigned misses{};};
    std::array<Slot,finger_probe_cache_slots> slots_{};
};
}
