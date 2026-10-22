#include "aim_ray_receipt.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
int main() {
    using namespace d2vr;
    WorldTrackedPose input;input.position={123.123456789,-456.7654321,7.00000001};
    input.basis={Vec3{.6,.8,0},Vec3{-.8,.6,0},Vec3{0,0,1}};
    const AimOwner owner{101,202,3};
    const auto ray=finalize_aim_ray(input,owner,10,20,30,40);assert(ray);
    assert(ray->current(owner,10,20,30));
    assert(!ray->current(owner,11,20,30) && !ray->current(owner,10,21,30) && !ray->current(owner,10,20,31));
    auto foreign=owner;++foreign.generation;assert(!ray->current(foreign,10,20,30));
    ++foreign.camera;assert(!ray->current(foreign,10,20,30));
    // The unchanged native publish API converts the float-exact hand back to floats.
    // Re-finalization must reproduce all 48 launch bytes, not resample/filter a ray.
    const auto native=finalize_aim_ray(ray->hand(),owner,10,20,30,40);assert(native);
    assert(std::memcmp(&ray->ray,&native->ray,sizeof(GameplayAim))==0);
    const auto point=ray->target(30.f);
    assert(point.x==double(native->ray.origin[0])+double(native->ray.axis[0])*30 &&
           point.y==double(native->ray.origin[1])+double(native->ray.axis[1])*30);
    input.position.x=std::numeric_limits<double>::infinity();assert(!finalize_aim_ray(input,owner,10,20,30,40));
    input.position.x=std::numeric_limits<double>::max();assert(!finalize_aim_ray(input,owner,10,20,30,40));
    input=ray->hand();input.basis[0].x=0;assert(!finalize_aim_ray(input,owner,10,20,30,40));
    assert(!finalize_aim_ray(ray->hand(),{},10,20,30,40));
    assert(!finalize_aim_ray(ray->hand(),owner,0,20,30,40));
    std::cout<<"aim_ray_receipt_check: passed\n";
}
