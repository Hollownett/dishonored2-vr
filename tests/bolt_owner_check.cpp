// v1284 (plan 2026-11-13 section 4.4): who owns a crossbow/pistol bolt choice, as tables over the pure rules NativeRangedFire uses
// (ranged_fire::pick_route in wheel_pick, native_ammo in track_ammo's native branch, copy_ammo_want in its copy branch). The plan's regression
// list as rule sequences: native left + copy right, choices of either hand, put away / take again, a toss, the same item from the other hand,
// a type running out. (The load reset - NativeRangedFire::forget_owners clearing the per-instance map - acts on game memory: not here.)
#include "../src/native_ranged_fire.hpp"
#include <cassert>
#include <cstdio>

using namespace d2vr::ranged_fire;

int main() {
    {   // pick_route(the choosing hand holds a copy of the chosen weapon, slot-1 holder hand, choosing hand, native in slot 1)
        struct Row {bool copy;int holder,hand;bool in_slot1;PickRoute want;};
        const Row rows[]={
            {true,-1,0,false,PickRoute::copy_own},{true,1,0,true,PickRoute::copy_own},{true,0,0,true,PickRoute::copy_own},{true,0,1,true,PickRoute::copy_own},   // a copy: its own, always
            {false,1,0,true,PickRoute::same_item_keep},{false,0,1,true,PickRoute::same_item_keep},   // the native drawn by the OTHER hand: kept for this hand's copy to come
            {false,0,0,true,PickRoute::native_own},{false,1,1,true,PickRoute::native_own},           // the native in this hand: its own
            {false,1,0,false,PickRoute::native_own},{false,0,1,false,PickRoute::native_own},         // the native not in slot 1: equipped as chosen
            {false,-1,0,true,PickRoute::native_own},{false,-1,1,false,PickRoute::native_own},        // no holder known
        };
        for(const auto& r:rows) assert(pick_route(r.copy,r.holder,r.hand,r.in_slot1)==r.want);
        // exhaustive: a choice is never routed to the native while the choosing hand has its own copy, and the native drawn by the other hand
        // never takes this hand's choice
        for(int copy=0;copy<2;++copy) for(int holder=-1;holder<2;++holder) for(int hand=0;hand<2;++hand) for(int in=0;in<2;++in) {
            const auto r=pick_route(copy!=0,holder,hand,in!=0);
            if(copy) assert(r==PickRoute::copy_own);
            if(!copy && in && holder>=0 && holder!=hand) assert(r==PickRoute::same_item_keep);
            if(r==PickRoute::native_own) assert(!copy && !(in && holder>=0 && holder!=hand));
        }
    }
    {   // native_ammo(current is its own, own type known, own type's pool)
        assert(native_ammo(true,true,5)==NativeAmmo::keep && native_ammo(true,false,0)==NativeAmmo::keep);
        assert(native_ammo(false,false,5)==NativeAmmo::follow);                                   // no own known: the game's ammo becomes its own
        assert(native_ammo(false,true,0)==NativeAmmo::follow && native_ammo(false,true,-1)==NativeAmmo::follow);   // its type ran out: native_follows
        assert(native_ammo(false,true,1)==NativeAmmo::switch_back && native_ammo(false,true,40)==NativeAmmo::switch_back);   // moved off its own with bolts left
    }
    {   // copy_ammo_want(own, own valid for this weapon, this hand's last choice, the native's)
        assert(copy_ammo_want<int>(7,true,5,3)==7);   // its own, kept through tosses / catches / hand changes
        assert(copy_ammo_want<int>(7,false,5,3)==5);  // a stale own (not this weapon's ammo): the hand's choice
        assert(copy_ammo_want<int>(0,false,5,3)==5);  // a new instance: this hand's last choice
        assert(copy_ammo_want<int>(0,false,0,3)==3);  // no choice yet: the native's
    }
    {   // the plan's sequences, ammo as numbers (1 regular, 2 sleep, 3 incendiary); own[] per instance, choice[] per hand
        int native_own=1,copy_own=0,choice[2]={0,0};const int native_hand=0,copy_hand=1;
        // the copy (right) appears: no own, no choice -> the native's
        copy_own=copy_ammo_want<int>(copy_own,copy_own!=0,choice[copy_hand],native_own);assert(copy_own==1);
        // the right hand chooses sleep darts: its copy's own; the native (left) unchanged
        assert(pick_route(true,native_hand,copy_hand,true)==PickRoute::copy_own);copy_own=2;choice[copy_hand]=2;assert(native_own==1);
        // the left hand chooses incendiary: the native's own (it is the left's); the copy keeps sleep
        assert(pick_route(false,native_hand,native_hand,true)==PickRoute::native_own);native_own=3;choice[native_hand]=3;
        assert(copy_ammo_want<int>(copy_own,true,choice[copy_hand],native_own)==2);
        // the copy is put away and taken again (the same instance): sleep; tossed to the other hand: still sleep (the instance owns it)
        assert(copy_ammo_want<int>(copy_own,true,choice[native_hand],native_own)==2);
        // a NEW copy for the right hand (same item route: the native drawn by the left): the right's last choice, the native keeps its own
        assert(pick_route(false,native_hand,copy_hand,true)==PickRoute::same_item_keep);choice[copy_hand]=1;
        assert(copy_ammo_want<int>(0,false,choice[copy_hand],native_own)==1 && native_own==3);
        // incendiary runs out: the game switches the native, it follows; the copy is untouched
        assert(native_ammo(false,true,0)==NativeAmmo::follow);native_own=1;assert(copy_own==2);
    }
    std::puts("bolt_owner_check: ok");
    return 0;
}
