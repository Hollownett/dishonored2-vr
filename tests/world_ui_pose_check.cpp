#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/world_ui_pose.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const auto straight=world_ui_front_pose({{1,1.72,2},{0,0,0,1}});
    assert(straight.position.x==1 && straight.position.y==1.72 && std::abs(straight.position.z-.95)<1e-9);
    const auto turned=world_ui_front_pose({{1,1.72,2},{0,std::sqrt(.5),0,std::sqrt(.5)}});
    assert(std::abs(turned.position.x+.05)<1e-9 && turned.position.y==1.72 && std::abs(turned.position.z-2)<1e-9);
    const auto tilted=world_ui_front_pose({{1,1.72,2},{std::sin(.3),0,std::sin(.2),std::sqrt(1-std::sin(.3)*std::sin(.3)-std::sin(.2)*std::sin(.2))}});
    assert(tilted.position.y==1.72 && tilted.orientation.x==0 && tilted.orientation.z==0);
    assert(std::abs(std::hypot(tilted.position.x-1,tilted.position.z-2)-1.05)<1e-9);
    bool rejected=false;try {world_ui_front_pose({{0,NAN,0},{}});}catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
    rejected=false;try {world_ui_front_pose({{0,1.7,0},{}},0);}catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
    for(const Vec3 hand:{Vec3{.2,1.1,-.35},Vec3{-.3,1.4,-.2},Vec3{0,1.0,-.6}}) { // held note: above the hand, face to the eyes
        const Vec3 head{0,1.7,0};const auto note=world_ui_hand_pose(head,hand);
        assert(std::abs(note.position.y-(hand.y+.12))<1e-9 && note.position.x==hand.x && note.position.z==hand.z);
        const auto face=rotated(note.orientation,{0,0,1}),to_head=(head-note.position)*(1/length(head-note.position));
        assert(length(face-to_head)<1e-9);
    }
    std::cout<<"PASS: centered eye-height world UI, upright yaw, held note facing the eyes and invalid pose rejection\n";
}
