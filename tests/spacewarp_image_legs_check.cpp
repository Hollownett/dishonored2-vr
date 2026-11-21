// Lab check of FullBodyDriver::image_joints (v1460) and of the order spacewarp's leg rule rests on. Since v861 the body a render draws is
// that image's own solve (begin_image at its eye-0 Begin; image_body writes it into the skin array). The engine runs game side N+1 before it
// presents image N (docs/ENGINE-DIG-NOTES.md section 2), and that game side's body upload re-uses the image's solve (same publication):
// the upload legs spacewarp read since v1438 (diagnostic().leg) at the Present of image N are image N's own legs, not a frame ahead, so
// v1439's "the image is the upload before the newest" placed every leg capsule one real frame behind the drawn legs.
//   build\night\lab.cmd spacewarp_image_legs_check /Ithird_party\openxr\include
#include "../src/full_body_driver.hpp"
#include <cstdio>
#include <string>

using namespace d2vr;
using namespace d2vr::fullbody;
static const std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};

int main() {
    int failures=0;const auto check=[&](bool ok,const std::string& what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what.c_str());failures+=!ok;};
    try {
        // the fixture of tests/full_body_frame_latch_check.cpp: the bind pose as the native upload, the hands at their bind grips
        Driver driver;std::array<Transform,20> native_body{};std::array<Transform,69> native_arm[2]{};Tracking tracking{};
        const auto body_skeleton=skeleton(data::kBody),left_arm=skeleton(data::kLeftArm),right_arm=skeleton(data::kRightArm);
        for(unsigned i=0;i<native_body.size();++i) native_body[i]=to_transform(body_skeleton.bind[i]);
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right_arm:left_arm;
            for(unsigned i=0;i<native_arm[side].size();++i) native_arm[side][i]=to_transform(arm.bind[i]);
            static_cast<void>(driver.arm(side,native_arm[side],identity_frame));
        }
        const auto camera=body_skeleton.bind[body_skeleton.find("camera_jnt")];
        tracking.eye={camera.q,camera.p};tracking.epoch=1;tracking.tick=1000;tracking.seconds=1;tracking.tracking_revision=10;tracking.tracked_hands=3;
        const auto chest=body_skeleton.bind[body_skeleton.find("center_spine03_jnt")];
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right_arm:left_arm;const std::string prefix=side?"right":"left";
            const auto wrist=to_transform(arm.bind[arm.find(prefix+"_wrist_jnt")]);
            std::array<Transform,4> knuckles{};const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned i=0;i<4;++i) knuckles[i]=to_transform(arm.bind[arm.find(prefix+"_"+fingers[i]+"_02_jnt")]);
            tracking.grip[side]=body::mul(chest,to_xf(rig::hand_bind_grip(wrist,knuckles)));tracking.grip_valid[side]=true;
        }
        driver.publish(tracking,{});
        if(!driver.body(native_body,identity_frame)) throw std::runtime_error("first solve");
        const int femur[2]={body_skeleton.find("left_femur_jnt"),body_skeleton.find("right_femur_jnt")},ankle[2]={body_skeleton.find("left_ankle_jnt"),body_skeleton.find("right_ankle_jnt")};
        std::optional<Driver::ImageJoints> previous;
        double moved_max=0,upload_vs_image_max=0,upload_vs_previous_min=1e9,drawn_vs_joints_max=0;unsigned images=0;
        for(std::uint64_t frame=1;frame<=12;++frame) {
            // render side `frame`: its Setup publishes the head walked on (3 cm a frame at 45 Hz: 1.35 m/s, the hands carried along), its
            // eye-0 Begin begins the image, and its skin copy draws image_body
            tracking.eye.p.x+=.03;for(auto& g:tracking.grip) g.p.x+=.03;
            tracking.tick+=22;tracking.seconds+=1/45.;++tracking.tracking_revision;
            driver.publish(tracking,{});
            driver.begin_image(frame);
            const auto drawn=driver.image_body(identity_frame,frame);
            const auto joints=driver.image_joints(frame);
            if(!drawn || !joints) {check(false,"image "+std::to_string(frame)+": no image body or joints");continue;}
            ++images;
            for(unsigned s=0;s<2;++s) {  // image_joints is what the skin copy draws (identity frame: model space = world)
                drawn_vs_joints_max=std::max(drawn_vs_joints_max,length(rig::position((*drawn)[static_cast<std::size_t>(femur[s])])-joints->leg[s][0]));
                drawn_vs_joints_max=std::max(drawn_vs_joints_max,length(rig::position((*drawn)[static_cast<std::size_t>(ankle[s])])-joints->leg[s][2]));
            }
            // game side frame+1, before the Present of image `frame`: the engine's arm and body uploads (keyed for the next frame)
            static_cast<void>(driver.arm_frozen(0,native_arm[0],identity_frame,0,frame+1));
            static_cast<void>(driver.body(native_body,identity_frame,frame+1));
            const auto d=driver.diagnostic();
            if(!d.legs_valid || !d.legs_solved) {check(false,"upload "+std::to_string(frame+1)+": no solved legs");continue;}
            double to_image=0,to_previous=0;
            for(unsigned s=0;s<2;++s) for(unsigned k=0;k<4;++k) {
                to_image=std::max(to_image,length(d.leg[s][k]-joints->leg[s][k]));
                if(previous) to_previous=std::max(to_previous,length(d.leg[s][k]-previous->leg[s][k]));
            }
            upload_vs_image_max=std::max(upload_vs_image_max,to_image);
            if(previous) {
                upload_vs_previous_min=std::min(upload_vs_previous_min,to_previous);
                double moved=0;for(unsigned s=0;s<2;++s) for(unsigned k=0;k<4;++k) moved=std::max(moved,length(joints->leg[s][k]-previous->leg[s][k]));
                moved_max=std::max(moved_max,moved);
            }
            previous=joints;
        }
        char line[300];
        check(images==12,"every image began has its joints ("+std::to_string(images)+" of 12)");
        std::snprintf(line,sizeof line,"image_joints are the legs the skin copy draws (hip, ankle: at most %.4f mm apart)",drawn_vs_joints_max*1000);check(drawn_vs_joints_max<1e-5,line);
        std::snprintf(line,sizeof line,"the walk moves the drawn legs (up to %.1f mm per frame)",moved_max*1000);check(moved_max>.01,line);
        std::snprintf(line,sizeof line,"the upload before image N's Present re-uses image N's solve: its legs are image N's (at most %.4f mm apart)",upload_vs_image_max*1000);
        check(upload_vs_image_max<1e-5,line);
        std::snprintf(line,sizeof line,"and not the previous image's (v1439's assumption: at least %.1f mm apart while walking)",upload_vs_previous_min*1000);
        check(upload_vs_previous_min>.005,line);
        check(!driver.image_joints(5),"an image that is not the newest has no joints (each Present asks for its own)");
    } catch(const std::exception& e) {std::printf("FAIL exception: %s\n",e.what());++failures;}
    std::printf("%s\n",failures?"FAILED":"all passed");return failures?1:0;
}
