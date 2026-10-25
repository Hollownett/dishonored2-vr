#include "../src/comfort_vignette.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace d2vr;
static bool close_to(double a,double b,double e=1e-5) {return std::fabs(a-b)<e;}

static void vignette_zero_at_rest() {
    ComfortVignette v;
    for(int i=0;i<20;++i) assert(v.update({0,0,.011})==0);
}
static void vignette_rises_with_stick_speed() {
    ComfortVignette slow,fast;
    float s{},f{};
    for(int i=0;i<30;++i) {s=slow.update({1.5,0,.01});f=fast.update({3.0,0,.01});}
    assert(close_to(s,.3) && close_to(f,.6)); // half speed -> half strength; full speed -> max
    ComfortVignette v;
    assert(close_to(v.update({3.0,0,.05}),.3)); // rise time .1 s: half way after 50 ms
    assert(close_to(v.update({3.0,0,.05}),.6));
    assert(close_to(v.update({3.0,0,1.}),.6)); // never above max
    ComfortVignette t;
    for(int i=0;i<30;++i) f=t.update({0,.75,.01});
    assert(close_to(f,.3)); // turning counts through the same path: 0.75 of 1.5 rad/s
    ComfortVignette both;
    for(int i=0;i<30;++i) f=both.update({1.5,1.5,.01});
    assert(close_to(f,.6)); // the larger of the two, not the sum
}
static void vignette_falls_slowly() {
    ComfortVignette v;
    for(int i=0;i<20;++i) v.update({3.0,0,.01});
    assert(close_to(v.opacity(),.6));
    assert(close_to(v.update({0,0,.1}),.4)); // fall time .3 s: a third gone after 100 ms
    assert(v.opacity()>0);
    assert(close_to(v.update({0,0,.1}),.2));
    assert(close_to(v.update({0,0,.1}),0));
    assert(v.update({0,0,.1})==0);
}
static void snap_turn_no_vignette() {
    assert(stick_turn_rate_rad_s(TurnControl::Mode::snap,1.,90)==0);
    assert(stick_turn_rate_rad_s(TurnControl::Mode::snap,-1.,360)==0);
    assert(stick_turn_rate_rad_s(TurnControl::Mode::native,1.,90)==0);
    assert(close_to(stick_turn_rate_rad_s(TurnControl::Mode::smooth,1.,90),3.14159265358979/2));
    assert(stick_turn_rate_rad_s(TurnControl::Mode::smooth,.1,90)==0); // deadzone
    ComfortVignette v;
    for(int i=0;i<50;++i) v.update({0,stick_turn_rate_rad_s(TurnControl::Mode::snap,1.,360),.01});
    assert(v.opacity()==0);
    for(int i=0;i<50;++i) v.update({0,stick_turn_rate_rad_s(TurnControl::Mode::smooth,1.,360),.01});
    assert(v.opacity()>.5); // smooth at the same stick does show it
    assert(stick_move_speed_mps(0,.1)==0);
    assert(close_to(stick_move_speed_mps(0,1),3.) && close_to(stick_move_speed_mps(1,1),3.)); // diagonal is clamped
}
static void knob_strength_parsing() {
    assert(close_to(parse_vignette_strength(".3"),.3));
    assert(close_to(parse_vignette_strength("0.45\n"),.45));
    assert(parse_vignette_strength("abc")==ComfortVignette::default_max_opacity);
    assert(parse_vignette_strength("")==ComfortVignette::default_max_opacity);
    assert(parse_vignette_strength("0.3x")==ComfortVignette::default_max_opacity);
    assert(parse_vignette_strength("nan")==ComfortVignette::default_max_opacity);
    assert(parse_vignette_strength("2")==1.f);
    assert(parse_vignette_strength("-1")==0.f);
    ComfortVignette v;v.set_max_opacity(.3f);
    for(int i=0;i<50;++i) v.update({3,0,.01});
    assert(close_to(v.opacity(),.3));
    v.set_enabled(false);assert(v.update({3,0,.1})==0); // no-comfort-vignette forces 0
}

