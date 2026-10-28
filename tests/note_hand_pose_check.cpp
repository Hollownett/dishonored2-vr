#include "../src/note_hand_pose.hpp"
#include "../src/full_body_driver.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
rig::ArmBinding binding_for(unsigned side) {
    const auto skeleton=fullbody::skeleton(side?fullbody::data::kRightArm:fullbody::data::kLeftArm);
    const std::string prefix=side?"right":"left";
    rig::ArmBinding binding{};
    binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
    binding.helpers={static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper1_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper2_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper3_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_rotation_jnt"))};
    std::array<rig::Transform,69> palette{};
    for(unsigned joint=0;joint<69;++joint) {binding.parents[joint]=static_cast<std::int16_t>(skeleton.parent[joint]);palette[joint]=fullbody::to_transform(skeleton.bind[joint]);}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
        binding.finger_chains[finger][segment]=static_cast<unsigned>(skeleton.find(prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt"));
    binding.palm={static_cast<unsigned>(skeleton.find(prefix+"_helper_underwrist_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_helper_upperwrist_jnt"))};
    binding.bind_wrist=palette[binding.chain.wrist];std::array<rig::Transform,4> knuckles{};
    for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
    binding.bind_grip=rig::hand_bind_grip(binding.bind_wrist,knuckles);
    binding.fingers=rig::finger_binding(palette,binding.parents,binding.finger_chains);
    return binding;
}
int main() {
    for(unsigned side=0;side<2;++side) {
        const auto binding=binding_for(side);NoteHandPose solver(binding);
        const auto& grip=binding.bind_grip;
        WorldTrackedPose palm{rig::position(grip),{rig::column(grip,0),rig::column(grip,1),rig::column(grip,2)}};
        const auto geometry=rig::hand_geometry(binding.fingers.open,binding.parents,binding.finger_chains,binding.palm,binding.chain.wrist,grip);
        const auto tablet=rig::note_tablet(geometry,palm);
        NotePanelReceipt page{};page.pose={tablet.centre,basis_quaternion(tablet.right,tablet.up,tablet.front)};
        page.width=.45;page.height=.253125;
        rig::FingerIkState state{};
        const auto posed=solver.pose(side,palm,page,state,0,.7f);
        rig::FingerIkState controls_state{};
        const auto controls=solver.controls(side,palm,page,controls_state,0,.7f);
        const auto& expected_palm=palm; // both hands are drawn at the tracked palm; the page rides the left one
        assert(length(controls.palm.position-expected_palm.position)<1e-12);
        for(unsigned a=0;a<3;++a)assert(length(controls.palm.basis[a]-expected_palm.basis[a])<1e-12);
        auto controlled=binding.fingers.open;
        for(unsigned f=0;f<5;++f)for(unsigned segment=0;segment<3;++segment) {
            const auto joint=binding.finger_chains[f][segment];
            const auto& rest=binding.fingers.open[joint];const auto current=controlled[joint];
            const auto axis=controls.axes[f][segment];
            const auto current_axis=rig::column(current,0)*dot(axis,rig::column(rest,0))+
                rig::column(current,1)*dot(axis,rig::column(rest,1))+rig::column(current,2)*dot(axis,rig::column(rest,2));
            rig::rotate_subtree(controlled,binding.parents,joint,rig::axis_angle(current_axis,controls.turns[f][segment]),rig::position(current));
        }
        rig::Transform controls_target{};controls_target.f[15]=1;
        for(unsigned a=0;a<3;++a) {
            controls_target.f[a*4]=static_cast<float>(controls.palm.basis[a].x);
            controls_target.f[a*4+1]=static_cast<float>(controls.palm.basis[a].y);
            controls_target.f[a*4+2]=static_cast<float>(controls.palm.basis[a].z);
        }
        controls_target.f[12]=static_cast<float>(controls.palm.position.x);
        controls_target.f[13]=static_cast<float>(controls.palm.position.y);
        controls_target.f[14]=static_cast<float>(controls.palm.position.z);
        for(unsigned j=0;j<69;++j)assert(rig::rigid_retarget(controlled[j],binding.bind_grip,controls_target).f==posed.joints[j].f);
        const auto& distal=posed.joints[binding.finger_chains[1][2]];const auto tip_local=solver.index_tip_local();
        assert(length(rig::position(distal)+rig::column(distal,0)*tip_local.x+rig::column(distal,1)*tip_local.y+
            rig::column(distal,2)*tip_local.z-posed.index_tip)<1e-12);
        assert(finite(posed.index_tip));
        for(unsigned j=0;j<69;++j) {
            assert(rig::valid(posed.joints[j]));
            assert(posed.hand[j]==rig::in_subtree(binding.parents,j,binding.chain.wrist));
        }
        if(side)assert(length(rig::position(posed.joints[binding.chain.wrist])-rig::position(binding.bind_wrist))<1e-6);
        else {
            const auto& held=palm;
            const auto attached=solver.page(palm,page.width,page.height);
            assert(length(attached.position-page.pose.position)<1e-9);
            for(const Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})assert(length(rotated(attached.orientation,axis)-rotated(page.pose.orientation,axis))<1e-6);
            rig::Transform target{};target.f[15]=1;
            for(unsigned a=0;a<3;++a) {
                target.f[a*4]=static_cast<float>(held.basis[a].x);target.f[a*4+1]=static_cast<float>(held.basis[a].y);target.f[a*4+2]=static_cast<float>(held.basis[a].z);
            }
            target.f[12]=static_cast<float>(held.position.x);target.f[13]=static_cast<float>(held.position.y);target.f[14]=static_cast<float>(held.position.z);
            const auto wrist=rig::rigid_retarget(binding.bind_wrist,binding.bind_grip,target);
            assert(length(rig::position(posed.joints[binding.chain.wrist])-rig::position(wrist))<1e-6);
            const auto page_up=rotated(page.pose.orientation,{0,1,0});
            const auto page_right=rotated(page.pose.orientation,{1,0,0});
            assert(dot(rig::position(wrist)-page.pose.position,page_right)<-page.width*.5); // the wrist stays outside the left edge
            const auto tip=posed.index_tip-page.pose.position;
            const auto front=rotated(page.pose.orientation,{0,0,1}),right=rotated(page.pose.orientation,{1,0,0});
            assert(dot(tip,front)<-rig::note_tablet_thickness); // fingers behind, thumb opposition in front
            assert(std::abs(dot(tip,right))<page.width*.5 && std::abs(dot(tip,page_up))<page.height*.5);
        }
        const auto native_wrist=rig::position(binding.bind_wrist)-rig::position(binding.bind_grip);
        assert(dot(native_wrist,rig::column(binding.bind_grip,2))>.05); // real native wrist points toward +grip-up
        for(const Vec3 head_position:{Vec3{0,1.65,0},palm.position,palm.position+Vec3{0,0,4}}) {
            rig::FingerIkState sleeve_state{};
            const auto sleeve=solver.pose(side,palm,page,sleeve_state,0,.7f,TrackedPose{head_position,{}});
            assert(length(sleeve.index_tip-posed.index_tip)<1e-12);
            for(unsigned joint=0;joint<69;++joint) {
                assert(rig::valid(sleeve.joints[joint]));
                if(posed.hand[joint])assert(sleeve.joints[joint].f==posed.joints[joint].f); // wrist and every finger bit-identical
            }
            const auto& chain=binding.chain;
            const double native_upper=length(rig::position(binding.fingers.open[chain.elbow])-rig::position(binding.fingers.open[chain.upper]));
            const double native_lower=length(rig::position(binding.fingers.open[chain.wrist])-rig::position(binding.fingers.open[chain.elbow]));
            assert(std::abs(length(rig::position(sleeve.joints[chain.elbow])-rig::position(sleeve.joints[chain.upper]))-native_upper)<1e-5);
            assert(std::abs(length(rig::position(sleeve.joints[chain.wrist])-rig::position(sleeve.joints[chain.elbow]))-native_lower)<1e-5);
        }
        const Quaternion q=normalized({.2,.3,-.4,.7});const Vec3 shift{.3,1.2,-.7};
        auto moved=palm;moved.position=shift+rotated(q,palm.position);
        for(unsigned a=0;a<3;++a)moved.basis[a]=rotated(q,palm.basis[a]);
        auto moved_page=page;moved_page.pose.position=shift+rotated(q,page.pose.position);
        moved_page.pose.orientation=normalized(multiplied(q,page.pose.orientation));
        rig::FingerIkState moved_state{};
        const auto carried=solver.pose(side,moved,moved_page,moved_state,0,.7f);
        if(side) {
            rig::FingerIkState carried_controls_state{};
            const auto carried_controls=solver.controls(side,moved,moved_page,carried_controls_state,0,.7f);
            assert(length(carried_controls.palm.position-moved.position)<1e-12);
            assert(carried_controls.turns[1]==controls.turns[1]); // touch index is independent of the world pose
        }
        assert(length(carried.index_tip-shift-rotated(q,posed.index_tip))<1e-6);
        for(unsigned j=0;j<69;++j)assert(length(rig::position(carried.joints[j])-shift-rotated(q,rig::position(posed.joints[j])))<1e-6);
        if(side) {
            rig::Upload upload{};upload.joints=binding.fingers.open;upload.model_frame={0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
            rig::ArmTrackingSnapshot frozen{};frozen.frame.tick=1000;frozen.frame.epoch=1;frozen.frame.hands=2;
            frozen.frame.grips[1]=moved;frozen.frame.has_hand_state=true;
            const auto gameplay=rig::ArmTracking::pose_frozen(upload,binding,1,1000,frozen);assert(gameplay);
            for(unsigned a=0;a<16;++a)assert(std::abs(gameplay->joints[binding.chain.wrist].f[a]-carried.joints[binding.chain.wrist].f[a])<1e-6);
        }
        if(!side) {
            rig::FingerIkState different_state{};auto different=page;different.pose.position=different.pose.position+tablet.front*.08;
            const auto changed=solver.pose(0,palm,different,different_state,0,.7f);
            double delta=0;for(unsigned j=0;j<69;++j)delta+=length(rig::position(changed.joints[j])-rig::position(posed.joints[j]));
            assert(delta>.001); // actual page position drives fingers, not regenerated tablet placement
        } else {
            rig::FingerIkState other_state{};const auto relaxed=solver.pose(1,palm,page,other_state,0,0);
            assert(length(relaxed.index_tip-posed.index_tip)<1e-6); // squeezing never bends touch index
        }
        NoteHandPose scaled(binding,.9);rig::FingerIkState scaled_state{};
        assert(length(scaled.index_tip_local()-solver.index_tip_local()*.9)<1e-7);
        const auto smaller=scaled.pose(1,palm,page,scaled_state,0,.7f);
        assert(std::abs(length(smaller.index_tip-palm.position)-.9*length(solver.pose(1,palm,page,state,0,.7f).index_tip-palm.position))<1e-6);
        if(!side)for(double scale:{.9,1.,1.1})for(double text_scale:{50./65,1.,100./65}) {
            NoteHandPose fit(binding,scale);rig::FingerIkState fit_state{};
            auto actual=page;actual.width=.45*text_scale;actual.height=.253125*text_scale;actual.pose=fit.page(palm,actual.width,actual.height);
            const auto held=fit.pose(0,palm,actual,fit_state,0);
            const auto up=rotated(actual.pose.orientation,{0,1,0}),right=rotated(actual.pose.orientation,{1,0,0}),front=rotated(actual.pose.orientation,{0,0,1});
            assert(dot(rig::position(held.joints[binding.chain.wrist])-actual.pose.position,right)<-actual.width*.5); // held by its left edge
            const auto tip=held.index_tip-actual.pose.position;
            assert(dot(tip,front)<-rig::note_tablet_thickness);
            assert(std::abs(dot(tip,right))<actual.width*.5 && std::abs(dot(tip,up))<actual.height*.5);
        }
    }
    std::cout<<"PASS: native bind menu hand posing, actual-page contact, XR carry, right touch index, explicit scale\n";
}
