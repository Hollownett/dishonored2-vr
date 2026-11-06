#include "hand_feel.hpp"
#include <cassert>
#include <cstdio>
using namespace d2vr;
int main() {
    // touch: silent when resting, stronger for faster/heavier contacts, bounded
    assert(hand_haptic(HandFeelEvent::touch,.1,10).amplitude==0);
    assert(hand_haptic(HandFeelEvent::touch,2,10).amplitude>hand_haptic(HandFeelEvent::touch,.5,10).amplitude);
    assert(hand_haptic(HandFeelEvent::touch,2,50).amplitude>hand_haptic(HandFeelEvent::touch,2,1).amplitude);
    assert(hand_haptic(HandFeelEvent::hit,100,100).amplitude<=1.f);
    assert(hand_haptic(HandFeelEvent::grab,0,75).amplitude<=.8f);
    // weight: light things free, heavy capped at 75%
    assert(held_move_scale(0)==1.f && held_move_scale(2.9)==1.f);
    assert(held_move_scale(12)<1.f && held_move_scale(12)>.9f);
    assert(std::abs(held_move_scale(1000)-.25f)<1e-6f);
    // hit gate: threshold, one event per swing, rearms after cooldown
    HandHitGate g;
    assert(g.fire(2.,0)==0);
    float s=g.fire(5.,1.);assert(s>0 && s<1);
    assert(g.fire(9.,1.1)==0);
    assert(g.fire(9.,1.31)==1.f);
    // prop impulse: light props fly, bodies barely move, capped
    assert(hit_prop_speed(5,.5)>hit_prop_speed(5,75));
    assert(hit_prop_speed(5,75)<.6);
    assert(hit_prop_speed(50,.1)<=8.);
    std::puts("hand_feel_check: all pass");
}
