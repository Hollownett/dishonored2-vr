#pragma once
#include <cstdint>

namespace d2vr::gun_catch {
// v1224 (user: "it must be fixed as well"; headset v1209 tick 179921890: a pistol caught 47 ms after the drop, v1216 tick 183843578: a crossbow
// caught 32 ms after it): a native gun thrown from one hand and caught by the other is always the same native gun, never a copy.
// The drop asks the engine for its instant unequip (EquipChange 64), but the engine keeps reporting the gun in slot 1 for a few frames. A
// catch inside that window used to see "slot 1 shows an item" and took the lying gun as a mod-owned copy, leaving the real gun unequipped
// in the inventory (two of the same weapon later).

// Input side: may a squeeze at a lying gun take it natively? A slot 1 that still shows THIS lying gun (its unequip has not finished) is
// free for the catch; any other item in slot 1 keeps the old rule (the gun becomes this hand's copy).
constexpr bool native_ok(bool lying_native,bool slot1_shows_item,bool slot1_is_this_gun,bool holding_object) noexcept {
    return lying_native && (!slot1_shows_item || slot1_is_this_gun) && !holding_object;
}

// Engine side (game thread): a lying native gun since its drop. `left_slot` turns true once slot 1 stops showing it (the unequip finished, or
// another item replaced it). A native catch made before that is a claim: the lying gun stays shown at the catching hand, and the equip is
// sent as soon as slot 1 is free; it is done when slot 1 shows the gun again. An unequip the engine never finishes (a running clip
// cancelled it) leaves the gun in slot 1: after unequip_wait_ms that IS the catch (the plan already routes slot 1 to the catching arm).
struct Lying {
    static constexpr std::uint64_t unequip_wait_ms=1500,give_up_ms=2500;
    enum class Do {nothing,wait,equip,arrived,give_up};
    bool left_slot{};int claim=-1;std::uint64_t dropped{},claimed{},equip_at{};
    void claim_for(unsigned side,std::uint64_t now) noexcept {claim=static_cast<int>(side&1u);claimed=now;equip_at=0;}
    bool unequipping() const noexcept {return !left_slot;}
    // Each game tick: does slot 1 show this gun now? The caller sends the equip on Do::equip (and calls equipped() when it was accepted),
    // releases the lying gun on Do::arrived, and lets it fall from the hand on Do::give_up.
    Do step(bool slot1_is_gun,std::uint64_t now) noexcept {
        if(!slot1_is_gun) left_slot=true;
        if(claim<0) return Do::nothing;
        if(equip_at && slot1_is_gun) return Do::arrived;
        if(!left_slot && now-dropped>=unequip_wait_ms) return Do::arrived;
        if(now-claimed>=give_up_ms) {claim=-1;return Do::give_up;}
        if(left_slot && !equip_at) return Do::equip;
        return Do::wait;
    }
    void equipped(std::uint64_t now) noexcept {equip_at=now?now:1;}
};
} // namespace d2vr::gun_catch
