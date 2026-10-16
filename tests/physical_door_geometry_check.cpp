#include "../src/physical_door_geometry.hpp"
#include <cassert>
#include <limits>

namespace {
using Geometry=d2vr::PhysicalDoorGeometry;
using Pressure=d2vr::PhysicalDoorController::Pressure;
using d2vr::Vec3;
const Geometry::Frame frame{{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
d2vr::NativeDoorContact::Surface surface(double angle,bool swing_ccw,bool opening) {
    const double sense=swing_ccw?1.:-1.;
    d2vr::NativeDoorContact::Surface result{};
    result.entity=0x20000;result.full_body=0x12000003;result.shape_key=0xffffffffu;
    result.point={sense*std::sin(angle),-std::cos(angle),1};
    result.normal={std::cos(angle),sense*std::sin(angle),0};
    if((swing_ccw && opening) || (!swing_ccw && !opening))result.normal=result.normal*-1.;
    result.depth=.03;
    return result;
}
void rotations() {
    for(const bool ccw:{false,true})for(const bool opening:{false,true})for(const double angle:{0.,.2,.8,1.4}) {
        const auto contact=surface(angle,ccw,opening);
        const auto tracked=contact.point+contact.normal*.02;
        const auto result=Geometry::sample(frame,contact,tracked,contact.normal*-.1,.05,ccw);
        assert(result && std::abs(result->current_angle_radians-angle)<1e-6);
        assert(result->pressure==(opening?Pressure::Opening:Pressure::Closing));
        assert(std::abs(result->inward_speed-.1)<1e-6);
        if(opening)assert(result->target_angle_radians>angle);
        else if(angle>.1)assert(result->target_angle_radians<angle);
        else assert(result->target_angle_radians==0);
        const auto withdrawn=Geometry::sample(frame,contact,tracked,contact.normal*.3,.05,ccw);
        assert(withdrawn && withdrawn->inward_speed<0);
    }
}
void reject() {
    auto contact=surface(.4,true,true);const auto tracked=contact.point+contact.normal*.02;
    auto modified=contact;modified.normal={0,0,1};assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.point={.8,.5,1};assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.depth=.081;assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.depth=0;assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.entity=0;assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.full_body=0x12ffffff;assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    modified=contact;modified.point.x=std::numeric_limits<double>::quiet_NaN();assert(!Geometry::sample(frame,modified,tracked,{},.05,true));
    assert(!Geometry::sample(frame,contact,tracked,{},.1,true));
    assert(!Geometry::sample(frame,contact,{0,0,1},{},.05,true));
    assert(!Geometry::sample(frame,contact,tracked,contact.normal*20.,.05,true));
    auto unsupported=frame;unsupported.up={0,0,-1};assert(!Geometry::sample(unsupported,contact,tracked,{},.05,true));
    std::array<float,12> transform{3,-2,1,1,0,0,0,1,0,0,0,1};assert(Geometry::frame(transform));
    transform[9]=1;assert(!Geometry::frame(transform));
    transform={3,-2,1,1,0,0,0,-1,0,0,0,1};assert(!Geometry::frame(transform));
}
void transformed() {
    const Geometry::Frame rotated{{3,-2,1},{0,1,0},{-1,0,0},{0,0,1}};
    const auto point=[](Vec3 value) {return Vec3{3-value.y,-2+value.x,1+value.z};};
    const auto direction=[](Vec3 value) {return Vec3{-value.y,value.x,value.z};};
    for(const bool ccw:{false,true})for(const bool opening:{false,true}) {
        auto contact=surface(.6,ccw,opening);const auto tracked=contact.point+contact.normal*.02;
        const auto original=Geometry::sample(frame,contact,tracked,contact.normal*-.2,.03,ccw);assert(original);
        contact.point=point(contact.point);contact.normal=direction(contact.normal);
        const auto moved=Geometry::sample(rotated,contact,point(tracked),contact.normal*-.2,.03,ccw);assert(moved);
        assert(std::abs(original->current_angle_radians-moved->current_angle_radians)<1e-6);
        assert(std::abs(original->target_angle_radians-moved->target_angle_radians)<1e-6 && original->pressure==moved->pressure);
    }
}
}
int main() {rotations();reject();transformed();}
