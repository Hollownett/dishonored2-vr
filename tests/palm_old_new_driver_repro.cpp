#include "../src/full_body_driver.hpp"
#include <cstdio>
#include <cstring>
using namespace d2vr;
using namespace d2vr::fullbody;
int main(int argc,char** argv) {
    const bool old=argc>1 && std::strcmp(argv[1],"old")==0;
    Driver driver;
    const auto body_skeleton=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
    Tracking tracking;tracking.eye={body_skeleton.bind[body_skeleton.find("camera_jnt")].q,body_skeleton.bind[body_skeleton.find("camera_jnt")].p};tracking.epoch=1;
    std::array<Transform,20> native_body{};
    for(unsigned index=0;index<20;++index)native_body[index]=to_transform(body_skeleton.bind[index]);
    std::array<Transform,69> native_arm[2];Transform palms[2];
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    for(unsigned side=0;side<2;++side) {
        const auto& arm_skeleton=side?right:left;const std::string prefix=side?"right":"left";
        for(unsigned index=0;index<69;++index)native_arm[side][index]=to_transform(arm_skeleton.bind[index]);
        std::array<Transform,4> knuckles;const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned index=0;index<4;++index)knuckles[index]=to_transform(arm_skeleton.bind[arm_skeleton.find(prefix+"_"+fingers[index]+"_02_jnt")]);
        palms[side]=rig::hand_bind_grip(native_arm[side][arm_skeleton.find(prefix+"_wrist_jnt")],knuckles);
        tracking.grip[side]=body::mul(body_skeleton.bind[body_skeleton.find("center_spine03_jnt")],to_xf(palms[side]));
        tracking.grip_valid[side]=true;
        (void)driver.arm(side,native_arm[side],identity);
    }
    tracking.tick=1000;tracking.source_tick=990;tracking.seconds=1.;tracking.pose_epoch=1;tracking.reference_epoch=2;
    tracking.mapping_revision=3;tracking.tracking_revision=1;tracking.tracked_hands=3;
    driver.note_root(identity);driver.tune(0,1);driver.publish(tracking,{});
    if(!driver.body(native_body,identity,71) || !driver.arm_frozen(0,native_arm[0],identity,1000,71))return 3;
    const auto diagnostic=driver.diagnostic();const auto& sample=diagnostic.palm_source[0];
    if(!sample.valid)return 4;
    auto current=sample.grip;current.position.x+=.3;
    const auto expected=current.position+(sample.palm.position-sample.grip.position);
    auto actual=current;
    if(old) {
        const auto old_offset=diagnostic.palm[0].p-current.position;
        actual.position=actual.position+old_offset;
    } else {
        DrawnPalmResidual cache;cache.note(sample,sample.context,1010);actual=cache.frame(current,1010);
    }
    const double error=length(actual.position-expected);
    std::printf("adapter=%s frame=%llu revision=%llu source_tick=%llu current_x=%.12f expected_x=%.12f actual_x=%.12f error=%.12f result=%s\n",
        old?"old":"new",static_cast<unsigned long long>(sample.frame),static_cast<unsigned long long>(sample.tracking_revision),
        static_cast<unsigned long long>(sample.source_tick),current.position.x,expected.x,actual.position.x,error,error<1e-9?"PASS":"FAIL");
    return error<1e-9?0:2;
}
