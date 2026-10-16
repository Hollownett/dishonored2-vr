#include "../src/weapon_pose_stabilizer.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace {
using Filter=d2vr::WeaponPoseStabilizer;
constexpr double radians=3.14159265358979323846/180.;
d2vr::WorldTrackedPose pose(double degrees) {
    const double angle=degrees*radians;
    return {{4,5,6},{{{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}}}};
}
double yaw(const d2vr::WorldTrackedPose& value) {return std::atan2(value.basis[0].y,value.basis[0].x)/radians;}
bool exact(const d2vr::WorldTrackedPose& first,const d2vr::WorldTrackedPose& second) {
    for(unsigned index=0;index<3;++index) {
        const auto delta=first.basis[index]-second.basis[index];if(d2vr::length(delta)!=0)return false;
    }
    return d2vr::length(first.position-second.position)==0;
}
}
int main() {
    const Filter::Key owner{0x12340000,7,8,0x12345678,0};
    for(const unsigned frequency:{72u,90u,120u}) {
        Filter filter;double raw_energy{},filtered_energy{};
        for(unsigned frame=0;frame<600;++frame) {
            const auto tick=1000000+static_cast<std::uint64_t>(std::llround(frame*1000000./frequency));
            const auto raw=pose((frame&1)?.04:-.04);const auto filtered=filter.sample(raw,owner,tick,true);assert(filtered);
            assert(d2vr::length(filtered->position-raw.position)==0 && Filter::proper(filtered->basis));
            assert(std::abs(yaw(*filtered)-yaw(raw))<=Filter::maximum_degrees+1e-9);
            if(frame>20) {raw_energy+=yaw(raw)*yaw(raw);filtered_energy+=yaw(*filtered)*yaw(*filtered);}
            const auto duplicate=filter.sample(raw,owner,tick,true);assert(duplicate && exact(*duplicate,*filtered));
        }
        assert(filtered_energy<raw_energy*.8);
        std::cout<<frequency<<"Hz jitter RMS ratio "<<std::sqrt(filtered_energy/raw_energy)<<'\n';
        const auto deliberate=pose(80);const auto turned=filter.sample(deliberate,owner,10000000,true);assert(turned && exact(*turned,deliberate));
    }
    Filter filter;const auto fixed=pose(13);
    for(unsigned frame=0;frame<100;++frame)assert(exact(*filter.sample(fixed,owner,1000000+frame*10000,true),fixed));
    filter.clear();assert(filter.sample(pose(0),owner,1000000,true));
    const auto quick=pose(45);assert(exact(*filter.sample(quick,owner,1010000,true),quick));
    const auto moving=pose(46);assert(exact(*filter.sample(moving,owner,1020000,true),moving));
    const auto jitter=filter.sample(pose(46.04),owner,1030000,true);assert(jitter && yaw(*jitter)<46.04);
    assert(!filter.sample(pose(90),owner,1020000,true));
    assert(!filter.sample(pose(90),owner,1030000,true));
    assert(exact(*filter.sample(pose(46.04),owner,1030000,true),*jitter));
    auto changed=owner;++changed.pose_epoch;assert(exact(*filter.sample(pose(46.08),changed,1040000,true),pose(46.08)));
    ++changed.reference_epoch;assert(exact(*filter.sample(pose(46.09),changed,1050000,true),pose(46.09)));
    ++changed.full_spawn;assert(exact(*filter.sample(pose(46.10),changed,1060000,true),pose(46.10)));
    ++changed.entity;assert(exact(*filter.sample(pose(46.11),changed,1070000,true),pose(46.11)));
    changed.hand=1;assert(exact(*filter.sample(pose(46.12),changed,1080000,true),pose(46.12)));
    assert(!filter.sample(pose(46.13),changed,1090000,false));
    assert(exact(*filter.sample(pose(46.14),changed,1100000,true),pose(46.14)));
    for(unsigned kind=0;kind<4;++kind) {
        auto invalid=pose(0);
        if(kind==0)invalid.basis[0].x=std::numeric_limits<double>::quiet_NaN();
        if(kind==1)invalid.basis[2]=invalid.basis[2]*-1;
        if(kind==2)invalid.basis[0]=invalid.basis[0]*1.01;
        if(kind==3)invalid.position.x=std::numeric_limits<double>::infinity();
        assert(!filter.sample(invalid,owner,1200000+kind*10000,true));
        assert(exact(*filter.sample(pose(3),owner,1200001+kind*10000,true),pose(3)));
    }
    auto missing=owner;missing.entity=0;assert(!filter.sample(pose(0),missing,1300000,true));
    missing=owner;missing.full_spawn=0;assert(!filter.sample(pose(0),missing,1300001,true));
    filter.clear();double largest_lag{};
    for(unsigned frame=0;frame<2000;++frame) {
        const auto raw=pose(frame*.019);const auto filtered=filter.sample(raw,owner,2000000+frame*1000,true);assert(filtered);
        const double lag=std::abs(yaw(raw)-yaw(*filtered));largest_lag=std::max(largest_lag,lag);
        assert(lag<=Filter::maximum_degrees+1e-9 && d2vr::length(raw.position-filtered->position)==0);
    }
    assert(largest_lag>.099);
    const d2vr::Quaternion rotation=d2vr::normalized({.2,.3,.4,.8});
    filter.clear();
    for(unsigned frame=0;frame<100;++frame) {
        auto raw=pose((frame&1)?.04:-.04);
        for(auto& axis:raw.basis)axis=d2vr::rotated(rotation,axis);
        const auto filtered=filter.sample(raw,owner,5000000+frame*10000,true);assert(filtered && Filter::proper(filtered->basis));
    }
    std::cout<<"PASS orientation only; static/fast exact, bounded jitter, rate, stale/duplicate, identity/focus and proper rotation checks\n";
}
