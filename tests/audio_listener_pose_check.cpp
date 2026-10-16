#include "../src/audio_listener_pose.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace {
void near(const std::array<float,3>& actual,d2vr::Vec3 expected) {
    assert(d2vr::length(d2vr::Vec3{actual[0],actual[1],actual[2]}-expected)<1e-5);
}
}
int main() {
    using namespace d2vr;
    GameCameraPose anchor{};anchor.position={10,20,30};anchor.basis={1,0,0,0,1,0,0,0,1};
    TrackedPose origin{{2,3,4},{0,0,0,1}},centre=origin;
    const Vec3 offset{.2,-.3,.4};
    auto pose=head_audio_pose(centre,origin,anchor,offset);assert(pose);
    near(pose->position,{9.6,19.8,29.7});near(pose->front,{1,0,0});near(pose->top,{0,0,1});
    // Only admitted translation is used: raw tracking movement and any eye
    // separation are deliberately absent from the listener contract.
    centre.position={100,200,300};
    assert(head_audio_pose(centre,origin,anchor,offset)->position==pose->position);
    constexpr double angle=.6;const double s=std::sin(angle),c=std::cos(angle);
    for(unsigned axis=0;axis<3;++axis) {
        centre.orientation={0,0,0,std::cos(angle/2)};
        if(axis==0) centre.orientation.x=std::sin(angle/2);
        if(axis==1) centre.orientation.y=std::sin(angle/2);
        if(axis==2) centre.orientation.z=std::sin(angle/2);
        pose=head_audio_pose(centre,origin,anchor,offset);assert(pose);
        if(axis==0) {near(pose->front,{c,0,s});near(pose->top,{-s,0,c});}
        if(axis==1) {near(pose->front,{c,s,0});near(pose->top,{0,0,1});}
        if(axis==2) {near(pose->front,{1,0,0});near(pose->top,{0,s,c});}
    }
    origin.orientation={0,std::sin(angle/2),0,std::cos(angle/2)};
    centre.orientation=origin.orientation;
    pose=head_audio_pose(centre,origin,anchor,{});assert(pose);near(pose->front,{1,0,0});
    GameCameraPose authored=anchor;authored.position={50,60,70};
    authored.basis={static_cast<float>(c),0,static_cast<float>(-s),0,1,0,
        static_cast<float>(s),0,static_cast<float>(c)};
    pose=head_audio_pose(centre,origin,anchor,offset,&authored);assert(pose);
    assert(pose->position==authored.position);near(pose->front,{c,0,-s});near(pose->top,{s,0,c});
    auto bad=authored;bad.basis[0]=2;assert(!head_audio_pose(centre,origin,anchor,{},&bad));
    bad=authored;bad.basis[3]=bad.basis[0];bad.basis[4]=0;bad.basis[5]=bad.basis[2];
    assert(!head_audio_pose(centre,origin,anchor,{},&bad));
    bad=anchor;bad.basis[8]=-1;assert(!head_audio_pose(centre,origin,anchor,{},&bad));
    bad=anchor;bad.position[0]=std::numeric_limits<float>::infinity();
    assert(!head_audio_pose(centre,origin,anchor,{},&bad));
    bad=anchor;bad.position[0]=1e8f;assert(!head_audio_pose(centre,origin,anchor,{},&bad));
    centre.orientation={0,0,0,0};assert(!head_audio_pose(centre,origin,anchor,{}));
    centre.orientation={0,0,0,1};origin.orientation={.1,0,0,std::sqrt(.99)};
    assert(!head_audio_pose(centre,origin,anchor,{}));
    origin.orientation={0,0,0,1};
    assert(!head_audio_pose(centre,origin,anchor,{std::numeric_limits<double>::quiet_NaN(),0,0}));
    std::cout<<"PASS: admitted physical centre, yaw/pitch/roll, yaw recenter, complete authored native camera, invalid geometry\n";
}
