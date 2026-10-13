#define NOMINMAX
#include "../src/wrist_hud_image.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const TrackedPose head{{0,1.7,0},{}};
    TrackedPose hand{{-.1,1.5,-.45},{}};
    assert(wrist_hud_pose(hand,head));
    hand.position.y=.8;assert(!wrist_hud_pose(hand,head));
    hand.position={1.5,1.7,0};assert(!wrist_hud_pose(hand,head));
    hand.position={0,1.65,-.05};assert(!wrist_hud_pose(hand,head));
    hand.position={-.1,1.5,-.45};hand.orientation={0,std::sin(.7),0,std::cos(.7)};assert(wrist_hud_pose(hand,head));
    hand.orientation={};hand.position={-.5,1.5,-.1};assert(!wrist_hud_pose(hand,head)); // outside glance cone
    { // Back of the wrist only: left hand (dorsal -X) turned so -X faces the head shows; palm toward the head hides.
        TrackedPose h{{-.1,1.5,-.45},{0,std::sin(.7854),0,std::cos(.7854)}}; // yaw +90: grip -X now points back toward +Z (the head)
        assert(wrist_hud_pose(h,head,-1) && !wrist_hud_pose(h,head,1));
    }
    hand.position={-.1,1.5,-.45};{const auto p=wrist_hud_pose(hand,head);assert(p && p->position.z>hand.position.z+.05);normalized(p->orientation);} // behind wrist, on forearm
    WristHudSnapshot snapshot{};snapshot.health_valid=snapshot.mana_valid=snapshot.ammo_valid=true;
    snapshot.health_fill=.75;snapshot.mana_fill=.4;snapshot.ammo_count=12;snapshot.loaded_count=1;
    const auto right=wrist_hud_image(snapshot,1),left=wrist_hud_image(snapshot,0);
    assert(right.size()==512*192*4 && left.size()==right.size() && left!=right);
    assert(right[3]==0 && right[(100*512+100)*4+3]==242);
    snapshot.health_fill=.2;assert(wrist_hud_image(snapshot,1)!=right);
    snapshot.ammo_count=11;assert(wrist_hud_image(snapshot,0)!=left);
    std::cout<<"PASS: watch gesture gates, two wrist layouts, native-value changes and transparent card edges\n";
}
