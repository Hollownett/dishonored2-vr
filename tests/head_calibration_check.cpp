#include "../src/head_calibration.hpp"
#include "../src/head_locomotion.hpp"
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
    {HeadCalibration h; // rotate_origin_keeps_identity
     const TrackedPose t{{.3,1.5,-.2},{0,std::sin(.25),0,std::cos(.25)}};
     h.rotate_origin(.3);assert(!h.get(t,1,0x10000,0x20000,1,false) && h.captures()==0); // no origin yet: no-op
     const auto o=*h.get(t,1,0x10000,0x20000,1,true);h.rotate_origin(.3);
     const auto r=h.get(moved,1,0x10000,0x20000,1,true);
     assert(r && h.captures()==1 && length(r->position-o.position)<1e-9);
     assert(std::fabs(tracked_yaw(r->orientation,o.orientation)-.3)<1e-9);
     h.rotate_origin(-.3);assert(std::fabs(tracked_yaw(h.get(moved,1,0x10000,0x20000,1,true)->orientation,o.orientation))<1e-9);
     h.rotate_origin(std::nan(""));assert(std::fabs(tracked_yaw(h.get(moved,1,0x10000,0x20000,1,true)->orientation,o.orientation))<1e-9); // non-finite ignored
     h.rotate_origin(.1);assert(!h.get(moved,2,0x10000,0x20000,1,true) || h.captures()==2);}
    {HeadCalibration h; // adopt_keeps_identity: a provisional (cutscene) origin taken as the room origin, no capture, no recapture
     const TrackedPose o{{.3,1.5,-.2},{0,std::sin(.4),0,std::cos(.4)}},head{{.6,1.4,.1},{0,std::sin(-.2),0,std::cos(-.2)}};
     assert(!h.holds(1,0x10000,0x20000,1) && h.adopt(o,1,0x10000,0x20000,1) && h.holds(1,0x10000,0x20000,1) && h.captures()==0);
     const auto r=h.get(head,1,0x10000,0x20000,1,true);
     assert(r && h.captures()==0 && length(r->position-o.position)<1e-12 && std::fabs(tracked_yaw(r->orientation,o.orientation))<1e-12);
     assert(!h.adopt({{0,0,0},{.2,0,0,.98}},1,0x10000,0x20000,1) && !h.adopt(o,0,0x10000,0x20000,1) && !h.adopt(o,1,0,0x20000,1) &&
            !h.adopt(o,1,0x10000,0x20000,-1) && !h.adopt({{std::nan(""),0,0},o.orientation},1,0x10000,0x20000,1));
     assert(length(h.get(head,1,0x10000,0x20000,1,true)->position-o.position)<1e-12); // refused adopts change nothing
     assert(h.get(head,2,0x10000,0x20000,1,true) && h.captures()==1);} // a new epoch still captures
    std::cout<<"PASS: stable room calibration across pause/transient logic, explicit reference and native-owner resets, canted/antipodal eye centre and invalid pose rejection\n";
}
