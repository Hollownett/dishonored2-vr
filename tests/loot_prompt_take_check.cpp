// Hands plan Task 4, ruling H14: the prompt equals the take. A hand's remembered loot prompt (the 500 ms held grace) survives that
// hand's own routed cycle only while the cycle still selects it; non-loot targets and a hand with nothing held keep the old behaviour.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/selection_policy.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    constexpr std::uintptr_t coin=0x1000,shelf=0x2000;
    // the hand's cycle still selects the coin: the remembered prompt stays
    assert(held_prompt_survives_cycle(coin,true,coin));
    // the cycle selected nothing (LOS blocked) or another owner: a lingering loot prompt would show a coin the squeeze cannot take
    assert(!held_prompt_survives_cycle(coin,true,0));
    assert(!held_prompt_survives_cycle(coin,true,shelf));
    // non-loot (a door, a usable) keeps the old 500 ms grace: the prompt's anchor across a routing flip
    assert(held_prompt_survives_cycle(shelf,false,0));
    assert(held_prompt_survives_cycle(shelf,false,coin));
    // nothing held: nothing to drop
    assert(held_prompt_survives_cycle(0,true,0) && held_prompt_survives_cycle(0,false,coin));
    std::cout<<"PASS: a loot prompt survives only its hand's selecting cycle; non-loot and empty holds keep the grace\n";
}