static void ring_image_shape() {
    const auto image=vignette_ring_image(96);
    assert(image.size()==96u*96*4);
    const auto alpha=[&](unsigned x,unsigned y) {return image[(static_cast<std::size_t>(y)*96+x)*4+3];};
    assert(alpha(48,48)==0 && alpha(30,48)==0); // transparent centre (radius .55)
    assert(alpha(0,48)==255 && alpha(48,0)==255 && alpha(0,0)==255); // black at the edges and corners
    assert(alpha(48,48+40)>0 && alpha(48,48+40)<255); // a soft ramp between
    for(unsigned i=0;i<image.size();i+=4) assert(image[i]==0 && image[i+1]==0 && image[i+2]==0);
}

static void max_not_sum() {
    ComfortVignette v;float f{};
    for(int i=0;i<50;++i) f=v.update({1.0,.5,.01}); // move 1/3, turn 1/3: the larger, not 2/3
    assert(close_to(f,.2));
    ComfortVignette w;
    for(int i=0;i<50;++i) f=w.update({1.5,.3,.01}); // move .5 beats turn .2
    assert(close_to(f,.3));
    ComfortVignette u;
    for(int i=0;i<50;++i) f=u.update({.6,1.2,.01}); // turn .8 beats move .2
    assert(close_to(f,.48));
}
static void turn_axis_normalisation() {
    const double full=3.14159265358979/2; // 90 deg/s
    assert(stick_turn_rate_rad_s(TurnControl::Mode::smooth,.15,90)==0);
    assert(close_to(stick_turn_rate_rad_s(TurnControl::Mode::smooth,1.,90),full));
    assert(close_to(stick_turn_rate_rad_s(TurnControl::Mode::smooth,.575,90),full*.5));
    assert(close_to(stick_turn_rate_rad_s(TurnControl::Mode::smooth,-.575,90),full*.5)); // magnitude only
    assert(close_to(stick_turn_rate_rad_s(TurnControl::Mode::smooth,2.,90),full)); // clamped
}
static void bad_input_leaves_opacity() {
    ComfortVignette v;
    for(int i=0;i<5;++i) v.update({3,0,.01});
    const float before=v.opacity();
    assert(before>0 && before<.6f);
    assert(v.update({3,0,std::nan("")})==before);
    assert(v.update({3,0,-.01})==before);
    assert(v.update({3,0,0})==before);
    assert(v.update({3,0,std::numeric_limits<double>::infinity()})==before);
    assert(v.update({std::nan(""),0,.01})<=before); // a bad speed is rest: it can only fall
    ComfortVignette r;
    for(int i=0;i<50;++i) r.update({3,0,.01});
    r.reset();assert(r.opacity()==0);
}
static void target_gating() {
    assert(vignette_target_allowed(true,false,false,false));
    assert(!vignette_target_allowed(false,false,false,false)); // loading / not gameplay
    assert(!vignette_target_allowed(true,true,false,false));   // takedown or cutscene
    assert(!vignette_target_allowed(true,false,true,false));   // menu / wheel
    assert(!vignette_target_allowed(true,false,false,true));   // settings panel
}
static void authored_easing_allows_vignette() { // G1: the 0.4 s scripted-moment ease lifts the authored inhibit, nothing else
    assert(vignette_target_allowed(true,true,false,false,true));    // authored and easing
    assert(!vignette_target_allowed(true,true,false,false,false));  // authored, not easing
    assert(!vignette_target_allowed(true,true,true,false,true));    // menu open
    assert(!vignette_target_allowed(true,true,false,true,true));    // settings panel
    assert(!vignette_target_allowed(false,true,false,false,true));  // not gameplay
}

int main() {
    authored_easing_allows_vignette();
    vignette_zero_at_rest();
    vignette_rises_with_stick_speed();
    vignette_falls_slowly();
    snap_turn_no_vignette();
    knob_strength_parsing();
    ring_image_shape();
    max_not_sum();
    turn_axis_normalisation();
    bad_input_leaves_opacity();
    target_gating();
    std::cout << "comfort vignette checks passed\n";
}
