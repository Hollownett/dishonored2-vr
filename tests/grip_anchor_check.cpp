#include "../src/grip_anchor.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace d2vr;
namespace {
GripBodyId identity() {return {0x10000,0x20000,0x30000,0x12000002,7,1,1};}
WorldTrackedPose pose(Vec3 position={},Vec3 axis={0,0,1},double angle=0) {
    axis=unit(axis);const auto rotate=[&](Vec3 v){return v*std::cos(angle)+cross(axis,v)*std::sin(angle)+axis*(dot(axis,v)*(1-std::cos(angle)));};
    return {position,{rotate({1,0,0}),rotate({0,1,0}),rotate({0,0,1})}};
}
bool near(Vec3 a,Vec3 b,double tolerance=1e-8) {return length(a-b)<tolerance;}
bool near(const WorldTrackedPose& a,const WorldTrackedPose& b) {
    return near(a.position,b.position) && near(a.basis[0],b.basis[0]) && near(a.basis[1],b.basis[1]) && near(a.basis[2],b.basis[2]);
}
GripBodyMotion body(const WorldTrackedPose& frame=pose()) {return {identity(),frame,frame.position,{},{},1,{1,1,1}};}
void arbitrary_frames() {
    for(unsigned i=0;i<1000;++i) {
        const double t=static_cast<double>(i)*.013;
        const auto original=pose({std::sin(t)*5,std::cos(t)*3,.5+t*.01},{1,2,3},t);
        const auto hand=grip_compose(original,pose({.04,-.08,.03},{3,1,2},t*.3));
        const auto contact=hand.position+hand.basis[0]*.05;
        const auto anchor=GripAnchor::capture(identity(),original,hand,contact,hand.basis[1]);assert(anchor);
        assert(near(*anchor->hand_target(identity(),original),hand));
        assert(near(*anchor->body_target(hand),original));
        const auto actual=pose({t*.2,-t*.1,2},{1,-2,1},t*.7);
        const auto resolved=anchor->hand_target(identity(),actual);assert(resolved);
        assert(near(anchor->hand_contact(*resolved),anchor->body_contact(actual)));
        const auto target=anchor->body_target(*resolved);assert(target && near(*target,actual));
        const auto rotated=grab_world(actual,anchor->contact_in_body());
        assert(near(anchor->body_contact(actual),actual.position+rotated));
        assert(std::abs(length(anchor->normal(actual))-1)<1e-8);
    }
}
void blocked_and_moving_parts() {
    const auto initial=body();const auto hand=pose({-.08,0,0});
    const auto anchor=GripAnchor::capture(initial.id,initial.pose,hand,{-.02,0,0},{-1,0,0});assert(anchor);
    GripTrackingMotion raw{hand,{},{}};raw.pose.position.x=.3;
    const auto drive=grip_drive(*anchor,initial,raw);assert(drive && near(drive->force,{60,0,0}));
    assert(near(*anchor->hand_target(initial.id,initial.pose),hand)); // A resisting body pins the visible hand while raw tracking continues.
    auto moved=initial;moved.pose=pose({.12,.03,.04},{0,1,0},.8);moved.centre_of_mass=moved.pose.position;
    const auto resolved=anchor->hand_target(moved.id,moved.pose);assert(resolved);
    assert(near(anchor->body_contact(moved.pose),anchor->hand_contact(*resolved)));
    assert(!near(resolved->position,raw.pose.position));
    auto heavy=initial;heavy.inverse_mass=.02;
    const auto light_step=grip_velocity_step(initial,*drive,1./90),heavy_step=grip_velocity_step(heavy,*drive,1./90);
    assert(light_step && heavy_step && std::abs(light_step->linear.x/heavy_step->linear.x-50)<1e-8);
    auto tiny=initial;tiny.inverse_mass=1000;tiny.inverse_inertia={1e6,1e6,1e6};
    auto rotated_raw=raw;rotated_raw.pose=pose(raw.pose.position,{0,0,1},.7);
    const auto tiny_drive=grip_drive(*anchor,tiny,rotated_raw);assert(tiny_drive && length(tiny_drive->torque)>0);
    const auto tiny_step=grip_velocity_step(tiny,*tiny_drive,1./90);assert(tiny_step && length(tiny_step->linear)<=60./90+1e-8 && length(tiny_step->angular)<=120./90+1e-8);
    assert(std::abs(length(tiny_step->angular)-120./90)<1e-8);
    auto fixed=initial;fixed.inverse_mass=0;fixed.inverse_inertia={};
    const auto static_drive=grip_drive(*anchor,fixed,raw);
    const auto fixed_step=static_drive?grip_velocity_step(fixed,*static_drive,1./90):std::nullopt;
    assert(fixed_step && near(fixed_step->linear,{}) && near(fixed_step->angular,{}));
}
void world_frame_and_principal_inertia() {
    auto actual=body(pose({.2,.1,-.3},{2,1,3},.4));actual.inverse_inertia={.2,.6,1.3};
    actual.centre_of_mass=actual.pose.position+grab_world(actual.pose,{.01,.03,-.02});
    actual.linear_velocity={.1,-.2,.3};actual.angular_velocity={-.4,.3,.1};
    const auto hand=grip_compose(actual.pose,pose({-.08,.02,0},{1,2,1},.3));
    const auto point=hand.position+hand.basis[0]*.06;
    const auto anchor=GripAnchor::capture(actual.id,actual.pose,hand,point,hand.basis[1]);assert(anchor);
    GripTrackingMotion raw{grip_compose(hand,pose({.1,.04,-.03},{0,1,0},.2)),{.4,.2,-.1},{.1,.3,.2}};
    const auto drive=grip_drive(*anchor,actual,raw);assert(drive);
    const auto step=grip_velocity_step(actual,*drive,1./90);assert(step);
    const auto world=pose({3,-2,5},{1,-3,2},1.2);
    auto changed=actual;changed.pose=grip_compose(world,actual.pose);
    changed.centre_of_mass=world.position+grab_world(world,actual.centre_of_mass);
    changed.linear_velocity=grab_world(world,actual.linear_velocity);changed.angular_velocity=grab_world(world,actual.angular_velocity);
    const auto changed_hand=grip_compose(world,hand);
    const auto changed_anchor=GripAnchor::capture(changed.id,changed.pose,changed_hand,world.position+grab_world(world,point),grab_world(world,hand.basis[1]));assert(changed_anchor);
    const GripTrackingMotion changed_raw{grip_compose(world,raw.pose),grab_world(world,raw.linear_velocity),grab_world(world,raw.angular_velocity)};
    const auto changed_drive=grip_drive(*changed_anchor,changed,changed_raw);assert(changed_drive);
    const auto changed_step=grip_velocity_step(changed,*changed_drive,1./90);assert(changed_step);
    assert(near(changed_drive->force,grab_world(world,drive->force)));
    assert(near(changed_drive->torque,grab_world(world,drive->torque)));
    assert(near(changed_step->linear,grab_world(world,step->linear)));
    assert(near(changed_step->angular,grab_world(world,step->angular)));
    const auto local=grab_local(actual.pose,drive->torque);
    const auto expected=grab_world(actual.pose,{local.x*.2,local.y*.6,local.z*1.3})*(1./90);
    assert(near(step->angular-actual.angular_velocity,expected));
    actual.inertia_axes=pose({}, {0,0,1},1.5707963267948966).basis;
    const auto offset_step=grip_velocity_step(actual,*drive,1./90);assert(offset_step);
    const auto offset_expected=grab_world(actual.pose,{local.x*.6,local.y*.2,local.z*1.3})*(1./90);
    assert(near(offset_step->angular-actual.angular_velocity,offset_expected));
    actual.inertia_axes[0]={};assert(!grip_velocity_step(actual,*drive,1./90));
}
void angular_contact_and_reaction() {
    auto actual=body();const auto hand=pose({-.08,0,0});
    const auto anchor=GripAnchor::capture(actual.id,actual.pose,hand,{-.02,0,0},{-1,0,0});assert(anchor);
    GripTrackingMotion raw{hand,{},Vec3{0,0,2}};
    const auto drive=grip_drive(*anchor,actual,raw);assert(drive && std::abs(drive->force.y-3.6)<1e-8);
    assert(near(drive->force+drive->hand_reaction_force,{}));
    const auto moment=drive->torque+cross(actual.centre_of_mass,drive->force)+drive->hand_reaction_torque+
        cross(drive->hand_reaction_origin,drive->hand_reaction_force);
    assert(near(moment,{}));
    actual.linear_velocity={.2,.1,-.1};actual.angular_velocity={.4,.2,.7};
    raw.pose=*anchor->hand_target(actual.id,actual.pose);raw.angular_velocity=actual.angular_velocity;
    raw.linear_velocity=actual.linear_velocity+cross(actual.angular_velocity,raw.pose.position-actual.centre_of_mass);
    const auto co_moving=grip_drive(*anchor,actual,raw);assert(co_moving && near(co_moving->force,{}) && near(co_moving->torque,{}));
    raw.pose=pose(hand.position,{0,1,0},3.14159265358979323846);raw.linear_velocity={};raw.angular_velocity={};
    actual.linear_velocity=actual.angular_velocity={};
    const auto half_turn=grip_drive(*anchor,actual,raw);assert(half_turn && finite(half_turn->torque) && length(half_turn->torque)>1);
}
void two_hands_one_body() {
    const auto actual=body();const auto left=pose({0,.15,0}),right=pose({0,-.15,0});
    const auto a=GripAnchor::capture(actual.id,actual.pose,left,{0,.1,0},{0,1,0});
    const auto b=GripAnchor::capture(actual.id,actual.pose,right,{0,-.1,0},{0,-1,0});assert(a && b);
    GripTrackingMotion raw_a{left,{},{}},raw_b{right,{},{}};raw_a.pose.position.x=.1;raw_b.pose.position.x=.1;
    const auto first=grip_drive(*a,actual,raw_a),second=grip_drive(*b,actual,raw_b);assert(first && second);
    const auto translation=grip_velocity_step(actual,std::array<std::optional<GripDrive>,2>{first,second},1./90);
    assert(translation && std::abs(translation->linear.x-60./90)<1e-8 && near(translation->angular,{}) && std::abs(translation->wrench_scale-.75)<1e-8);
    auto other_shape=actual;other_shape.id.shape_owner+=0x100;other_shape.id.shape_key=12;other_shape.id.geometry_generation=2;
    const auto shape_anchor=GripAnchor::capture(other_shape.id,actual.pose,right,{0,-.1,0},{0,-1,0});assert(shape_anchor);
    const auto shape_drive=grip_drive(*shape_anchor,other_shape,raw_b);assert(shape_drive);
    const auto compound_step=grip_velocity_step(actual,std::array<std::optional<GripDrive>,2>{first,shape_drive},1./90);
    assert(compound_step && near(compound_step->linear,translation->linear) && near(compound_step->angular,translation->angular));
    raw_b.pose.position.x=-.1;const auto opposing=grip_drive(*b,actual,raw_b);assert(opposing);
    const auto twist=grip_velocity_step(actual,std::array<std::optional<GripDrive>,2>{first,opposing},1./90);
    assert(twist && near(twist->linear,{}) && std::abs(twist->angular.z+8./90)<1e-8);
    auto foreign=*second;++foreign.body.body_generation;
    assert(!grip_velocity_step(actual,std::array<std::optional<GripDrive>,2>{first,foreign},1./90));
    auto torque=*first;torque.force={};torque.torque={0,0,100};
    const auto combined_spin=grip_velocity_step(actual,std::array<std::optional<GripDrive>,2>{torque,torque},1./90);
    assert(combined_spin && near(combined_spin->angular,{0,0,120./90}) && std::abs(combined_spin->wrench_scale-.6)<1e-8);
}
void identity_and_invalid_data() {
    const auto actual=body();const auto hand=pose({-.08,0,0});
    const auto anchor=GripAnchor::capture(actual.id,actual.pose,hand,{-.02,0,0},{-1,0,0});assert(anchor);
    for(unsigned change=0;change<7;++change) {
        auto replaced=actual;
        switch(change) {case 0:++replaced.id.world;break;case 1:++replaced.id.body_owner;break;case 2:++replaced.id.shape_owner;break;
            case 3:++replaced.id.body_handle;break;case 4:++replaced.id.shape_key;break;case 5:++replaced.id.body_generation;break;default:++replaced.id.geometry_generation;}
        assert(!anchor->hand_target(replaced.id,replaced.pose));assert(!grip_drive(*anchor,replaced,{hand,{},{}}));
    }
    auto invalid=identity();invalid.geometry_generation=0;assert(!GripAnchor::capture(invalid,actual.pose,hand,{-.02,0,0},{-1,0,0}));
    assert(!GripAnchor::capture(identity(),actual.pose,hand,{2,0,0},{-1,0,0}));
    assert(!GripAnchor::capture(identity(),actual.pose,hand,{-.02,0,0},{0,0,0}));
    auto bad=actual;bad.inverse_inertia.x=-1;assert(!grip_drive(*anchor,bad,{hand,{},{}}));
    GripDriveSettings settings;settings.maximum_force=std::numeric_limits<double>::quiet_NaN();assert(!grip_drive(*anchor,actual,{hand,{},{}},settings));
    const auto drive=grip_drive(*anchor,actual,{hand,{},{}});assert(drive);
    assert(!grip_velocity_step(actual,*drive,0));assert(!grip_velocity_step(actual,*drive,.1));
    auto corrupt=hand;corrupt.position.x=std::numeric_limits<double>::infinity();assert(!anchor->body_target(corrupt));
}
}
int main() {
    arbitrary_frames();blocked_and_moving_parts();angular_contact_and_reaction();world_frame_and_principal_inertia();two_hands_one_body();identity_and_invalid_data();
    std::cout<<"PASS: shared body-local surface anchor, resisting-body hand IK target, force-capped mass response, angular contact/reaction and two-hand aggregation\n";
}
