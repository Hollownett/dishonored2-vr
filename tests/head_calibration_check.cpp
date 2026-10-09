#include "../src/head_calibration.hpp"
#include "../src/xr_continuity.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace d2vr;
    const TrackedPose left{{-.032,1.7,0},{0,0,0,1}},right{{.032,1.7,0},{0,0,0,-1}};
    const auto centre=eye_centre_pose(left,right);
    assert(length(centre.position-Vec3{0,1.7,0})<1e-9 && std::abs(centre.orientation.w-1)<1e-9);
    const auto angle=.4;
    const TrackedPose canted_left{{-.032,1.7,0},{0,std::sin(-angle/2),0,std::cos(angle/2)}};
    const TrackedPose canted_right{{.032,1.7,0},{0,std::sin(angle/2),0,std::cos(angle/2)}};
    assert(std::abs(eye_centre_pose(canted_left,canted_right).orientation.y)<1e-9);
    HeadCalibration calibration;
    auto origin=calibration.get(centre,1,0x10000,0x20000,1,true);assert(origin && calibration.captures()==1);
    const TrackedPose moved{{.3,1.5,-.2},{0,std::sin(.5),0,std::cos(.5)}};
    assert(!calibration.get(moved,1,0x10000,0x20000,1,false));
    auto resumed=calibration.get(moved,1,0x10000,0x20000,1,true);
    assert(resumed && length(resumed->position-origin->position)<1e-9 && resumed->orientation.y==origin->orientation.y && calibration.captures()==1);
    for(unsigned i=0;i<100;++i) {
        calibration.get(moved,1,0x10000,0x20000,1,false);
        calibration.get(moved,1,0x10000,0x20000,1,true);
    }
    assert(calibration.captures()==1);
    resumed=calibration.get(moved,2,0x10000,0x20000,1,true);assert(resumed && calibration.captures()==2 && length(resumed->position-moved.position)<1e-9);
    assert(calibration.get(centre,2,0x10000,0x20000,2,true) && calibration.captures()==3);
    assert(calibration.get(centre,2,0x30000,0x40000,2,true) && calibration.captures()==4);
    assert(!calibration.get(centre,0,0x30000,0x40000,2,true));
    assert(!calibration.get(centre,2,0x30000,0x40000,-1,true));
    calibration.clear();assert(calibration.get(centre,2,0x30000,0x40000,2,true) && calibration.captures()==5);
    bool rejected=false;try {eye_centre_pose(left,{{0,0,0},{0,0,0,0}});} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    XrContinuity continuity;HeadCalibration persistent;
    continuity.advance(true,true,true,100);
    persistent.get(centre,continuity.reference_epoch(),0x10000,0x20000,1,true);
    continuity.advance(true,false,true,200);continuity.advance(true,true,true,300);
    auto returned=persistent.get(moved,continuity.reference_epoch(),0x10000,0x20000,1,true);
    assert(returned && persistent.captures()==1 && length(returned->position-centre.position)<1e-9);
    continuity.reference_change(500);continuity.advance(true,true,true,499);
    persistent.get(moved,continuity.reference_epoch(),0x10000,0x20000,1,true);assert(persistent.captures()==1);
    continuity.advance(true,true,true,500);persistent.get(moved,continuity.reference_epoch(),0x10000,0x20000,1,true);
    assert(persistent.captures()==2);
    std::cout<<"PASS: stable room calibration across pause/transient logic, explicit reference and native-owner resets, canted/antipodal eye centre and invalid pose rejection\n";
}
