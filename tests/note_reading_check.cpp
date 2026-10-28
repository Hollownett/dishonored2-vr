#define NOMINMAX
#include "../src/note_reading.hpp"
#include "../src/controller_input.hpp"
#include <cassert>
#include <iostream>

int main() {
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
    assert(!note_pointer_y({{1,1.3,0},{}},panel));
    assert(!note_pointer_y({{0,1.3,-1},{0,1,0,0}},panel)); // back face

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
    std::cout<<"PASS: note drag edges, release/reacquire, epoch/focus/discontinuity, native scroll axis, final-palm mapping\n";
}
