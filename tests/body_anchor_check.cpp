#include "../src/body_anchor.hpp"
#include <cassert>
#include <iostream>

int main() {
    // A tilted native finisher keeps its complete basis and stereo separation.
    d2vr::GameCameraPose action{};action.position={10,20,30};
    const double tilt=.6;
    action.basis={static_cast<float>(std::cos(tilt)),0,static_cast<float>(-std::sin(tilt)),
        0,1,0,static_cast<float>(std::sin(tilt)),0,static_cast<float>(std::cos(tilt))};
    const d2vr::TrackedPose centre{{2,3,4},{0,0,0,1}};
    auto eye=centre;eye.position.x-=.032;
    const auto native_left=d2vr::authored_eye_camera(action,centre,eye);assert(native_left);
    eye.position.x+=.064;
    const auto native_right=d2vr::authored_eye_camera(action,centre,eye);assert(native_right);
    assert(native_left->basis==action.basis && native_right->basis==action.basis);
    assert(std::abs(native_left->position[1]-native_right->position[1]-.064)<1e-5);
    // Head orientation does not replace the native animation; rotated XR IPD
    // still maps to the same native eye separation.
    auto turned=centre;turned.orientation={0,std::sin(.5),0,std::cos(.5)};
    auto turned_eye=turned;turned_eye.position=turned.position+d2vr::rotated(turned.orientation,{-.032,0,0});
    const auto rotated_left=d2vr::authored_eye_camera(action,turned,turned_eye);assert(rotated_left);
    assert(rotated_left->position==native_left->position);
    eye.position.x+=1;assert(!d2vr::authored_eye_camera(action,centre,eye));
    auto bad_action=action;bad_action.basis[0]=2;assert(!d2vr::authored_eye_camera(bad_action,centre,centre));
    assert(std::abs(std::tan(d2vr::authored_fov_angle(-.7))/std::tan(-.7)-1.05)<1e-9);
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
    d2vr::AuthoredCameraAnchor authored_pair;
    auto native_a=action,native_b=action;native_a.position[1]+=.032f;native_b.position[1]-=.032f;
    const auto first=authored_pair.get(17,3,state,native_a,native_a,native_b);assert(first);
    assert(first->basis==action.basis && first->position==action.position);
    // An authored camera advances between callbacks: both rendered eyes still
    // use the same snapshot, including its complete native pitch and roll.
    auto later=native_b;later.position[0]+=1;
    const auto second=authored_pair.get(17,3,state,later,native_a,later);assert(second);
    assert(second->basis==first->basis && second->position==first->position);
    assert(!authored_pair.get(18,3,state,native_a,native_a,later)); // mixed-time prepared pair rejected
    assert(authored_pair.get(18,3,state,native_a,native_a,native_b));
    ++state.camera_identity.generation;
    native_a.position[0]+=2;native_b.position[0]+=2;
    const auto recycled=authored_pair.get(18,3,state,native_a,native_a,native_b);
    assert(recycled && std::abs(recycled->position[0]-12)<1e-6);
    authored_pair.clear();native_b.basis=source.basis;
    assert(!authored_pair.get(19,3,state,native_a,native_a,native_b));
    std::cout<<"PASS: owned heading, consistent stereo snapshot, authored native origin/yaw, body-follow compensation and invalid basis rejection\n";
}
