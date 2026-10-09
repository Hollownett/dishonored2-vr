#include "../src/arm_palette.hpp"
#include <cassert>
#include <iostream>

namespace r=d2vr::rig;
using d2vr::Vec3;
int main() {
    r::Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    for(auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},d2vr::unit(Vec3{1,2,-3})}) {
        auto swing=r::align_direction(axis,d2vr::unit(axis+Vec3{.3,-.1,.2}));
        for(double angle:{-3.,-1.5,-.5,0.,.5,1.5,3.}) {
            const d2vr::Quaternion twist{axis.x*std::sin(angle*.5),axis.y*std::sin(angle*.5),axis.z*std::sin(angle*.5),std::cos(angle*.5)};
            const auto target=r::turn_root(identity,d2vr::multiplied(swing,twist),{});
            assert(std::abs(r::forearm_roll(identity,target,axis)-angle)<1e-5);
        }
    }
    r::Upload native{};r::ArmHierarchy parents{};parents.fill(4);
    parents[0]=-1;parents[1]=0;parents[2]=1;parents[3]=2;parents[4]=3;
    for(auto& t:native.joints) {t=identity;t.f[14]=-.6f;}
    native.joints[0].f[14]=native.joints[1].f[14]=native.joints[2].f[14]=0;native.joints[3].f[14]=-.3f;
    const r::ForearmHelpers helpers{64,65,66,67};
    for(unsigned i=0;i<4;++i) {parents[helpers[i]]=3;native.joints[helpers[i]].f[14]=-.3f-.3f*(i+1)/4;}
    const auto original=native;
    const auto neutral=r::pose_arm(native,parents,{2,3,4},native.joints[4],{0,1,0},&helpers);
    for(unsigned j=0;j<69;++j) for(unsigned i=0;i<16;++i) assert(std::abs(neutral.joints[j].f[i]-native.joints[j].f[i])<1e-6);
    const auto target=r::turn_root(native.joints[4],{0,0,std::sin(.5),std::cos(.5)},r::position(native.joints[4]));
    const auto posed=r::pose_arm(native,parents,{2,3,4},target,{0,1,0},&helpers);
    for(unsigned i=0;i<4;++i) {
        const auto& t=posed.joints[helpers[i]];const double angle=(i+1)/4.;
        assert(d2vr::length(r::column(t,0)-Vec3{std::cos(angle),std::sin(angle),0})<1e-6);
        assert(d2vr::length(r::position(t)-r::position(native.joints[helpers[i]]))<1e-6);
        assert(r::valid(t));
    }
    assert(native.joints[0].f==original.joints[0].f);
    bool rejected=false;auto bad=helpers;bad[0]=4;
    try {r::pose_arm(native,parents,{2,3,4},target,{0,1,0},&bad);} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);rejected=false;
    bad=helpers;bad[1]=bad[0];
    try {r::pose_arm(native,parents,{2,3,4},target,{0,1,0},&bad);} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);rejected=false;
    try {r::forearm_roll(identity,target,{0,0,0});} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    std::cout<<"PASS: twist separated from swing across axes, weighted native helper roll, neutral/source preservation and invalid-owner/axis rejection\n";
}
