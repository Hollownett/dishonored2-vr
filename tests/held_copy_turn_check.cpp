#define NOMINMAX
#include "../src/held_copy.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
// v816 (headset v815: "hands carried over after the turn", "guns double while moving forward"; pace p50 18 ms, p95 45 ms).
// A copy is teleported at Stage0, before the physics step turns and moves the player; the arm is carried by the whole root (FullBodyDriver::anchored).
// Truth: the hand rigid with the root after the step. old = v779 (translation only, step dropped past a 50 ms service gap); new = carry() with yaw_step.
using namespace d2vr;using namespace d2vr::held_copy;
static Pose yawed(Vec3 p,double y) {const double c=std::cos(y),s=std::sin(y);return {p,{Vec3{c,s,0},Vec3{-s,c,0},Vec3{0,0,1}}};}
static Pose rigid(Vec3 local,Vec3 root,double y) {return to_world(yawed(root,y),yawed(local,0));} // a point held still in the player's frame
int main() {
    const double pi=3.14159265358979323846;
    {   // yaw_step: last smooth step, scaled by the game dt; snaps, gaps, unknowns
        assert(std::abs(yaw_step(0,.03,.02)-.03)<1e-12 && std::abs(yaw_step(0,.03,.02,.02,.04)-.06)<1e-12);
        assert(yaw_step(0,pi/4,.02,.02,.02)==0);                         // a 45 degree snap is never repeated
        assert(yaw_step(0,.03,.2)==0 && yaw_step(0,.03,0)==0);            // a pause or no time
        assert(std::abs(yaw_step(pi-.01,-pi+.01,.02)-.02)<1e-9);          // across +-pi
        assert(yaw_step(0,.2,.02,.02,.02)==0);                            // 10 rad/s: not a smooth turn
        assert(yaw_step(0,NAN,.02)==0);
    }
    {   // frame_yaw: any axis convention gives the same yaw differences (the body model's rows need not be forward/left/up)
        for(double y:{0.,.4,-2.,3.}) {
            assert(std::abs(std::remainder(frame_yaw(yawed({},y))-y,2*pi))<1e-12);
            const auto odd=[](double a) {const Pose p=yawed({},a);return Pose{{},{p.basis[2],p.basis[0],p.basis[1]}};}; // rows = up, forward, left
            assert(std::abs(std::remainder(frame_yaw(odd(y))-frame_yaw(odd(0))-y,2*pi))<1e-12);
        }
    }
    {   // carry: a quarter turn about the root
        const auto c=carry(yawed({1,0,1},0),{0,0,0},{0,0,0},pi/2);
        assert(length(c.position-Vec3{0,1,1})<1e-12 && length(c.basis[0]-Vec3{0,1,0})<1e-12 && valid(c));
        const auto m=carry(yawed({1,0,1},0),{1,0,0},{.1,0,0},0);assert(length(m.position-Vec3{1.1,0,1})<1e-12);
        const auto w=carry(yawed({1,0,1},0),{0,0,0},{.1,0,0},pi/2);assert(length(w.position-Vec3{0,1.1,1})<1e-12); // the step turns with the player
    }
    {   // root_step: a 70 ms frame is one sim step (game dt known), no longer dropped
        const Vec3 a{0,0,0},b{.3,0,0};
        assert(length(root_step(a,b,.07,.07,.07)-(b-a))<1e-12 && length(root_step(a,b,.12))==0);
    }
    {   // lab: smooth turn 90 deg/s while walking 3 m/s forward, frame times alternate 18/45 ms (the headset's p50/p95); hand 0.45 m ahead-right
        const double rate=pi/2,speed=3;const Vec3 hand_local{.45,-.2,1.2};
        const double dts[]={.018,.045,.018,.018,.045,.030,.018,.045};
        double t=0,yaw=0;Vec3 root{};double old_max=0,new_max=0,old_yaw_max=0;
        double prev_yaw=0,prev_seen_dt=0;Vec3 prev_root{};double prev_gap=0;bool have=false;
        for(unsigned i=0;i<40;++i) {
            const double dt=dts[i%8];
            // Stage0 of this frame: the root is the last frame's (drawn with the hand the render thread fed)
            const Pose drawn_hand=rigid(hand_local,root,yaw);   // hand rigid with the root (root yaw about the root origin)
            Vec3 step{};double turn=0;
            if(have) {step=root_step(prev_root,root,prev_gap,prev_seen_dt,dt);turn=yaw_step(prev_yaw,yaw,prev_gap,prev_seen_dt,dt);}
            const Vec3 old_step=have?root_step(prev_root,root,prev_gap,prev_seen_dt,dt,.05):Vec3{};
            prev_root=root;prev_yaw=yaw;prev_seen_dt=dt;prev_gap=dt;have=true;
            // the physics step: turn and walk along the heading
            yaw+=rate*dt;root=root+Vec3{std::cos(yaw),std::sin(yaw),0}*(speed*dt);t+=dt;
            const Pose truth=rigid(hand_local,root,yaw);
            Pose old_copy=drawn_hand;old_copy.position=old_copy.position+old_step;
            const Pose new_copy=carry(drawn_hand,prev_root,step,turn);
            if(i>2) {old_max=std::max(old_max,length(old_copy.position-truth.position));new_max=std::max(new_max,length(new_copy.position-truth.position));
                     old_yaw_max=std::max(old_yaw_max,angle(old_copy,truth));}
        }
        std::printf("smooth turn + walk, 18/45 ms frames: copy-to-hand error old %.1f mm (%.2f deg), new %.1f mm\n",old_max*1000,old_yaw_max*180/pi,new_max*1000);std::fflush(stdout);
        assert(old_max>.01 && new_max<.004);  // old: cm per frame; new: the a*dt^2 of walking a curve
    }
    std::cout << "held_copy_turn_check ok\n";
    return 0;
}
