#include "../src/arm_palette.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstring>

namespace r=d2vr::rig;
int main(int argc,char** argv) {
    if(argc!=1 && argc!=2) return 2;
    r::Upload native{};r::ArmHierarchy hierarchy{};
    hierarchy.fill(4);hierarchy[0]=-1;hierarchy[1]=0;hierarchy[2]=1;hierarchy[3]=2;hierarchy[4]=3;
    for(unsigned i=64;i<68;++i) hierarchy[i]=3;hierarchy[68]=2;
    for(auto& t:native.joints) {t.f[0]=t.f[5]=t.f[10]=1;t.f[14]=-.6f;}
    native.joints[0].f[14]=native.joints[1].f[14]=native.joints[2].f[14]=0;
    native.joints[3].f[14]=-.3f;native.joints[5].f[12]=.1f;
    for(unsigned i=64;i<68;++i) native.joints[i].f[14]=-.4f;
    native.joints[68].f[14]=-.1f;
    auto target=native.joints[4];target.f[12]=.3f;target.f[14]=-.3f;
    target.f[0]=0;target.f[1]=1;target.f[4]=-1;target.f[5]=0;
    const auto posed=r::pose_arm(native,hierarchy,{2,3,4},target,{0,-1,0});
    assert(!posed.reach_clamped);
    assert(d2vr::length(r::position(posed.joints[4])-r::position(target))<1e-6);
    assert(std::abs(d2vr::length(r::position(posed.joints[3])-r::position(posed.joints[2]))-.3)<1e-6);
    assert(std::abs(d2vr::length(r::position(posed.joints[4])-r::position(posed.joints[3]))-.3)<1e-6);
    assert(d2vr::length(r::position(posed.joints[5])-r::position(posed.joints[4])-d2vr::Vec3{0,.1,0})<1e-6);
    assert(posed.joints[5].f[0]==0 && posed.joints[5].f[1]==1);
    auto hand=r::pose_hand(native,hierarchy,{2,3,4},target);
    r::hide_arm(hand,hierarchy,4);
    // A native block animation may move the complete rig; its wrist transform
    // must never compete with the controller or change the local finger pose.
    auto animated=native;
    const auto animated_wrist=r::turn_root(native.joints[4],{0,0,std::sin(.5),std::cos(.5)},{8,3,2});
    for(unsigned joint=0;joint<69;++joint)
        animated.joints[joint]=r::rigid_retarget(native.joints[joint],native.joints[4],animated_wrist);
    auto blocked_hand=r::pose_hand(animated,hierarchy,{2,3,4},target);
    r::hide_arm(blocked_hand,hierarchy,4);
    for(unsigned joint=0;joint<69;++joint) {
        assert(d2vr::length(r::position(hand.joints[joint])-r::position(blocked_hand.joints[joint]))<1e-5);
        for(unsigned axis=0;axis<3;++axis)
            assert(d2vr::length(r::column(hand.joints[joint],axis)-r::column(blocked_hand.joints[joint],axis))<1e-5);
        if(r::in_subtree(hierarchy,joint,4)) assert(r::valid(hand.joints[joint]));
        else {
            assert(d2vr::length(r::position(hand.joints[joint])-r::position(target))<1e-6);
            assert(std::abs(d2vr::length(r::column(hand.joints[joint],0))-.02)<1e-6);
        }
    }
    assert(posed.joints[0].f==native.joints[0].f && posed.joints[1].f==native.joints[1].f);
    assert(std::abs(d2vr::length(r::position(posed.joints[64])-r::position(posed.joints[3]))-.1)<1e-6);
    assert(std::abs(d2vr::length(r::position(posed.joints[68])-r::position(posed.joints[2]))-.1)<1e-6);
    for(const auto& t:posed.joints) assert(r::valid(t));
    const auto fitted=r::calibrated_arm_source(native,hierarchy,{2,3,4},{.05,.02,.1},1.2);
    assert(d2vr::length(r::position(fitted.joints[2])-d2vr::Vec3{.05,.02,.1})<1e-6);
    assert(std::abs(d2vr::length(r::position(fitted.joints[3])-r::position(fitted.joints[2]))-.36)<1e-6);
    assert(std::abs(d2vr::length(r::position(fitted.joints[4])-r::position(fitted.joints[3]))-.36)<1e-6);
    assert(d2vr::length(r::position(fitted.joints[5])-r::position(fitted.joints[4])-d2vr::Vec3{.1,0,0})<1e-6);
    // Root and clavicle translate with the fitted limb so the sleeve cut stays attached.
    assert(d2vr::length(r::position(fitted.joints[1])-r::position(fitted.joints[2])-(r::position(native.joints[1])-r::position(native.joints[2]))*1.2)<1e-6);
    bool excessive_reach=false;auto distant=target;distant.f[12]=1;
    try {(void)r::pose_arm(native,hierarchy,{2,3,4},distant,{0,-1,0},nullptr,true,1.08);} catch(const std::invalid_argument&) {excessive_reach=true;}
    assert(excessive_reach);
    target.f[12]=20;target.f[14]=0;auto clamped=r::pose_arm(native,hierarchy,{2,3,4},target,{0,-1,0});
    assert(clamped.reach_clamped && std::abs(d2vr::length(r::position(clamped.joints[4]))-.6)<1e-6);
    auto reverse=r::align_direction({0,0,-1},{0,0,1});assert(d2vr::length(d2vr::rotated(reverse,{0,0,-1})-d2vr::Vec3{0,0,1})<1e-6);
    bool rejected=false;hierarchy[3]=1;
    try {r::pose_arm(native,hierarchy,{2,3,4},target,{0,-1,0});} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    std::cout << "PASS: hands-only hides both arm segments, ignores native block motion, preserves fingers; connected arm IK, reach clamp and invalid chain rejection\n";
    if(argc==2) {
        std::ifstream input(argv[1],std::ios::binary);char magic[8]{};input.read(magic,8);
        if(!input || std::memcmp(magic,"D2VRAF01",8)) throw std::runtime_error("Invalid offline arm fixture");
        for(unsigned side=0;side<2;++side) {
            r::Upload captured{};r::ArmHierarchy parents{};std::array<std::uint32_t,3> indices{};
            input.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));input.read(reinterpret_cast<char*>(indices.data()),sizeof(indices));
            input.read(reinterpret_cast<char*>(captured.joints.data()),sizeof(captured.joints));input.read(reinterpret_cast<char*>(captured.model_frame.data()),sizeof(captured.model_frame));
            if(!input || !r::valid_hierarchy(parents) || indices[0]>=69 || indices[1]>=69 || indices[2]>=69)
                throw std::runtime_error("Incomplete offline arm fixture");
            const r::ArmChain chain{indices[0],indices[1],indices[2]};const r::ModelFrame model(captured.model_frame);
            const auto shoulder=r::position(captured.joints[chain.upper]),elbow=r::position(captured.joints[chain.elbow]),wrist=r::position(captured.joints[chain.wrist]);
            const auto upper_length=d2vr::length(elbow-shoulder),fore_length=d2vr::length(wrist-elbow);
            for(unsigned step=0;step<360;++step) {
                const auto angle=step*6.283185307179586/360;
                const auto requested=wrist+d2vr::Vec3{.15*std::cos(angle),.15*std::sin(angle),.07*std::sin(angle*2)};
                const auto target_pose=r::turn_root(captured.joints[chain.wrist],{0,0,std::sin(angle*.5),std::cos(angle*.5)},requested);
                const auto result=r::pose_arm(captured,parents,chain,target_pose,elbow-shoulder);
                const auto a=r::position(result.joints[chain.upper]),b=r::position(result.joints[chain.elbow]),c=r::position(result.joints[chain.wrist]);
                assert(d2vr::length(a-shoulder)<1e-6 && std::abs(d2vr::length(b-a)-upper_length)<1e-5 && std::abs(d2vr::length(c-b)-fore_length)<1e-5);
                assert(d2vr::length(model.model_point(model.world_point(c))-c)<1e-5);
                for(unsigned joint=0;joint<69;++joint) {
                    assert(r::valid(result.joints[joint]));
                    if(r::in_subtree(parents,joint,chain.wrist))
                        assert(std::abs(d2vr::length(r::position(result.joints[joint])-c)-d2vr::length(r::position(captured.joints[joint])-wrist))<1e-5);
                }
            }
        }
        if(input.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Extra offline fixture bytes");
        std::cout << "PASS: 720 wrist targets on actual native upload palettes, both arm chains, model/world conversion and preserved finger/attachment offsets; no game writes\n";
    }
}
