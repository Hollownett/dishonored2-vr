#include "../src/full_body_driver.hpp"
#include <cassert>
#include <iostream>

using namespace d2vr;
using namespace d2vr::fullbody;
namespace {
constexpr std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
WorldTrackedPose world_pose(const Xf& pose) {
    return {pose.p,{rotated(pose.q,{1,0,0}),rotated(pose.q,{0,1,0}),rotated(pose.q,{0,0,1})}};
}
Xf rigid(const WorldTrackedPose& pose) {return from_basis(pose.position,pose.basis[0],pose.basis[1],pose.basis[2]);}
std::array<float,15> model_frame(const Xf& pose) {
    const auto p=world_pose(pose);
    return {static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z),
        static_cast<float>(p.basis[0].x),static_cast<float>(p.basis[0].y),static_cast<float>(p.basis[0].z),
        static_cast<float>(p.basis[1].x),static_cast<float>(p.basis[1].y),static_cast<float>(p.basis[1].z),
        static_cast<float>(p.basis[2].x),static_cast<float>(p.basis[2].y),static_cast<float>(p.basis[2].z),1,1,1};
}
void same_pose(const Xf& actual,const Xf& expected) {
    assert(length(actual.p-expected.p)<.005);
    for(const auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
        assert(length(rotated(actual.q,axis)-rotated(expected.q,axis))<.01);
}
struct Rig {
    body::Skeleton body=skeleton(data::kBody);
    body::Skeleton arms[2]{skeleton(data::kLeftArm),skeleton(data::kRightArm)};
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Transform bind_palm[2]{};
    unsigned wrist[2]{};
    Tracking tracking{};
    Rig() {
        tracking.eye=body.bind[body.find("camera_jnt")];
        tracking.epoch=1;tracking.pose_epoch=2;tracking.reference_epoch=3;tracking.mapping_revision=4;
        tracking.tick=100;tracking.source_tick=100;tracking.seconds=.1;tracking.tracked_hands=3;tracking.raw_grip_hands=3;
        for(unsigned j=0;j<20;++j)native_body[j]=to_transform(body.bind[j]);
        for(unsigned side=0;side<2;++side) {
            const auto& arm=arms[side];const std::string prefix=side?"right":"left";
            for(unsigned j=0;j<69;++j)native_arm[side][j]=to_transform(arm.bind[j]);
            wrist[side]=static_cast<unsigned>(arm.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};const char* fingers[]{"index","medium","ring","pinky"};
            for(unsigned f=0;f<4;++f)knuckles[f]=native_arm[side][arm.find(prefix+"_"+fingers[f]+"_02_jnt")];
            bind_palm[side]=rig::hand_bind_grip(native_arm[side][wrist[side]],knuckles);
            tracking.grip[side]=body::mul(body.bind[body.find("center_spine03_jnt")],to_xf(bind_palm[side]));
            // Put the test hand comfortably inside the actual rig's reachable workspace.
            tracking.grip[side].p.y+=side?.12:-.12;
            tracking.grip[side].p.x+=.08;
            tracking.raw_grip[side]=tracking.grip[side];
            tracking.grip_valid[side]=true;tracking.hand_empty[side]=true;
        }
    }
    void seed(Driver& driver) const {
        driver.tune(0,1);driver.note_root(identity_frame);
        for(unsigned side=0;side<2;++side)(void)driver.arm(side,native_arm[side],identity_frame);
        driver.publish(tracking,{});assert(driver.body(native_body,identity_frame,1));
    }
    Xf palm(const std::array<Transform,69>& palette,const std::array<float,15>& frame,unsigned side=0) const {
        const auto local=rig::rigid_retarget(bind_palm[side],native_arm[side][wrist[side]],palette[wrist[side]]);
        return to_world(frame_of(frame),to_xf(local));
    }
};
}
int main() {
    for(unsigned side=0;side<2;++side) {
        Rig rig;
        rig.tracking.tracking_revision=7;
        rig.tracking.has_finger[side]=true;
        rig.tracking.has_finger_axis[side]=true;
        for(unsigned f=0;f<5;++f)for(unsigned j=0;j<3;++j) {
            rig.tracking.finger_turn[side][f][j]=.15f+static_cast<float>(f+j)*.04f;
            rig.tracking.finger_axis[side][f][j][1]=1;
        }
        Driver driver;rig.seed(driver);
        const Xf root{body::rot_z(.23),{.025,-.018,.01}};
        const auto frame=model_frame(root);driver.note_root(frame);
        const auto actual=driver.arm_frozen(side,rig.native_arm[side],frame,100,1);assert(actual);
        const auto prepared=driver.prepare_contact_arm(side,*actual,frame,100);assert(prepared);
        const auto before=driver.diagnostic();
        const auto target=body::mul(actual->world_from_solve,rig.tracking.raw_grip[side]);
        auto corrected=target;corrected.p.x-=.025;
        const auto solved=prepared->solve_world_grip(corrected,101);assert(solved && solved->reached);
        same_pose(rig.palm(solved->joints,frame,side),corrected);
        assert(solved->raw_joints && actual->raw_joints);
        for(unsigned j=0;j<69;++j)for(unsigned k=0;k<16;++k)assert((*solved->raw_joints)[j].f[k]==(*actual->raw_joints)[j].f[k]);
        const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
        const auto& skeleton=rig.arms[side];const std::string prefix=side?"right":"left";
        for(unsigned f=0;f<5;++f)for(unsigned k=0;k<3;++k) {
            const auto j=skeleton.find(prefix+"_"+digits[f]+"_0"+std::to_string(k+(f?2:1))+"_jnt");
            const auto parent=skeleton.parent[j];
            const auto local=body::mul(body::inverse(to_xf(solved->joints[parent])),to_xf(solved->joints[j]));
            const auto original=body::mul(body::inverse(to_xf((*actual->raw_joints)[parent])),to_xf((*actual->raw_joints)[j]));
            same_pose(local,original);
            const auto axis=rotated(body::conj(skeleton.bind[j].q),Vec3{0,1,0});
            const auto expected=body::mul(skeleton.local[j].q,body::axis_angle(axis,rig.tracking.finger_turn[side][f][k]));
            for(const auto v:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})assert(length(rotated(local.q,v)-rotated(expected,v))<1e-5);
        }
        const auto repeat=prepared->solve_world_grip(corrected,101);assert(repeat);
        for(unsigned j=0;j<69;++j)for(unsigned k=0;k<16;++k)assert(solved->joints[j].f[k]==repeat->joints[j].f[k]);
        const auto after=driver.diagnostic();assert(before.solved_revision==after.solved_revision && before.callback_serial==after.callback_serial);
        for(unsigned f=0;f<5;++f)for(unsigned k=0;k<3;++k)assert(solved->tracking.finger_turn[side][f][k]==rig.tracking.finger_turn[side][f][k]);
        assert(driver.contact_arm_current(*prepared,*actual,frame,101));
        assert(!prepared->solve_world_grip(corrected,133));
        auto far=corrected;far.p.x+=4;const auto limited=prepared->solve_world_grip(far,101);assert(limited && !limited->reached);
        auto newer=rig.tracking;newer.tick=newer.source_tick=110;newer.tracking_revision++;
        newer.eye=body::mul(root,newer.eye);
        for(unsigned h=0;h<2;++h) {newer.grip[h]=body::mul(root,newer.grip[h]);newer.raw_grip[h]=body::mul(root,newer.raw_grip[h]);}
        driver.publish(newer,{});const auto next=driver.arm_frozen(side,rig.native_arm[side],frame,110,2);assert(next);
        assert(driver.contact_arm_current(*prepared,*next,frame,110));
        auto changed=*next;changed.tracking.finger_turn[side][0][0]+=.1f;assert(!driver.contact_arm_current(*prepared,changed,frame,110));
        changed=*next;changed.tracking.mapping_revision++;assert(!driver.contact_arm_current(*prepared,changed,frame,110));
        driver.clear();assert(!driver.contact_arm_current(*prepared,*next,frame,110));
        assert(prepared->solve_world_grip(corrected,111));
    }
    std::cout<<"full body contact arm check passed\n";
}
