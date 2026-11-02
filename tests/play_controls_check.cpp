#include "../src/play_controls.hpp"
#include <cstdio>

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
int main() {
    {
        d2vr::CrouchFlick c;
        require(!c.update(0,true,0),"an idle stick never crouches");
        require(!c.update(-.69f,true,5),"a push inside the flick dead zone never crouches");
        require(c.update(-.7f,true,10),"a full flick starts the crouch pulse");
        require(c.update(-.7f,true,129),"the crouch pulse holds through 120 ms");
        require(!c.update(-.7f,true,130),"the crouch pulse ends after 120 ms");
        require(!c.update(-.7f,true,500),"a held flick never pulses twice");
        require(!c.update(-.4f,true,600),"the re-arm margin needs -0.3");
        require(!c.update(-.8f,true,610),"a push below the margin stays spent");
        require(!c.update(-.3f,true,620),"the -0.3 boundary re-arms");
        require(c.update(-.8f,true,630),"a re-armed flick pulses again");
        require(c.update(-.8f,true,749),"the second pulse holds its 120 ms");
        require(!c.update(-.8f,true,750),"the second pulse ends on its own");
    }
    {
        d2vr::CrouchFlick c;
        require(!c.update(-.8f,false,0),"a flick while disallowed never pulses");
        require(!c.update(-.8f,true,10),"allowed returning under the held flick never pulses late");
        require(!c.update(-.8f,true,1000),"the disallowed flick stays spent while held");
        require(!c.update(-.3f,true,1100),"the centre return re-arms the spent flick");
        require(c.update(-.8f,true,1110),"the next allowed flick pulses");
    }
    {
        d2vr::CrouchFlick c;
        require(c.update(-.9f,true,0),"the pulse starts");
        require(!c.update(-.9f,false,50),"lost allowed ends the pulse at once");
        require(!c.update(-.9f,true,60),"the ended pulse never resumes");
        require(!c.update(-.9f,true,300),"a full 120 ms without allowed never resumes");
        require(!c.update(-.2f,true,400),"the return to centre re-arms");
        require(c.update(-.9f,true,410),"a fresh allowed flick pulses");
    }
    {
        d2vr::CrouchFlick c;
        require(!c.update(0,false,0),"a disallowed idle stick does nothing");
        require(c.update(-.8f,true,10),"a disallowed idle stick keeps the flick armed");
    }
    {   // v779 (user, headset v778): dual wield, the right stick pushed up is the jump, held while up (swim up, climb)
        d2vr::JumpStick j;
        require(!j.update(0,0,true),"an idle stick never jumps");
        require(!j.update(0,.69f,true),"a push inside .7 never jumps");
        require(j.update(0,.7f,true) && j.update(.2f,.5f,true),"pushed up it holds the jump until it falls under .3");
        require(!j.update(0,.29f,true),"... and lets go below .3");
        require(!j.update(.9f,.8f,true),"a push more sideways than up (a snap turn) never jumps");
        require(!j.update(0,0,true) && !j.update(0,.9f,false),"a push while not allowed never jumps");
        require(!j.update(0,.9f,true),"... nor late once allowed while still up");
        require(!j.update(0,.1f,true) && j.update(0,.9f,true),"the return to centre re-arms it");
        require(!j.update(0,.9f,false),"losing allowed lets go at once");
    }
    {
        d2vr::HolsterHold h;
        require(!h.update(false,true,false,false,0),"an idle B never holds X");
        require(h.update(true,true,false,false,10),"a fresh B press starts the holster hold");
        require(h.update(true,true,false,false,1009),"the hold lasts its full 1000 ms");
        require(!h.update(true,true,false,false,1010),"the hold ends at 1000 ms");
        require(!h.update(true,true,false,false,1500),"a B held past the hold cannot restart it");
        require(!h.update(false,true,false,false,1600),"releasing B clears the completed hold");
        require(h.update(true,true,false,false,1610),"a press after the release holsters again");
    }
    {
        d2vr::HolsterHold h;
        require(h.update(true,true,false,false,0),"a tap starts the hold");
        require(h.update(false,true,false,false,100),"a released tap keeps holding X");
        require(h.update(false,true,false,false,999),"the tap holds the full 1000 ms");
        require(!h.update(false,true,false,false,1000),"the tap hold ends at 1000 ms");
        require(h.update(true,true,false,false,1010),"a tap press after the release starts again");
    }
    {
        d2vr::HolsterHold h;
        require(h.update(true,true,false,false,0),"the press starts");
        require(!h.update(true,true,true,false,50),"a target appearing cancels the holster at once");
        require(!h.update(true,true,true,false,200),"the cancelled hold stays off while B is held");
        require(!h.update(false,true,true,false,210),"the release is required before a restart");
        require(!h.update(true,true,true,false,220),"a press with a target never starts");
        require(!h.update(false,true,false,false,230),"the release clears the cancelled press");
        require(h.update(true,true,false,false,240),"a target-free press holsters again");
    }
    {
        d2vr::HolsterHold h;
        require(h.update(true,true,false,false,0),"the press starts");
        require(!h.update(true,true,false,true,50),"a trigger press cancels the holster");
        require(!h.update(true,true,false,false,60),"the cancelled hold stays spent while B is held");
        h.update(false,true,false,false,70);
        require(h.update(true,true,false,false,80),"a fresh press after release starts");
        require(!h.update(true,false,false,false,120),"lost allowed cancels the holster");
        require(!h.update(true,true,false,false,130),"the cancelled hold never resumes while B is held");
        h.update(false,true,false,false,140);
        require(h.update(true,true,false,false,150),"a fresh allowed press holsters again");
    }
    {
        d2vr::HolsterHold h;
        require(!h.update(true,false,false,false,0),"a disallowed press never starts");
        require(!h.update(true,true,false,false,500),"allowed returning under the held press never starts");
        h.update(false,true,false,false,510);
        require(h.update(true,true,false,false,520),"the next fresh press holsters");
    }
    {
        d2vr::HolsterHold h;
        require(!h.update(true,true,true,false,0),"a target at the press edge blocks the holster");
        require(!h.update(true,true,false,false,10),"the blocked press never starts");
        require(!h.update(true,false,false,false,20),"a disallowed press at the edge never starts");
        require(!h.update(true,true,false,true,30),"a trigger at the press edge blocks the holster");
        require(!h.update(true,true,false,false,40),"the blocked press still never starts after the trigger releases");
    }
    {
        d2vr::HolsterHold h(50);
        require(h.update(true,true,false,false,0),"the custom hold starts");
        require(h.update(false,true,false,false,49),"the custom hold holds 50 ms");
        require(!h.update(false,true,false,false,50),"the custom hold ends at 50 ms");
    }
    const std::array<float,2> no{0,0};
    const std::array<bool,2> none{false,false};
    {
        d2vr::GripTriggerGuard g;
        auto out=g.update(no,no,none,0);
        require(!out[0] && !out[1],"idle triggers are not consumed");
        g.update({0,.6f},no,none,100);
        out=g.update({0,.6f},{0,.6f},none,300);
        require(!out[0] && out[1],"a trigger within 400 ms of the grip press is consumed");
        out=g.update({0,.6f},{0,.7f},none,320);
        require(out[1],"the consumed trigger latches while it is held");
        out=g.update({0,.6f},{0,.3f},none,340);
        require(!out[1],"the latch clears on the trigger release");
        out=g.update({0,.6f},{0,.6f},none,350);
        require(out[1],"another press inside the window is consumed");
    }
    {
        d2vr::GripTriggerGuard g;
        g.update({0,.6f},no,none,0);
        auto out=g.update({0,.6f},{0,.6f},none,400);
        require(out[1],"a trigger at exactly 400 ms is consumed");
        out=g.update({0,.6f},{0,.3f},none,410);
        out=g.update({0,.6f},{0,.6f},none,420);
        require(!out[1],"a trigger past 400 ms is not consumed");
        require(!g.swing_blocked(1,420),"the swing block expires past 400 ms");
    }
    {
        d2vr::GripTriggerGuard g;
        g.update({0,.6f},no,{false,true},0);
        auto out=g.update({0,.6f},{0,.6f},{false,true},5000);
        require(!out[0] && out[1],"holding the grabbed object consumes the trigger at any time");
        require(g.swing_blocked(1,5000),"holding the grabbed object blocks the swing at any time");
        g.update({0,.6f},no,{false,false},5010);
        require(!g.swing_blocked(1,5010),"releasing the grab past 400 ms ends the swing block");
        out=g.update({0,.6f},{0,.6f},none,5110);
        require(!out[1],"a new press past the window is not consumed");
    }
    {
        d2vr::GripTriggerGuard g;
        g.update({0,.6f},no,none,0);
        g.update({0,.4f},no,none,100);
        require(g.swing_blocked(1,100),"grip hysteresis keeps a partly released grip held");
        g.update({0,.36f},no,none,200);
        require(g.swing_blocked(1,200),"the grip holds above the .35 release threshold");
        g.update({0,.35f},no,none,201);
        require(!g.swing_blocked(1,201),"the .35 release threshold clears the grip");
    }
    {
        d2vr::GripTriggerGuard g;
        auto out=g.update({0,0},{0,.6f},none,0);
        require(!out[1],"a trigger pressed before the grip is not consumed");
        out=g.update({0,.6f},{0,.7f},none,10);
        require(!out[1],"a grip pressed mid-trigger never upgrades the press");
        g.update({0,.6f},{0,.3f},none,20);
        out=g.update({0,.6f},{0,.6f},none,30);
        require(out[1],"a new press after the grip is consumed");
    }
    {
        d2vr::GripTriggerGuard g;
        g.update({.6f,0},no,none,0);
        auto out=g.update({.6f,0},{.6f,0},none,100);
        require(out[0] && !out[1],"the left grip consumes only the left trigger");
        require(g.swing_blocked(0,100) && !g.swing_blocked(1,100),"only the gripping side blocks the swing");
        out=g.update({.6f,.6f},{.6f,0},none,120);
        require(out[0] && !out[1],"the right grip engages on its own press only");
    }
    {
        d2vr::GripTriggerGuard g;
        auto out=g.update(no,{.55f,.55f},none,0);
        require(!out[0] && !out[1],"triggers without their grip are not consumed");
        g.update({0,.55f},no,none,10);
        require(g.swing_blocked(1,10),"the .55 grip threshold presses");
        out=g.update({0,.55f},{0,.55f},none,100);
        require(out[1],"the .55 trigger threshold presses");
        out=g.update({0,.55f},{0,.35f},none,120);
        require(!out[1],"the .35 trigger threshold releases the latch");
        require(g.swing_blocked(1,120),"the grip survives its release hysteresis band");
    }
    {   // Physical crouch: edge-triggered, never fights the stick, retries a refused pulse.
        d2vr::PhysicalCrouch p;
        require(!p.update(.1,false,false,true,0),"standing does nothing");
        require(p.update(.4,false,false,true,10),"sitting down pulses crouch");
        require(p.update(.4,false,false,true,100),"the pulse lasts 120 ms");
        require(!p.update(.4,false,false,true,140),"the pulse ends");
        require(!p.update(.4,true,false,true,200),"crouched now: no more pulses");
        require(p.compensate(true),"physically low and crouched lifts the camera");
        require(!p.update(.3,true,false,true,300),"hysteresis band holds the crouch");
        require(p.update(.1,true,false,true,400),"standing up pulses stand");
        require(!p.update(.1,true,false,true,600),"no spam while the state changes");
        require(p.update(.1,true,false,true,1500),"a refused stand retries after a second");
        require(!p.update(.1,false,false,true,2000) && !p.compensate(false),"stood up: done");
        d2vr::PhysicalCrouch q;
        require(!q.update(.1,true,false,true,0),"a stick crouch while standing is left alone");
        require(!q.update(.1,true,false,true,5000),"still left alone later");
        require(!q.update(.4,true,false,true,6000),"sitting while already crouched needs no pulse");
        require(q.update(.1,true,false,true,7000),"standing up from it stands");
        d2vr::PhysicalCrouch r;
        require(!r.update(.4,false,false,false,0),"disallowed (menu) never pulses");
        require(r.update(.4,false,false,true,10),"allowed again: the edge is still seen");
        d2vr::PhysicalCrouch f;
        f.update(.4,false,false,true,0);f.update(.4,true,true,true,200);
        require(!f.update(.1,true,true,true,400),"a forced crouch (low ceiling) is not stood up");
    }
    {   // Crouch lift follows the native animation, freezes, and ramps back on standing.
        d2vr::CrouchLift l;
        require(l.update(false,1.6,0)==0,"standing: no lift");
        require(std::abs(l.update(true,1.6,10))<1e-9,"crouch flips: still at the standing height");
        require(std::abs(l.update(true,1.35,200)-.25)<1e-9,"follows the camera drop mid-animation");
        require(std::abs(l.update(true,1.1,500)-.5)<1e-9,"full drop");
        require(std::abs(l.update(true,1.1,700)-.5)<1e-9,"frozen after 600 ms");
        require(std::abs(l.update(true,.9,900)-.5)<1e-9,"stairs while crouched do not leak in");
        require(std::abs(l.update(false,.9,1000)-.5)<1e-9,"standing flips: lift held at first");
        require(std::abs(l.update(false,1.15,1200)-.25)<1e-9,"ramps down as the game raises the camera");
        require(l.update(false,1.4,1700)==0,"back to zero");
    }
    {   // Wheel hand dial: frozen frame at open, dead zone, direction-only, pose loss keeps the last direction.
        d2vr::WheelDial d;float x{},y{};
        const std::array<double,3> right{1,0,0};
        require(!d.update(false,true,{0,1,0},right,x,y),"closed wheel: nothing");
        require(!d.update(true,true,{0,1,0},right,x,y),"opening frame freezes the origin");
        require(!d.update(true,true,{.02,1,0},right,x,y),"inside the dead zone: nothing");
        require(d.update(true,true,{.06,1,0},right,x,y) && x>.99f && std::abs(y)<.01f,"hand right selects right");
        require(d.update(true,true,{0,1.06,0},right,x,y) && y>.99f,"hand up selects up");
        require(d.update(true,false,{9,9,9},right,x,y) && y>.99f,"lost pose keeps the last direction");
        require(!d.update(false,true,{0,1,0},right,x,y) && !d.update(true,true,{.5,1,0},right,x,y),"reopening re-freezes at the new hand position");
        require(d.update(true,true,{.5,.9,0},right,x,y) && y<-.99f,"down from the new origin");
    }
    if(failures) {std::printf("%d requirement(s) failed\n",failures);return 1;}
    std::printf("PASS: crouch flick once-per-flick/120 ms pulse/-0.3 re-arm/disallowed-held and lost-allowed no late pulse, "
        "holster fresh-press start/1000 ms tap-target-trigger-allowed cancel/B-release re-arm/custom window, "
        "grip guard 400 ms window and holding consume/hysteresis/latch-until-release/mid-press no upgrade/swing_blocked\n");
    return 0;
}
