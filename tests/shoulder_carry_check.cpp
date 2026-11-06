#include "../src/shoulder_carry.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
namespace {
ShoulderCarryIn in(bool armed,std::uintptr_t body,std::uintptr_t target,bool loot,bool carrying,std::uint64_t now,std::uint64_t armed_ms) {
    ShoulderCarryIn i;i.enabled=true;i.armed=armed;i.body=body;i.target=target;i.loot=loot;i.carrying=carrying;i.now=now;i.armed_ms=armed_ms;return i;
}
}
int main() {
    {   // A body whose Loot action is still offered is never pressed: the game's Use on that body is its Loot, not its Carry.
        // The physical shoulder keeps the body and the status counts the reason; looting it first clears the offer.
        ShoulderCarryTrigger t;
        const auto o=t.update(in(true,0x10,0x10,true,false,1000,1000));
        assert(!o.press && o.loot_blocked && o.presses==0);
        assert(!t.update(in(true,0x10,0x10,true,false,1500,1000)).loot_blocked);   // the notice is an edge, counted once
        assert(!t.update(in(true,0x10,0x10,true,false,2500,1000)).press);          // and never becomes a press
    }
    {   // Knob off (build/no-shoulder-carry, build/no-shoulder-native) or the legacy corpse carry: nothing at all.
        ShoulderCarryTrigger t;auto i=in(true,0x10,0x10,false,false,1000,1000);i.enabled=false;
        const auto o=t.update(i);assert(!o.press && !o.started && !o.gave_up && !o.loot_blocked);
    }
    {   // Not a placement (the dock is not anchored, no body or no placement tick): nothing, and no give-up after it ends.
        ShoulderCarryTrigger t;
        assert(!t.update(in(false,0x10,0x10,false,false,1000,1000)).press);
        assert(!t.update(in(true,0,0,false,false,1000,1000)).press);
        assert(!t.update(in(true,0x10,0x10,false,false,1000,0)).press);
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        assert(!t.update(in(false,0x10,0x10,false,false,1100,1000)).press);
        assert(!t.update(in(false,0x10,0x10,false,false,2000,1000)).gave_up);
    }
    {   // The hand is not yet on the body: no press; when the window closes the physical shoulder keeps it (gave_up once).
        ShoulderCarryTrigger t;
        assert(!t.update(in(true,0x10,0,false,false,1000,1000)).press);
        assert(!t.update(in(true,0x10,0,false,false,2000,1000)).press);
        assert(t.update(in(true,0x10,0,false,false,1000+shoulder_carry_window_ms,1000)).gave_up);
        assert(!t.update(in(true,0x10,0,false,false,1000+shoulder_carry_window_ms,1000)).gave_up);
    }
    {   // The hold is a level, not a tap: the game's carry is its HELD Use on the body (v613 measured ~0.55 s). One episode,
        // held frame after frame while the hand's target stays the body.
        ShoulderCarryTrigger t;
        auto a=t.update(in(true,0x10,0x10,false,false,1000,1000));assert(a.press && a.presses==1);
        auto b=t.update(in(true,0x10,0x10,false,false,1050,1000));assert(b.press && b.presses==1);
        auto c=t.update(in(true,0x10,0x10,false,false,1100,1000));assert(c.press && c.presses==1);
        auto d=t.update(in(true,0x10,0x10,false,false,1200,1000));assert(d.press && d.presses==1);
    }
    {   // A lost target keeps the hold for a round trip: taking the game's interaction query for this hand invalidates the
        // selection, so the hand's own target reads empty for a frame or two after the press begins. Another body ends it at once.
        ShoulderCarryTrigger t;
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        assert(t.update(in(true,0x10,0,false,false,1100,1000)).press);        // empty inside the grace: still held
        assert(t.update(in(true,0x10,0x10,false,false,1200,1000)).press);     // and back on the body
        assert(!t.update(in(true,0x10,0x11,false,false,1250,1000)).press);    // another object under the hand: the hold ends
        assert(!t.update(in(true,0x10,0x10,false,false,1300,1000)).press);    // the retry gap
        const auto again=t.update(in(true,0x10,0x10,false,false,1250+shoulder_carry_retry_ms,1000));
        assert(again.press && again.presses==2);                               // a second episode
    }
    {   // A started hold is kept for the engine's own latency (the carry needs the Use held; the locked query answers empty for a
        // round trip), and it ends once that floor has passed; the retry gap follows.
        ShoulderCarryTrigger t;
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        assert(t.update(in(true,0x10,0,false,false,1000+shoulder_carry_target_grace_ms,1000)).press);   // empty, inside the hold floor
        assert(t.update(in(true,0x10,0,false,false,1000+shoulder_carry_min_hold_ms-1,1000)).press);     // still inside it
        assert(!t.update(in(true,0x10,0,false,false,1000+shoulder_carry_min_hold_ms,1000)).press);      // the floor has passed: the hold ends
        assert(!t.update(in(true,0x10,0x10,false,false,1000+shoulder_carry_min_hold_ms+50,1000)).press); // the retry gap
    }
    {   // The game took the body: started once; no further press or give-up for that placement (the dock yields to the carry).
        ShoulderCarryTrigger t;
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        const auto o=t.update(in(true,0x10,0x10,false,true,1050,1000));
        assert(o.started && !o.press && !o.gave_up);
        assert(!t.update(in(true,0x10,0x10,false,true,1400,1000)).started);   // once
        assert(!t.update(in(true,0x10,0x10,false,true,1600,1000)).press);
    }
    {   // A carry that was already running when the body was placed is not the handoff (the game's own carry: no started,
        // no press), so the caller's auto-drop keeps its existing behaviour.
        ShoulderCarryTrigger t;
        assert(!t.update(in(true,0x10,0x10,false,true,1000,1000)).started);
        assert(!t.update(in(true,0x10,0x10,false,true,1300,1000)).press);
    }
    {   // A new placement restarts the attempt: another body, or the same one put on the shoulder again.
        ShoulderCarryTrigger t;
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        const auto second=t.update(in(true,0x11,0x11,false,false,2000,2000));
        assert(second.press && second.presses==1);
        const auto third=t.update(in(true,0x10,0x10,false,false,3000,3000));
        assert(third.press && third.presses==1);
    }
    {   // The window is bounded: the body is never held in the air waiting for the game indefinitely, and the hold is released.
        ShoulderCarryTrigger t;
        assert(t.update(in(true,0x10,0x10,false,false,1000,1000)).press);
        const auto o=t.update(in(true,0x10,0x10,false,false,1000+shoulder_carry_window_ms,1000));
        assert(!o.press && o.gave_up);
        assert(!t.update(in(true,0x10,0x10,false,false,1000+shoulder_carry_window_ms,1000)).gave_up);
    }
    {   // v1041: the game's carry aborts ("Both hands have not been unequipped") unless both hands are EMPTY, so CarryCorpse's lock-and-empty
        // (requester 45; 0x21, or 0x81 after a grapple) keeps its unequip and only loses the lock. Nothing else is touched.
        assert(carry_left_equip_code(0x21,45,false)==0x20 && carry_left_equip_code(0x81,45,false)==0x80);   // never 0x24: a kept item aborts the carry
        for(const auto c:{0x21u,0x81u}) assert((carry_left_equip_code(c,45,false)&0x0cu)==0);                 // no draw bits: the hand ends empty
        assert(carry_left_equip_code(0x21,49,false)==0x21);     // HoldObject's request is the v789 hook's, not this one
        assert(carry_left_equip_code(0x06,45,false)==0x06);     // the carry's own unlock + restore at its end stays
        assert(carry_left_equip_code(0x04,45,false)==0x04);     // a draw is never rewritten
        assert(carry_left_equip_code(0x21,45,true)==0x21);      // an explicit item (the wheel) is the player's
    }
    // The settled stage is the carried one the game holds until the drop (sim v1043 journal: 1 -> 2 -> 3); 4 never came, so v1041's redraw never fired.
    static_assert(carry_corpse_settled_stage==3,"CarryCorpse settles at stage 3");
    {   // CarryLeftRedraw: the item the carry put away is drawn once the carry has settled
        CarryLeftRedraw r;
        const auto in=[](bool armed,bool carrying,bool settled,bool empty,std::uint64_t now) {CarryRedrawIn i;i.armed=armed;i.carrying=carrying;i.settled=settled;i.left_empty=empty;i.now=now;return i;};
        assert(!r.update(in(false,true,true,true,100)));                       // not armed: nothing
        assert(!r.update(in(true,true,false,true,1000)));                      // armed while the carry is still entering
        assert(!r.update(in(false,true,false,true,1100)));                     // stage 2/3: wait
        assert(r.update(in(false,true,true,true,1200)) && r.draws()==1);       // settled: one draw
        assert(!r.update(in(false,true,true,true,1300)));                      // retry gap
        assert(r.update(in(false,true,true,true,1500)) && r.draws()==2);       // the draw did not land: again
        assert(!r.update(in(false,true,true,false,1600)));                     // the item is back: done
        assert(!r.update(in(false,true,true,true,5000)));                      // ...and stays done
    }
    {   CarryLeftRedraw r;const auto in=[](bool armed,bool carrying,bool settled,bool empty,std::uint64_t now) {CarryRedrawIn i;i.armed=armed;i.carrying=carrying;i.settled=settled;i.left_empty=empty;i.now=now;return i;};
        assert(!r.update(in(true,true,false,true,1000)));
        assert(!r.update(in(false,false,false,true,1100)));                    // the game aborted the carry: nothing to give back
        assert(!r.update(in(false,true,true,true,1200)));                      // a later carry is not this one
        r.update(in(true,true,true,true,2000));r.update(in(false,true,true,true,2300));r.update(in(false,true,true,true,2600));
        assert(r.draws()==3 && !r.update(in(false,true,true,true,3000)));      // at most three draws
        assert(!r.update(in(true,false,false,true,9000)) );                    // armed, the carry never appears
        assert(!r.update(in(false,false,false,true,9000+CarryLeftRedraw::window_ms+1)) && !r.update(in(false,true,true,true,9000+CarryLeftRedraw::window_ms+2)));
    }
    std::cout<<"PASS: shoulder carry handoff (Loot blocks the press, held Use, retry gap, bounded window, started once, give-up, new placement)\n";
    return 0;
}
