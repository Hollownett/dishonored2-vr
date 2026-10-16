#include "../src/lean_control.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
int main() {
    NativeLeanSnapshot s{};s.gamepad=true;s.owner.player=123;s.owner.player_id=4;s.state=456;
    ControllerInput raw{};raw.active=true;
    LeanControl r;auto tick=[&](unsigned t){return r.update(raw,s,true,true,1,t);};
    assert(!tick(1).y);raw.y=true;auto o=tick(10);assert(o.y&&o.lean_suspended&&!o.lean_modifier);
    s.active=true;raw.turn_x=.8f;raw.move_y=.8f;o=tick(20);assert(o.y&&o.lean_modifier);
    o.lean_controlled=o.lean_modifier=o.lean_suspended=false;
    PadComposer p;ControllerInput blank{};blank.active=true;p.compose(blank,true,1);
    auto pad=p.compose(o,true,20);assert(!(pad.wButtons&XINPUT_GAMEPAD_Y)&&pad.sThumbLX==0&&pad.sThumbLY>20000&&pad.sThumbRX>20000&&pad.sThumbRY==0);
    raw.y=false;o=tick(30);assert(!o.y&&o.lean_suspended&&o.turn_x==0);
    assert(!tick(129).y);assert(tick(130).y);assert(tick(279).y);assert(!tick(280).y);
    s.active=false;assert(tick(290).lean_suspended);raw.move_y=raw.turn_x=0;assert(!tick(300).lean_suspended);
    // Interrupted wheel is withheld even after inactive acknowledgement until release.
    raw.y=true;assert(tick(310).y);s.active=true;tick(320);raw.left_grip=1;
    assert(tick(330).left_grip==0);assert(tick(430).y);tick(580);s.active=false;raw.y=false;
    assert(tick(590).left_grip==0);raw.left_grip=0;tick(600);raw.left_grip=1;assert(tick(610).left_grip==1);
    // Fresh Y explicitly exits preexisting native lean, and never repeats a failed exit.
    LeanControl pre;raw={};raw.active=true;s.active=true;
    pre.update(raw,s,true,true,1,1);raw.y=true;
    assert(!pre.update(raw,s,true,true,1,10).y);assert(pre.update(raw,s,true,true,1,110).y);
    assert(!pre.update(raw,s,true,true,1,260).y);
    for(unsigned t=360;t<1500;t+=100) assert(!pre.update(raw,s,true,true,1,t).y);
    // Focus, menu, epoch and unknown state cancel pulses without activating held intent.
    auto cancellation=[&](int kind) {
        LeanControl c;raw={};raw.active=true;s.active=false;c.update(raw,s,true,true,1,1);
        raw.y=true;assert(c.update(raw,s,true,true,1,10).y);s.active=true;c.update(raw,s,true,true,1,20);
        raw.y=false;c.update(raw,s,true,true,1,30);assert(c.update(raw,s,true,true,1,130).y);
        raw.y=true;raw.menu=kind==0;raw.left_grip=1;
        std::optional<NativeLeanSnapshot> obs=s;if(kind==3)obs.reset();
        auto cancelled=c.update(raw,obs,kind!=1,kind!=2,kind==4?2:1,140);
        assert(cancelled.y==(kind==1 && raw.y));assert(cancelled.menu==raw.menu);
        raw.menu=false;auto resumed=c.update(raw,s,true,true,kind==4?2:1,150);
        assert(!resumed.y&&resumed.lean_suspended&&resumed.move_x==0&&resumed.turn_x==0);
    };
    for(int i=0;i<5;++i)cancellation(i);
    // Ownership replacement cannot complete a pending exit on the new actor.
    LeanControl replaced;raw={};raw.active=true;s.active=false;replaced.update(raw,s,true,true,1,1);
    raw.y=true;replaced.update(raw,s,true,true,1,10);s.active=true;replaced.update(raw,s,true,true,1,20);
    raw.y=false;replaced.update(raw,s,true,true,1,30);++s.owner.player_id;
    assert(!replaced.update(raw,s,true,true,1,130).y);
    // A native exit outside this router must not map LX as walking while Y stays held.
    LeanControl external;raw={};raw.active=true;s.active=false;external.update(raw,s,true,true,1,1);
    raw.y=true;external.update(raw,s,true,true,1,10);s.active=true;external.update(raw,s,true,true,1,20);
    s.active=false;raw.turn_x=.9f;o=external.update(raw,s,true,true,1,30);
    assert(!o.y && !o.lean_modifier && o.lean_suspended && o.turn_x==0);
    o={};o.active=true;o.y=true;o.move_y=.8f;o.turn_x=.8f;
    PadComposer pulse;pulse.compose(blank,true,1);pad=pulse.compose(o,true,10);
    assert(!(pad.wButtons&XINPUT_GAMEPAD_Y));assert(pad.sThumbLY>25000&&pad.sThumbRX>25000);
    LeanControl stalled;raw={};raw.active=true;s.active=false;stalled.update(raw,s,true,true,1,1);
    raw.y=true;stalled.update(raw,s,true,true,1,10);s.active=true;stalled.update(raw,s,true,true,1,20);
    raw.y=false;stalled.update(raw,s,true,true,1,30);assert(stalled.update(raw,s,true,true,1,130).y);
    raw.y=true;o=stalled.update(raw,s,true,true,1,500);assert(!o.y&&o.lean_suspended);
    // Native mouse Resume must not deadlock gamepad selection by suppressing
    // every ordinary nonzero input. A verified inactive state can pass walking.
    LeanControl mouse;raw={};raw.active=true;s.active=false;s.gamepad=false;
    assert(!mouse.update(raw,s,true,true,1,1).lean_suspended);
    raw.move_y=.5f;raw.right_trigger=.7f;raw.x=true;
    o=mouse.update(raw,s,true,true,1,10);
    assert(!o.y&&!o.lean_modifier&&!o.lean_suspended&&o.move_y==.5f&&o.right_trigger==.7f&&o.x);
    raw={};raw.active=true;mouse.update(raw,s,true,true,1,20);
    raw.y=true;o=mouse.update(raw,s,true,true,1,30);
    assert(o.y&&!o.lean_modifier&&o.lean_suspended); // Real Y-only bootstrap, no invented button.
    s.gamepad=true;assert(mouse.update(raw,s,true,true,1,40).y);
    s.active=true;assert(mouse.update(raw,s,true,true,1,50).lean_modifier);
    s.gamepad=false;raw.move_x=.8f;raw.turn_x=.8f;
    o=mouse.update(raw,s,true,true,1,60);
    assert(!o.y&&o.lean_suspended&&o.move_x==0&&o.turn_x==0);
    raw.menu=true;o=mouse.update(raw,s,true,true,1,70);assert(o.menu&&!o.y);
    LeanControl held;raw={};raw.active=true;raw.y=true;s.active=false;s.gamepad=false;
    assert(!held.update(raw,s,true,true,1,1).y);
    s.gamepad=true;assert(!held.update(raw,s,true,true,1,10).y);
    raw.y=false;held.update(raw,s,true,true,1,20);
    raw.y=true;assert(held.update(raw,s,true,true,1,30).y);
    std::cout<<"lean control checks passed\n";
}
