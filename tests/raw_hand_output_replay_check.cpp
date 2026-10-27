#include "../src/full_body_driver.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
using namespace d2vr;
using namespace d2vr::fullbody;

const std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};

std::uint32_t bits(float value) {
    std::uint32_t result{};
    std::memcpy(&result,&value,sizeof(result));
    return result;
}

struct Fixture {
    Driver driver;
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Tracking tracking{};
    body::Ownership ownership{};
    std::array<float,15> model_frame=identity_frame;

    Fixture() {
        const auto body_skeleton=skeleton(data::kBody);
        const auto left=skeleton(data::kLeftArm);
        const auto right=skeleton(data::kRightArm);
        for(unsigned joint=0;joint<native_body.size();++joint) native_body[joint]=to_transform(body_skeleton.bind[joint]);
        const auto camera=body_skeleton.bind[body_skeleton.find("camera_jnt")];
        const auto chest=body_skeleton.bind[body_skeleton.find("center_spine03_jnt")];
        tracking.eye={camera.q,camera.p};tracking.epoch=7;tracking.pose_epoch=3;tracking.reference_epoch=5;tracking.mapping_revision=11;
        tracking.tracked_hands=3;tracking.grip_valid[0]=tracking.grip_valid[1]=true;
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right:left;
            const std::string prefix=side?"right":"left";
            for(unsigned joint=0;joint<native_arm[side].size();++joint) native_arm[side][joint]=to_transform(arm.bind[joint]);
            const auto wrist=static_cast<unsigned>(arm.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};
            const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned finger=0;finger<4;++finger) knuckles[finger]=to_transform(arm.bind[arm.find(prefix+"_"+fingers[finger]+"_02_jnt")]);
            tracking.grip[side]=body::mul(chest,to_xf(rig::hand_bind_grip(native_arm[side][wrist],knuckles)));
            static_cast<void>(driver.arm(side,native_arm[side],identity_frame));
        }
        tracking.grip_value[0]=tracking.grip_value[1]=.35;
        tracking.trigger[0]=tracking.trigger[1]=.1;
        tracking.hand_empty[0]=tracking.hand_empty[1]=true;
        tracking.tick=1000;tracking.seconds=1.;
        driver.tune(.08,.92);
        driver.publish(tracking,ownership);
        assert(driver.body(native_body,model_frame));
    }

    void step(std::ofstream& output,const std::string& scenario,unsigned frame,bool arm_first) {
        const double time=static_cast<double>(frame+1)/90.;
        tracking.tick=1000+static_cast<std::uint64_t>((frame+1)*1000/90);
        tracking.seconds=1.+time;
        tracking.tracking_revision=100+frame;
        tracking.source_tick=tracking.tick;
        const double phase=time*2.*3.14159265358979323846;
        tracking.eye.p.x=.12*time;
        tracking.eye.p.y=.025*std::sin(phase);
        tracking.eye.p.z=(scenario=="crouch"?-.18*std::min(1.,time/.5):0.);
        tracking.eye.q=body::mul(body::rot_z(scenario=="turn"?.65*std::min(1.,time/.6):.08*std::sin(phase)),tracking.eye.q);
        for(unsigned side=0;side<2;++side) {
            tracking.grip[side].p.x=tracking.eye.p.x+(side?-.12:.12)+.018*std::sin(phase+(side?.4:0));
            tracking.grip[side].p.y+=(side?-.004:.004)*std::sin(phase*.5);
            tracking.grip[side].p.z+=.012*std::cos(phase+(side?.7:0));
            tracking.grip[side].q=body::mul(body::rot_z(.15*std::sin(phase+(side?.5:0))),tracking.grip[side].q);
            tracking.grip_value[side]=.2+.7*(.5+.5*std::sin(phase+(side?.8:0)));
            tracking.trigger[side]=.1+.6*(.5+.5*std::cos(phase+(side?.3:0)));
            tracking.has_finger[side]=true;tracking.has_finger_axis[side]=(frame%2)==0;
            for(unsigned finger=0;finger<5;++finger) for(unsigned joint=0;joint<3;++joint) {
                tracking.finger_turn[side][finger][joint]=static_cast<float>(.08*std::sin(phase+finger*.21+joint*.13+side*.3));
                tracking.finger_axis[side][finger][joint][0]=0;
                tracking.finger_axis[side][finger][joint][1]=static_cast<float>((finger%2)?1:0);
                tracking.finger_axis[side][finger][joint][2]=static_cast<float>((finger%2)?0:1);
            }
            tracking.hand_empty[side]=(frame%40)<25;
            tracking.reverse_grip[side]=side==1 && scenario=="turn" && frame>25;
        }
        if(scenario=="nativearm" && frame>=18 && frame<42) ownership=body::Ownership{0,.3,{0,1},.2,.3}; // a right-arm-only native state (CarryCorpse before v628)
        else if(scenario=="nativearm" && frame>=42) ownership=body::native_ownership("Mantle");
        else ownership={};
        for(unsigned side=0;side<2;++side) {
            const auto translation=tracking.eye.p;
            model_frame[0]=static_cast<float>(translation.x);model_frame[1]=static_cast<float>(translation.y);model_frame[2]=static_cast<float>(translation.z);
            if(arm_first) assert(driver.arm_frozen(side,native_arm[side],model_frame,tracking.tick,frame+2));
        }
        driver.publish(tracking,ownership);
        const auto body_pose=driver.body(native_body,model_frame,frame+2);
        assert(body_pose);
        std::array<std::optional<fullbody::ArmPose>,2> arms{};
        for(unsigned side=0;side<2;++side) {
            if(!arm_first) arms[side]=driver.arm_frozen(side,native_arm[side],model_frame,tracking.tick,frame+2);
            else arms[side]=driver.arm_frozen(side,native_arm[side],model_frame,tracking.tick,frame+2);
            assert(arms[side]);
        }
        output<<scenario<<','<<frame<<','<<driver.solves()<<','<<driver.failures()<<',';
        for(const auto& joint:*body_pose) for(float value:joint.f) output<<std::hex<<std::setw(8)<<std::setfill('0')<<bits(value);
        for(unsigned side=0;side<2;++side) for(const auto& joint:arms[side]->joints) for(float value:joint.f)
            output<<std::hex<<std::setw(8)<<std::setfill('0')<<bits(value);
        const auto diagnostic=driver.diagnostic();
        output<<','<<diagnostic.revision<<','<<diagnostic.solved_revision<<','<<diagnostic.tracking.tracking_revision<<'\n';
        output<<std::dec;
    }
};

void run_scenario(std::ofstream& output,const std::string& scenario,bool arm_first) {
    Fixture fixture;
    for(unsigned frame=0;frame<60;++frame) fixture.step(output,scenario,frame,arm_first);
}
}

int main(int argc,char** argv) {
    assert(argc==2);
    std::ofstream output(argv[1],std::ios::binary|std::ios::trunc);
    assert(output.good());
    output<<"raw-hand-output-replay-v1\n";
    run_scenario(output,"walk",false);
    run_scenario(output,"turn",true);
    run_scenario(output,"crouch",false);
    run_scenario(output,"nativearm",true);
    output.close();
    assert(output.good());
    std::cout<<"PASS baseline replay: 240 frames, all accepted body/arm float bits recorded\n";
}
