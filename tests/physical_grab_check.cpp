#include "../src/physical_grab.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    WorldTrackedPose hand{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},object=hand;object.position={.1,0,0};
    PhysicalGrab grab;
    assert(!grab.sample(hand,.9f,true,1,100));assert(!grab.bind(object));
    assert(grab.sample(hand,.9f,true,1,110));assert(!grab.pressed()); // Held grip cannot acquire on focus return.
    grab.sample(hand,0,true,1,120);grab.sample(hand,.9f,true,1,130);assert(grab.bind(object));
    assert(length(grab.target().position-object.position)<1e-9); // No pickup teleport.
    hand.position.x=.2;grab.sample(hand,.9f,true,1,180);const auto moving=grab.motor(object,true);
    assert(moving && moving->linear.x>0 && length(moving->linear)<=3.000001);
    hand.basis={{{0,1,0},{-1,0,0},{0,0,1}}};grab.sample(hand,.9f,true,1,190);
    const auto turned=grab.target();assert(length(turned.position-Vec3{.2,.1,0})<1e-9);
    assert(grab.motor(object,true)->angular.z>0);
    grab.sample(hand,0,true,1,200);const auto thrown=grab.motor(object,true);assert(thrown && thrown->release && thrown->linear.x>0 && length(thrown->linear)<=6);
    assert(!grab.held());object=hand;grab.sample(hand,.9f,true,1,210);assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,220));assert(grab.motor(object,false)->release);assert(!grab.pressed());
    grab.sample(hand,0,true,2,230);grab.sample(hand,.9f,true,2,240);object.position=hand.position+Vec3{.4,0,0};assert(!grab.bind(object));
    hand.position.x+=.15;grab.sample(hand,.9f,true,2,250);assert(grab.bind(object));
    hand.position.x+=.1;grab.sample(hand,.9f,true,2,260);
    object.position.x-=2;assert(grab.motor(object,true)->release); // Blocked carry releases.
    grab.sample(hand,0,true,2,270);grab.sample(hand,.9f,true,2,280);object=hand;assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,600));assert(grab.motor(object,false)->linear.x==0);
    const WorldTrackedPose identity{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    auto rotated=identity;
    for(unsigned axis=0;axis<3;++axis) {
        rotated=identity;for(unsigned i=0;i<3;++i) if(i!=axis) rotated.basis[i]=rotated.basis[i]*-1;
        const auto error=grab_rotation_error(identity,rotated);
        assert(finite(error) && std::abs(length(error)-std::acos(-1.))<1e-9);
        PhysicalGrab half_turn;half_turn.sample(identity,0,true,1,100);half_turn.sample(identity,0,true,1,110);
        half_turn.sample(identity,.9f,true,1,120);assert(half_turn.bind(identity));half_turn.sample(rotated,.9f,true,1,130);
        const auto motor=half_turn.motor(identity,true);assert(motor && !motor->release && std::abs(length(motor->angular)-5)<1e-9);
    }
    for(const double angle:{1e-7,.2,1.57,3.1415,-.2,-3.1415}) {
        rotated=identity;rotated.basis={{{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}}};
        assert(length(grab_rotation_error(identity,rotated)-Vec3{0,0,angle})<1e-8);
        assert(length(grab_rotation_error(rotated,identity)+Vec3{0,0,angle})<1e-8);
    }
    assert(length(grab_rotation_error(identity,identity))==0);
    // An occupied native hand (weapon/power/carried body) leaves the lane
    // unusable: the squeeze never acquires and a held prop is released.
    PhysicalGrab occupied;
    occupied.sample(identity,.9f,false,1,100);assert(!occupied.pressed() && !occupied.bind(identity));
    occupied.sample(identity,0,true,1,110);occupied.sample(identity,.9f,true,1,120);assert(occupied.bind(identity));
    occupied.sample(identity,.9f,true,1,130);
    const auto occupied_release=occupied.motor(identity,false);
    assert(occupied_release && occupied_release->release && !occupied.held() && length(occupied_release->linear)==0);
    std::cout<<"PASS: grip hysteresis/rearm, relative pose, bounded dynamic motor/throw, focus/epoch/stale/blocked release, occupied-hand refusal and release\n";
}
