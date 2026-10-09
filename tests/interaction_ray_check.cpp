#include "../src/interaction_ray.hpp"
#include <cassert>
#include <iostream>
int main() {
    d2vr::InteractionRay ray{{1.25f,2,3,0,2,0,0,1,.5f,3.402820018e38f,3.402820018e38f,.5f}};
    std::uint32_t flags=0x3f000007;std::memcpy(&ray.f[11],&flags,4);
    const std::array<float,3> origin{1,2,3};const std::array<float,9> axes{1,0,0,0,1,0,0,0,1};
    d2vr::WorldTrackedPose hand{{1,2.5,3},{{{0,1,0},{-1,0,0},{0,0,1}}}};
    const auto original=ray;
    const auto routed=d2vr::controller_interaction_ray(ray,origin,axes,hand);assert(routed);
    assert(routed->f[0]==1 && routed->f[1]==2.75f && routed->f[2]==3);
    assert(routed->f[4]==0 && routed->f[5]==2 && routed->f[6]==0 && routed->f[7]==ray.f[7]);
    assert(ray.f==original.f && routed->f[9]==.5f);
    hand.position={100,0,0};assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));hand.position={1,2.5,3};
    ray.f[0]=10;assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));ray=original;
    ray.f[7]=0;assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));ray=original;
    ray.f[11]=0;assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));
    ray=original;hand.basis[1]=hand.basis[0];assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));
    hand.basis[1]={-1,0,0};hand.basis[2]={0,0,-1};assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));
    hand.basis[2]={0,0,std::numeric_limits<double>::quiet_NaN()};assert(!d2vr::controller_interaction_ray(ray,origin,axes,hand));
    std::cout<<"PASS: local native ray retarget, original range/offset, reciprocal axes, untouched source, reach/type guards\n";
}
