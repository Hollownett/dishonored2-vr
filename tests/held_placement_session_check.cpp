#include "../src/held_placement_session.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using Session=d2vr::HeldPlacementSession;
    const Session::Key key{0x10000,0x20000,0x30000,0x40002,1,5,6,7};
    Session session;
    assert(session.update(key));
    // A continuous hold keeps its captured contact and completed fit.
    for(unsigned frame=0;frame<100;++frame)assert(!session.update(key));
    // The same native entity after release must capture the new pickup contact.
    session.clear();assert(session.update(key));assert(!session.update(key));
    session.clear();session.clear();assert(session.update(key));
    // Pointer reuse, native state changes, handoff and tracking resets invalidate
    // the old fit even when the held entity address itself has not changed.
    for(unsigned field=0;field<8;++field) {
        auto next=key;
        if(field==0)++next.entity;
        if(field==1)++next.rf;
        if(field==2)++next.state;
        if(field==3)next.spawn+=0x10000;
        if(field==4)next.side=0;
        if(field==5)++next.pose_epoch;
        if(field==6)++next.reference_epoch;
        if(field==7)++next.acquisition;
        assert(session.update(next));assert(!session.update(next));
        assert(session.update(key));assert(!session.update(key));
    }
    assert(session.update(std::nullopt));assert(!session.update(std::nullopt));
    assert(session.update(std::optional<Session::Key>{key}));assert(!session.update(key));
    const d2vr::WorldTrackedPose expected{{1,2,3},{{{0,1,0},{-1,0,0},{0,0,1}}}};
    const d2vr::Vec3 local{.1,.2,.3};
    const auto world=expected.position+d2vr::grab_world(expected,local);
    const auto minimum=world-d2vr::Vec3{.2,.3,.4},maximum=world+d2vr::Vec3{.2,.3,.4};
    const auto centre=d2vr::held_placement_mesh_centre(expected,expected,minimum,maximum);
    assert(centre && d2vr::length(*centre-local)<1e-9);
    auto changed=expected;changed.basis={{{1,0,0},{0,1,0},{0,0,1}}};
    assert(!d2vr::held_placement_mesh_centre(changed,expected,minimum,maximum));
    changed=expected;changed.position.x+=.021;
    assert(!d2vr::held_placement_mesh_centre(changed,expected,minimum,maximum));
    changed=expected;changed.basis[2]={0,0,-1};
    assert(!d2vr::held_placement_mesh_centre(changed,changed,minimum,maximum));
    changed=expected;changed.basis[0]={0,2,0};
    assert(!d2vr::held_placement_mesh_centre(changed,changed,minimum,maximum));
    changed=expected;changed.basis[0]={.005,1,0};
    assert(!d2vr::held_placement_mesh_centre(changed,changed,minimum,maximum));
    auto invalid=minimum;invalid.x=std::numeric_limits<double>::quiet_NaN();
    assert(!d2vr::held_placement_mesh_centre(expected,expected,invalid,maximum));
    assert(!d2vr::held_placement_mesh_centre(expected,expected,maximum,minimum));
    assert(!d2vr::held_placement_mesh_centre(expected,expected,minimum,minimum));
    std::puts("PASS held placement acquisition boundaries, identity reuse and tracking epochs");
}
