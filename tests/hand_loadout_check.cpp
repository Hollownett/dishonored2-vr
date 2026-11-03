#include "../src/hand_loadout.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using d2vr::HandLoadout;
    {   HandLoadout l;assert(l.melee==1 && l.ranged()==0 && !l.mirrored());
        assert(l.hand_bits(1)==1 && l.hand_bits(2)==2 && l.hand_bits(3)==3);    // the game's hands: slot bits are hand bits
        HandLoadout::In in{};assert(!l.update(in));
    }
    {   HandLoadout l;HandLoadout::In in{};in.sword_take={true,false};in.slot1_item=false; // the empty left hand takes the sword from its holster
        assert(l.update(in) && l.melee==0 && l.ranged()==1 && l.mirrored());
        assert(l.hand_bits(2)==1 && l.hand_bits(1)==2 && l.hand_bits(3)==3);
        assert(!l.update(in));                                                 // already there
    }
    {   HandLoadout l;HandLoadout::In in{};in.sword_take={true,false};in.gun_held=true; // the left hand holds the gun: it cannot take the sword
        assert(!l.update(in) && l.melee==1);
    }
    {   HandLoadout l;HandLoadout::In in{};in.gun_take={false,true};            // the right hand draws the gun from the hip
        assert(l.update(in) && l.melee==0);
        in={};in.gun_take={true,false};in.sword_held=true;                      // the left hand holds the sword now: it cannot take the gun
        assert(!l.update(in) && l.melee==0);
        in.sword_held=false;assert(l.update(in) && l.melee==1);                 // free again: back to the game's hands
        l.melee=0;in.sword_drawn=true;assert(!l.update(in) && l.melee==0);       // v785: the sword drawn in it, ungripped: still busy
    }
    {   HandLoadout l;HandLoadout::In in{};in.wheel_hand=1;in.sword_held=true;  // the right wheel: the slot-1 item to the right hand
        assert(l.update(in) && l.ranged()==1);
        in.wheel_hand=1;assert(!l.update(in));
        in.wheel_hand=0;assert(l.update(in) && l.ranged()==0);
    }
    {   // v773 (user, headset v772: "two pistols no longer work"): the left holds a pistol copy while the right drives slot 1 with a power;
        // the right taking the sword hands slot 1 to the left, which keeps its copy (a power is nothing in the hand). A visible slot-1 item
        // (gun, grenade, the Heart) still takes the hand.
        using E=HandLoadout::Extra;HandLoadout l;l.melee=0;l.extra[0]=E::pistol;
        HandLoadout::In in{};in.sword_take={false,true};in.slot1_item=false;
        assert(l.update(in) && l.melee==1 && l.ranged()==0 && l.extra[0]==E::pistol);
        // v784 (user, headset v793: "each hand is its own object"): a hand showing its slot-1 item (gun, grenade, the Heart) is busy, gripped or
        // not; it never takes the sword (headset v793: the picked-up pistol's hand took the sword back and the pistol went to the other hand's hip)
        l.melee=0;in.slot1_item=true;assert(!l.update(in) && l.melee==0 && l.extra[0]==E::pistol);
        HandLoadout k;k.melee=0;HandLoadout::In g{};g.sword_take={false,true};g.slot1_item=true;assert(!k.update(g) && k.ranged()==1);
    }
    {   using E=HandLoadout::Extra;HandLoadout l;                               // left pistol (native), right sword; the right wheel picks the crossbow
        HandLoadout::In in{};in.wheel_hand=1;assert(l.update(in) && l.ranged()==1);
        l.wheel_second(1,E::pistol);assert(l.extra[0]==E::pistol && l.extra[1]==E::none);   // the left keeps a copy of its pistol
        assert(l.hand_bits(1)==3);                                              // both hands hold a weapon
        in={};in.sword_take={true,false};assert(!l.update(in));                 // the left hand is busy with its copy
        l.drop(0);assert(l.extra[0]==E::none);
        assert(!l.pickup(0,E::pistol,true,false) && l.extra[0]==E::pistol);    // slot 1 is drawn (crossbow): the picked-up pistol is mod-owned
        l.drop(0);assert(l.pickup(0,E::pistol,false,false) && l.ranged()==0 && l.extra[0]==E::none); // slot 1 free: native, the left drives it
        assert(!l.pickup(1,E::guard_sword,true,false) && l.extra[1]==E::guard_sword); // a guard's weapon is always mod-owned
        l.wheel_second(0,E::none);assert(l.extra[1]==E::guard_sword);           // the wheel never takes the other hand's guard sword
    }
    {   // v963 (user, headset v962: "choosing the Heart makes the weapon in the other hand disappear"): the other hand's sword
        // (or its own copy) is not replaced by the displaced native gun; a free other hand still keeps it as a copy.
        using E=HandLoadout::Extra;HandLoadout l;                             // left native pistol, right sword; the right wheel picks the Heart
        HandLoadout::In in{};in.wheel_hand=1;assert(l.update(in) && l.melee==0 && l.ranged()==1);
        l.wheel_second(1,E::pistol,true);assert(l.extra[0]==E::none && l.extra[1]==E::none); // the left keeps the sword native, the pistol stays in the inventory
        HandLoadout k;HandLoadout::In k2{};k2.wheel_hand=1;assert(k.update(k2) && k.melee==0);
        k.wheel_second(1,E::pistol,false);assert(k.extra[0]==E::pistol && k.extra[1]==E::none); // a free hand gets the copy
    }
    {   // v759: a wheel route survives the other hand gripping its gun (headset v757: two pistols, crossbow + pistol never came)
        assert(!HandLoadout::wheel_route_lost(false,1,0,true,true));             // the right wheel while the left grips its pistol
        assert(!HandLoadout::wheel_route_lost(false,1,0,true,false));            // the left grips its gun after the commit: still the right wheel's item
        assert(HandLoadout::wheel_route_lost(false,0,0,true,false));             // the wheel's own hand took a gun from the hip meanwhile
        assert(!HandLoadout::wheel_route_lost(false,0,0,true,true));             // it already held one when its wheel committed
        assert(HandLoadout::wheel_route_lost(true,1,0,false,false));             // timed out
    }
    {   // v780 (user, headset v779): left pistol copy, Blink in the right hand (slot 1 rides it); the pistol is tossed to the right hand
        using E=HandLoadout::Extra;HandLoadout l;l.melee=0;l.extra[0]=E::pistol;
        l.drop(0);l.extra[1]=E::pistol;                                         // the right hand caught it (its copy now)
        assert(l.power_hand_over(1,true,true) && l.melee==1 && l.ranged()==0);  // Blink goes to the left hand, the right keeps the pistol
        assert(l.hand_bits(0)==2);                                              // only the right hand holds a weapon
        HandLoadout k;k.melee=0;k.extra[1]=E::pistol;
        assert(!k.power_hand_over(1,false,true) && k.melee==0);                 // slot 1 holds no power: nothing moves
        assert(!k.power_hand_over(1,true,false) && k.melee==0);                 // the other hand holds the sword: nothing moves
        k.extra[0]=E::crossbow;assert(!k.power_hand_over(1,true,true));         // the other hand holds its own weapon: nothing moves
        HandLoadout m;assert(!m.power_hand_over(1,true,true) && m.melee==1);   // the sword hand is not the power's hand
    }
    std::puts("hand_loadout_check ok");
    return 0;
}
