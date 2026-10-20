#pragma once
// Pure policy for sword contacts on NPCs (render and game thread share it through small adapters).
//  - StealthHold: a blade contact on a guard the engine calls unaware (isUnaware 0x5bf560) must not land the ordinary alerting hit:
//    the native Slash that follows turns it into the Assassination (state 13) only while he is still unaware. The contact waits for
//    that; if the native kill never starts it becomes the stealth kill itself (lethal damage through the same engine path, only
//    while the engine still says unaware). A noticed guard gets the ordinary hit at once; a Choke or any native takedown owns it.
//  - SwordHitGate: one hit per NPC per stroke instead of a flat 450 ms per NPC (a quick second stroke was dropped).
//  - pick_contact_victim: a live NPC on the blade beats the corpse the cast happened to meet first (swinging into a pile of bodies
//    chopped the dead and never damaged the living).
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
enum class StealthAct {wait,drop,hit,kill};
// native_offer: the engine's kill sensor targets this victim right now, so its own Assassination is expected from the next Slash press.
inline constexpr std::uint64_t stealth_window_ms(bool native_offer) noexcept {return native_offer?700:120;}
// A stealth strike is victim-based (Bonelab: the contact itself is the hit, no body-part pick): a deliberate contact (tip speed at least
// the sword hit gate) from the rear or the side of the victim (NPC forward is minus row 0 of its reference frame, entity +0x30c;
// attacker cos to forward < +0.2). The game's kill sensor is NOT a gate any more (it flickers with gaze); the engine-side
// damageable/unaware checks on the game thread still are, so an alerted or frontal guard gets the ordinary hit.
inline constexpr float rear_side_cos=.2f;
inline bool attacker_rear_or_side(const float row0[3],const float npc[3],const float attacker[3]) noexcept {
    const float fx=-row0[0],fy=-row0[1],dx=attacker[0]-npc[0],dy=attacker[1]-npc[1];
    const float f=fx*fx+fy*fy,d=dx*dx+dy*dy;
    if(!(f>.01f) || !(d>1e-6f)) return false;
    return (fx*dx+fy*dy)/std::sqrt(f*d)<rear_side_cos;
}
inline bool stealth_strike(double tip_speed,bool rear_or_side,double hit_speed) noexcept {return rear_or_side && tip_speed>=hit_speed;}
template<class Payload> class StealthHold {
    std::optional<Payload> held_;std::uint32_t victim_{};std::uint64_t tick_{},window_{};
public:
    bool pending() const noexcept {return held_.has_value();}
    const Payload* payload() const noexcept {return held_?&*held_:nullptr;}
    std::uint32_t victim() const noexcept {return held_?victim_:0;}
    // true: absorbed (a free slot, or another contact on the held victim, whose first contact keeps the deadline).
    bool hold(const Payload& payload,std::uint32_t victim,std::uint64_t now,bool native_offer) {
        if(held_) return victim==victim_;
        held_=payload;victim_=victim;tick_=now;window_=stealth_window_ms(native_offer);return true;
    }
    void clear() noexcept {held_.reset();victim_=0;}
    // owned: owner/equipment/victim still the ones captured. native: the player FSM runs Assassination (13) or another takedown.
    StealthAct service(std::uint64_t now,bool unaware,bool alive,bool native,bool choking,bool owned) noexcept {
        if(!held_) return StealthAct::wait;
        const auto done=[&](StealthAct act) {clear();return act;};
        if(now<tick_ || !owned || !alive || native || choking) return done(StealthAct::drop);
        if(!unaware) return done(StealthAct::hit);
        return now-tick_>=window_?done(StealthAct::kill):StealthAct::wait;
    }
};
class SwordHitGate {
    std::uintptr_t victim_{};std::uint64_t at_{};bool slowed_{};
public:
    inline static constexpr std::uint64_t same_victim_ms=450,rearm_ms=150;
    inline static constexpr double pause_speed=1.0,hit_speed=1.5; // m/s head-relative tip speed: the stroke stopped / a new one is moving
    void observe(double instant_speed) noexcept {if(victim_ && instant_speed<pause_speed) slowed_=true;}
    bool open(std::uintptr_t victim,std::uint64_t now,double instant_speed) const noexcept {
        if(victim!=victim_ || now<at_ || now-at_>=same_victim_ms) return true;
        return slowed_ && now-at_>=rearm_ms && instant_speed>=hit_speed;
    }
    void commit(std::uintptr_t victim,std::uint64_t now) noexcept {victim_=victim;at_=now;slowed_=false;}
};
// cast_npc: nearest NPC along the blade (cast_dead: it is a corpse); live_near: a live NPC whose bounds the blade touches; creature: small creature.
inline std::uintptr_t pick_contact_victim(std::uintptr_t cast_npc,bool cast_dead,std::uintptr_t live_near,std::uintptr_t creature) noexcept {
    if(cast_npc && !cast_dead) return cast_npc;
    if(live_near) return live_near;
    return cast_npc?cast_npc:creature;
}
}
