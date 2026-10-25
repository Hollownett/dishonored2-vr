#include "../src/right_grip_owner.hpp"
#include "../src/controller_input.hpp"
#include "../src/physical_grab.hpp"
#include <cassert>
#include <iostream>
int main() {
    using Role=d2vr::RightGripOwner::Role;d2vr::RightGripOwner role;
    assert(role.filter(.9f,true,true)==0 && role.role()==Role::release);
    assert(role.filter(0,true,true)==0 && role.role()==Role::ready);
    assert(role.deciding(.4f,true));assert(role.filter(.4f,true,true)==0 && role.physical());
    assert(role.filter(.56f,true,false)==0 && role.physical());
    assert(role.filter(.9f,true,false)==0 && role.physical());
    role.revoke();assert(role.filter(.9f,true,true)==0 && !role.physical());
    role.filter(.2f,true,false);assert(role.filter(.4f,true,false)==.4f && role.role()==Role::native);
    assert(role.filter(.9f,true,true)==.9f && !role.physical()); // Contact appearing during block cannot steal it.
    assert(role.filter(.9f,false,true)==0);assert(role.filter(.9f,true,true)==0);
    role={};assert(role.filter(.9f,true,true)==0); // Owner/epoch invalidation requires the same fresh release.
    role.filter(0,true,false);assert(role.filter(.4f,true,true)==0 && role.physical());
    // An occupied native hand (weapon, power or carried body) revokes any
    // latched physical role and returns the raw squeeze for the native RB.
    assert(role.filter(.9f,true,true,true)==.9f && role.role()==Role::release && !role.physical());
    role.filter(0,true,true);assert(role.filter(.4f,true,true,true)==.4f && !role.physical()); // Contact cannot latch while occupied.
    // Native RB is visible for the native press, and absent throughout reserved physical squeeze.
    d2vr::PadComposer pad;d2vr::ControllerInput input{};input.active=true;
    pad.compose(input,true,1);role.filter(0,true,false);
    input.right_grip=role.filter(.9f,true,false);
    assert(pad.compose(input,true,2).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
    input.right_grip=role.filter(0,true,false);pad.compose(input,true,3);
    input.right_grip=role.filter(.56f,true,true);
    assert(!(pad.compose(input,true,4).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    input.right_grip=role.filter(.9f,true,true);
    assert(!(pad.compose(input,true,5).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    // Occupied: the raw squeeze still reaches the native right shoulder, even
    // without gameplay focus, and never reserves the physical role.
    input.right_grip=role.filter(.9f,false,true,true);
    assert(input.right_grip==.9f && !role.physical() && (pad.compose(input,true,6).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    // Pending normal release retains the measured velocity; a focus-loss
    // release passes false and must produce zero velocity instead.
    d2vr::PhysicalGrab grab;d2vr::WorldTrackedPose pose{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    grab.sample(pose,0,true,1,100);grab.sample(pose,0,true,1,110);
    grab.sample(pose,.9f,true,1,120);assert(grab.bind(pose));
    pose.position.x=.02;grab.sample(pose,.9f,true,1,140);
    assert(d2vr::length(grab.release(true).linear)>0);
    assert(d2vr::length(grab.release(false).linear)==0);
    {   // a squeeze the game took becomes physical when a yanked/woken body appears under it; a released or ready grip does not
        d2vr::RightGripOwner o;o.filter(0,true,false);assert(o.filter(1,true,false)==1 && o.role()==d2vr::RightGripOwner::Role::native);
        o.claim_physical();assert(o.physical() && o.filter(1,true,false)==0);
        d2vr::RightGripOwner r;r.filter(0,true,false);r.claim_physical();assert(!r.physical()); // ready: nothing to take over
        d2vr::PhysicalGrab g;d2vr::WorldTrackedPose hp{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        g.sample(hp,.9f,false,1,100);g.sample(hp,.9f,true,1,110);assert(!g.pressed()); // squeezed while the game owned it
        g.arm();g.sample(hp,.9f,true,1,120);assert(g.pressed() && g.bind(hp)); // handed over mid-press: latches and binds
    }
    {   // empty_fist_never_presses_native_rb: the native RB is pressed only with something in hand (a hold, a carry, a held or drawn sword); the full 16-row table
        for(int mask=0;mask<16;++mask) assert(d2vr::fist_may_press_native_rb(mask&1,mask&2,mask&4,mask&8)==(mask!=0));
        assert(!d2vr::fist_may_press_native_rb(false,false,false,false));
        assert(d2vr::fist_may_press_native_rb(true,false,false,false) && d2vr::fist_may_press_native_rb(false,true,false,false));
        assert(d2vr::fist_may_press_native_rb(false,false,true,false) && d2vr::fist_may_press_native_rb(false,false,false,true));
    }
    {   // G3 (plan Task 5): the 150 ms grab buffer. A pending press returns 0 (no native RB, no physical latch), then resolves to physical
        // or native exactly once; a release, focus loss, an occupied hand or a revoke ends it.
        d2vr::RightGripOwner o;o.filter(0,true,false);assert(o.deciding(.9f,true));
        o.begin_pending();assert(o.role()==Role::pending && o.pending() && !o.physical() && !o.deciding(.9f,true));
        assert(o.filter(.9f,true,true)==0 && o.pending());   // eligibility seen by filter() never resolves it: only the buffer does
        assert(o.filter(.9f,true,false)==0 && o.pending());
        assert(o.resolve_pending(true) && o.physical() && !o.resolve_pending(false) && o.physical());
        d2vr::RightGripOwner n;n.filter(0,true,false);n.begin_pending();
        assert(n.resolve_pending(false) && n.role()==Role::native && !n.resolve_pending(true) && n.role()==Role::native);
        assert(n.filter(.9f,true,false)==.9f);   // expired: the squeeze passes as today
        d2vr::RightGripOwner c;c.filter(0,true,false);c.begin_pending();c.catch_pending();   // caught: decided again on the same frame
        assert(c.role()==Role::ready && c.deciding(.9f,true) && c.filter(.9f,true,true)==0 && c.physical());
        d2vr::RightGripOwner r;r.filter(0,true,false);r.begin_pending();
        assert(r.filter(.2f,true,false)==.2f && r.role()==Role::ready && !r.resolve_pending(true));   // released inside the window
        d2vr::RightGripOwner f;f.filter(0,true,false);f.begin_pending();assert(f.filter(.9f,false,true)==0 && f.role()==Role::release);
        d2vr::RightGripOwner u;u.filter(0,true,false);u.begin_pending();assert(u.filter(.9f,true,true,true)==.9f && u.role()==Role::release);
        d2vr::RightGripOwner v;v.filter(0,true,false);v.begin_pending();v.revoke();assert(v.role()==Role::release && !v.resolve_pending(true));
        d2vr::RightGripOwner w;w.begin_pending();assert(w.role()==Role::release);   // only a ready (released) press can buffer
        d2vr::RightGripOwner y;y.filter(0,true,false);y.begin_pending();y.claim_physical();assert(y.physical());   // a yank takes it over
    }
    std::cout<<"PASS: pre-RB reservation, latched native/physical choice, contact change, menus/focus, occupied-hand revoke, fresh release, native pad mapping and empty-fist RB gate\n";
}
