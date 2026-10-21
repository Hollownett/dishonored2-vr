#include "../src/y_hold.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace d2vr;

namespace {
constexpr double step=.016;
struct Run {unsigned fires{};double fire_at{-1};double suppress_from{-1};bool suppress_when_released{};bool suppress_before_fire{};};
// y is held from `from` to `to` seconds (inclusive of the start step), blocked while blocked_until > t.
void hold(YHold& h,Run& run,double from,double to,double blocked_until=0,double start=0) {
    for(double t=from;t<to;t+=step) {
        const auto r=h.update(true,t<blocked_until,t+start);
        if(r.fire) {++run.fires;if(run.fire_at<0) run.fire_at=t;}
        if(r.suppress_y) {if(run.suppress_from<0) run.suppress_from=t;if(!r.fire && run.fires==0) run.suppress_before_fire=true;}
    }
}
}

void short_press_never_fires() {
    YHold h;
    for(double t=.016;t<.1;t+=step) {const auto r=h.update(true,false,t);assert(!r.fire && !r.suppress_y);}
    const auto r=h.update(false,false,.12);assert(!r.fire && !r.suppress_y);
}
void hold_fires_once() {
    YHold h;Run run;
    hold(h,run,0,2.0);
    assert(run.fires==1);
    assert(run.fire_at>=.6-1e-9 && run.fire_at<.6+step+1e-9);
    assert(run.suppress_from==run.fire_at && !run.suppress_before_fire);
    assert(!h.update(false,false,2.1).suppress_y); // released
}
void release_then_new_press_fires_again() {
    YHold h;Run run;
    hold(h,run,0,1.0);
    (void)h.update(false,false,1.05);
    hold(h,run,1.1,2.5);
    assert(run.fires==2);
}
void blocked_at_start_never_fires() {
    YHold h;Run run;
    hold(h,run,0,2.0,.3);
    assert(run.fires==0 && run.suppress_from<0);
}
void talk_offer_suppresses_hold() {
    YHold h;Run run;
    hold(h,run,0,2.0,10.0);
    assert(run.fires==0 && run.suppress_from<0);
}
void blocked_midway_cancels_the_press() {
    YHold h;
    for(double t=0;t<.4;t+=step) (void)h.update(true,false,t);
    for(double t=.4;t<2.0;t+=step) {const auto r=h.update(true,t<.5,t);assert(!r.fire);}
}
void only_first_sample_blocked_never_fires() {
    YHold h;
    assert(!h.update(true,true,0).fire);
    for(double t=step;t<2.0;t+=step) {const auto r=h.update(true,false,t);assert(!r.fire && !r.suppress_y);}
    (void)h.update(false,false,2.1);
    assert(!h.update(true,false,3.0).fire && h.update(true,false,3.6).fire); // the next press is free again
}
void hold_exactly_at_threshold() {
    YHold h;
    (void)h.update(false,false,0);
    assert(!h.update(true,false,1.0).fire);
    assert(!h.update(true,false,1.599).fire);
    assert(h.update(true,false,1.6).fire);
}
void negative_or_backwards_time_resets() {
    YHold h;
    assert(!h.update(true,false,5.0).fire); // first update ever never fires
    assert(!h.update(true,false,5.3).fire);
    assert(!h.update(true,false,4.0).fire); // backwards: reset
    assert(!h.update(true,false,4.5).fire); // only .5 s since the reset
    assert(h.update(true,false,4.7).fire);
    YHold g;
    assert(!g.update(true,false,-3.0).fire);
    assert(!g.update(true,false,-1.0).fire);
}
void nan_time_ignored() {
    YHold h;
    (void)h.update(true,false,0);
    (void)h.update(true,false,.5);
    const auto r=h.update(true,false,std::numeric_limits<double>::quiet_NaN());
    assert(!r.fire && !r.suppress_y);
    assert(!h.update(true,false,.7).fire); // reset: .6 s has not passed since
    const auto inf=h.update(true,false,std::numeric_limits<double>::infinity());
    assert(!inf.fire);
}
void fire_not_repeated_while_held_after_menu_closes() {
    YHold h;Run run;
    hold(h,run,0,.8);
    assert(run.fires==1);
    hold(h,run,1.0,6.0); // the menu closed, Y still down
    assert(run.fires==1);
    assert(h.update(true,false,6.1).suppress_y); // and still kept from the game
    assert(!h.update(false,false,6.2).suppress_y);
}

int main() {
    short_press_never_fires();
    hold_fires_once();
    release_then_new_press_fires_again();
    blocked_at_start_never_fires();
    talk_offer_suppresses_hold();
    blocked_midway_cancels_the_press();
    only_first_sample_blocked_never_fires();
    hold_exactly_at_threshold();
    negative_or_backwards_time_resets();
    nan_time_ignored();
    fire_not_repeated_while_held_after_menu_closes();
    std::puts("y_hold_check ok");
}
