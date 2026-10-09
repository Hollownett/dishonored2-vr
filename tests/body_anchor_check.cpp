#include "../src/body_anchor.hpp"
#include <cassert>
#include <iostream>

int main() {
    d2vr::NativePlayerCamera state{};state.player=123;state.camera=456;state.camera_identity.generation=4;
    const double yaw=.7;
    state.entity_axis={static_cast<float>(std::cos(yaw)),static_cast<float>(std::sin(yaw)),0,
        static_cast<float>(-std::sin(yaw)),static_cast<float>(std::cos(yaw)),0,0,0,1};
    d2vr::GameCameraPose source{};source.horizontal_fov=1.4f;source.vertical_fov=1;
    source.position={10,20,30};source.basis={0,0,1,1,0,0,0,1,0};
    d2vr::BodyAnchor anchor;
    const auto left=anchor.get(10,1,state,source);assert(left);
    assert(d2vr::length(d2vr::Vec3{left->basis[0],left->basis[1],left->basis[2]}-d2vr::Vec3{std::cos(yaw),std::sin(yaw),0})<1e-6);
    // Animation and an entity update between eye callbacks cannot split a pair.
    source.position={11,22,33};source.basis={1,0,0,0,1,0,0,0,1};
    state.entity_axis={0,1,0,-1,0,0,0,0,1};
    const auto right=anchor.get(10,1,state,source);assert(right && right->position==left->position && right->basis==left->basis);
    const auto next=anchor.get(11,1,state,source);assert(next && next->position==source.position && next->basis==state.entity_axis);
    state.entity_axis={1,0,0,0,1,0,0,0,1};
    const auto recentered=anchor.get(11,2,state,source);assert(recentered && recentered->basis==state.entity_axis);
    state.camera_identity.generation=5;source.position[0]=40;
    assert(anchor.get(11,2,state,source)->position[0]==40);
    // A replacement can restart its generation counter within the same frame.
    state.camera=789;source.position[0]=45;
    assert(anchor.get(11,2,state,source)->position[0]==45);
    state.camera=0;assert(!anchor.get(11,2,state,source));state.camera=789;
    state.entity_axis[0]=2;assert(!anchor.get(12,2,state,source));
    state.entity_axis={1,0,0,0,1,0,0,0,1};state.code_lock=true;assert(anchor.get(13,2,state,source));
    state.code_lock=false;assert(!anchor.get(14,0,state,source));
    for(auto field:{&state.mode,&state.target_mode}) {
        *field=2;assert(state.authored_view() && anchor.get(14,2,state,source));*field=0;
    }
    state.blend_body_yaw=true;assert(state.authored_view());state.blend_body_yaw=false;
    assert(!state.authored_view());
    assert(anchor.get(14,3,state,source));anchor.clear();source.position[0]=50;
    assert(anchor.get(14,3,state,source)->position[0]==50);
    state.mode=2;source.basis={0,1,0,-1,0,0,0,0,1};
    const auto authored=anchor.get(15,3,state,source);assert(authored && authored->basis==source.basis && authored->position==source.position);
    state.mode=0;state.entity_axis=source.basis;
    const auto compensated=anchor.get(16,3,state,source,1.5707963267948966);
    assert(compensated && std::abs(compensated->basis[0]-1)<1e-6 && std::abs(compensated->basis[1])<1e-6);
    std::cout<<"PASS: owned heading, consistent stereo snapshot, authored native origin/yaw, body-follow compensation and invalid basis rejection\n";
}
