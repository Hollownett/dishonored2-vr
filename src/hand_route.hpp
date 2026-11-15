#pragma once
// The hand routing of the full-body publication (dxgi_probe.cpp): which authored clip poses each hand (the arm's own, the other arm's mirrored,
// the kept clip of a gun copy's type) and which item each hand holds (body::Inputs::item_key, the key of a hold and of its remembered grip).
// Pure: its inputs are the control tick's hand-state snapshot and the arm plan, so tests/dual_wield_grip_replay_check.cpp replays it.
#include "arm_state.hpp"
#include "full_body_driver.hpp"
#include <cstdint>
#include <string_view>

namespace d2vr::hand_route {
struct In {
    unsigned pack=arm::pack_default;   // the arm plan (arm_state.hpp pack): the sword arm, the slot-1 arm, the cross bits, each arm's copy
    bool fresh{};                      // the hand-state snapshot is recent (the control tick read it within 300 ms)
    bool dual_wield{};
    unsigned empty_bits{};             // hand_empty_bits: bit 0 slot 1 (the game's left hand) empty, bit 1 slot 0 (the sword) empty
    unsigned weapon_bits{};            // hand_weapon_bits: bit 0 a slot-1 item drawn, bit 1 the sword drawn
    bool left_power{};                 // slot 1 holds a power (the player's fingers pose that hand)
    bool left_gun{};                   // slot 1 holds a gun (its model is known)
    bool holding_object{};unsigned hold_side{};   // a native held object (a bottle) and the hand that holds it
    bool carrying_body{};              // a body the game carries (fresh carry bits, no held object)
    int slot1_type{};                  // slot 1's component type (grip_clips::Type): the control tick's latch, else the live read
    std::string_view equip_name;       // slot 1's equip name ("Gun", "Crossbow", a power's)
};

inline void route(const In& in,fullbody::Tracking& t) {
    using arm::Held;
    const bool fresh=in.fresh;
    const unsigned arm_cross=arm::cross_of(in.pack),mh=arm::sword_arm_of(in.pack),rh=arm::slot1_arm_of(in.pack);
    const auto extra_of=[&](unsigned s) {return arm::copy_of(in.pack,s);};
    for(unsigned side=0;side<2;++side) t.hand_empty[side]=fresh && ((in.empty_bits>>side)&1u);
    if(fresh && in.left_power) t.hand_empty[0]=true; // a power holds nothing: the player's fingers, not its authored pose
    // dual wield, crossed, per arm (the arm-joint cross bits). v763 (user, headset v762: "the right hand holds every weapon exactly as the left
    // does, the left the sword as the right"): an arm holding the other arm's item is posed by that arm's own native clip mirrored onto it
    // (FullBodyDriver mirror_native); the item rides it through the attachment cross (y180, exact for a mirrored hold).
    if(fresh && (arm_cross&1u)) {const bool left_item=!(in.empty_bits&1u) && !in.left_power;t.mirror_native[1]=left_item;t.hand_empty[1]=!left_item;}   // the slot-1 item on the right arm
    if(fresh && (arm_cross&2u)) {const bool sword=!(in.empty_bits&2u);t.mirror_native[0]=sword;t.hand_empty[0]=!sword;}                                 // the sword on the left arm
    // v783 (user, headset v791: "with a bottle the pistol in the other hand is drawn wrong"): a native held object fills ONE hand
    // (hand_hold_side); the other keeps its weapon's routing. Before dual wield the game holstered everything, so both hands were freed.
    const auto holds=[&](unsigned s) {return in.holding_object && (in.hold_side&1u)==s;};
    const auto extra_type=[&](unsigned s) {const auto e=extra_of(s);return e==Held::pistol||e==Held::guard_pistol?1u:e==Held::crossbow?2u:0u;};
    const auto copy_gun=[&](unsigned s) {return !holds(s) && arm::is_gun(extra_of(s));};
    t.slot1_gun=fresh && in.dual_wield && in.left_gun && !holds(rh); // the driver keeps its calm clip (v771)
    t.slot1_type=t.slot1_gun?static_cast<unsigned>(in.slot1_type):0u;
    // v1370 (user, headset v1360: "the crossbow held with the pistol's grip ... then it kept blinking, as if it tried to change its pose in the hand"):
    // a copy hand is posed by the kept clip of its own type (FullBodyDriver gun_clip), always. v1104 gave a copy of the native gun's type the native
    // arm's live clip, which is the OTHER hand's gun: its hold latched whatever that arm played when it started (an equip, the frames after a wheel)
    // and never followed it while the player walked (the S5 crossbow copy with spread fingers in the run's burst frames); and the copy switched
    // clips, and keys, with the other hand's gun type and with every one-frame miss of the gun read, each switch a new hold.
    for(unsigned s=0;s<2;++s) if(in.dual_wield && copy_gun(s)) {t.mirror_native[s]=false;t.gun_clip[s]=true;t.hand_empty[s]=false;}
    // v1033 (user, headset v1032: the hand that put a body on the shoulder went slack with straight fingers and came back
    // only when the body was put down): a body the game carries takes no hand of the player's. The carry's arm clip must not
    // drive either hand's fingers (body::Inputs::hand_empty gates the procedural fingers in body_ik's finger_mode), so both
    // hands keep the player's grip and finger IK while a body is carried; the slots are empty and the sword is already
    // cleared for a body carry (hand_weapon_bits 0).
    // v1038: the left slot's gun keeps its own routing (the native-left hand = the non-melee hand); the carrying hand is the one cleared.
    {const bool gun_kept=(in.weapon_bits&1u)!=0;
     if(in.carrying_body) for(unsigned s=0;s<2;++s) if(!(gun_kept && s==rh)) {t.hand_empty[s]=true;t.mirror_native[s]=false;t.gun_clip[s]=false;}}
    // v1104/v1106: the type of the item each physical hand holds. A mod-owned copy's type is known from the loadout even
    // when the native hand snapshot is stale (`fresh` false), so it no longer reports 0 in a menu/load status; the native
    // gun hand takes the control-tick latch or the publisher's live item-record read.
    for(unsigned s=0;s<2;++s) {
        if(in.dual_wield && copy_gun(s)) t.weapon_type[s]=extra_type(s);
        else if(s==rh && in.left_gun) t.weapon_type[s]=static_cast<unsigned>(in.slot1_type);
        else if(s==mh && fresh && !(in.empty_bits&(1u<<s))) t.weapon_type[s]=3; // the sword/other authored item
        else t.weapon_type[s]=0;
    }
    // Which item each hand holds, for the remembered grip (body::Inputs::item_key): the sword's hand holds the sword.
    {std::uint64_t h=1469598103934665603ull;
     for(const char c:in.equip_name) {h^=static_cast<unsigned char>(c);h*=1099511628211ull;}
     const std::uint64_t sword_key=0x53574f5244ull,slot_key=in.equip_name.empty() || in.equip_name=="Empty"?0:h;
     t.item_key[1]=(arm_cross&1u)?slot_key:sword_key;t.item_key[0]=(arm_cross&2u)?sword_key:slot_key;}   // per arm: the cross says which item rides it
    // a copy hand remembers the copy's hold, not the slot-1 item's (v771). v1370: whenever it holds the copy, not only while routed to a clip: the
    // left hand's copy of the native gun's type had the SWORD key (the cross bit), shared by a pistol copy and a crossbow copy, and every change of
    // the route changed the key and so restarted the hold (v1360).
    for(unsigned s=0;s<2;++s) if(copy_gun(s)) t.item_key[s]=0x434f505900ull|static_cast<unsigned>(extra_of(s));
}
} // namespace d2vr::hand_route
