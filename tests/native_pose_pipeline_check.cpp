#define NOMINMAX
#include "../src/full_body_driver.hpp"
#include "../src/native_attachment_pose.hpp"
#include <cassert>
#include <iostream>
#include <thread>

namespace {
struct Fixture {
    alignas(16) std::array<std::byte,0x300> animation{};
    alignas(16) std::array<std::byte,64> states{};
    alignas(16) std::array<std::byte,0x1900> model{};
    alignas(16) std::array<d2vr::rig::Transform,69> native{};
    std::array<short,69> remap{};
};
bool read(std::uintptr_t address,void* out,std::size_t size) {
    SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;
}
template<class T> void put(std::byte* address,const T& value) {std::memcpy(address,&value,sizeof(value));}
}
namespace d2vr {
struct NativeAttachmentPoseCheck {
    static void run() {
        using namespace fullbody;
        Driver driver;driver.tune(0,1);
        NativeAttachmentPose attachment;attachment.read_=read;attachment.active_=true;
        const auto b=skeleton(data::kBody),left=skeleton(data::kLeftArm),right=skeleton(data::kRightArm);
        const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        std::array<Transform,20> native_body{};
        for(unsigned j=0;j<20;++j)native_body[j]=to_transform(b.bind[j]);
        std::array<Fixture,2> fixtures{};
        std::array<Transform,2> palms{};std::array<unsigned,2> wrists{};
        Tracking track;track.eye=b.bind[b.find("camera_jnt")];track.epoch=1;track.pose_epoch=2;
        track.reference_epoch=3;track.mapping_revision=4;track.tracking_revision=5;
        track.tick=GetTickCount64();
        for(unsigned side=0;side<2;++side) {
            auto& fixture=fixtures[side];const auto& a=side?right:left;const std::string prefix=side?"right":"left";
            for(unsigned j=0;j<69;++j) {fixture.native[j]=to_transform(a.bind[j]);fixture.remap[j]=static_cast<short>(j);}
            wrists[side]=static_cast<unsigned>(a.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};const char* names[]={"index","medium","ring","pinky"};
            for(unsigned j=0;j<4;++j)knuckles[j]=to_transform(a.bind[a.find(prefix+"_"+names[j]+"_02_jnt")]);
            palms[side]=rig::hand_bind_grip(fixture.native[wrists[side]],knuckles);
            track.grip[side]=body::mul(b.bind[b.find("center_spine03_jnt")],to_xf(palms[side]));track.grip_valid[side]=true;
            const auto animation=reinterpret_cast<std::uintptr_t>(fixture.animation.data());
            const auto states=reinterpret_cast<std::uintptr_t>(fixture.states.data());
            const auto model=reinterpret_cast<std::uintptr_t>(fixture.model.data());
            put(fixture.animation.data()+0x18,rig::Identity{static_cast<std::int32_t>(side+1),10});put(fixture.animation.data()+0x38,model);
            put(fixture.animation.data()+0x2d8,reinterpret_cast<std::uintptr_t>(fixture.native.data()));
            put(fixture.animation.data()+0x2e8,states);put(fixture.states.data()+48,reinterpret_cast<std::uintptr_t>(fixture.remap.data()));
            put(fixture.states.data()+56,std::uint32_t{69});
            assert(animation);(void)driver.arm(side,fixture.native,identity);
        }
        track.seconds=track.tick/1000.;driver.publish(track,{});assert(driver.body(native_body,identity));
        // Equal timestamps still carry a new publication. Arm callbacks precede
        // this publication's body callback, using the retained native body.
        ++track.tracking_revision;track.grip[0].p.z+=.002;driver.publish(track,{});
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto before=driver.solves();
        std::array<NativeAttachmentSource,2> sources{};
        std::array<fullbody::ArmPose,2> poses{};
        for(unsigned side=0;side<2;++side) {
            auto& fixture=fixtures[side];auto moving=identity;
            moving[0]=static_cast<float>(3+side);moving[1]=static_cast<float>(-2-static_cast<int>(side));
            if(side) {moving[3]=0;moving[4]=1;moving[6]=-1;moving[7]=0;}
            put(fixture.model.data()+0x1860,moving);
            const auto posed=driver.arm_frozen(side,fixture.native,moving);assert(posed);poses[side]=*posed;
            assert(driver.solves()==before+1 && posed->tracking.tracking_revision==track.tracking_revision);
            assert(driver.diagnostic().palm_residual[side]<.005 && !driver.diagnostic().gap_reset);
            const auto& frozen=posed->tracking;
            sources[side]={frozen.tick,frozen.tracking_revision,frozen.pose_epoch,frozen.reference_epoch,frozen.mapping_revision,
                reinterpret_cast<std::uintptr_t>(fixture.model.data()),{static_cast<std::int32_t>(side+1),10},moving};
            attachment.set_allowed(3,frozen.pose_epoch,frozen.tick,frozen.tracking_revision,frozen.reference_epoch,frozen.mapping_revision);
            assert(attachment.publish(side,reinterpret_cast<std::uintptr_t>(fixture.animation.data()),
                reinterpret_cast<std::uintptr_t>(fixture.native.data()),posed->joints,sources[side]));
        }
        assert(driver.body(native_body,identity) && driver.solves()==before+1);
        const auto query=[&](unsigned side,bool tracked) {
            auto& fixture=fixtures[side];const auto saved=fixture.native[wrists[side]];unsigned calls{};
            const auto result=attachment.around(reinterpret_cast<std::uintptr_t>(fixture.animation.data()),3,static_cast<short>(wrists[side]),[&] {
                ++calls;const auto& expected=tracked?poses[side].joints[wrists[side]]:saved;
                assert(!std::memcmp(&fixture.native[wrists[side]],&expected,sizeof(expected)));
                return std::uint64_t{17};
            });
            assert(result==17 && calls==1 && !std::memcmp(&fixture.native[wrists[side]],&saved,sizeof(saved)));
        };
        query(0,true);query(1,true); // 200ms tracking source supports arm and item together.
        const auto source=sources[0];
        attachment.set_allowed(native_attachment_allowed_mask(3,1,true,true,true,false),source.epoch,source.tick,
            source.revision,source.reference_epoch,source.mapping_revision);
        query(0,false);query(1,true);
        assert(!attachment.publish(0,reinterpret_cast<std::uintptr_t>(fixtures[0].animation.data()),
            reinterpret_cast<std::uintptr_t>(fixtures[0].native.data()),poses[0].joints,source));
        attachment.set_allowed(3,source.epoch,source.tick,source.revision,source.reference_epoch,source.mapping_revision);
        query(0,false); // ownership restoration cannot revive revoked joints.
        auto rejected=sources[1];--rejected.revision;
        assert(!attachment.publish(1,reinterpret_cast<std::uintptr_t>(fixtures[1].animation.data()),
            reinterpret_cast<std::uintptr_t>(fixtures[1].native.data()),poses[1].joints,rejected));
        query(1,false); // rejected replacement removes the previously accepted item palette.
        ++track.tracking_revision;driver.publish(track,{});
        const auto next=driver.arm_frozen(0,fixtures[0].native,sources[0].model_frame);assert(next);
        attachment.set_allowed(3,next->tracking.pose_epoch,next->tracking.tick,next->tracking.tracking_revision,
            next->tracking.reference_epoch,next->tracking.mapping_revision);
        assert(!attachment.publish(0,reinterpret_cast<std::uintptr_t>(fixtures[0].animation.data()),
            reinterpret_cast<std::uintptr_t>(fixtures[0].native.data()),poses[0].joints,source));
        query(0,false);assert(driver.diagnostic().consumed_dt==0 && !driver.diagnostic().gap_reset);
        auto active=source;active.revision=next->tracking.tracking_revision;
        assert(attachment.publish(0,reinterpret_cast<std::uintptr_t>(fixtures[0].animation.data()),
            reinterpret_cast<std::uintptr_t>(fixtures[0].native.data()),next->joints,active));
        attachment.set_allowed(3,active.epoch,active.tick,active.revision+1,active.reference_epoch,active.mapping_revision);
        query(0,true); // New tracking alone does not replace the last rendered arm.
    }
};
}
int main() {d2vr::NativeAttachmentPoseCheck::run();std::cout<<"PASS: arm-first frozen Driver to native attachment pipeline, 200ms source, per-side moving model frames, duplicate publication, ownership revocation and rejected replacement\n";}

