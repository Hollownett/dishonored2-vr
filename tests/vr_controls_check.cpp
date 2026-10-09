#include "../src/vr_controls.hpp"
#include <cstdio>

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
int main() {
    {
        d2vr::XWheelOwner x;
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"idle X stays closed");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"a fresh allowed X opens the wheel");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"held X keeps the wheel open");
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"release closes the wheel for the native equip");
        x.update(true,true);
        require(x.wheel(),"a released X can open the wheel again");
        x.update(true,false);
        require(!x.wheel() && x.x_consumed(),"lost allowed cancels at once and keeps the press claimed");
        x.update(true,true);
        require(!x.wheel() && x.x_consumed(),"allowed returning mid-press never reopens");
        x.update(false,true);
        require(!x.x_consumed(),"the cancelled claim ends on release");
        x.update(true,false);
        require(!x.wheel() && !x.x_consumed(),"a press that starts disallowed is ignored");
        x.update(true,true);
        require(!x.wheel() && !x.x_consumed(),"an ignored press never late-opens");
        x.update(false,true);x.update(true,true);
        require(x.wheel(),"release re-arms the fresh press edge");
        x.reset();
        require(!x.wheel() && !x.x_consumed(),"reset clears the owner");
        x.update(true,true);
        require(x.wheel(),"reset does not latch X");
    }
    const std::array<bool,2> both{true,true},left_hand{true,false},none{false,false};
    const std::array<bool,2> no_trigger{false,false},left_trigger{true,false},right_trigger{false,true};
    {
        d2vr::PointingHandSelector s;
        require(s.active()==1,"the selector defaults to the right hand");
        s.update(both,{0.9,0.1},no_trigger,1000);
        require(s.active()==1,"a more forward left hand cannot switch on its first frame");
        s.update(both,{0.9,0.1},no_trigger,1149);
        require(s.active()==1,"the dwell needs the whole 150 ms");
        s.update(both,{0.9,0.1},no_trigger,1150);
        require(s.active()==0,"150 ms of a clearly more forward hand switches");
        s.reset();
        for(std::uint64_t t=0;t<=2000;t+=100) s.update(both,{0.24,0.1},no_trigger,t);
        s.update(both,{0.24,0.1},no_trigger,2100);
        require(s.active()==1,"a 0.14 forwardness margin never switches");
        s.update(both,{0.25,0.1},no_trigger,2200);
        require(s.active()==1,"the 0.15 margin only starts the dwell");
        s.update(both,{0.25,0.1},no_trigger,2349);
        require(s.active()==1,"the 0.15 margin still dwells at 149 ms");
        s.update(both,{0.25,0.1},no_trigger,2350);
        require(s.active()==0,"the 0.15 margin switches at 150 ms");
        s.reset();
        s.update(both,{0.9,0.1},no_trigger,0);
        s.update(both,{0.9,0.1},no_trigger,100);
        s.update(both,{0.1,0.9},no_trigger,120);
        s.update(both,{0.9,0.1},no_trigger,130);
        s.update(both,{0.9,0.1},no_trigger,279);
        require(s.active()==1,"a dropped margin restarts the dwell");
        s.update(both,{0.9,0.1},no_trigger,280);
        require(s.active()==0,"a continuous 150 ms dwell still switches");
        s.reset();
        s.update(both,{0.9,0.1},left_trigger,0);
        s.update(both,{0.9,0.1},right_trigger,5100);
        require(s.active()==1,"a held trigger latches the active hand");
        s.update(both,{0.9,0.1},right_trigger,6000);
        require(s.active()==1,"the right trigger latches too");
        s.update(left_hand,{0.9,0.1},right_trigger,6100);
        require(s.active()==1,"a held trigger blocks the invalid-hand fallback");
        s.update(left_hand,{0.9,0.1},no_trigger,6200);
        require(s.active()==0,"release applies the invalid-hand fallback at once");
        s.reset();
        s.update(left_hand,{0.9,0.1},no_trigger,0);
        require(s.active()==0,"a lost pointing hand falls back immediately");
        s.update(none,{0.9,0.1},no_trigger,10);
        require(s.active()==0,"two untracked hands keep the last active hand");
        s.update(both,{0.1,0.9},no_trigger,20);
        require(s.active()==0,"the fallback does not hand the role back without a dwell");
        s.update(both,{0.1,0.9},no_trigger,170);
        require(s.active()==1,"switching back needs its own dwell");
    }
    {
        d2vr::InteractOwner o;
        const std::array<float,2> idle{0,0};
        auto settle=[&](int hand,bool target,bool wheel,bool allowed) {o.update(idle,hand,target,wheel,allowed);};
        auto r=o.update(idle,1,true,false,true);
        require(!r.interact && !r.consume_trigger[0] && !r.consume_trigger[1],"idle triggers own nothing");
        r=o.update({0,0.5f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a pull under the press threshold starts no role");
        r=o.update({0,0.6f},1,true,false,true);
        require(r.interact && r.consume_trigger[1] && !r.consume_trigger[0],"the active hand with a target interacts and consumes");
        r=o.update({0,0.4f},1,true,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role survives the release hysteresis band");
        r=o.update({0,0.3f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"interact ends below the release threshold");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,false,false,true);
        require(!r.interact && !r.consume_trigger[1],"no target is native passthrough");
        r=o.update({0,0.7f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a target appearing mid-press cannot upgrade a native role");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the native role ends on release");
        settle(1,true,false,true);
        r=o.update({0.6f,0},1,true,false,true);
        require(!r.interact && !r.consume_trigger[0],"a non-active trigger is native");
        o.reset();settle(0,true,false,true);
        r=o.update({0.6f,0.6f},0,true,false,true);
        require(r.interact && r.consume_trigger[0] && !r.consume_trigger[1],"only the active side becomes an interact");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && !r.consume_trigger[1],"an interact press needs allowed at the press edge");
        o.reset();settle(1,true,false,true);
        r=o.update({0.6f,0.7f},1,true,true,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"an open wheel consumes either trigger");
        r=o.update({0.6f,0.7f},1,true,false,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"the wheel role latches past the wheel closing");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"an interact press starts");
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && r.consume_trigger[1],"allowed false cancels the interact but keeps consuming");
        r=o.update({0,0.6f},1,true,false,true);
        require(!r.interact && r.consume_trigger[1],"the cancelled press never resumes");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the cancelled press frees on release");
        require(o.update({0,0.6f},1,true,false,true).interact,"a fresh press interacts again");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"interact starts with a target");
        r=o.update({0,0.6f},1,false,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role latches when the target disappears");
        d2vr::InteractOwner held;
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation is consumed, not owned");
        for(int i=0;i<5;++i) r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation never starts a role");
        require(!held.update({0,0},0,true,false,true).consume_trigger[0],"release clears the created-hold drain");
        require(held.update({0.8f,0},0,true,false,true).interact,"a fresh press after release owns the interact");
        held.reset();
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"reset under a held trigger drains it");
        require(!held.update({0.4f,0},0,true,false,true).interact,"a partly released trigger stays in the drain");
        require(!held.update({0.3f,0},0,true,false,true).consume_trigger[0],"below the release threshold the drain ends");
        require(held.update({0.8f,0},0,true,false,true).interact,"a post-reset release allows a fresh interact");
    }
    if(failures) {std::printf("%d requirement(s) failed\n",failures);return 1;}
        {
        // A fresh press on the other hand switches the pointing hand at once.
        d2vr::PointingHandSelector selector;
        selector.update({true,true},{.5,.5},{false,false},1000,{true,false});
        if(selector.active()!=0) {std::puts("FAIL: fresh press did not switch");return 1;}
        selector.update({true,true},{.5,.5},{false,true},1010,{false,true});
        if(selector.active()!=1) {std::puts("FAIL: fresh trigger press did not switch back");return 1;}
        // A non-active trigger waits for its own ray, then interacts or fires.
        d2vr::InteractOwner owner;owner.update({0,0},1,false,false,true,1);
        auto r=owner.update({1,0},1,false,false,true,100);
        if(!r.consume_trigger[0] || r.interact) {std::puts("FAIL: pending press not consumed");return 1;}
        r=owner.update({1,0},0,true,false,true,170);
        if(!r.interact) {std::puts("FAIL: pending press did not become interact");return 1;}
        owner.update({0,0},0,false,false,true,200);
        r=owner.update({1,0},1,false,false,true,300);r=owner.update({1,0},1,false,false,true,460);
        if(r.consume_trigger[0]) {std::puts("FAIL: pending press did not fall back to native");return 1;}
    }
std::printf("PASS: X wheel open/close/cancel/no-reopen/late-press, selector default/dwell+hysteresis/trigger-latch/invalid-hand fallback, "
        "interact role latch/cancel-consume/held-at-reset and native passthrough\n");
    return 0;
}
