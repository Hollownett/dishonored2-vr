#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_rigid_body.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
using namespace d2vr;
namespace {
constexpr std::uintptr_t base=0x140000000,game=0x2000000,entity=0x2700000,component=0x2800000,entries=0x2900000,world=0x3000000,pool=0x3100000,model=0x3200000,
    pickable=0x3300000;
std::map<std::uintptr_t,unsigned char> bytes;
template<class T> void put(std::uintptr_t at,const T& v) {const auto* b=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof v;++i) bytes[at+i]=b[i];}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;static_cast<unsigned char*>(out)[i]=it->second;}
    return true;
}
// One loose prop at (1,0,0) with model bounds `box` and rendered origin `origin`; flags: the pool record's motion word (2 dynamic, 6 keyframed).
void prop(std::array<float,8> box,std::array<float,3> origin,std::uint32_t flags=130,bool with_pickable=false) {
    bytes.clear();
    put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x3130b0,std::uintptr_t{0x2100000});
    put(game+0x3130b8,entity);put(entity,base+0x1c886a8);put(entity+0xa8,std::uint32_t{0x10001});put(entity+0x300,std::array<float,3>{1,0,0});
    put(entity+0x268,rig::List{entries,with_pickable?2u:1u,4u<<8});put(entries,component);if(with_pickable) {put(entries+8,pickable);put(pickable,base+0x1c5a3b8);}
    put(entity+0xf0,model);put(model,base+0x1b0dd88);put(component,base+0x1c745c8);
    put(component+0x10,entity);put(component+0x18,rig::Identity{7,10});put(component+0xe8,base+0x1c74798);
    constexpr std::uint32_t handle=0x1000002;put(component+0xf8,handle);put(component+0xc8,std::uintptr_t{});put(component+0xd2,static_cast<unsigned char>(0));
    put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,std::uint32_t{3});
    rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=1;
    const auto record=pool+2*0xb0;put(record,t);put(record+0x44,flags);put(record+0x40,std::uint32_t{1});put(record+0x70,handle);put(record+0x74,handle);
    put(model+0x1840,box);put(model+0x1860,origin);
}
}
int main() {
    BodyReject why{};
    {   // centre_offset_only_accepted_when_pose_inside: a paper stack 0.6 x 0.4 x 0.01 m whose body origin sits 0.28 m off its rendered centre
        const NativeGrabBounds paper{{.7,-.2,-.005},{1.3,.2,.005}};
        assert(accept_bounds(paper,{1,0,0},{1,0,0},&why) && why==BodyReject::ok);                 // the usual case
        assert(accept_bounds(paper,{1,0,0},{.72,0,0},&why) && why==BodyReject::ok);               // offset .28 m, pose inside: the box is kept
        assert(accept_bounds(paper,{1,0,0},{.72,0,.0059},&why));                                  // within 1 mm of the face counts as inside
        assert(!accept_bounds(paper,{1,0,0},{.72,0,.0062},&why) && why==BodyReject::bounds_centre);
    }
    {   // centre_offset_with_pose_outside_rejected: the same offset with the pose 2 cm off the box falls back to the origin point
        const NativeGrabBounds paper{{.7,-.2,-.005},{1.3,.2,.005}};
        assert(!accept_bounds(paper,{1,0,0},{.72,0,.025},&why) && why==BodyReject::bounds_centre);
        assert(!accept_bounds(paper,{1,0,0},{.68,0,0},&why) && why==BodyReject::bounds_centre);   // 2 cm past the end
        assert(accept_bounds(paper,{.9,0,0},{.68,0,0},&why));                                     // a small offset (0.22 m) keeps the old rule
    }
    {   // size_limits_unchanged: 1 mm to 3 m diagonal, ordered and finite, body within 0.5 m of the box
        assert(!accept_bounds({{0,0,0},{.0005,0,0}},{0,0,0},{0,0,0},&why) && why==BodyReject::bounds_size);
        assert(!accept_bounds({{0,0,0},{2.5,2,0}},{1,1,0},{1,1,0},&why) && why==BodyReject::bounds_size);
        assert(accept_bounds({{0,0,0},{2.4,1.7,0}},{1,1,0},{1,1,0},&why));
        assert(!accept_bounds({{1,0,0},{0,1,1}},{.5,.5,.5},{.5,.5,.5},&why) && why==BodyReject::bounds_size);
        assert(!accept_bounds({{0,0,0},{std::nan(""),1,1}},{.5,.5,.5},{.5,.5,.5},&why) && why==BodyReject::bounds_size);
        assert(!accept_bounds({{0,0,0},{.2,.2,.2}},{.1,.1,.1},{.1,.1,.71},&why) && why==BodyReject::bounds_centre);   // 0.51 m from the box
    }
    {   // reject_reason_reported: native_rigid_body says why (the result itself is unchanged)
        prop({.7f,-.2f,-.005f,0,1.3f,.2f,.005f,0},{1,0,0});auto b=native_rigid_body(read,base,entity,false,&why);
        assert(b && b->bounds && why==BodyReject::ok);
        prop({.95f,-.2f,-.005f,0,1.55f,.2f,.005f,0},{1.25f,0,0});b=native_rigid_body(read,base,entity,false,&why);   // thin prop, pose inside
        assert(b && b->bounds && why==BodyReject::ok);
        prop({1.05f,-.2f,-.005f,0,1.65f,.2f,.005f,0},{1.35f,0,0});b=native_rigid_body(read,base,entity,false,&why);  // pose outside: origin point
        assert(b && !b->bounds && why==BodyReject::bounds_centre);
        prop({.9f,-.1f,-.1f,0,1.1f,.1f,.1f,0},{1,0,0},134);b=native_rigid_body(read,base,entity,false,&why);       // keyframed (flags&7 == 6)
        assert(!b && why==BodyReject::not_dynamic);
        prop({.9f,-.1f,-.1f,0,1.1f,.1f,.1f,0},{1,0,0},130,true);b=native_rigid_body(read,base,entity,false,&why);  // a native carryable
        assert(!b && why==BodyReject::pickable);
        b=native_rigid_body(read,base,game,false,&why);assert(!b && why==BodyReject::not_prop);
        prop({.9f,-.1f,-.1f,0,1.1f,.1f,.1f,0},{1,0,0});assert(native_rigid_body(read,base,entity));   // why is optional
        assert(std::string(body_reject_name(BodyReject::bounds_centre))=="bounds_centre");
    }
    std::cout<<"PASS: thin-prop bounds (centre offset accepted only with the pose inside), size limits unchanged, reject reasons reported\n";
}
