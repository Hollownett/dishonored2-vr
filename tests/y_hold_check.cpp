#include "../src/y_hold.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>

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

void menu_routing() {
    using Screen=YHoldMenu::Screen;
    YHoldMenu menu;
    auto sample=[&](bool y,Screen screen,double t,bool blocked=false,bool open=false,bool legacy=false) {
        return menu.update(y,blocked,screen,open,legacy,t);
    };
    sample(false,Screen::gameplay,0);
    assert(!sample(true,Screen::gameplay,1).pause);
    auto r=sample(true,Screen::gameplay,1.61);
    assert(r.pause && !r.settings && r.suppress_y && r.held_s>=.6);
    assert(!sample(true,Screen::gameplay,2).pause); // no repeated event even if the game has not polled Start yet
    r=sample(true,Screen::pause,2.1);
    assert(!r.settings && r.suppress_y);
    assert(!sample(true,Screen::pause,5).settings); // opening hold cannot also open settings
    sample(false,Screen::pause,5.1);
    sample(true,Screen::pause,6);
    r=sample(true,Screen::pause,6.61);
    assert(!r.settings && !r.pause && r.suppress_y);   // v1070: a hold in Pause opens nothing (VR settings are the game's Options page)
    sample(false,Screen::pause,6.8);
    sample(true,Screen::pause,9);
    assert(!sample(true,Screen::pause,9.61).settings);
}

void menu_cancellation_and_legacy() {
    using Screen=YHoldMenu::Screen;
    for(bool blocked_at_start:{false,true}) {
        YHoldMenu menu;
        menu.update(false,false,Screen::gameplay,false,false,0);
        auto r=menu.update(true,blocked_at_start,Screen::gameplay,false,false,1);
        assert(!r.pause && r.blocked_press==blocked_at_start);
        menu.update(true,true,Screen::gameplay,false,false,1.3); // Talk/wheel/modifier claims the press
        r=menu.update(true,false,Screen::gameplay,false,false,2);
        assert(!r.pause && !r.settings && !r.suppress_y);
        menu.update(false,false,Screen::gameplay,false,false,2.1);
        menu.update(true,false,Screen::gameplay,false,false,3);
        assert(menu.update(true,false,Screen::gameplay,false,false,3.61).pause);
    }
    // Focus/tracking/load/wheel loss cancels a pending hold, and a held return is not a fresh press.
    for(auto restored:{Screen::gameplay,Screen::pause}) {
        YHoldMenu menu;
        menu.update(false,false,Screen::gameplay,false,false,0);
        menu.update(true,false,Screen::gameplay,false,false,1);
        menu.update(true,false,Screen::unavailable,false,false,1.4);
        auto r=menu.update(true,false,restored,false,false,2);
        assert(!r.pause && !r.settings && r.suppress_y);
        r=menu.update(true,false,restored,false,false,3);
        assert(!r.pause && !r.settings);
        menu.update(false,false,restored,false,false,3.1);
        menu.update(true,false,restored,false,false,4);
        r=menu.update(true,false,restored,false,false,4.61);
        assert(r.pause==(restored==Screen::gameplay) && !r.settings);
    }
    YHoldMenu legacy;
    legacy.update(false,false,Screen::gameplay,false,true,0);
    legacy.update(true,false,Screen::gameplay,false,true,1);
    const auto r=legacy.update(true,false,Screen::gameplay,false,true,1.61);
    assert(r.settings && !r.pause);
    legacy.update(false,false,Screen::pause,false,true,2);
    legacy.update(true,false,Screen::pause,false,true,3);
    assert(!legacy.update(true,false,Screen::pause,false,true,4).settings); // old paused short-Y shortcut owns this
    YHoldMenu tap;
    tap.update(false,false,Screen::gameplay,false,false,0);
    assert(!tap.update(true,false,Screen::gameplay,false,false,1).suppress_y);
    const auto short_release=tap.update(false,false,Screen::gameplay,false,false,1.1);
    assert(!short_release.pause && !short_release.settings && !short_release.suppress_y);
}

// I4: a Talk offer / wheel that appears on the fire frame (after the previous controls pass said "free") cancels the press: no fire, no
// Start, Y not suppressed, so the game's Talk still sees the press; the next press is free again.
void ownership_on_fire_frame_cancels() {
    using Screen=YHoldMenu::Screen;
    YHold h;
    bool owned=false;unsigned asked=0;
    auto probe=[&]{++asked;return owned;};
    for(double t=0;t<.6-1e-9;t+=step) {const auto r=h.update(true,false,t,probe);assert(!r.fire && !r.suppress_y);}
    assert(asked==0); // only the would-fire frame asks
    owned=true;
    auto r=h.update(true,false,.62,probe);
    assert(asked==1 && !r.fire && !r.suppress_y);
    owned=false;
    for(double t=.64;t<2.0;t+=step) {r=h.update(true,false,t,probe);assert(!r.fire && !r.suppress_y);} // stays cancelled for this press
    assert(asked==1);
    (void)h.update(false,false,2.1,probe);
    (void)h.update(true,false,3.0,probe);
    assert(h.update(true,false,3.61,probe).fire);

    YHoldMenu menu;
    owned=true;
    menu.update(false,false,Screen::gameplay,false,false,0,probe);
    menu.update(true,false,Screen::gameplay,false,false,1,probe);
    auto m=menu.update(true,false,Screen::gameplay,false,false,1.61,probe);
    assert(!m.pause && !m.settings && !m.suppress_y);
    m=menu.update(true,false,Screen::gameplay,false,false,2.5,probe);
    assert(!m.pause && !m.suppress_y);
    menu.update(false,false,Screen::gameplay,false,false,2.6,probe);
    owned=false;
    menu.update(true,false,Screen::gameplay,false,false,3,probe);
    assert(menu.update(true,false,Screen::gameplay,false,false,3.61,probe).pause);
    // The paused screen never asks: a Talk offer does not exist there.
    YHoldMenu pause;owned=true;asked=0;
    pause.update(false,false,Screen::pause,false,false,0,probe);
    pause.update(true,false,Screen::pause,false,false,1,probe);
    assert(pause.update(true,false,Screen::pause,false,false,1.61,probe).suppress_y && asked==0);
}

int main() {
    ownership_on_fire_frame_cancels();
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
    menu_routing();
    menu_cancellation_and_legacy();
    std::puts("y_hold_check ok");
}
