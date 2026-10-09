#include "../src/right_grip_owner.hpp"
#include "../src/controller_input.hpp"
#include "../src/physical_grab.hpp"
#include <cassert>
#include <iostream>
int main() {
    using Role=d2vr::RightGripOwner::Role;d2vr::RightGripOwner role;
    assert(role.filter(.9f,true,true)==0 && role.role()==Role::release);
    assert(role.filter(0,true,true)==0 && role.role()==Role::ready);
    assert(role.deciding(.4f,true));assert(role.filter(.4f,true,true)==0 && role.physical());
    assert(role.filter(.56f,true,false)==0 && role.physical());
    assert(role.filter(.9f,true,false)==0 && role.physical());
    role.revoke();assert(role.filter(.9f,true,true)==0 && !role.physical());
    role.filter(.2f,true,false);assert(role.filter(.4f,true,false)==.4f && role.role()==Role::native);
    assert(role.filter(.9f,true,true)==.9f && !role.physical()); // Contact appearing during block cannot steal it.
    assert(role.filter(.9f,false,true)==0);assert(role.filter(.9f,true,true)==0);
    role={};assert(role.filter(.9f,true,true)==0); // Owner/epoch invalidation requires the same fresh release.
    role.filter(0,true,false);assert(role.filter(.4f,true,true)==0 && role.physical());
    // An occupied native hand (weapon, power or carried body) revokes any
    // latched physical role and returns the raw squeeze for the native RB.
    assert(role.filter(.9f,true,true,true)==.9f && role.role()==Role::release && !role.physical());
    role.filter(0,true,true);assert(role.filter(.4f,true,true,true)==.4f && !role.physical()); // Contact cannot latch while occupied.
    // Native RB is visible for the native press, and absent throughout reserved physical squeeze.
    d2vr::PadComposer pad;d2vr::ControllerInput input{};input.active=true;
    pad.compose(input,true,1);role.filter(0,true,false);
    input.right_grip=role.filter(.9f,true,false);
    assert(pad.compose(input,true,2).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
    input.right_grip=role.filter(0,true,false);pad.compose(input,true,3);
    input.right_grip=role.filter(.56f,true,true);
    assert(!(pad.compose(input,true,4).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    input.right_grip=role.filter(.9f,true,true);
    assert(!(pad.compose(input,true,5).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    // Occupied: the raw squeeze still reaches the native right shoulder, even
    // without gameplay focus, and never reserves the physical role.
    input.right_grip=role.filter(.9f,false,true,true);
    assert(input.right_grip==.9f && !role.physical() && (pad.compose(input,true,6).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    // Pending normal release retains the measured velocity; a focus-loss
    // release passes false and must produce zero velocity instead.
    d2vr::PhysicalGrab grab;d2vr::WorldTrackedPose pose{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    grab.sample(pose,0,true,1,100);grab.sample(pose,0,true,1,110);
    grab.sample(pose,.9f,true,1,120);assert(grab.bind(pose));
    pose.position.x=.02;grab.sample(pose,.9f,true,1,140);
    assert(d2vr::length(grab.release(true).linear)>0);
    assert(d2vr::length(grab.release(false).linear)==0);
    std::cout<<"PASS: pre-RB reservation, latched native/physical choice, contact change, menus/focus, occupied-hand revoke, fresh release and native pad mapping\n";
}
