#include "../src/grab_indicator.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const TrackedPose head{{0,1.7,0},{}};
    const Vec3 object{.3,1.1,-1};
    const auto cue=grab_indicator_pose(object,head);assert(cue);
    assert(std::abs(length(cue->position-object)-.008)<1e-9);
    assert(length(cue->position-object)<.01); // Exact surface, not hand or wrist.
    assert(!grab_indicator_pose({0,1.7,-5},head));
    assert(!grab_indicator_pose(head.position,head));
    const auto image=grab_indicator_image();
    assert(image[3]==0 && image[(15*32+15)*4+3]==0);
    unsigned count{};for(std::size_t i=3;i<image.size();i+=4) if(image[i]>128) ++count;
    assert(count>60 && count<200);
    std::cout<<"PASS: small surface-local grab indicator with transparent centre/edges and bounded visibility\n";
}
