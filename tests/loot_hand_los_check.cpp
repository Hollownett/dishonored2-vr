// Hands plan Task 4 fix round 1, ruling H16 (user 2026-10-25): loot line of sight is judged from the hand. The gate
// loot_los_hand_origin(takeable, hand_mode, clamped_palm, point) picks the clamped palm for loot in hand mode, else the eye (nullopt).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/selection_policy.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace d2vr;
    const Vec3 coin{-25.657,46.081,34.003};                 // the cot coin's hit point (T100910)
    const Vec3 clamped{-25.60,46.08,34.10};                  // the drawn palm, held out of the cot by the clamp
    // loot + hand mode + a clamped palm -> the palm is the origin
    const auto o=loot_los_hand_origin(true,true,clamped,coin);
    assert(o && length(*o-clamped)<1e-12);
    // non-loot (a door, a lever, a note) keeps the eye
    assert(!loot_los_hand_origin(false,true,clamped,coin));
    // gaze / reach mode keeps the eye
    assert(!loot_los_hand_origin(true,false,clamped,coin));
    // no clamped palm this cycle (no clamp, a yield, a re-acquire, a released side, stale) keeps the eye
    assert(!loot_los_hand_origin(true,true,std::nullopt,coin));
    // the raw controller pushed through a wall is never an input: with the clamped palm outside the wall the origin is the clamped palm,
    // whatever the controller does (the gate has no raw-controller parameter; dxgi_probe publishes the clamp's drawn palm only)
    const Vec3 wall_clamped{0.00,0,1.0},raw_through_wall{0.30,0,1.0},behind_wall{0.40,0,1.0};
    const auto w=loot_los_hand_origin(true,true,wall_clamped,behind_wall);
    assert(w && length(*w-wall_clamped)<1e-12 && length(*w-raw_through_wall)>.29);
    // a palm farther than 1 m from the hit point is not this hand's reach: the eye
    assert(!loot_los_hand_origin(true,true,Vec3{coin.x+1.01,coin.y,coin.z},coin));
    assert(loot_los_hand_origin(true,true,Vec3{coin.x+.99,coin.y,coin.z},coin));
    // non-finite inputs: the eye
    assert(!loot_los_hand_origin(true,true,Vec3{NAN,0,0},coin));
    assert(!loot_los_hand_origin(true,true,clamped,Vec3{0,INFINITY,0}));
    std::cout<<"PASS: loot LOS from the clamped palm in hand mode; non-loot, gaze, no clamped palm, far or non-finite keep the eye\n";
}
