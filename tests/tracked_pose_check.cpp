#include "../src/tracked_pose.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace d2vr;
void close(Vec3 a,Vec3 b) {assert(length(a-b)<1e-5);}
template<class F> void rejects(F call) {
    bool rejected=false;
    try {call();} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
}
int main() {
    GameCameraPose anchor;anchor.position={10,20,30};anchor.basis={1,0,0,0,1,0,0,0,1};
    TrackedPose origin{{0,1.7,0},{}};
    auto head=to_game_pose(origin,origin,anchor,1);
    close(head.position,{10,20,30});close(head.basis[0],{1,0,0});
    close(to_game_pose({{.1,1.9,-.3},{}},origin,anchor,2).position,{10.6,19.8,30.4});
    // A controller below/right of the head must remain there when head yaw
    // changes. The same origin/character frame, never the current head, is used.
    TrackedPose wrist{{.3,1.2,-.4},{}};
    close(to_game_pose(wrist,origin,anchor,1).position,{10.4,19.7,29.5});
    double s=std::sqrt(.5);
    auto turned=to_game_pose({origin.position,{0,s,0,s}},origin,anchor,1);
    close(turned.basis[0],{0,1,0});
    auto aimed=to_game_pose({wrist.position,{s,0,0,s}},origin,anchor,1);
    close(aimed.basis[0],{0,0,1});
    origin.orientation={0,s,0,s};
    auto recentered=to_game_pose(origin,origin,anchor,1);
    close(recentered.position,{10,20,30});close(recentered.basis[0],{1,0,0});
    anchor.basis={0,1,0,-1,0,0,0,0,1};
    origin.orientation={};
    close(to_game_pose({{.2,1.7,-.5},{}},origin,anchor,1).position,{10.2,20.5,30});
    for(int i=0;i<360;++i) {
        double a=i*3.141592653589793/360;
        auto p=to_game_pose({wrist.position,{0,std::sin(a),0,std::cos(a)}},origin,anchor,1);
        close(cross(p.basis[0],p.basis[1]),p.basis[2]);
        assert(std::abs(dot(p.basis[0],p.basis[1]))<1e-6);
    }
    rejects([&]{to_game_pose(wrist,origin,anchor,0);});
    rejects([&]{to_game_pose({wrist.position,{0,0,0,0}},origin,anchor,1);});
    rejects([&]{to_game_pose(wrist,{origin.position,{s,0,0,s}},anchor,1);});
    rejects([&]{to_game_pose({{std::numeric_limits<double>::quiet_NaN(),0,0},{}},origin,anchor,1);});
    anchor.basis[0]=2;
    rejects([&]{to_game_pose(wrist,origin,anchor,1);});
    std::cout << "Tracked pose checks passed: XR axes, recenter, independent wrist orientation, scale, invalid input.\n";
}
