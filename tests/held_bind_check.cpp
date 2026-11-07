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
    const Mat H=hand(0),Fw=mul(H,pose(.32,.09,-.03,.4));unsigned off=0;bool back=false;
    for(int i=0;i<150;++i){const double t=5+i*.011;if(b.frame(0,7,H,Fw,t,out)) {back=true;assert(distance(out,Fw)<.001);} else ++off;}
    assert(off>=40 && back);
    // a new entity starts over (not placed while it settles)
    assert(!b.frame(0,8,H,mul(H,G),7,out));
    std::puts("PASS");
}
