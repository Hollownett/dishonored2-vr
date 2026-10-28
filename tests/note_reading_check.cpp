#define NOMINMAX
#include "../src/note_reading.hpp"
#include "../src/controller_input.hpp"
#include <cassert>
#include <iostream>

int main() {
    assert(d2vr::native_note_recording_frame(42,41) && d2vr::native_note_recording_frame(42,42));
    assert(!d2vr::native_note_recording_frame(42,43) && !d2vr::native_note_recording_frame(42,40));
    assert(!d2vr::native_note_recording_frame(0,0) && !d2vr::native_note_recording_frame(1,~std::uint64_t{}));
    using namespace d2vr;
    NoteScroll scroll;
    assert(scroll.update(true,true,0.,0,1000,1,1)==0); // trigger held on opening cannot drag
    assert(scroll.update(true,true,.02,0,1010,1,1)==0);
    scroll.update(true,false,0.,0,1020,1,1);
    assert(scroll.update(true,true,0.,0,1030,1,1)==0);
    assert(scroll.update(true,true,.0035,0,1040,1,1)<-.99f); // up drags content up
    assert(scroll.update(true,true,.0035,0,1050,1,1)==0);
    assert(scroll.update(true,true,.0035,.6f,1060,1,1)==.6f);
    assert(scroll.update(true,true,{},0,1070,1,1)==0);
    assert(scroll.update(true,true,.02,0,1080,1,1)==0); // lost hit cannot reacquire held
    scroll.update(true,false,0.,0,1090,1,1);scroll.update(true,true,0.,0,1100,1,1);
    assert(scroll.update(true,true,.01,0,1110,2,1)==0); // recenter cancels
    assert(scroll.update(false,true,.01,.8f,1120,2,1)==0);
    assert(scroll.update(true,true,.02,0,1130,2,1)==0);
    scroll.update(true,false,0.,0,1140,2,1);scroll.update(true,true,0.,0,1150,2,1);
    assert(scroll.update(true,true,.3,0,1160,2,1)==0); // discontinuity cancels
    assert(scroll.update(true,true,.31,0,1170,2,1)==0);
    assert(scroll.update(true,true,.3,NAN,1180,2,1)==0);
    scroll.update(true,false,0.,0,1200,2,1);scroll.update(true,true,0.,0,1210,2,1);
    assert(scroll.update(true,true,.0035,0,1210,2,1)==0);
    assert(scroll.update(true,true,.0035,0,1220,2,1)<-.99f); // same-tick movement is retained

    NotePanelReceipt panel{{{0,1.3,-.5},{}},.45,.253125,1000,3,4,1};
    assert(panel.current(1100,3,4,1) && !panel.current(1101,3,4,1));
    assert(!panel.current(999,3,4,1) && !panel.current(1000,4,4,1) && !panel.current(1000,3,5,1));
    assert(!panel.current(1000,3,4,2));
    assert(note_pointer_y({{0,1.35,0},{}},panel) && std::abs(*note_pointer_y({{0,1.35,0},{}},panel)-.05)<1e-9);
    assert(std::abs(*note_pointer_axis({{.1,1.35,0},{}},panel,true)-.1)<1e-9);
    assert(!note_pointer_y({{1,1.3,0},{}},panel));
    assert(!note_pointer_y({{0,1.3,-1},{0,1,0,0}},panel)); // back face

    NoteTouchScroll touch;
    auto touch_panel=panel;
    std::uint64_t touch_tick=2000;
    const auto swipe=[&](std::optional<Vec3> p,bool active=true,std::uint64_t epoch=3,std::uint64_t generation=0) {
        touch_tick+=10;touch_panel.tick=touch_tick;touch_panel.pose_epoch=epoch;touch_panel.generation=generation;
        return touch.update(active,p,touch_panel,0,touch_tick,epoch,4,1);
    };
    assert(!note_touch_y({0,1.3,0},panel)); // remote hand cannot touch the page
    assert(!note_touch_y({.23,1.3,-.5},panel)); // outside its actual edge
    assert(!note_touch_y({0,1.3,-.475},panel)); // 25 mm from the surface
    assert(note_touch_y({0,1.3,-.489},panel)); // 11 mm contact slab
    auto rotated_page=panel;
    rotated_page.pose.orientation={0,0,std::sqrt(.5),std::sqrt(.5)};
    const auto rotated_y=note_touch_y(rotated_page.pose.position+Vec3{-.05,0,0},rotated_page);
    assert(rotated_y && std::abs(*rotated_y-.05)<1e-9); // page-local up follows its real pose
    assert(swipe(Vec3{0,1.3,-.5})==0); // opening with hand on page is blocked
    assert(swipe(Vec3{0,1.31,-.5})==0);
    assert(swipe(Vec3{0,1.3,0})==0); // lift away to arm
    assert(swipe(Vec3{0,1.3,-.5})==0);
    assert(swipe(Vec3{0,1.3035,-.5})<-.99f); // physical upward swipe -> native negative Y
    assert(swipe(Vec3{0,1.3,-.482})>.99f); // downward swipe, contact retained at 18 mm
    assert(swipe(Vec3{0,1.31,-.475})==0); // lift releases immediately
    assert(swipe(Vec3{0,1.31,-.482})==0); // retention slab cannot engage a new swipe
    assert(swipe(Vec3{0,1.31,-.5})==0);
    assert(swipe(Vec3{0,1.3135,-.5})<-.99f);
    assert(swipe({},true)==0); // missing tracking cancels and requires lift
    assert(swipe(Vec3{0,1.32,-.5})==0);
    assert(swipe(Vec3{0,1.33,-.5})==0);
    swipe(Vec3{0,1.3,0});swipe(Vec3{0,1.3,-.5});
    assert(swipe(Vec3{0,1.31,-.5},true,5)==0); // recenter cancels
    assert(swipe(Vec3{0,1.32,-.5},true,5)==0);
    swipe(Vec3{0,1.3,0},true,5);swipe(Vec3{0,1.3,-.5},true,5);
    assert(swipe(Vec3{0,1.31,-.5},false,5)==0); // focus loss cancels
    assert(swipe(Vec3{0,1.32,-.5},true,5)==0);
    swipe(Vec3{0,1.3,0},true,5);swipe(Vec3{0,1.3,-.5},true,5);
    assert(swipe(Vec3{0,1.31,-.5},true,5,1)==0); // replacement page cancels
    assert(swipe(Vec3{0,1.32,-.5},true,5,1)==0);
    swipe(Vec3{0,1.3,0},true,5,1);swipe(Vec3{0,1.3,-.5},true,5,1);
    assert(touch.update(true,Vec3{0,1.31,-.5},touch_panel,0,touch_tick+10,5,4,2)==0); // stale displayed frame
    assert(swipe(Vec3{0,1.32,-.5},true,5,1)==0);

    NoteViewMapping map{{{0,1.7,0},{}},{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}},{0,1.7,0},.45,.253125,true};
    const auto pose=map.panel({1.5,2,2.6},{0,-1,0},{0,0,1},{-1,0,0});
    assert(pose && length(pose->position-Vec3{0,1.3,-.5})<1e-8);
    assert(length(rotated(pose->orientation,{0,0,1})-Vec3{0,0,1})<1e-8);
    map.active=false;assert(!map.panel({},{1,0,0},{0,1,0},{0,0,1}));

    // Native notes use left analog Y; no triggers or shoulder repeats escape the note routing.
    PadComposer composer;ControllerInput input{};input.active=true;
    composer.compose(input,false,1000);input.move_y=.75f;
    const auto pad=composer.compose(input,false,1010);
    assert(pad.sThumbLY>24000 && !pad.sThumbLX && !pad.sThumbRY && !pad.bLeftTrigger && !pad.bRightTrigger);
    std::cout<<"PASS: note drag edges, physical touch bounds/hysteresis/release, epoch/focus/frame safety, native scroll axis, final-palm mapping\n";
}
