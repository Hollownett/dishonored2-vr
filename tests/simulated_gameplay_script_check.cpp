#include "../src/simulated_gameplay_script.hpp"
#include "../src/right_grip_owner.hpp"
#include <sstream>
#include <string>
#include <cassert>
#include <iostream>
static bool rejects(const std::string& text) {
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
    assert(native.sThumbLY>16000 && native.bLeftTrigger==0 && (native.wButtons&XINPUT_GAMEPAD_A)); // plan H3: a .2 squeeze is below the .55 power press level
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
    // Version 5: absolute poses (head + right grip) lerp toward the next back-to-back segment, hold across a gap, and stay optional per segment.
    std::string zeros;for(int i=0;i<26;++i) zeros+=" 0"; // 8 analog, 7 buttons, head pitch, 9 offsets, hand pitch
    const std::string v5="D2VR_GAMEPLAY_SCRIPT 5 10000 4\n"
        "1000 2000"+zeros+" 5 0 1.65 0 0 0 0 1 1 0 0 0 0 0 1\n"
        "2000 3000"+zeros+" 5 1 1.65 0 0 -.7071068 0 -.7071068 3 0 0 0 0 0 1\n" // negated quaternion: same rotation, shortest arc
        "4000 5000"+zeros+" 1 9 1.65 0 0 0 0 1\n"
        "5000 6000"+zeros+" 0\n";
    d2vr::SimulatedGameplayScript absolute;std::istringstream v5_source(v5);absolute.load(v5_source,10);assert(absolute.size()==4);
    auto poses=absolute.poses(1000);assert(poses.mask==5 && poses.pose[0][0]==0 && poses.pose[0][1]==1.65f && poses.pose[2][0]==1);
    poses=absolute.poses(1500);assert(std::abs(poses.pose[0][0]-.5f)<1e-5f && std::abs(poses.pose[2][0]-2)<1e-5f && poses.mask==5);
    assert(std::abs(poses.pose[0][4]-.3826834f)<1e-4f && std::abs(poses.pose[0][6]-.9238795f)<1e-4f); // 45 degrees: half-way to the sign-flipped 90, unit length
    poses=absolute.poses(2500);assert(poses.pose[0][0]==1 && poses.pose[2][0]==3); // gap after 3000: hold, never lerp across it
    assert(std::abs(poses.pose[0][4]+.7071068f)<1e-5f && std::abs(poses.pose[0][6]+.7071068f)<1e-5f); // the held pose is the loaded quaternion, unit
    assert(absolute.poses(3500).mask==0 && absolute.poses(4500).mask==1 && absolute.poses(5500).mask==0);
    assert(absolute.poses(4500).pose[0][0]==9);
    poses=script.poses(1000);assert(poses.mask==0); // legacy scripts carry no poses
    assert(absolute.sample(1000,true,true).active && absolute.offsets(1500)[0]==0);
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros+" 8")); // unknown mask bit
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros+" 1 0 1.65 0 0 0 0 2")); // not a unit quaternion
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros+" 1 0 1.65 0 0 0 0 nan"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros+" 1 11 1.65 0 0 0 0 1")); // more than 10 m away
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros+" 1 0 1.65 0 0 0")); // truncated pose
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 1\n1000 2000"+zeros)); // no mask
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 5 10000 4097"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 8 10000 1"));
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 6 10000 1\n1000 2000"+zeros+" 0")); // v6 requires when after the mask
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 6 10000 1\n1000 2000"+zeros+" 0 3")); // when out of range
    assert(rejects("D2VR_GAMEPLAY_SCRIPT 6 10000 1\n1000 2000"+zeros+" 0 -1"));
    {std::string a_zeros;for(int i=0;i<26;++i) a_zeros+=i==8?" 1":" 0"; // button A pressed
     d2vr::SimulatedGameplayScript when;std::istringstream w("D2VR_GAMEPLAY_SCRIPT 6 10000 3\n1000 2000"+a_zeros+" 0 1\n3000 4000"+a_zeros+" 0 2\n5000 6000"+a_zeros+" 0 0\n");
     when.load(w,10);index=7;
     assert(when.sample(1500,false,true,&index).a && index==0); // menu-only applies outside gameplay
     const auto in=when.sample(1500,true,true,&index);assert(!in.a && in.active && index==-1); // ...and not in gameplay
     assert(when.sample(3500,false,true).a && when.sample(3500,true,true).a); // always
     assert(!when.sample(5500,false,true).a && when.sample(5500,true,true).a); // gameplay only
     assert(!when.sample(1500,false,false).active && !when.sample(3500,true,false).a);} // unfocused stays neutral
    {std::string z;for(int i=0;i<26;++i) z+=" 0"; // version 7: ten touch flags (left then right) after when; older versions read no touch
     d2vr::SimulatedGameplayScript touch;std::istringstream tsrc("D2VR_GAMEPLAY_SCRIPT 7 10000 2\n1000 2000"+z+" 0 0 0 0 0 0 0 1 0 0 0 0\n3000 4000"+z+" 0 0 0 1 0 0 0 0 0 0 0 0\n");
     touch.load(tsrc,10);
     assert(touch.touch(1500)[1][0] && !touch.touch(1500)[0][0] && !touch.touch(1500)[1][1]); // right trigger touch
     assert(touch.touch(3500)[0][1] && !touch.touch(3500)[1][0]); // left thumbstick touch
     assert(!touch.touch(2500)[0][0] && !touch.touch(2500)[1][0]); // between segments: all clear
     d2vr::SimulatedGameplayScript old;std::istringstream osrc("D2VR_GAMEPLAY_SCRIPT 6 10000 1\n1000 2000"+z+" 0 2\n");old.load(osrc,10);
     for(unsigned side=0;side<2;++side) for(unsigned k=0;k<5;++k) assert(!old.touch(1500)[side][k]); // a v6 script has no touch
     assert(rejects("D2VR_GAMEPLAY_SCRIPT 7 10000 1\n1000 2000"+z+" 0 0 0 0 0 0 0 0 0 0 0 2")); // touch flag not 0/1
     assert(rejects("D2VR_GAMEPLAY_SCRIPT 7 10000 1\n1000 2000"+z+" 0 0 0 0 0 0 0 0 0 0 0")); // truncated touch row
    }
    {d2vr::SimulatedGameplayScript many;std::string text="D2VR_GAMEPLAY_SCRIPT 5 10000 1000\n";
     for(int i=0;i<1000;++i) text+=std::to_string(i*10)+" "+std::to_string(i*10+10)+zeros+" 0\n";
     std::istringstream many_source(text);many.load(many_source,10);assert(many.size()==1000);} // more than 128 segments only from version 5
    std::cout<<"PASS: finite script validation, interval boundaries, neutral release, native/physical arbitration and actual pad composition\n";
}
