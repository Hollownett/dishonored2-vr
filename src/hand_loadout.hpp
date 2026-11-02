#pragma once
#include <array>

namespace d2vr {
// Dual wield (knob build/dual-wield, docs/research-dual-wield.md, docs/dual-wield-two-hands-plan.md): which physical hand drives each
// native slot, and what a hand holds beyond it. The engine keeps two slots (slot 0 the sword, drawn on the right arm; slot 1 the gun,
// crossbow, grenade, mine or power, on the left arm) and the item's decl decides the slot, so the mod routes a slot's trigger, aim, hits
// and grip to the hand that took it. A second weapon of a kind (two pistols, crossbow + pistol, a guard's weapon) is mod-owned: a copy
// in that hand, fired or swung by the mod. Default, and always with the knob off: the game's own hands, the sword right, the rest left.
struct HandLoadout {
    enum class Extra : unsigned {none,pistol,crossbow,guard_sword,guard_pistol};
    unsigned melee=1;                    // the physical hand (0 left, 1 right) that holds slot 0
    std::array<Extra,2> extra{};         // a hand's mod-owned weapon (shown as a copy); a hand holds at most one thing
    unsigned ranged() const noexcept {return melee^1u;}
    bool mirrored() const noexcept {return melee==0;}
    static bool gun(Extra e) noexcept {return e==Extra::pistol || e==Extra::crossbow || e==Extra::guard_pistol;}
    struct In {
        std::array<bool,2> sword_take{};   // a fresh squeeze at a sword holster (or the lying sword) while no hand holds the sword
        std::array<bool,2> gun_take{};     // a fresh squeeze at the gun holster while no gun is drawn
        int wheel_hand=-1;                 // the wheel this hand opened equipped a slot-1 item: it goes to that hand
        bool sword_held{},gun_held{};      // the sword / the gun is held by its hand right now (hold-to-hold)
        bool sword_drawn{};                // v785: the sword is drawn (in its hand, gripped or not)
        bool slot1_item=true;              // slot 1 holds something the hand shows (gun, grenade, the Heart); false: a power or nothing (v773)
    };
    // True when the slots change hands: the caller restarts the grip machines for the new hands (a sword left in the old hand is then
    // put away by SwordGrip's own not-held rule). A hand with a mod-owned weapon is busy for the native slots.
    bool update(const In& in) noexcept {
        int want=-1;   // the new melee hand
        if(in.wheel_hand==0 || in.wheel_hand==1) want=in.wheel_hand^1;
        else for(unsigned h=0;h<2;++h) {
            if(extra[h]!=Extra::none) continue;
            // a hand holding the gun is busy; v784 (user, headset v793: a pistol picked up by the sword's hand went to the hip): so is one that
            // only shows its slot-1 item, ungripped for a moment (the sword then came back and pushed the gun to the other hand, onto the hip)
            if(in.sword_take[h] && h!=melee && !((in.gun_held || in.slot1_item) && h==ranged())) want=static_cast<int>(h);
            // so is one holding the sword; v785 (headset v795: the sword hand resting at the hip took the gun back from the other hand, which then
            // holstered it): or with the sword drawn in it, gripped or not
            else if(in.gun_take[h] && h!=ranged() && !((in.sword_held || in.sword_drawn) && h==melee)) want=static_cast<int>(h^1u);
        }
        if(want<0 || static_cast<unsigned>(want)==melee) return false;
        melee=static_cast<unsigned>(want);
        // The native slot-1 hand holds the native item (its copy goes away), unless slot 1 shows nothing: a power is no thing in the hand,
        // so the hand keeps its copy (v773, headset v772: a left pistol copy fell out when the right took the sword).
        if(extra[ranged()]!=Extra::none && in.slot1_item) extra[ranged()]=Extra::none;
        return true;
    }
    // The wheel gave the hand that opened it (`hand`) a slot-1 item natively. The other hand keeps what it held: a native gun or crossbow
    // it held becomes its mod-owned copy (`other_had`: what that hand's native slot 1 was; Extra::none for a power, grenade or nothing).
    // `same_item`: the wheel chose what slot 1 already held (two pistols): the other hand keeps a copy of it.
    void wheel_second(unsigned hand,Extra other_had) noexcept {
        const unsigned other=hand^1u;
        if(gun(other_had) && extra[other]==Extra::none && other==melee) extra[other]=other_had;
        extra[hand]=Extra::none;
    }
    // A hand picks up a gun lying in the world (or a guard's weapon): native when slot 1 is free and the hand can drive it, else mod-owned.
    // Returns true when it goes native (the caller then makes this hand the slot-1 hand and asks the game to equip it).
    bool pickup(unsigned hand,Extra kind,bool slot1_drawn,bool sword_in_hand) noexcept {
        const bool native=(kind==Extra::pistol || kind==Extra::crossbow) && !slot1_drawn && !(sword_in_hand && hand==melee);
        if(native) {if(hand!=ranged()) {melee=hand^1u;}extra[hand]=Extra::none;return true;}
        extra[hand]=kind;return false;
    }
    void drop(unsigned hand) noexcept {extra[hand&1u]=Extra::none;}
    // v780 (user, headset v779: "I toss the pistol to the hand with Blink and expect Blink to move to the other hand; any weapon"): the slot-1
    // hand took a weapon (`hand` holds it now) while slot 1 holds a power: slot 1 goes to the other hand when that one is free (no weapon of its
    // own, no sword drawn or held there). True when it moved; the caller restarts the grip machines. (The sword take does this in update().)
    bool power_hand_over(unsigned hand,bool slot1_power,bool other_free) noexcept {
        hand&=1u;
        if(hand!=ranged() || !slot1_power || !other_free || extra[hand^1u]!=Extra::none) return false;
        melee=hand;return true;
    }
    // A wheel commit's item goes to the hand that opened the wheel once the equip lands. It is dropped when that takes too long, or when the
    // wheel's own hand took a gun from the hip meanwhile (that equip is the hip's). The other hand gripping its gun is no reason: hold-to-hold
    // keeps that grip squeezed (headset v757 lost every second pistol and crossbow to it).
    static bool wheel_route_lost(bool timed_out,unsigned wheel_hand,unsigned gun_hand,bool gun_held,bool gun_held_at_commit) noexcept {
        return timed_out || (gun_held && !gun_held_at_commit && wheel_hand==gun_hand);
    }
    // Slot bits (bit 0: a slot-1 item drawn, bit 1: the sword drawn, as hand_weapon_bits) as physical hand bits (bit 0 left, bit 1 right),
    // with each hand's mod-owned weapon added.
    unsigned hand_bits(unsigned slot_bits) const noexcept {
        unsigned b=mirrored()?(slot_bits&~3u)|((slot_bits&1u)<<1)|((slot_bits>>1)&1u):slot_bits;
        for(unsigned h=0;h<2;++h) if(extra[h]!=Extra::none) b|=1u<<h;
        return b;
    }
};
} // namespace d2vr
