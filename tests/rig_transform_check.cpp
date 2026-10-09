#include "../src/rig_transform.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace r=d2vr::rig;
int main() {
    r::Transform joint{};joint.f[0]=joint.f[5]=joint.f[10]=1;
    joint.f[12]=1;joint.f[13]=2;joint.f[14]=3;
    joint.f[3]=joint.f[7]=joint.f[11]=joint.f[15]=std::numeric_limits<float>::quiet_NaN();
    const auto packed=r::affine_rows(joint);
    assert((packed==std::array<float,12>{1,0,0,1,0,1,0,2,0,0,1,3}));
    // Native model formula: local X/Y/Z follow axis rows, not their transpose.
    r::ModelFrame model({10,20,30,0,1,0,-1,0,0,0,0,1,2,3,4});
    auto world=model.world_point({1,2,3});
    assert(world.x==4 && world.y==22 && world.z==42);
    auto local=model.model_point(world);assert(d2vr::length(local-d2vr::Vec3{1,2,3})<1e-10);
    auto direction=model.world_direction({1,0,0});assert(direction.x==0 && direction.y==1);
    assert(d2vr::length(model.model_direction(direction)-d2vr::Vec3{1,0,0})<1e-10);
    auto target=joint;target.f[0]=0;target.f[1]=1;target.f[4]=-1;target.f[5]=0;
    target.f[12]=4;target.f[13]=5;target.f[14]=6;
    auto finger=joint;finger.f[12]+=1;
    const auto wrist=r::rigid_retarget(joint,joint,target),moved=r::rigid_retarget(finger,joint,target);
    assert(r::position(wrist).x==4 && r::position(wrist).y==5 && r::position(wrist).z==6);
    assert(r::position(moved).x==4 && r::position(moved).y==6 && r::position(moved).z==6);
    assert(moved.f[0]==0 && moved.f[1]==1 && d2vr::length(r::position(moved)-r::position(wrist))==1);
    assert(std::isnan(moved.f[3]) && std::isnan(moved.f[15]));
    bool rejected=false;
    try {r::ModelFrame invalid({0,0,0,1,0,0,0,1,0,0,0,-1,1,1,1});} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);rejected=false;target.f[0]=2;
    try {r::rigid_retarget(joint,joint,target);} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    std::cout << "PASS: native joint packing, scaled model/world mapping, wrist/finger rigid delta and invalid pose rejection\n";
}
