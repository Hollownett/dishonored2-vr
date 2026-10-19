
#include "../src/drawn_palm_residual.hpp"
#include <cassert>
using namespace d2vr;
static WorldTrackedPose pose(Vec3 position={},bool quarter=false) {
    return {position,quarter?std::array<Vec3,3>{{{0,1,0},{-1,0,0},{0,0,1}}}:
        std::array<Vec3,3>{{{1,0,0},{0,1,0},{0,0,1}}}};
}
static DrawnPalmSource source() {
    DrawnPalmSource result;
    result.context={7,8,9,10,pose(),true};result.grip=pose({1,2,3});result.palm=pose({.8,2,3});
    result.tick=1000;result.source_tick=990;result.tracking_revision=20;result.frame=30;result.callback=40;result.valid=true;
    return result;
}
static void near(Vec3 actual,Vec3 expected) {assert(length(actual-expected)<1e-12);}
int main() {
    auto sample=source();auto context=sample.context;DrawnPalmResidual cache;

    assert(cache.age_ms(1010)==-1);
    cache.note(sample,context,1010);near(cache.frame(pose({4,2,3}),1010).position,{3.8,2,3});
    assert(cache.age_ms(1010)==10 && cache.age_ms(1490)==490 && cache.age_ms(900)==-1);
    assert(cache.age_ms(1491)==-1 && cache.age_ms(1600)==-1); // past 500 ms frame() no longer applies it: no age reported

    near(cache.frame(pose({4,2,3},true),1010).position,{3.8,2,3});

    context.root=pose({100,200,300});cache.note(sample,context,1011);
    near(cache.frame(pose({104,202,303}),1011).position,{103.8,202,303});
    context.root=pose({100,200,300},true);cache.note(sample,context,1012);
    near(cache.frame(pose({104,202,303}),1012).position,{104,201.8,303});

    auto mapped=sample;mapped.context.root=pose({10,20,30},true);mapped.grip=pose({8,21,33},true);mapped.palm=pose({8,20.8,33},true);
    DrawnPalmResidual mapped_cache;mapped_cache.note(mapped,mapped.context,1010);
    near(mapped_cache.frame(pose({4,5,6}),1010).position,{4,4.8,6});

    DrawnPalmResidual left,right;left.note(sample,sample.context,1010);
    auto other=sample;other.palm.position={1,2.1,3};other.callback=41;right.note(other,other.context,1010);
    near(left.frame(pose(),1010).position,{-.2,0,0});near(right.frame(pose(),1010).position,{0,.1,0});

    cache.note(sample,context,1480);near(cache.frame(pose(),1491).position,{});
    for(unsigned field=0;field<4;++field) {
        DrawnPalmResidual reset;reset.note(sample,sample.context,1010);auto changed=sample.context;
        if(field==0)++changed.pose_epoch;if(field==1)++changed.reference_epoch;
        if(field==2)++changed.mapping_revision;if(field==3)++changed.fit_revision;
        reset.note(sample,changed,1011);near(reset.frame(pose(),1011).position,{});
    }
    auto invalid_context=sample.context;invalid_context.valid=false;
    left.note(sample,invalid_context,1011);near(left.frame(pose(),1011).position,{});
    DrawnPalmResidual regression;regression.note(sample,sample.context,1010);
    auto old=sample;--old.tracking_revision;old.palm.position={1,2.2,3};++old.callback;
    regression.note(old,old.context,1011);near(regression.frame(pose(),1011).position,{-.2,0,0});
    auto older_xr=sample;older_xr.tick=1001;older_xr.source_tick=989;++older_xr.tracking_revision;++older_xr.frame;++older_xr.callback;
    older_xr.palm.position={1,2.2,3};regression.note(older_xr,older_xr.context,1011);
    near(regression.frame(pose(),1011).position,{-.2,0,0});
    auto no_xr=sample;no_xr.source_tick=0;DrawnPalmResidual no_clock;no_clock.note(no_xr,no_xr.context,1010);near(no_clock.frame(pose(),1010).position,{});
    auto bad=sample;bad.frame=0;DrawnPalmResidual no_frame;no_frame.note(bad,bad.context,1010);near(no_frame.frame(pose(),1010).position,{});
    bad=sample;bad.source_tick=1020;no_frame.note(bad,bad.context,1010);near(no_frame.frame(pose(),1010).position,{});
}
