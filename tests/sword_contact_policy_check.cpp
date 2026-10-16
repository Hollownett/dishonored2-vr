#include "../src/sword_contact_policy.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace d2vr;

namespace {
struct Contact {std::uint64_t tick;std::uint32_t victim;};
struct Outcome {StealthAct act{StealthAct::wait};std::uint64_t tick{};unsigned hits_applied{};};
// Drives the hold the way the game thread does: contacts arrive, service runs every 16 ms. native_from: tick the player FSM enters
// Assassination (0 = never). The old code applied an ordinary hit at every first contact (hits_applied counts those for comparison).
Outcome run(const std::vector<Contact>& contacts,bool native_offer,std::uint64_t native_from,bool unaware_until_noticed=false,std::uint64_t noticed_at=0) {
    StealthHold<int> hold;Outcome out;std::size_t next=0;
    for(std::uint64_t t=contacts.front().tick;t<contacts.front().tick+3000;t+=16) {
        while(next<contacts.size() && contacts[next].tick<=t) {hold.hold(0,contacts[next].victim,contacts[next].tick,native_offer);++next;}
        const bool noticed=unaware_until_noticed && t>=noticed_at;
        const auto act=hold.service(t,!noticed,true,native_from && t>=native_from,false,true);
        if(act!=StealthAct::wait) {out.act=act;out.tick=t;return out;}
    }
    return out;
}
}

// Recorded v560 headset run (PID51420, artifacts/runs/20261015T231649...-arm-v560/native-combat.csv + native-assassination-receipts.jsonl):
// guard 0x1aecfed1810 took contact_hit at 91360625, 91361234, 91361687; the native Assassination entered at 91361875. Every contact
// was an ordinary 10-damage hit before the Slash ever arbitrated, so the guard was already hurt/alerted.
void recorded_contacts_before_native_assassination() {
    const std::vector<Contact> guard{{91360625,1},{91361234,1},{91361687,1}};
    // old behaviour: first contact lands an ordinary hit at once
    // new, native kill never requested by the swing: the contact turns into the stealth kill after the native-offer window, no hit
    auto late=run(guard,true,91361875);
    assert(late.act==StealthAct::kill && late.tick-guard[0].tick>=700 && late.tick-guard[0].tick<716);
    // new, the contact itself requests the Slash press (VrCombat contact_attack): the native entry follows within one frame pair
    auto prompt=run(guard,true,91360625+32);
    assert(prompt.act==StealthAct::drop && prompt.tick<=91360625+48);
    // recorded second case: guard 0x1aecfed1fb0, contact_hit 91427093, native13 entry 91427234 (141 ms later): native wins, no mod damage
    const std::vector<Contact> second{{91427093,2}};
    auto native=run(second,true,91427234);
    assert(native.act==StealthAct::drop && native.tick-second[0].tick<=160);
}

void window_and_terminal_cases() {
    const std::vector<Contact> one{{1000,7}};
    auto no_offer=run(one,false,0);                     // sensor does not target him: short window, then the stealth kill
    assert(no_offer.act==StealthAct::kill && no_offer.tick-1000>=120 && no_offer.tick-1000<136);
    auto noticed=run(one,true,0,true,1100);             // he turned and noticed: ordinary hit immediately, never a silent kill
    assert(noticed.act==StealthAct::hit && noticed.tick<1120);
    StealthHold<int> hold;
    assert(hold.hold(1,7,1000,false) && hold.hold(2,7,1050,false) && !hold.hold(3,8,1060,false)); // same victim absorbed, other victim refused
    assert(hold.service(1060,true,true,false,false,true)==StealthAct::wait);
    assert(hold.service(1119,true,true,false,false,true)==StealthAct::wait);
    assert(hold.service(1120,true,true,false,false,true)==StealthAct::kill && !hold.pending());
    for(unsigned why=0;why<4;++why) {                   // dead, native takedown, choke, ownership lost: nothing is dealt
        StealthHold<int> h;h.hold(1,7,1000,true);
        assert(h.service(1010,true,why!=0,why==1,why==2,why!=3)==StealthAct::drop && !h.pending());
    }
    StealthHold<int> backwards;backwards.hold(1,7,1000,true);
    assert(backwards.service(900,true,true,false,false,true)==StealthAct::drop);
}

void stroke_gate() {
    SwordHitGate gate;
    assert(gate.open(5,1000,6.0));gate.commit(5,1000);
    assert(!gate.open(5,1100,6.0));                      // same stroke still sweeping the body
    gate.observe(0.4);                                   // stroke paused
    assert(!gate.open(5,1100,0.5));                      // not a new stroke yet
    assert(!gate.open(5,1120,6.0));                      // too soon after the hit
    assert(gate.open(5,1200,6.0));                       // second stroke 200 ms later: was dropped by the flat 450 ms rule
    assert(gate.open(6,1010,3.0));                       // another victim is never held back
    gate.commit(5,1200);
    assert(!gate.open(5,1300,6.0) && gate.open(5,1650,0.1)); // the old 450 ms rule still releases a settled blade
    // recorded v560 spacing on one guard (609 and 453 ms) stays open under both rules
    SwordHitGate recorded;recorded.commit(1,91360625);assert(recorded.open(1,91361234,4.1));recorded.commit(1,91361234);assert(recorded.open(1,91361687,4.1));
}

void live_beats_corpse() {
    assert(pick_contact_victim(0x10,false,0x20,0x30)==0x10);   // live NPC on the cast
    assert(pick_contact_victim(0x10,true,0x20,0x30)==0x20);    // corpse met first, a live guard on the blade: the guard
    assert(pick_contact_victim(0x10,true,0,0x30)==0x10);       // only the corpse: still chopped
    assert(pick_contact_victim(0,false,0x20,0x30)==0x20);
    assert(pick_contact_victim(0,false,0,0x30)==0x30);
    assert(pick_contact_victim(0,false,0,0)==0);
}

int main() {
    recorded_contacts_before_native_assassination();
    window_and_terminal_cases();
    stroke_gate();
    live_beats_corpse();
    std::puts("sword_contact_policy_check OK");
}
