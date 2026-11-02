#include "../src/ranged_aim_gate.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
static RangedAimInputs free_hand() {
    RangedAimInputs in{};in.focused=in.action_synced=in.pose_valid=in.controller_active=in.holster_clear=in.swing_clear=true;return in;
}
int main() {
    auto gate=ranged_aim_gate(free_hand());assert(gate.shots && gate.reticle);
    // The user attacks (right trigger) or grips (left grip): shots and powers stop, the crosshair stays.
    for(float trigger: {.3f,.5f,1.f}) {auto in=free_hand();in.right_trigger=trigger;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    for(float grip: {.5f,.8f,1.f}) {auto in=free_hand();in.left_grip=grip;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.right_trigger=.29f;in.left_grip=.49f;gate=ranged_aim_gate(in);assert(gate.shots && gate.reticle);}
    // v776 (headset v775: a crossbow held by its squeezed grip shot from the camera, 6 of 6): the grip that holds the hand's weapon (a fist round a grenade or mine,
    // a drawn gun in dual wield) is no Use or grab, so it never stops a shot; every other rule still does, and a bare squeezed grip still stops it.
    for(float grip: {.5f,.8f,1.f}) {auto in=free_hand();in.left_grip=grip;in.grip_holds_weapon=true;gate=ranged_aim_gate(in);assert(gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;in.grip_holds_weapon=true;in.swing_tip=true;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;in.grip_holds_weapon=true;in.swing_clear=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;in.grip_holds_weapon=true;in.holster_clear=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;in.grip_holds_weapon=true;in.authored_camera=true;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.left_grip=1.f;in.grip_holds_weapon=true;in.right_trigger=.5f;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    // A sword swing, the holster window and a swing cool-down also keep only the crosshair.
    {auto in=free_hand();in.swing_tip=true;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.holster_clear=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.swing_clear=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    // An inactive controller / lost focus / failed action sync stops shots; the crosshair needs only a valid pose (as it did).
    {auto in=free_hand();in.controller_active=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.focused=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    {auto in=free_hand();in.action_synced=false;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    // No aim pose: neither. A game-owned view: no crosshair; a head-owned action camera: no shots.
    {auto in=free_hand();in.pose_valid=false;gate=ranged_aim_gate(in);assert(!gate.shots && !gate.reticle);}
    {auto in=free_hand();in.authored_view=true;in.authored_camera=true;gate=ranged_aim_gate(in);assert(!gate.shots && !gate.reticle);}
    {auto in=free_hand();in.authored_camera=true;gate=ranged_aim_gate(in);assert(!gate.shots && gate.reticle);}
    std::cout<<"ranged_aim_gate_check: passed\n";
}
