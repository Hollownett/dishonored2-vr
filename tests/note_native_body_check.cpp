#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../src/full_body_driver.hpp"
#include "../src/note_hand_pose.hpp"
#include "../src/note_hand_mesh.hpp"
#include "../src/note_native_skin.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
namespace fb=d2vr::fullbody;
rig::ArmBinding note_binding(const body::Skeleton& skeleton,unsigned side) {
    const std::string p=side?"right":"left";rig::ArmBinding b{};
    const auto joint=[&](const std::string& name){return static_cast<unsigned>(skeleton.find(p+name));};
    b.chain={joint("_arm_jnt"),joint("_elbow_jnt"),joint("_wrist_jnt")};
    b.helpers={joint("_lower_armhelper1_jnt"),joint("_lower_armhelper2_jnt"),joint("_lower_armhelper3_jnt"),joint("_wrist_rotation_jnt")};
    b.palm={joint("_helper_underwrist_jnt"),joint("_helper_upperwrist_jnt")};
    std::array<rig::Transform,69> bind{};
    for(unsigned j=0;j<69;++j) {b.parents[j]=static_cast<std::int16_t>(skeleton.parent[j]);bind[j]=fb::to_transform(skeleton.bind[j]);}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned f=0;f<5;++f)for(unsigned j=0;j<3;++j)b.finger_chains[f][j]=joint("_"+digits[f]+"_0"+std::to_string(j+(f?2:1))+"_jnt");
    b.bind_wrist=bind[b.chain.wrist];std::array<rig::Transform,4> knuckles{};
    for(unsigned f=0;f<4;++f)knuckles[f]=bind[b.finger_chains[f+1][0]];
    b.bind_grip=rig::hand_bind_grip(b.bind_wrist,knuckles);b.fingers=rig::finger_binding(bind,b.parents,b.finger_chains);
    return b;
}
WorldTrackedPose world_pose(const body::Xf& p) {
    return {p.p,{rotated(p.q,{1,0,0}),rotated(p.q,{0,1,0}),rotated(p.q,{0,0,1})}};
}
int main() {
    const auto body_sk=fb::skeleton(fb::data::kBody);
    const std::array<body::Skeleton,2> arm_sk{fb::skeleton(fb::data::kLeftArm),fb::skeleton(fb::data::kRightArm)};
    const std::array<rig::ArmBinding,2> bindings{note_binding(arm_sk[0],0),note_binding(arm_sk[1],1)};
    const std::array<NoteHandPose,2> posers{NoteHandPose(bindings[0]),NoteHandPose(bindings[1])};
    std::array<rig::Transform,20> native_body{};std::array<std::array<rig::Transform,69>,2> native_arms{};
    for(unsigned j=0;j<20;++j)native_body[j]=fb::to_transform(body_sk.bind[j]);
    for(unsigned side=0;side<2;++side)for(unsigned j=0;j<69;++j)native_arms[side][j]=fb::to_transform(arm_sk[side].bind[j]);
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    fb::Driver driver;driver.tune(0,1);driver.note_root(identity);
    for(unsigned side=0;side<2;++side)(void)driver.arm(side,native_arms[side],identity);
    const auto chest_bind=body_sk.bind[body_sk.find("center_spine03_jnt")];
    fb::Tracking tracking{};tracking.eye=body_sk.bind[body_sk.find("camera_jnt")];
    tracking.epoch=tracking.pose_epoch=tracking.reference_epoch=tracking.mapping_revision=1;
    tracking.tracked_hands=3;tracking.anchor_root=identity;tracking.anchor_root_valid=true;
    std::array<rig::FingerIkState,2> fingers{};
    body::Xf previous_wrist{},previous_shoulder{};Vec3 previous_tip{};bool have_previous=false;
    double left_bend=0;body::Xf first_left_wrist{};
    for(unsigned frame=0;frame<12;++frame) {
        const double step=static_cast<double>(frame);
        NotePanelReceipt page{};page.width=.45+step*.002;page.height=page.width*.5625;
        auto left=body::mul(chest_bind,fb::to_xf(bindings[0].bind_grip));left.q=body::mul(body::rot_z(step*.008),left.q); // the wrist turns the held page
        page.pose=posers[0].page(world_pose(left),page.width,page.height);
        auto right=body::mul(chest_bind,fb::to_xf(bindings[1].bind_grip));
        right.p=right.p+Vec3{step*.002,0,step*.001};right.q=body::mul(body::rot_z(step*.012),right.q);
        std::array<NoteHandPose::Controls,2> controls;
        for(unsigned side=0;side<2;++side) {
            const auto tracked=world_pose(side?right:left);
            controls[side]=posers[side].controls(side,tracked,page,fingers[side],.01,.6f);
            const auto& c=controls[side];tracking.grip[side]=fb::from_basis(c.palm.position,c.palm.basis[0],c.palm.basis[1],c.palm.basis[2]);
            tracking.grip_valid[side]=tracking.hand_empty[side]=tracking.has_finger[side]=tracking.has_finger_axis[side]=true;
            for(unsigned f=0;f<5;++f)for(unsigned j=0;j<3;++j) {
                tracking.finger_turn[side][f][j]=c.turns[f][j];
                tracking.finger_axis[side][f][j][0]=static_cast<float>(c.axes[f][j].x);
                tracking.finger_axis[side][f][j][1]=static_cast<float>(c.axes[f][j].y);
                tracking.finger_axis[side][f][j][2]=static_cast<float>(c.axes[f][j].z);
            }
        }
        for(const auto& finger:controls[0].turns)for(float turn:finger) {assert(std::isfinite(turn));left_bend+=std::abs(turn);}
        tracking.tick=tracking.source_tick=1000+frame*10;tracking.seconds=1.+step*.01;tracking.tracking_revision=frame+1;
        driver.publish(tracking,{});const auto before=driver.solves();const auto key=frame+1;
        const auto posed_body=driver.body(native_body,identity,key);assert(posed_body);
        std::array<fb::ArmPose,2> arms;
        for(unsigned side=0;side<2;++side) {
            const auto result=driver.arm_frozen(side,native_arms[side],identity,tracking.tick,key);assert(result);arms[side]=*result;
            assert(result->tracking.tracking_revision==tracking.tracking_revision && result->owned==0);
        }
        assert(driver.solves()==before+1);const auto diagnostic=driver.diagnostic();
        assert(diagnostic.body_valid && diagnostic.arm_valid[0] && diagnostic.arm_valid[1]);
        const auto chest=fb::to_xf((*posed_body)[body_sk.find("center_spine03_jnt")]);
        for(unsigned side=0;side<2;++side) {
            const auto origin=fb::to_xf(arms[side].joints[arm_sk[side].find("origin")]);
            assert(length(origin.p-chest.p)<1e-5);
            const auto& b=bindings[side];const auto wrist=fb::to_xf(arms[side].joints[b.chain.wrist]);
            const auto want=body::mul(tracking.grip[side],body::mul(body::inverse(fb::to_xf(b.bind_grip)),fb::to_xf(b.bind_wrist)));
            assert(length(wrist.p-want.p)<.005);
            for(const Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})assert(length(rotated(wrist.q,axis)-rotated(want.q,axis))<.01);
            assert(!arms[side].tracking.native_socket_token[side] && arms[side].tracking.hand_empty[side]);
        }
        const auto right_wrist=fb::to_xf(arms[1].joints[bindings[1].chain.wrist]);
        const auto left_wrist=fb::to_xf(arms[0].joints[bindings[0].chain.wrist]);
        if(!frame)first_left_wrist=left_wrist;
        if(frame==11)assert(length(rotated(left_wrist.q,{1,0,0})-rotated(first_left_wrist.q,{1,0,0}))>.05);
        const auto right_shoulder=fb::to_xf(arms[1].joints[arm_sk[1].find("right_shoulder_jnt")]);
        const auto distal=fb::to_xf(arms[1].joints[bindings[1].finger_chains[1][2]]);
        const auto tip=distal.p+rotated(distal.q,posers[1].index_tip_local());assert(finite(tip));
        if(have_previous) {
            assert(length(right_wrist.p-previous_wrist.p)>.001);
            assert(length(rotated(right_wrist.q,{1,0,0})-rotated(previous_wrist.q,{1,0,0}))>.005);
            assert(length(tip-previous_tip)>.001);
            assert(length(right_shoulder.p-previous_shoulder.p)<.001); // the wrist moves without a detached shoulder bridge
        }
        previous_wrist=right_wrist;previous_shoulder=right_shoulder;previous_tip=tip;have_previous=true;
    }
    assert(left_bend>.01);
    AvatarCalibrationResult fit{1,1.6,.36,.22,1.08,{}};
    fit.torso_width_metres=.42;fit.torso_width_measured=true;driver.configure_calibration(fit,.36);
    tracking.tick=tracking.source_tick=1200;tracking.seconds=1.2;tracking.tracking_revision=20;
    driver.publish(tracking,{});const auto calibrated_body=driver.body(native_body,identity,20);assert(calibrated_body);
    const auto rows=[](const auto& joints) {
        std::vector<NoteNativeSkin::Rows> result(joints.size());
        for(std::size_t j=0;j<joints.size();++j)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
            result[j][row*4+col]=joints[j].f[col*4+row];
        return result;
    };
    std::array<rig::Transform,20> body_inverse{};
    for(unsigned j=0;j<20;++j)body_inverse[j]=fb::to_transform(body::inverse(body_sk.bind[j]));
    assert(NoteNativeSkin::pack(rows(*calibrated_body),rows(body_inverse)).size()==24);
    NoteViewMapping mapping{};mapping.active=true;mapping.anchor={Vec3{}, {Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}};
    for(unsigned side=0;side<2;++side) {
        const auto arm=driver.arm_frozen(side,native_arms[side],identity,tracking.tick,20);assert(arm);
        assert(!rig::valid(arm->joints[arm_sk[side].find("origin")])); // actual calibrated affine basis
        std::array<rig::Transform,69> inverse_palette{};note_hand::Mesh mesh{};
        for(unsigned j=0;j<69;++j)inverse_palette[j]=fb::to_transform(body::inverse(arm_sk[side].bind[j]));
        const auto inverse_rows=rows(inverse_palette);
        std::copy(inverse_rows.begin(),inverse_rows.end(),mesh.inverse.begin());
        assert(NoteNativeSkin::pack(rows(arm->joints),inverse_rows).size()==72);
        note_hand::Surface surface{};
        for(const unsigned joint:{static_cast<unsigned>(arm_sk[side].find("origin")),
            static_cast<unsigned>(arm_sk[side].find(side?"right_shoulder_jnt":"left_shoulder_jnt")),bindings[side].chain.wrist}) {
            note_hand::Vertex vertex{};vertex.position={.01f,.02f,.03f};vertex.weights[0]=255;
            vertex.joints[0]=static_cast<std::uint8_t>(joint);surface.vertices.push_back(vertex);
        }
        surface.indices={0,1,2};mesh.surfaces.push_back(std::move(surface));
        const auto witness=note_hand::skin(mesh,mesh.inverse,arm->joints,identity,mapping);
        assert(witness.vertices.size()==3);for(const auto& vertex:witness.vertices)assert(finite(vertex));
    }
    std::cout<<"PASS: shared native body/arms solve consumes note controls across 12 frames; connected chest roots, moving/rotating wrists, explicit fingers, same-palette fingertip and calibrated torso affine pack/skin; no graphics/headset proof\n";
}
