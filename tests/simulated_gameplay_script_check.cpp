#include "../src/simulated_gameplay_script.hpp"
#include "../src/right_grip_owner.hpp"
#include <sstream>
#include <cassert>
#include <iostream>
static bool rejects(const char* text) {
    d2vr::SimulatedGameplayScript script;std::istringstream source(text);
    try {script.load(source,10);return false;} catch(const std::invalid_argument&) {return true;}
}
int main() {
    d2vr::SimulatedGameplayScript script;
    std::istringstream source("D2VR_GAMEPLAY_SCRIPT 1 10000 2\n1000 2000 0 .5 0 0 .2 0 0 0 1 0 0 0 0 0 0\n3000 4000 0 0 -.5 0 0 0 0 0 0 0 0 0 1 0 0\n");
    script.load(source,10);assert(script.size()==2);
    int index=42;
    auto input=script.sample(999,true,true,&index);assert(index==-1 && input.active && input.move_y==0);
    input=script.sample(1000,true,true,&index);assert(index==0 && input.move_y==.5f && input.a && input.left_trigger==.2f);
    input=script.sample(2000,true,true,&index);assert(index==-1 && input.move_y==0 && !input.a);
    input=script.sample(3000,true,true,&index);assert(index==1 && input.turn_x==-.5f && input.left_click);
    input=script.sample(3500,false,true,&index);assert(index==-1 && input.active && !input.left_click && input.turn_x==0);
    input=script.sample(3500,true,false,&index);assert(index==-1 && !input.active && input.turn_x==0);
    input=script.sample(1000000,true,true,&index);assert(index==-1 && input.active && input.turn_x==0);
    // A native PadComposer sees the same commands and retains its activation gates.
    d2vr::PadComposer pad;pad.compose(script.sample(900,true,true),true,900);
    const auto native=pad.compose(script.sample(1000,true,true),true,1000);
    assert(native.sThumbLY>16000 && native.bLeftTrigger==51 && (native.wButtons&XINPUT_GAMEPAD_A));
    const auto neutral=pad.compose(script.sample(1001,true,false),false,1001);
    assert(neutral.sThumbLY==0 && neutral.bLeftTrigger==0 && neutral.wButtons==0);
    // The script's raw squeeze follows the production press-owner/pad path.
    // Eligibility stands for the separately validated native Empty/contact preview.
    d2vr::SimulatedGameplayScript grips;
    std::istringstream grip_source("D2VR_GAMEPLAY_SCRIPT 1 10000 3\n1000 2000 0 0 0 0 0 0 0 .56 0 0 0 0 0 0 0\n3000 4000 0 0 0 0 0 0 0 .9 0 0 1 0 0 0 0\n5000 6000 0 0 0 0 0 0 0 .9 0 0 0 0 0 0 0\n");
    grips.load(grip_source,10);
    d2vr::RightGripOwner owner;d2vr::PadComposer grip_pad;
    auto composed=[&](UINT64 time,bool gameplay,bool focused,bool eligible) {
        auto raw=grips.sample(time,gameplay,focused);
        raw.right_grip=owner.filter(raw.right_grip,gameplay && focused,eligible);
        return grip_pad.compose(raw,gameplay,time);
    };
    composed(900,true,true,false);
    assert(composed(1000,true,true,false).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
    assert(composed(1500,true,true,true).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER);
    assert(!owner.physical()); // Later contact cannot steal a native block press.
    composed(2000,true,true,true);
    auto physical=composed(3000,true,true,true);
    assert(owner.physical() && !(physical.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER));
    assert(physical.wButtons&XINPUT_GAMEPAD_X); // Native Use remains independent.
    composed(3100,true,false,true);
    assert(!owner.physical());
    assert(composed(3200,true,true,true).wButtons==0 && !owner.physical());
    composed(4000,true,true,true); // A real neutral gap permits another reservation.
    composed(5000,true,true,true);assert(owner.physical());
    composed(5100,false,true,true);assert(!owner.physical()); // Menu revokes ownership.
    composed(5200,true,true,true);assert(!owner.physical()); // Held resume stays blocked.
    auto end=composed(10000,true,true,true);
    assert(end.wButtons==0 && owner.role()==d2vr::RightGripOwner::Role::ready);
    d2vr::SimulatedGameplayScript head;
    std::istringstream head_source("D2VR_GAMEPLAY_SCRIPT 2 10000 2\n1000 2000 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 -60\n3000 4000 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 80\n");
    head.load(head_source,10);float pitch=99;
    assert(head.sample(1000,true,true,nullptr,&pitch).x && pitch==-60);
    const auto q=d2vr::SimulatedGameplayScript::pitch_orientation(pitch);
    assert(std::abs(q[0]+.5f)<1e-6f && std::abs(q[3]-.8660254f)<1e-6f);
    assert(2*q[0]*q[3]<0); // Rotated OpenXR forward -Z has negative Y.
    head.sample(2000,true,true,nullptr,&pitch);assert(pitch==0);
    head.sample(1500,false,true,nullptr,&pitch);assert(pitch==0);
    head.sample(1500,true,false,nullptr,&pitch);assert(pitch==0);
    head.sample(3000,true,true,nullptr,&pitch);assert(pitch==80);
    script.sample(1000,true,true,nullptr,&pitch);assert(pitch==0); // Legacy v1.
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 2 10000 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -81"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 2 10000 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 81"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 2 10000 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 nan"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 2 10000 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 3 10000 1"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 0"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 11000 1"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 129"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 1 9000 10001"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 1 1 2 0 2 0 0 0 0 0 0 0 0 0 0 0 0 0"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 1 1 2 0 0 0 0 -.1 0 0 0 0 0 0 0 0 0 0"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 1 1 2 0 0 0 0 0 0 0 0 2 0 0 0 0 0 0"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 2 1000 2000 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1500 2500"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 1 10000 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 extra"));
    std::cout<<"PASS: finite script validation, interval boundaries, neutral release, native/physical arbitration and actual pad composition\n";
}
