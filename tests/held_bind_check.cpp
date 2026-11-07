#include "weapon_draw_place.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr::weapon_draw;
static Mat pose(double x,double y,double z,double yaw){Mat m=identity();m[0]=std::cos(yaw);m[1]=-std::sin(yaw);m[4]=std::sin(yaw);m[5]=std::cos(yaw);m[3]=x;m[7]=y;m[11]=z;return m;}
int main(){
    HeldBind b;Mat G=pose(.02,.09,-.03,.4);for(int c=0;c<3;++c) for(int r=0;r<3;++r) G[r*4+c]*=1.5;   // a scaled model
    const auto hand=[](double t){return pose(.3*std::sin(t*3),.2*std::cos(t*2),1.2,.5*std::sin(t*4));};
    Mat out{};double worst=0;unsigned placed=0;
    // the hand stands still 1 s (learn), then waves; the game's frame is one 11 ms tick old
    for(int i=0;i<400;++i){const double t=i*.011;const double tm=t<1?0:t-1,tp=t<1?0:std::max(0.,t-1-.011);
        const Mat H=hand(tm),F=mul(hand(tp),G);
        if(b.frame(0,7,H,F,t,out)){++placed;const Mat want=mul(H,G);worst=std::max(worst,distance(out,want));}}
    std::printf("placed %u, worst %.3f mm\n",placed,worst*1000);
    assert(placed>300 && worst<.002);
    // a wall: the game's frame stays 30 cm off while the hand is still -> not placed, then relearned after 0.5 s
    const Mat H=hand(0),Fw=mul(H,pose(.32,.09,-.03,.4));unsigned off=0;bool back=false;b.frame(0,7,H,mul(H,G),4.99,out);   // the hand arrives there first (no teleport into the wall)
    for(int i=0;i<150;++i){const double t=5+i*.011;if(b.frame(0,7,H,Fw,t,out)) {back=true;assert(distance(out,Fw)<.001);} else ++off;}
    assert(off>=40 && back);
    // a new entity starts over (not placed while it settles)
    assert(!b.frame(0,8,H,mul(H,G),7,out));
    // v1069 walking: the hand still in tracking space, the player at 3 m/s, the physics frame 20 cm behind: still placed, at the hand
    {HeldBind w;const Mat G0=pose(.02,.09,-.03,.4);unsigned n=0;double far=0;
     for(int i=0;i<300;++i){const double t=i*.011,x=t<1?0:(t-1)*3;const Mat Hw=pose(x,0,1.2,0),F=mul(pose(t<1?0:x-.2,0,1.2,0),G0);
        if(w.frame(0,9,Hw,F,t,out,0.,t<1?0.:3.)){++n;far=std::max(far,distance(out,mul(Hw,G0)));}}
     std::printf("walking: placed %u, worst %.3f mm\n",n,far*1000);std::fflush(stdout);assert(n>=230 && far<.001);}
    // v1069 pickup on the move: 0.8 s of a moving hand with the frame 3 cm behind teaches nothing; the still hand after it gives the true hold
    {HeldBind m;const Mat G0=pose(.02,.09,-.03,.4);unsigned n=0;double far=0;
     for(int i=0;i<200;++i){const double t=i*.011;const bool moving=t<.8;const Mat Hw=pose(moving?t:.8,0,1.2,0),F=mul(pose(moving?t-.03:.8,0,1.2,0),G0);
        const bool p=m.frame(0,10,Hw,F,t,out,moving?1.:0.,0.);if(moving) assert(!p);else if(p){++n;far=std::max(far,distance(out,mul(Hw,G0)));}}
     std::printf("pickup on the move: placed %u, worst %.3f mm\n",n,far*1000);std::fflush(stdout);assert(n>100 && far<.001);}
    // v1071 a fast swing: the hand at 2 m/s, the physics frame 25 cm behind (more than off_m): still placed, at the hand
    {HeldBind f;const Mat G0=pose(.02,.09,-.03,.4);unsigned n=0;double far=0;
     for(int i=0;i<200;++i){const double t=i*.011,x=t<1?0:(t-1)*2;const Mat Hs=pose(x,0,1.2,0),F=mul(pose(t<1?0:x-.25,0,1.2,0),G0);
        if(f.frame(0,11,Hs,F,t,out,t<1?0.:2.,0.) && t>=1){++n;far=std::max(far,distance(out,mul(Hs,G0)));}}
     std::printf("fast swing: placed %u, worst %.3f mm\n",n,far*1000);std::fflush(stdout);assert(n>=85 && far<.001);}
    std::puts("PASS");
}
