#include "../src/avatar_calibration.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    AvatarCalibration calibration;TrackedPose head{{0,1.7,0},{}};
    std::array<TrackedPose,2> hands{{{{-.8,1.48,0},{}},{{.8,1.48,0},{}}}};
    {
        AvatarCalibration hold;hold.begin(9,{1.5,.3});
        for(unsigned i=0;i<=40;++i) hold.observe(head,hands,3,true,false,i*25,9);
        assert(hold.state()==AvatarCalibrationState::grips_released && hold.progress()==0 && !hold.result(9));
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,1100+i*25,9);
        assert(std::abs(hold.progress()-.5)<1e-6 && !hold.result(9));
        assert(hold.observe(head,hands,3,true,false,1625,9)==AvatarCalibrationState::grips_released && hold.progress()==0);
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,1700+i*25,9);
        auto bad=hands;bad[0].position.y-=.2;
        assert(hold.observe(head,bad,3,true,true,2225,9)==AvatarCalibrationState::pose_invalid && hold.progress()==0);
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,2300+i*25,9);
        assert(hold.observe(head,hands,1,true,true,2825,9)==AvatarCalibrationState::tracking_lost && hold.progress()==0);
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,2900+i*25,9);
        auto moved=hands;moved[0].position.x-=.03;
        assert(hold.observe(head,moved,3,true,true,3425,9)==AvatarCalibrationState::pose_moving && hold.progress()==0);
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,3500+i*25,9);
        assert(hold.observe(head,hands,3,true,true,4225,9)==AvatarCalibrationState::pose_moving && hold.progress()==0);
        for(unsigned i=0;i<=40;++i) hold.observe(head,hands,3,true,true,4300+i*25,9);
        assert(hold.result(9) && hold.progress()==1);
        const auto accepted=hold.result(9);
        hold.begin(9,{1.5,.3});
        for(unsigned i=0;i<=20;++i) hold.observe(head,hands,3,true,true,5500+i*25,9);
        assert(hold.observe(head,hands,3,false,true,6025,9)==AvatarCalibrationState::tracking_lost && hold.progress()==0);
        assert(hold.result(9)->palm_span_metres==accepted->palm_span_metres);
        assert(hold.synchronize(9,false)==AvatarCalibrationState::ready);
        hold.begin(9,{1.5,.3});
        hold.observe(head,hands,3,true,true,6100,9);
        assert(hold.observe(head,hands,3,true,true,6125,10)==AvatarCalibrationState::reference_changed && hold.progress()==0 && !hold.result(9));
    }
    assert(calibration.observe(head,hands,3,true,true,0,1)==AvatarCalibrationState::idle);
    calibration.begin(1,{1.5,.3});
    for(unsigned i=0;i<=40;++i) calibration.observe(head,hands,3,true,true,i*25,1);
    auto result=calibration.result(1);assert(result && !result->standing_eye_height_metres);
    assert(std::abs(result->arm_scale-1.6/1.5)<1e-6 && std::abs(result->eye_to_shoulder_metres-.22)<1e-6);
    assert(!calibration.result(2));
    assert(calibration.observe(head,hands,3,true,true,1100,2)==AvatarCalibrationState::reference_changed);
    calibration.begin(2,{1.5,.3},.1);
    for(unsigned i=0;i<20;++i) calibration.observe(head,hands,3,true,true,i*25,2);
    assert(!calibration.result(2));
    calibration.observe(head,hands,1,true,true,500,2);assert(calibration.state()==AvatarCalibrationState::tracking_lost);
    for(unsigned i=0;i<=40;++i) calibration.observe(head,hands,3,true,true,1000+i*25,2);
    result=calibration.result(2);assert(result && result->standing_eye_height_metres && std::abs(*result->standing_eye_height_metres-1.6)<1e-6);
    const auto previous=result;
    calibration.begin(2,{1.5,.3});
    for(unsigned i=0;i<10;++i) calibration.observe(head,hands,3,true,true,3000+i*25,2);
    assert(calibration.state()==AvatarCalibrationState::hold_pose && calibration.result(2));
    assert(calibration.synchronize(2,false)==AvatarCalibrationState::ready);
    assert(calibration.result(2)->standing_eye_height_metres==previous->standing_eye_height_metres);
    // Reference-space changes must be visible even with the wizard closed.
    assert(calibration.synchronize(3,false)==AvatarCalibrationState::reference_changed && !calibration.result(2));
    calibration.begin(3,{1.5,.3});
    auto invalid=hands;std::swap(invalid[0],invalid[1]);
    assert(calibration.observe(head,invalid,3,true,true,0,3)==AvatarCalibrationState::pose_invalid);
    for(unsigned i=0;i<100;++i) {
        auto moving=hands;moving[0].position.x+=i%2?.05:0;
        calibration.observe(head,moving,3,true,true,i*25,3);
    }
    assert(!calibration.result(3));
    assert(calibration.synchronize(3,false)==AvatarCalibrationState::cancelled);
    for(unsigned i=0;i<=40;++i) calibration.observe(head,hands,3,true,true,4000+i*25,3);
    assert(calibration.state()==AvatarCalibrationState::cancelled && !calibration.result(3));
    calibration.invalidate_rig();assert(calibration.state()==AvatarCalibrationState::rig_changed);
    calibration.clear();assert(calibration.state()==AvatarCalibrationState::idle && !calibration.result(3));
    calibration.invalidate_rig();assert(calibration.state()==AvatarCalibrationState::idle);
    std::cout<<"PASS: explicit stable T-pose capture, measured dimensions, optional floor datum, tracking/motion rejection, visible epoch/rig invalidation and cancelled retry preservation\n";
}
