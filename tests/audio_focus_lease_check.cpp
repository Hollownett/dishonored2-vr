#include "../src/audio_focus_lease.hpp"
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
using L=AudioFocusLease;using A=L::Action;using P=L::Phase;
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Sim {
    L lease;AudioFocusSnapshot s{{0x1000,1},true,true,true,false};
    AudioFocusTracking t{1,100,true,true,false};
    std::uint64_t now=100;bool writer=true;
    int native_focus{},overlay{},total{},depth{};unsigned calls{},skips{};
    void check() const {CHECK(total>=0 && native_focus>=0 && overlay>=0 && total==native_focus+overlay);}
    void native(bool suspend) {
        ++calls;
        if(suspend && s.suspend_enabled){++native_focus;++total;}
        else {CHECK(native_focus>0);--native_focus;--total;}
        check();
    }
    A apply(L::Plan p,bool request=true) {
        if(p.action==A::forward)native(request);
        if(p.action==A::suspend)native(true);
        if(p.action==A::resume)native(false);
        if(p.action==A::skip)++skips;
        lease.finish(p,true);check();return p.action;
    }
    A loss() {++depth;const auto action=apply(lease.focus(s,true,t,now,writer));s.window_active=false;s.focus_suspended=true;return action;}
    A gain() {CHECK(depth>0);--depth;const auto action=apply(lease.focus(s,false,t,now,writer),false);s.window_active=depth==0;s.focus_suspended=depth!=0;return action;}
    A service() {return apply(lease.service(s,t,now,writer));}
    void overlay_on() {++overlay;++total;check();}
    void overlay_off() {CHECK(overlay>0);--overlay;--total;check();}
};
void matched_and_overlay() {
    for(unsigned order=0;order<4;++order) {
        Sim x;if(order&1)x.overlay_on();
        CHECK(x.loss()==A::skip && x.native_focus==0 && !x.lease.stop_ready());
        if(order&2)x.overlay_on();
        CHECK(x.gain()==A::skip && x.native_focus==0 && x.calls==0);
        while(x.overlay)x.overlay_off();CHECK(x.total==0 && x.lease.stop_ready());
    }
    Sim x;x.writer=false;x.overlay_on();CHECK(x.loss()==A::forward && x.total==2);
    x.writer=true;CHECK(x.service()==A::resume && x.total==1);
    x.t.tracked=false;CHECK(x.service()==A::suspend && x.total==2);
    x.t.tracked=true;CHECK(x.service()==A::resume && x.total==1);
    CHECK(x.gain()==A::skip && x.total==1);x.overlay_off();CHECK(x.total==0);
}
void freshness_and_settings() {
    AudioFocusTracking t{1,100,true,true,false};CHECK(t.fresh(100) && t.fresh(350));
    CHECK(!t.fresh(99) && !t.fresh(351));t.tick=0;CHECK(!t.fresh(100));t.tick=100;
    t.epoch=0;CHECK(!t.fresh(100));t.epoch=1;t.focused=false;CHECK(!t.fresh(100));
    t.focused=true;t.tracked=false;CHECK(!t.fresh(100));t.tracked=true;t.simulated=true;CHECK(!t.fresh(100));
    for(unsigned reason=0;reason<6;++reason) {
        Sim x;CHECK(x.loss()==A::skip);
        if(reason==0)x.now=351;
        if(reason==1)x.t.tracked=false;
        if(reason==2)x.t.focused=false;
        if(reason==3)x.t.simulated=true;
        if(reason==4)x.t.epoch=2;
        if(reason==5)x.writer=false;
        CHECK(x.service()==A::suspend && x.native_focus==1);
        CHECK(x.gain()==A::forward && x.total==0);
    }
    Sim x;CHECK(x.loss()==A::skip);x.s.suspend_enabled=false;x.lease.stop();
    CHECK(x.service()==A::none && !x.lease.stop_ready() && !x.lease.restart());
    x.s.suspend_enabled=true;CHECK(x.service()==A::suspend && x.lease.stop_ready());
    CHECK(x.gain()==A::forward && x.total==0);CHECK(x.lease.restart());
    Sim y;y.writer=false;CHECK(y.loss()==A::forward);y.s.suspend_enabled=false;y.writer=true;
    CHECK(y.service()==A::none && y.native_focus==1);CHECK(y.gain()==A::forward && y.total==0);
}
void startup_and_duplicates() {
    Sim x;x.s.window_active=false;x.s.focus_suspended=true;x.overlay_on();
    CHECK(x.service()==A::none && x.lease.phase()==P::unknown && x.total==1);
    x.s.window_active=true;x.s.focus_suspended=false;CHECK(x.service()==A::none && x.lease.phase()==P::clear);
    CHECK(x.loss()==A::skip);CHECK(x.loss()==A::forward && x.native_focus==1);
    CHECK(x.gain()==A::skip && x.native_focus==1);CHECK(x.gain()==A::forward && x.native_focus==0);
    x.overlay_off();CHECK(x.total==0);
    Sim y;y.writer=false;CHECK(y.loss()==A::forward);CHECK(y.loss()==A::forward && y.native_focus==2);
    y.writer=true;CHECK(y.service()==A::resume && y.native_focus==1);
    y.writer=false;CHECK(y.service()==A::suspend && y.native_focus==2);
    CHECK(y.gain()==A::forward);CHECK(y.gain()==A::forward && y.total==0);
}
void retirement_and_failures() {
    Sim x;CHECK(x.loss()==A::skip);const auto old=x.s.owner;
    x.s.owner.generation=2;CHECK(x.service()==A::none && x.lease.owner()==old);
    CHECK(!x.lease.retired(x.s.owner));CHECK(x.lease.retired(old));
    CHECK(x.service()==A::none && x.lease.owner()==x.s.owner && x.lease.phase()==P::unknown);
    x.s.window_active=true;x.s.focus_suspended=false;CHECK(x.service()==A::none);
    CHECK(x.lease.retired(x.s.owner));x.s.owner={0x1000,3};CHECK(x.service()==A::none && x.lease.owner().generation==3);
    for(unsigned outcome=0;outcome<2;++outcome) {
        L lease;AudioFocusSnapshot s{{0x1000,1},true,true,true,false};AudioFocusTracking t{1,100,true,true,false};
        auto p=lease.focus(s,true,t,100,true);CHECK(p.action==A::skip && lease.busy());
        CHECK(!lease.retired(s.owner));lease.stop();CHECK(!lease.restart());
        // A C++ exception or SEH fault has the same ambiguous native-call outcome.
        (void)outcome;lease.finish(p,false);CHECK(lease.faulted() && !lease.busy() && !lease.stop_ready());
        CHECK(lease.service(s,t,100,true).action==A::none);
        CHECK(lease.focus(s,true,t,100,true).action==A::forward);
        CHECK(lease.retired(s.owner) && lease.stop_ready() && lease.restart());
    }
    // Successful completion after stop still owns a bypass until its matched
    // contribution is reinstated; restart cannot discard that obligation.
    Sim stopped;auto pending=stopped.lease.focus(stopped.s,true,stopped.t,stopped.now,true);
    stopped.lease.stop();CHECK(!stopped.lease.stop_ready());
    stopped.apply(pending);stopped.depth=1;stopped.s.window_active=false;stopped.s.focus_suspended=true;
    CHECK(!stopped.lease.restart() && stopped.service()==A::suspend);
    CHECK(stopped.gain()==A::forward && stopped.total==0 && stopped.lease.restart());
    // Native resume/suspend may partly execute before C++/SEH failure. Do not
    // infer a compensating operation from a snapshot after that ambiguity.
    for(unsigned direction=0;direction<2;++direction) {
        Sim partial;L::Plan operation;
        if(direction==0) {partial.writer=false;partial.loss();partial.writer=true;
            operation=partial.lease.service(partial.s,partial.t,partial.now,true);
            CHECK(operation.action==A::resume);partial.native(false);
        } else {partial.loss();partial.writer=false;
            operation=partial.lease.service(partial.s,partial.t,partial.now,false);
            CHECK(operation.action==A::suspend);partial.native(true);
        }
        partial.lease.finish(operation,false);const auto count=partial.calls;
        CHECK(partial.lease.faulted() && !partial.lease.stop_ready());
        CHECK(partial.service()==A::none && partial.calls==count);
    }
    L lease;AudioFocusSnapshot s{{0x1000,1},true,true,true,false};AudioFocusTracking t{1,100,true,true,false};
    auto p=lease.focus(s,true,t,100,true);CHECK(lease.busy());
    CHECK(lease.service(s,t,100,true).action==A::none && lease.busy());
    CHECK(lease.focus(s,true,t,100,true).action==A::forward && lease.faulted());
    lease.finish(p,true);CHECK(!lease.busy() && lease.faulted() && !lease.stop_ready());
    CHECK(lease.retired(s.owner) && lease.restart());
    auto q=lease.focus(s,true,t,100,true);auto wrong=q;++wrong.serial;lease.finish(wrong,true);
    CHECK(lease.faulted() && lease.busy());lease.finish(q,false);CHECK(!lease.busy());
}
// Exhaustively interleave valid nested focus requests, independent overlay,
// tracking/writer changes and service passes. The simulator checks that every
// native focus subtraction has its own contribution and never consumes overlay.
unsigned long long leaves{};
void explore(Sim x,unsigned remaining) {
    x.check();if(!remaining) {
        x.writer=false;x.t.tracked=false;x.service();
        while(x.depth)x.gain();while(x.overlay)x.overlay_off();
        CHECK(x.total==0 && x.native_focus==0);++leaves;return;
    }
    for(unsigned event=0;event<8;++event) {
        Sim y=x;
        if(event==0) {if(y.depth>=2)continue;y.loss();}
        if(event==1) {if(!y.depth)continue;y.gain();}
        if(event==2) {if(y.overlay)continue;y.overlay_on();}
        if(event==3) {if(!y.overlay)continue;y.overlay_off();}
        if(event==4)y.t.tracked=!y.t.tracked;
        if(event==5)y.writer=!y.writer;
        if(event==6)y.service();
        if(event==7){++y.t.epoch;y.service();}
        explore(y,remaining-1);
    }
}
int main() {
    matched_and_overlay();freshness_and_settings();startup_and_duplicates();retirement_and_failures();
    explore(Sim{},7);CHECK(leaves>100000);
    std::printf("audio_focus_lease_check ok: %llu exhaustive leaves; real native lifetime/settings fence remains required\n",leaves);
}
