#include "../src/full_body_driver.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
using namespace d2vr;
using namespace d2vr::fullbody;

const std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};

struct Fixture {
    Driver driver;
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Tracking tracking{};

    Fixture() {
        const auto body_skeleton=skeleton(data::kBody);
        const auto left_arm=skeleton(data::kLeftArm);
        const auto right_arm=skeleton(data::kRightArm);
        for(unsigned index=0;index<native_body.size();++index)native_body[index]=to_transform(body_skeleton.bind[index]);
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right_arm:left_arm;
            for(unsigned index=0;index<native_arm[side].size();++index)native_arm[side][index]=to_transform(arm.bind[index]);
            static_cast<void>(driver.arm(side,native_arm[side],identity_frame));
        }
        const auto camera=body_skeleton.bind[body_skeleton.find("camera_jnt")];
        tracking.eye={camera.q,camera.p};
        tracking.epoch=1;tracking.tick=1000;tracking.tracking_revision=10;tracking.tracked_hands=3;
        const auto chest=body_skeleton.bind[body_skeleton.find("center_spine03_jnt")];
        for(unsigned side=0;side<2;++side) {
            const auto& arm=side?right_arm:left_arm;
            const std::string prefix=side?"right":"left";
            const auto wrist=to_transform(arm.bind[arm.find(prefix+"_wrist_jnt")]);
            std::array<Transform,4> knuckles{};
            const char* fingers[]={"index","medium","ring","pinky"};
            for(unsigned index=0;index<4;++index)knuckles[index]=to_transform(arm.bind[arm.find(prefix+"_"+fingers[index]+"_02_jnt")]);
            const auto grip=rig::hand_bind_grip(wrist,knuckles);
            tracking.grip[side]=body::mul(chest,to_xf(grip));
            tracking.grip_valid[side]=true;
        }
        driver.publish(tracking,{});
        assert(driver.body(native_body,identity_frame));
    }

    void publish_new_revision() {
        tracking.tick+=16;tracking.seconds=tracking.tick/1000.;++tracking.tracking_revision;tracking.tracked_hands=1;
        for(auto& grip:tracking.grip)grip.p.x+=.025;
        driver.publish(tracking,{});
    }

    static bool same_body(const std::array<Transform,20>& first,const std::array<Transform,20>& second) {
        for(unsigned joint=0;joint<first.size();++joint)for(unsigned component=0;component<16;++component)
            if(first[joint].f[component]!=second[joint].f[component])return false;
        return true;
    }
};

void arm_first_cached_frame_retains_solved_revision() {
    Fixture fixture;
    constexpr std::uint64_t frame_key=771;
    const auto solved_body=fixture.driver.body(fixture.native_body,identity_frame);
    assert(solved_body);
    const auto solved=fixture.driver.diagnostic();
    const auto solve_count=fixture.driver.solves();
    const auto cached_arm=fixture.driver.arm_frozen(0,fixture.native_arm[0],identity_frame,0,frame_key);
    assert(cached_arm && cached_arm->revision==solved.solved_revision && cached_arm->tracking.tracking_revision==solved.tracking.tracking_revision);
    assert(fixture.driver.solves()==solve_count);
    fixture.publish_new_revision();
    const auto retained_arm=fixture.driver.arm_frozen(1,fixture.native_arm[1],identity_frame,0,frame_key);
    const auto retained_body=fixture.driver.body(fixture.native_body,identity_frame,frame_key);
    const auto retained=fixture.driver.diagnostic();
    assert(retained_arm && retained_body && fixture.driver.solves()==solve_count);
    assert(retained.solved_revision==solved.solved_revision && retained.revision!=solved.revision);
    assert(retained_arm->tracking.tracking_revision==solved.tracking.tracking_revision && retained_arm->tracking.tracked_hands==3);
    assert(length(retained_arm->tracking.grip[0].p-solved.tracking.grip[0].p)==0);
    assert(Fixture::same_body(*solved_body,*retained_body));
    const auto next_frame=fixture.driver.body(fixture.native_body,identity_frame,frame_key+1);
    assert(next_frame && fixture.driver.solves()==solve_count+1);
    const auto updated=fixture.driver.diagnostic();
    assert(updated.solved_revision==updated.revision && updated.solved_revision!=solved.solved_revision);
    assert(updated.tracking.tracking_revision==fixture.tracking.tracking_revision && updated.tracking.tracked_hands==1);
}

