#include "../src/full_hand_contact.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
namespace {
NativeWorldQuerySource source() {
    NativeWorldQuerySource value{};value.base=0x140000000;value.game=0x2000000;value.player=0x2100000;value.camera=0x2200000;
    value.camera_controller=0x2300000;value.physics=0x2400000;value.delegate=0x2500000;value.controller=0x2600000;value.world=0x2700000;
    value.pool=0x2800000;value.body=value.pool+2*0xb0;value.handle=0x12000002;value.spawn=6;
    value.body_count=64;value.broadphase=1;value.body_flags=3;value.camera_identity={7,10};value.physics_identity={9,10};
    value.component_list_data=0x2900000;value.component_list_count=value.component_list_capacity=4;assert(value.valid());return value;
}
HandProxies hand(Vec3 centre={}) {
    HandProxies result{};for(auto& proxy:result)proxy={centre,.02};return result;
}
using QueryArray=std::array<WorldTraceQueryResult,hand_proxy_count>;
struct QueryBatch {
    std::unique_ptr<QueryArray> value=std::make_unique<QueryArray>();
    WorldTraceQueryResult& operator[](unsigned index) {return (*value)[index];}
    operator const QueryArray&() const {return *value;}
};
auto queries() {
    QueryBatch result;
    for(auto& query:*result.value) {query.complete=true;query.initial.emplace();query.regular.emplace();}
    return result;
}
void plane(WorldTraceQueryResult& query,Vec3 point,Vec3 normal,std::uint32_t body=0x12000003) {
    auto& hit=query.regular->hits[query.regular->count++];hit.point=point;hit.normal=normal;hit.fraction=.5;
    hit.body_handle=body;hit.body=body&0xffffffu;hit.normal_valid=true;
}
void overlap(WorldTraceQueryResult& query,Vec3 normal,float separation,std::uint32_t body=0x12000003) {
    auto& hit=query.initial->hits[query.initial->count++].contact;hit.normal=normal;hit.signed_separation=separation;
    hit.normal_valid=true;hit.source_body=source().handle;hit.body=body;hit.shape_key=7;
}
bool close_to(double a,double b) {return std::abs(a-b)<1e-6;}
void check_surfaces() {
    // Palm, fingertip and back all contribute through the exact palette proxy index.
    for(const unsigned proxy:{0u,7u,3u}) {
        auto batch=queries();plane(batch[proxy],{0,0,0},{0,0,1});
        const auto result=full_hand_contact_reduce(hand({0,0,.03}),hand(),batch,source());
        assert(result.valid && !result.blocked && close_to(result.shift.z,.02) && result.planes[0].proxy==proxy);
    }
    auto batch=queries();plane(batch[0],{0,0,0},{1,0,0});plane(batch[7],{0,0,0},{0,1,0});
    auto result=full_hand_contact_reduce(hand({.03,.03,0}),hand(),batch,source());
    assert(result.valid && !result.blocked && close_to(result.shift.x,.02) && close_to(result.shift.y,.02));
    plane(batch[3],{0,0,0},{-1,0,0});
    result=full_hand_contact_reduce(hand(),hand(),batch,source());assert(result.valid && result.blocked && length(result.shift)==0);
}
void check_initial_and_sweep() {
    auto batch=queries();overlap(batch[0],{1,0,0},-.01f);
    batch[0].initial->hits[0].entity=0x3000000;
    auto result=full_hand_contact_reduce(hand(),hand({.03,0,0}),batch,source());
    assert(result.valid && !result.blocked && length(result.shift)==0 && result.planes[0].depth<0);
    assert(result.planes[0].entity==0x3000000);
    // A nonpenetrating initial plane still limits another proxy's correction.
    plane(batch[1],{.02,0,0},{-1,0,0});
    result=full_hand_contact_reduce(hand(),hand({.03,0,0}),batch,source());
    assert(result.valid && result.blocked);
    auto thin=queries();plane(thin[7],{0,0,0},{-1,0,0});
    thin[7].regular->hits[0].entity=0x3100000;
    result=full_hand_contact_reduce(hand({-.15,0,0}),hand({.15,0,0}),thin,source());
    assert(result.valid && !result.blocked && close_to(result.shift.x,-.17));
    assert(result.planes[0].entity==0x3100000);
    result=full_hand_contact_reduce(hand({-.3,0,0}),hand({.15,0,0}),thin,source());assert(!result.valid);
}
void check_invalid() {
    auto batch=queries();batch[23].complete=false;
    assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
    batch=queries();batch[1].initial->count=33;assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
    batch=queries();batch[1].regular->count=33;assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
    batch=queries();overlap(batch[0],{1,0,0},-.01f);batch[0].initial->hits[0].contact.source_body=4;
    assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
    batch=queries();plane(batch[0],{0,0,0},{1,0,0},source().handle);
    assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
    batch=queries();plane(batch[0],{0,0,0},{1,0,0});batch[0].regular->hits[0].entity=0x3000000;
    assert(!full_hand_contact_reduce(hand(),hand(),batch,source(),0x3000000).valid);
    batch[0].regular->hits[0].normal.x=std::numeric_limits<double>::quiet_NaN();
    assert(!full_hand_contact_reduce(hand(),hand(),batch,source()).valid);
}
void check_reprojection() {
    auto batch=queries();plane(batch[0],{0,0,0},{1,0,0});
    const auto result=full_hand_contact_reduce(hand(),hand(),batch,source());
    auto current=hand({-.01,0,0});
    auto projected=full_hand_contact_reproject(result,current,1032,1000,1000);
    assert(projected.valid && !projected.blocked && close_to(projected.shift.x,.03));
    assert(!full_hand_contact_reproject(result,current,1033,1000,1000).valid);
    assert(!full_hand_contact_reproject(result,current,1032,1000,999).valid);
    assert(!full_hand_contact_reproject(result,current,999,1000,1000).valid);
    current[23].radius+=.001;assert(!full_hand_contact_reproject(result,current,1010,1000,1000).valid);
    current=hand();current[23].centre.z=.061;assert(!full_hand_contact_reproject(result,current,1010,1000,1000).valid);
}
void check_grab() {
    const NativeGrabBounds bounds{{0,0,0},{.1,.1,.1}};
    auto proxies=hand({-.03,.05,.05});assert(!full_hand_contact_grab(proxies,bounds));
    proxies[7].centre.x=-.022;const auto contact=full_hand_contact_grab(proxies,bounds);
    assert(contact && close_to(contact->x,0) && close_to(contact->y,.05));
    assert(!full_hand_contact_grab(proxies,bounds,.004));
    assert(!full_hand_contact_grab(hand({-.1,.05,.05}),bounds));
}
}
int main() {
    check_surfaces();check_initial_and_sweep();check_invalid();check_reprojection();check_grab();
    std::cout<<"full_hand_contact_check passed\n";
}
