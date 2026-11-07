// v1074: a held prop drawn at drawn grip * hold * K is exactly where the grab holds it, from its first frame, wherever the hand goes (running,
// swinging): hold = PhysicalGrab::relative() (the body in the sampled grip's frame), K = inverse(body) * model frame at the bind.
#include "weapon_draw_place.hpp"
#include "physical_grab.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;using namespace d2vr::weapon_draw;
static WorldTrackedPose pose(double x,double y,double z,double yaw,double pitch) {
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    WorldTrackedPose p{};p.position={x,y,z};
    p.basis[0]={cy*cp,sy*cp,-sp};p.basis[1]={-sy,cy,0};p.basis[2]={cy*sp,sy*sp,cp};   // forward, left, up
    return p;
}
int main() {
    // the prop's render model frame relative to its body: an offset pivot, a turned mesh and a 1.3 scale (what K must carry)
    Mat k_true=identity();{const double c=std::cos(.7),s=std::sin(.7);k_true[0]=c*1.3;k_true[1]=-s*1.3;k_true[4]=s*1.3;k_true[5]=c*1.3;k_true[10]=1.3;k_true[3]=.04;k_true[7]=-.02;k_true[11]=.06;}
    PhysicalGrab g;
    const auto hand0=pose(10,5,1.2,.3,.2),body0=pose(10.25,5.1,1.1,1.1,-.4);
    g.sample(hand0,0.f,true,1,1000);g.sample(hand0,0.f,true,1,1010);g.sample(hand0,1.f,true,1,1020);   // a released grip arms, then the squeeze presses
    assert(g.bind(body0,body0.position));
    // the bind: the body and its model frame read together at rest
    const Mat f0=mul(pose_mat(body0),k_true);
    Mat inv_body{};assert(inverse(pose_mat(body0),inv_body));
    const Mat k=mul(inv_body,f0);
    // the hold, then the hand runs and swings for 2 s: the drawn thing must be where the motor's goal puts the model frame, exactly
    double worst=0;
    for(int i=0;i<200;++i) {
        const double t=i*.011;
        const auto hand=pose(10+5*t,5+.3*std::sin(9*t),1.2+.2*std::sin(7*t),.3+1.5*std::sin(3*t),.2+.6*std::sin(5*t));   // 5 m/s and fast turns
        g.sample(hand,1.f,true,1,1030+static_cast<std::uint64_t>(i)*11);
        const Mat drawn=mul(mul(pose_mat(hand),pose_mat(g.relative())),k);
        const Mat want=mul(pose_mat(g.target()),k_true);
        worst=std::max(worst,std::max(distance(drawn,want),angle(drawn,want)));
    }
    std::printf("held exact: worst %.2e (m / rad)\n",worst);
    assert(worst<1e-9);
    // at the bind itself the drawn thing is the game's own frame (no jump from the first frame)
    {PhysicalGrab b;b.sample(hand0,0.f,true,2,5000);b.sample(hand0,1.f,true,2,5010);assert(b.bind(body0,body0.position));
     const Mat first=mul(mul(pose_mat(hand0),pose_mat(b.relative())),k);assert(distance(first,f0)<1e-9 && angle(first,f0)<1e-9);}
    std::puts("PASS");
}
