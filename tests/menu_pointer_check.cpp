#define NOMINMAX
#include "../src/menu_pointer.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const TrackedPose panel{{0,0,-1.6},{}};
    const auto hit=panel_pointer({{.2,.1,0},{}},panel,1.2,1.2);assert(hit);
    const auto marker=menu_pointer_pose(panel,*hit,1.2,1.2);
    assert(std::abs(marker.position.x-.2)<1e-9 && std::abs(marker.position.y-.1)<1e-9);
    assert(std::abs(marker.position.z+1.598)<1e-9);
    const Quaternion yaw{0,std::sin(.4),0,std::cos(.4)};
    const TrackedPose rotated_panel{{1,2,3},yaw};
    const auto rotated_marker=menu_pointer_pose(rotated_panel,{.75,.25},1.2,1.2);
    const auto expected=rotated_panel.position+rotated(yaw,{.3,.3,.002});
    assert(length(rotated_marker.position-expected)<1e-9);
    bool rejected=false;try {menu_pointer_pose(panel,{1.1,.5},1.2,1.2);} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    const auto idle=menu_pointer_image(false),pressed=menu_pointer_image(true);
    assert(idle[3]==0 && idle[(15*32+15)*4+3]==255 && idle!=pressed);
    std::cout<<"PASS: menu aim marker agrees with ray UV under translated/rotated panels, rejects invalid hits and has transparent corners\n";
}
