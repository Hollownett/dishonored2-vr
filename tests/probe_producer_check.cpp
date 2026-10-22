#define NOMINMAX
// The probe slots are shared by the hand clamp, the held clamp and the safe release; every request carries its producer and an answer is only read by
// the producer whose request it answers.
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <cstdio>
using namespace d2vr;
using Aim=NativeInteractionAim;
namespace {
WorldTraceHit hit_at(double x) {WorldTraceHit h{};h.point={x,0,0};h.entity=0x77;return h;}
// What the game thread does in one scope: take the requests, answer them (every slot hits at x), record.
void scope(double x,std::uint64_t now) {
    const auto segments=Aim::take_weapon_requests();
    std::array<std::array<std::optional<WorldTraceHit>,Aim::slot_count>,2> hits{};
    for(unsigned side=0;side<2;++side) for(unsigned slot=0;slot<Aim::slot_count;++slot) if(segments[side][slot].valid) hits[side][slot]=hit_at(x);
    Aim::record_weapon_answers(segments,hits,now);
}
}
int main() {
    const std::uint64_t now=GetTickCount64();
    // the release probe reads its own answer
    Aim::post_weapon_segment(0,0,{0,0,0},{0,0,.1},.1,0x10,-1,0,Aim::producer_release);scope(1,now);
    assert(Aim::weapon_serial(0,0,Aim::producer_release)>0 && Aim::weapon_hit(0,0,now,Aim::producer_release)->point.x==1);
    // the hand clamp and the held clamp never read it
    assert(Aim::weapon_serial(0,0)==0 && !Aim::weapon_hit(0,0,now));
    assert(Aim::weapon_serial(0,0,Aim::producer_held)==0 && !Aim::weapon_hit(0,0,now,Aim::producer_held));
    assert(!Aim::held_hits(0,now)[0] && Aim::held_hits(0,now,Aim::producer_release)[0]);
    // another producer overwrites the release request before the game thread copies it: the answer is the hand clamp's, the release reads nothing
    Aim::post_weapon_segment(0,0,{0,0,0},{0,0,.1},.1,0x10,-1,0,Aim::producer_release);
    Aim::post_weapon_segment(0,0,{5,0,0},{5,0,.1},.05);   // the hand clamp's palm sweep
    scope(2,now);
    assert(!Aim::weapon_hit(0,0,now,Aim::producer_release) && Aim::weapon_serial(0,0,Aim::producer_release)==0);
    assert(Aim::weapon_hit(0,0,now)->point.x==2 && Aim::weapon_serial(0,0)>0);
    // the held sweeps are tagged held by default, and the other side's slot is untouched
    {
        std::array<ClampSweep,4> sweeps{};sweeps[0]={{0,0,0},{0,0,.1},.1,true,std::nullopt};
        Aim::post_held_sweeps(1,sweeps,0x20);scope(3,now);
        assert(Aim::held_hits(1,now)[0]->point.x==3 && !Aim::held_hits(1,now,Aim::producer_release)[0] && !Aim::weapon_hit(1,0,now));
    }
    std::puts("probe_producer_check OK");
}
