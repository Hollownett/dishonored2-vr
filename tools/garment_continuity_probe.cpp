#include "../src/full_body_driver.hpp"
#include "../src/native_torso_fit.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);
    using namespace d2vr;using namespace fullbody;
    const auto b=skeleton(data::kBody),l=skeleton(data::kLeftArm),r=skeleton(data::kRightArm);
    const auto chest=b.bind[b.find("center_spine03_jnt")];
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    Tracking t;t.epoch=1;t.tick=1000;t.eye=b.bind[b.find("camera_jnt")];
    std::array<Transform,20> native{};std::array<Transform,69> arms[2];
    Vec3 palm[2],shoulder[2];double reaches[2];
    for(unsigned j=0;j<20;++j)native[j]=to_transform(b.bind[j]);
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?r:l;const std::string prefix=side?"right":"left";
        for(unsigned j=0;j<69;++j)arms[side][j]=to_transform(a.bind[j]);
        std::array<Transform,4> knuckles;const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned j=0;j<4;++j)knuckles[j]=arms[side][a.find(prefix+"_"+fingers[j]+"_02_jnt")];
        const auto grip=to_xf(rig::hand_bind_grip(arms[side][a.find(prefix+"_wrist_jnt")],knuckles));
        t.grip[side]=body::mul(chest,grip);t.grip_valid[side]=true;palm[side]=t.grip[side].p;
        shoulder[side]=body::mul(chest,a.bind[a.find(prefix+"_arm_jnt")]).p;
        reaches[side]=length(palm[side]-shoulder[side]);
    }
    AvatarCalibrationResult fit{1,length(palm[0]-palm[1]),length(shoulder[0]-shoulder[1]),.148397,1,{}};

    const double profiles[][5]={{1,1,1,1,1},{1,1.15,1.1,.95,1.05},{1.15,1.25,1.15,1.05,.95},{.95,1.3,1.05,.95,.9},{1,1.4,1,.9,1.1}};
    std::ofstream output(argv[1]);assert(output);output.precision(17);output<<"[";
    bool comma=false;
    for(unsigned character=0;character<2;++character)for(unsigned profile=0;profile<5;++profile)for(unsigned fitted=0;fitted<2;++fitted)for(unsigned posture=0;posture<3;++posture) {
        const auto& values=profiles[profile];const std::string name=character?"corvo":"emily";
        const auto width=native_torso_width("models/characters/player/"+name+"/body.md6",character?10391:7951);assert(width);
        auto measured=fit;measured.shoulder_span_measured=measured.torso_width_measured=measured.arm_lengths_measured=true;
        measured.standing_eye_height_metres=t.eye.p.z*values[0];measured.shoulder_span_metres=fit.shoulder_span_metres*values[2];
        measured.eye_to_shoulder_metres=(t.eye.p.z-(shoulder[0].z+shoulder[1].z)*.5)*values[0];
        measured.torso_width_metres=*width*(fitted?values[1]:1);
        measured.arm_reach_metres={reaches[0]*values[3],reaches[1]*values[4]};
        auto tracking=t;const double height_delta=*measured.standing_eye_height_metres-t.eye.p.z;tracking.eye.p.z+=height_delta;
        for(unsigned side=0;side<2;++side) {
            auto centre=shoulder[side];centre.y*=values[2];centre.z=tracking.eye.p.z-measured.eye_to_shoulder_metres;
            tracking.grip[side].p=centre+unit(palm[side]-shoulder[side])* *measured.arm_reach_metres[side];
        }
        const auto standing=tracking;
        if(posture) for(unsigned side=0;side<2;++side) {
            auto centre=shoulder[side];centre.y*=values[2];centre.z=tracking.eye.p.z-measured.eye_to_shoulder_metres;
            const Vec3 direction=posture==1?Vec3{.5,side?-.2:.2,-.1}:Vec3{.25,side?-.15:.15,.65};
            tracking.grip[side].p=centre+unit(direction)*(*measured.arm_reach_metres[side])*(posture==1?.75:.85);
        }
        Driver driver;driver.tune(0,1);driver.configure_calibration(measured,width);
        for(unsigned side=0;side<2;++side)(void)driver.arm(side,arms[side],identity);
        driver.publish(standing,{});assert(driver.body(native,identity));
        for(unsigned step=1;step<=90;++step) {
            auto current=standing;current.tick+=step*11;const double blend=std::min(1.,step/45.);
            for(unsigned side=0;side<2;++side)current.grip[side].p=standing.grip[side].p+(tracking.grip[side].p-standing.grip[side].p)*blend;
            driver.publish(current,{});for(unsigned side=0;side<2;++side)assert(driver.arm(side,arms[side],identity));assert(driver.body(native,identity));
        }
        const auto body_pose=driver.body(native,identity);assert(body_pose);
        std::array<Transform,69> arm_pose[2];for(unsigned side=0;side<2;++side){const auto arm=driver.arm(side,arms[side],identity);assert(arm);arm_pose[side]=*arm;}
        if(comma)output<<',';comma=true;
        output<<"{\"character\":\""<<name<<"\",\"profile\":"<<profile<<",\"posture\":"<<posture<<",\"palm_residual\":["<<driver.diagnostic().palm_residual[0]<<','<<driver.diagnostic().palm_residual[1]<<"],\"fitted\":"<<(fitted?"true":"false")<<",\"height_scale\":"<<values[0]<<",\"torso_factor\":"<<(fitted?values[1]:1)<<",\"shoulder_factor\":"<<values[2]<<",\"reach_factor\":["<<values[3]<<','<<values[4]<<"],\"body\":[";
        const auto write_palette=[&](const auto& palette){bool first=true;for(const auto& joint:palette){if(!first)output<<',';first=false;output<<'[';for(unsigned i=0;i<16;++i){if(i)output<<',';output<<joint.f[i];}output<<']';}};
        write_palette(*body_pose);output<<"],\"arms\":[[";write_palette(arm_pose[0]);output<<"],[";write_palette(arm_pose[1]);output<<"]]}";
    }
    output<<"]";std::cout<<"Generated 60 actual Driver garment fit palettes\n";
}
