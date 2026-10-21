#include "../src/arm_flicker.hpp"
#include <cassert>
#include <iostream>
#include <string>

using d2vr::ArmFlicker;
int main() {
    ArmFlicker f;
    // A steady tracked hand never flips.
    for(unsigned i=0;i<30;++i) {f.note(0,true,0);f.note(1,true,0);}
    assert(f.of(0).gate_flips==0 && f.of(0).run_max==0 && f.of(0).fallbacks==0 && f.of(0).uploads==30);
    // One missed sample: two flips (tracked->fallback->tracked), a run of one.
    f.note(0,false,ArmFlicker::callback_before_track);f.note(0,true,0);
    assert(f.of(0).gate_flips==2 && f.of(0).run_max==1 && f.of(0).fallbacks==1);
    assert(f.of(0).why[ArmFlicker::callback_before_track]==1 && f.of(1).gate_flips==0);
    // A longer run raises the maximum only; a later shorter one does not lower it.
    for(unsigned i=0;i<4;++i) f.note(0,false,ArmFlicker::track_aged);
    f.note(0,true,0);f.note(0,false,ArmFlicker::publish_refused);f.note(0,true,0);
    assert(f.of(0).run_max==4 && f.of(0).gate_flips==6);
    // A fallback with the reason "tracked" or out of range is recorded as other, never as a tracked upload.
    f.note(1,false,ArmFlicker::tracked);f.note(1,false,999);
    assert(f.of(1).why[ArmFlicker::other]==2 && f.of(1).fallbacks==2);
    f.note(2,false,0);assert(f.of(0).uploads==30+2+4+3);
    f.weapon_hidden_frame();f.weapon_hidden_frame();
    const auto text=f.json(3);
    assert(text.find("\"gate_flips\":[6,")!=std::string::npos);
    assert(text.find("\"attach_clears\":3,\"weapon_hidden_frames\":2}")!=std::string::npos);
    assert(text.find("\"fallback_runs_max_frames\":[4,2]")!=std::string::npos);
    f.reset();assert(f.of(0).uploads==0 && f.weapon_hidden_frames()==0);
    std::cout<<"arm_flicker_check passed\n";
}
