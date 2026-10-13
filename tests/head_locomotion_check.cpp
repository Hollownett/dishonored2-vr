#include "../src/head_locomotion.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
int main() {
    constexpr double pi=3.141592653589793;
    float x=0,y=1;head_directed_stick(x,y,pi/2);assert(std::abs(x+1)<1e-6 && std::abs(y)<1e-6);
    x=.4f;y=.7f;const auto speed=std::hypot(x,y);head_directed_stick(x,y,-2.3);assert(std::abs(std::hypot(x,y)-speed)<1e-6);
    x=y=1;head_directed_stick(x,y,NAN);assert(x==1 && y==1); // no heading: unrotated pass-through
    HeadFollowing follow;follow.publish(pi/2,100,1,10,20,2,true);
    for(unsigned i=0;i<30;++i) follow.commit(follow.delta(.01,true,100+i));
    assert(std::abs(follow.compensation(1)-pi/2)<1e-6 && std::abs(follow.relative_yaw(130))<1e-6);
    assert(follow.delta(.01,true,400)==0 && follow.delta(.2,true,130)==0 && follow.delta(.01,false,130)==0);
    follow.publish(pi/2,500,1,10,20,2,false);assert(follow.delta(.01,true,500)==0 && std::abs(follow.compensation(1)-pi/2)<1e-6);
    follow.publish(pi/2,510,1,10,20,2,true);assert(follow.delta(.01,true,510)==0);
    follow.publish(0,520,2,10,20,2,true);assert(follow.compensation(2)==0 && follow.compensation(1)==0);
    follow.publish(3.13,530,2,10,20,2,true);follow.commit(3.13);
    follow.publish(-3.13,540,2,10,20,2,true);assert(follow.delta(.01,true,540)>0 && follow.delta(.01,true,540)<.03);
    follow.publish(0,550,2,11,20,2,true);assert(follow.compensation(2)==0);
    follow.publish(.4,560,2,11,20,2,true);follow.publish(NAN,570,2,11,20,2,true);
    assert(std::abs(follow.relative_yaw(570)-.4)<1e-6); // Looking vertically keeps the last horizontal direction.
    // Following the physical head must not rotate the world view twice, nor
    // swing a hand around the character when its native body follows.
    GameCameraPose anchor{};anchor.position={10,20,30};anchor.basis={1,0,0,0,1,0,0,0,1};
    const TrackedPose origin{{0,1.6,0},{}};
    const TrackedPose head{{.2,1.6,-.1},{0,std::sin(.4),0,std::cos(.4)}};
    const auto before=to_game_pose(head,origin,anchor,1);
    const double body=.8,compensation=.8,room=body-compensation;
    anchor.basis={static_cast<float>(std::cos(room)),static_cast<float>(std::sin(room)),0,
        static_cast<float>(-std::sin(room)),static_cast<float>(std::cos(room)),0,0,0,1};
    const auto after=to_game_pose(head,origin,anchor,1);
    assert(length(before.position-after.position)<1e-6 && length(before.basis[0]-after.basis[0])<1e-6);
    assert(std::abs(tracked_yaw(head.orientation,origin.orientation)-.8)<1e-6);
    assert(!std::isfinite(tracked_yaw({std::sin(pi/4),0,0,std::cos(pi/4)},{})));
    std::cout<<"PASS: head-directed stick, native body-follow compensation, focus/stale/owner/recenter gates, wrap and unchanged world head/hand pose\n";
}
