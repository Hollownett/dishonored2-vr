#include "../src/full_body_driver.hpp"
#include "../src/native_torso_fit.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
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
    fit.shoulder_span_measured=true;fit.torso_width_measured=true;fit.arm_lengths_measured=true;
    fit.arm_reach_metres={reaches[0]*.9,reaches[1]*1.1};
    const auto native_width=native_torso_width("models/characters/player/emily/player_body_emily.md6mesh",7951);assert(native_width);
    assert(!native_torso_width("unknown",7951));assert(!native_torso_width("models/characters/player/emily/player_body_emily.md6mesh",10391));
    fit.torso_width_metres=*native_width*1.4;
    Driver driver;driver.tune(0,1);bool rejected=false;
    try {driver.configure_calibration(fit);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    driver.configure_calibration(fit,native_width);const auto d=driver.diagnostic();
    assert(std::abs(d.torso_scale-1.4)<1e-9 && d.arm_scale_side[0]<1 && d.arm_scale_side[1]>1);
    for(unsigned side=0;side<2;++side)(void)driver.arm(side,arms[side],identity);
    driver.publish(t,{});const auto posed=driver.body(native,identity);assert(posed);
    assert(length(rig::position((*posed)[b.find("camera_jnt")])-t.eye.p)<1e-6);
    for(unsigned side=0;side<2;++side) {
        // Reposition targets inside the shorter side's reach; palm and wrist
        // remain rigid even though the chest palette contains affine scale.
        t.grip[side].p=shoulder[side]+unit(palm[side]-shoulder[side])*.4;
    }
    t.tick+=33;t.seconds=t.tick/1000.;driver.publish(t,{});
    for(unsigned side=0;side<2;++side) {
        const auto p=driver.arm(side,arms[side],identity);assert(p);
        for(unsigned j=2;j<69;++j)for(unsigned axis=0;axis<3;++axis)assert(std::abs(length(rig::column((*p)[j],axis))-1)<1e-5);
        assert(driver.diagnostic().palm_residual[side]<.005);
    }
    for(unsigned i=0;i<45;++i) {t.tick+=33;t.seconds=t.tick/1000.;driver.publish(t,body::native_ownership("Mantle"));assert(driver.body(native,identity));}
    assert(driver.owned(1)==1);const auto authored_body=driver.body(native,identity);assert(authored_body);
    for(const auto name:{"center_spine01_jnt","center_spine02_jnt","center_spine03_jnt"})assert(rig::valid((*authored_body)[b.find(name)]));
    // Separate link constraints fit both arms without resizing the wrist subtree.
    body::Calibration cal{t.eye.p.z,0,fit.shoulder_span_metres,fit.eye_to_shoulder_metres,1.4};
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?r:l;const std::string prefix=side?"right":"left";
        cal.arm_reach[side]=length(a.bind[a.find(prefix+"_hand_attachment_jnt")].p-a.bind[a.find(prefix+"_arm_jnt")].p)*(side?1.1:.9);
    }
    body::BodySolver solver(b,l,r,cal);
    for(unsigned side=0;side<2;++side) {
        const auto& a=solver.skeleton(1+side);const auto& original=side?r:l;const std::string prefix=side?"right":"left";
        assert(std::abs(length(a.bind[a.find(prefix+"_hand_attachment_jnt")].p-a.bind[a.find(prefix+"_arm_jnt")].p)-cal.arm_reach[side])<1e-8);
        const auto wrist=a.find(prefix+"_wrist_jnt");
        for(size_t j=0;j<a.local.size();++j)for(int ancestor=a.parent[j];ancestor>=0;ancestor=a.parent[ancestor])if(ancestor==wrist) {
            assert(length(a.local[j].p-original.local[j].p)<1e-9);break;
        }
    }
    // Export actual Driver palettes, with native inverse bind, for eight-weight
    // skinning of both character meshes rather than checking a scale formula.
    if(argc>1) {
        std::ofstream file(argv[1]);assert(file);file.precision(17);file<<"{\"inverse\":[";
        for(unsigned j=0;j<20;++j) {if(j)file<<',';const auto matrix=to_transform(body::inverse(b.bind[j]));file<<'[';
            for(unsigned k=0;k<16;++k){if(k)file<<',';file<<matrix.f[k];}file<<']';}
        file<<"],\"characters\":[";
        for(unsigned c=0;c<4;++c) {
            if(c)file<<',';const std::string character=(c&1)?"corvo":"emily";
            const auto width=native_torso_width("models/characters/player/"+character+"/player_body_"+character+".md6mesh",(c&1)?10391:7951);
            Driver geometry;geometry.tune(0,1);auto geometry_fit=fit;geometry_fit.arm_reach_metres={};geometry_fit.arm_lengths_measured=false;
            auto geometry_tracking=t;
            if(c>=2) {
                const double height=t.eye.p.z*1.15;geometry_fit.standing_eye_height_metres=height;
                for(auto& grip:geometry_tracking.grip)grip.p.z+=height-t.eye.p.z;
                geometry_tracking.eye.p.z=height;
            }
            geometry_fit.torso_width_metres=*width*1.4;geometry.configure_calibration(geometry_fit,width);
            for(unsigned side=0;side<2;++side)(void)geometry.arm(side,arms[side],identity);
            geometry.publish(geometry_tracking,{});const auto palette=geometry.body(native,identity);assert(palette);
            file<<"{\"name\":\""<<character<<"\",\"height_scale\":"<<(c>=2?1.15:1)<<",\"width\":"<<*width<<",\"palette\":[";
            for(unsigned j=0;j<20;++j){if(j)file<<',';file<<'[';for(unsigned k=0;k<16;++k){if(k)file<<',';file<<(*palette)[j].f[k];}file<<']';}
            file<<"],\"arm_palettes\":[";
            Driver baseline;baseline.tune(0,1);auto baseline_fit=geometry_fit;baseline_fit.torso_width_metres=*width;
            baseline.configure_calibration(baseline_fit,width);
            for(unsigned side=0;side<2;++side)(void)baseline.arm(side,arms[side],identity);
            baseline.publish(geometry_tracking,{});assert(baseline.body(native,identity));
            for(unsigned side=0;side<2;++side) {
                if(side)file<<',';const auto posed_arm=geometry.arm(side,arms[side],identity);assert(posed_arm);
                file<<'[';for(unsigned j=0;j<69;++j){if(j)file<<',';file<<'[';for(unsigned k=0;k<16;++k){if(k)file<<',';file<<(*posed_arm)[j].f[k];}file<<']';}file<<']';
            }
            file<<"],\"arm_baseline\":[";
            for(unsigned side=0;side<2;++side) {
                if(side)file<<',';const auto posed_arm=baseline.arm(side,arms[side],identity);assert(posed_arm);
                file<<'[';for(unsigned j=0;j<69;++j){if(j)file<<',';file<<'[';for(unsigned k=0;k<16;++k){if(k)file<<',';file<<(*posed_arm)[j].f[k];}file<<']';}file<<']';
            }
            file<<"]}";
        }
        file<<"]}";
    }
    std::cout<<"PASS: independent native-surface torso fit, asymmetric arm constraints and rigid tracked wrists\n";
}
