#include "../src/hand_palette_contact.hpp"
#include "../src/full_body_driver.hpp"
#include <cassert>
#include <iostream>

namespace {
using namespace d2vr;
const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
rig::ArmBinding binding_for(unsigned side) {
    const auto skeleton=fullbody::skeleton(side?fullbody::data::kRightArm:fullbody::data::kLeftArm);
    const std::string prefix=side?"right":"left";
    rig::ArmBinding binding{};
    binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
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
WorldTrackedPose palm_for(const rig::ArmBinding& binding,const std::array<rig::Transform,69>& palette) {
    std::array<rig::Transform,4> knuckles{};for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
    const auto palm=rig::hand_bind_grip(palette[binding.chain.wrist],knuckles);
    return {rig::position(palm),{rig::column(palm,0),rig::column(palm,1),rig::column(palm,2)}};
}
void centres(const rig::ArmBinding& binding,const std::array<rig::Transform,69>& palette,const HandProxies& proxies,const std::array<float,15>& frame) {
    const rig::ModelFrame model(frame);
    for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
        assert(length(proxies[4+finger*4+segment].centre-model.world_point(rig::position(palette[binding.finger_chains[finger][segment]])))<1e-7);
}
void native_and_posed(unsigned side) {
    const auto binding=binding_for(side);const auto shape=HandPaletteContact::bind(binding);assert(shape);
    const auto& open=binding.fingers.open;
    const auto proxies=shape->proxies(open,identity);assert(proxies && proxies->size()==24);centres(binding,open,*proxies,identity);
    const auto geometry=rig::hand_geometry(open,binding.parents,binding.finger_chains,binding.palm,binding.chain.wrist,binding.bind_grip);
    const auto expected=hand_proxies(geometry,palm_for(binding,open),HandJointTurns{});
    for(unsigned index=0;index<24;++index) {assert(length((*proxies)[index].centre-expected[index].centre)<1e-6);assert((*proxies)[index].radius==expected[index].radius);}
    const auto curled=rig::procedural_curl(open,binding.parents,binding.finger_chains,binding.palm,binding.chain.wrist,{.8f,.3f,.9f,.6f,1.f});
    const auto curled_proxies=shape->proxies(curled,identity);assert(curled_proxies);centres(binding,curled,*curled_proxies,identity);
    std::array<float,69> blends{};blends.fill(.37f);
    const auto blended=rig::blend_finger_palette(open,open,curled,binding.parents,binding.fingers.mask,blends);
    const auto blended_proxies=shape->proxies(blended,identity);assert(blended_proxies);centres(binding,blended,*blended_proxies,identity);
    auto changed=open;const auto distal=binding.finger_chains[1][2];
    rig::rotate_subtree(changed,binding.parents,distal,rig::axis_angle(rig::column(open[distal],0),.9),rig::position(open[distal]));
    const auto changed_proxies=shape->proxies(changed,identity);assert(changed_proxies);
    assert(length((*changed_proxies)[11].centre-(*proxies)[11].centre)>.005);
    double best_scalar_error=1.;
    for(unsigned sample=0;sample<=100;++sample) {
        std::array<float,5> curl{};curl[1]=static_cast<float>(sample)*.01f;
        const auto scalar=hand_proxies(geometry,palm_for(binding,open),curl);double error{};
        for(unsigned segment=0;segment<4;++segment)error+=length(scalar[8+segment].centre-(*changed_proxies)[8+segment].centre);
        best_scalar_error=std::min(best_scalar_error,error);
    }
    assert(best_scalar_error>.005);
    const std::array<float,15> frame{4,5,6,0,1,0,-1,0,0,0,0,1,1.7f,1.7f,1.7f};
    const auto moved=shape->proxies(blended,frame);assert(moved);const rig::ModelFrame model(frame);
    for(unsigned index=0;index<24;++index) {
        assert(length((*moved)[index].centre-model.world_point((*blended_proxies)[index].centre))<1e-6);
        assert(std::abs((*moved)[index].radius-(*blended_proxies)[index].radius*model.scale[0])<1e-9);
    }
    auto rigid=blended;rig::Transform target{};target.f[0]=target.f[5]=target.f[10]=1;target.f[12]=2;target.f[13]=-3;
    rig::Transform origin{};origin.f[0]=origin.f[5]=origin.f[10]=1;
    for(auto& joint:rigid)joint=rig::rigid_retarget(joint,origin,target);
    const auto translated=shape->proxies(rigid,identity);assert(translated);
    for(unsigned index=0;index<24;++index)assert(length((*translated)[index].centre-((*blended_proxies)[index].centre+Vec3{2,-3,0}))<1e-6);
    auto bad_frame=frame;bad_frame[13]+=.00001f;assert(!shape->proxies(open,bad_frame));
    bad_frame=identity;bad_frame[0]=NAN;assert(!shape->proxies(open,bad_frame));
    auto bad=open;bad[distal].f[0]=NAN;assert(!shape->proxies(bad,identity));
    bad=open;for(unsigned component=0;component<3;++component)bad[distal].f[component]*=1.2f;assert(!shape->proxies(bad,identity));
    auto bad_binding=binding;bad_binding.finger_chains[1][2]=69;assert(!HandPaletteContact::bind(bad_binding));
    bad_binding=binding;bad_binding.finger_chains[1]=binding.finger_chains[2];assert(!HandPaletteContact::bind(bad_binding));
    bad_binding=binding;bad_binding.parents[distal]=static_cast<std::int16_t>(binding.chain.wrist);assert(!HandPaletteContact::bind(bad_binding));
    auto no_helpers=binding;no_helpers.palm={};const auto fallback=HandPaletteContact::bind(no_helpers);assert(fallback && fallback->proxies(open,identity));
    auto no_tip=binding;
    for(unsigned joint=0;joint<69;++joint)if(no_tip.parents[joint]==static_cast<int>(distal)) {
        no_tip.fingers.open[joint].f[12]=open[distal].f[12];no_tip.fingers.open[joint].f[13]=open[distal].f[13];no_tip.fingers.open[joint].f[14]=open[distal].f[14];
    }
    const auto tip_fallback=HandPaletteContact::bind(no_tip);assert(tip_fallback);
    const auto fallback_geometry=rig::hand_geometry(no_tip.fingers.open,no_tip.parents,no_tip.finger_chains,no_tip.palm,no_tip.chain.wrist,no_tip.bind_grip);
    assert(length(fallback_geometry.finger[1].bone[2]-fallback_geometry.finger[1].bone[1]*.8)<1e-6);
    assert(tip_fallback->proxies(no_tip.fingers.open,identity));
}
}
int main() {native_and_posed(0);native_and_posed(1);std::cout<<"hand palette contact check passed\n";}