void body_first_cached_frame_retains_solved_revision() {
    Fixture fixture;
    constexpr std::uint64_t frame_key=882;
    const auto solved_body=fixture.driver.body(fixture.native_body,identity_frame);
    assert(solved_body);
    const auto solved=fixture.driver.diagnostic();
    const auto solve_count=fixture.driver.solves();
    const auto cached_body=fixture.driver.body(fixture.native_body,identity_frame,frame_key);
    assert(cached_body && Fixture::same_body(*solved_body,*cached_body) && fixture.driver.solves()==solve_count);
    fixture.publish_new_revision();
    const auto retained_arm=fixture.driver.arm_frozen(0,fixture.native_arm[0],identity_frame,0,frame_key);
    const auto retained_body=fixture.driver.body(fixture.native_body,identity_frame,frame_key);
    const auto retained=fixture.driver.diagnostic();
    assert(retained_arm && retained_body && fixture.driver.solves()==solve_count);
    assert(retained.solved_revision==solved.solved_revision && retained_arm->tracking.tracking_revision==solved.tracking.tracking_revision);
    assert(retained_arm->tracking.tracked_hands==3 && Fixture::same_body(*solved_body,*retained_body));
    assert(fixture.driver.arm_frozen(1,fixture.native_arm[1],identity_frame,0,frame_key+1));
    const auto updated=fixture.driver.diagnostic();
    assert(fixture.driver.solves()==solve_count+1 && updated.solved_revision==updated.revision);
    assert(updated.tracking.tracking_revision==fixture.tracking.tracking_revision && updated.tracking.tracked_hands==1);
}

void new_key_rejects_stale_tracking_age() {
    Fixture fixture;
    constexpr std::uint64_t frame_key=993;
    assert(fixture.driver.arm_frozen(0,fixture.native_arm[0],identity_frame,0,frame_key));
    const auto solve_count=fixture.driver.solves();
    std::this_thread::sleep_for(std::chrono::milliseconds(tracked_pose_freshness_ms+20));
    assert(!fixture.driver.arm_frozen(1,fixture.native_arm[1],identity_frame,0,frame_key+1));
    assert(fixture.driver.solves()==solve_count && fixture.driver.diagnostic().arm_gate[1]==ArmGate::stale_age);
}

void equal_key_does_not_survive_epoch_clear_or_calibration_invalidation() {
    {
        Fixture fixture;
        constexpr std::uint64_t frame_key=114;
        assert(fixture.driver.body(fixture.native_body,identity_frame,frame_key));
        const auto solve_count=fixture.driver.solves();
        ++fixture.tracking.epoch;fixture.tracking.tick+=16;fixture.tracking.seconds=fixture.tracking.tick/1000.;++fixture.tracking.tracking_revision;
        fixture.driver.publish(fixture.tracking,{});
        const auto updated=fixture.driver.arm_frozen(0,fixture.native_arm[0],identity_frame,0,frame_key);
        assert(updated && fixture.driver.solves()==solve_count+1 && updated->tracking.epoch==fixture.tracking.epoch);
    }
    {
        Fixture fixture;
        constexpr std::uint64_t frame_key=225;
        assert(fixture.driver.body(fixture.native_body,identity_frame,frame_key));
        const auto solve_count=fixture.driver.solves();
        fixture.driver.clear();
        for(unsigned side=0;side<2;++side)static_cast<void>(fixture.driver.arm(side,fixture.native_arm[side],identity_frame));
        fixture.tracking.tick+=16;fixture.tracking.seconds=fixture.tracking.tick/1000.;++fixture.tracking.tracking_revision;
        fixture.driver.publish(fixture.tracking,{});
        const auto updated=fixture.driver.body(fixture.native_body,identity_frame,frame_key);
        assert(updated && fixture.driver.solves()==solve_count+1 && fixture.driver.diagnostic().solved_revision==fixture.driver.diagnostic().revision);
    }
    {
        Fixture fixture;
        constexpr std::uint64_t frame_key=336;
        assert(fixture.driver.body(fixture.native_body,identity_frame,frame_key));
        const auto solve_count=fixture.driver.solves();
        AvatarCalibrationResult fit{1,1.6,.36,.22,1.08,{}};
        fixture.driver.configure_calibration(fit);
        const auto updated=fixture.driver.body(fixture.native_body,identity_frame,frame_key);
        assert(updated && fixture.driver.solves()==solve_count+1 && fixture.driver.diagnostic().fit_revision==1);
    }
}

void zero_key_still_solves_newest_revision() {
    Fixture fixture;
    constexpr std::uint64_t frame_key=447;
    assert(fixture.driver.body(fixture.native_body,identity_frame,frame_key));
    const auto solve_count=fixture.driver.solves();
    fixture.publish_new_revision();
    const auto updated=fixture.driver.arm_frozen(0,fixture.native_arm[0],identity_frame);
    assert(updated && fixture.driver.solves()==solve_count+1);
    assert(updated->tracking.tracking_revision==fixture.tracking.tracking_revision && updated->tracking.tracked_hands==1);
}
}

int main() {
    arm_first_cached_frame_retains_solved_revision();
    body_first_cached_frame_retains_solved_revision();
    new_key_rejects_stale_tracking_age();
    equal_key_does_not_survive_epoch_clear_or_calibration_invalidation();
    zero_key_still_solves_newest_revision();
    std::cout<<"PASS full-body keyed frame latch, invalidation, stale-age and unkeyed newest behavior\n";
}
