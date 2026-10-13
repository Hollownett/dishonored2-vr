#include "../src/native_rigid_body.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <iostream>
int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000,game=0x1000000,entity=0x2000000,component=0x3000000,entries=0x4000000,world=0x5000000,pool=0x6000000;
    const std::uint32_t id=0x10001,handle=0x1000002;const auto record=pool+2*0xb0;
    std::map<std::uintptr_t,unsigned char> bytes;
    auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;};
    put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x3130b0,std::uintptr_t{0x7000000});put(game+0x3130b8,entity);put(entity+0xa8,id);
    put(entity,base+0x1c886a8);put(entity+0x268,rig::List{entries,1,1<<8});put(entries,component);put(component,base+0x1c745c8);put(component+0x10,entity);put(component+0x18,rig::Identity{7,10});
    put(entity+0xf0,std::uintptr_t{0x8000000});put(0x8000000,base+0x1b0dd88);
    put(component+0xe8,base+0x1c74798);put(component+0xf8,handle);put(component+0xc8,std::uintptr_t{0});put(component+0xd2,static_cast<unsigned char>(0));
    put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,std::uint32_t{3});
    rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=1;put(record,t);put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{1});put(record+0x70,handle);put(record+0x74,handle);
    const auto body=native_rigid_body(read,base,entity);assert(body && body->identity.generation==7 && body->pose.position.x==1);
    put(entity,base+0x1c87d58);assert(native_rigid_body(read,base,entity)); // Exact idEntity owns visible standalone dynamics too.
    put(record+0x44,std::uint32_t{129});assert(!native_rigid_body(read,base,entity));
    put(record+0x44,std::uint32_t{134});assert(!native_rigid_body(read,base,entity));
    put(record+0x44,std::uint32_t{130});put(record+0x74,handle+1);assert(!native_rigid_body(read,base,entity));
    put(record+0x74,handle);put(component+0xc8,entity);assert(!native_rigid_body(read,base,entity));
    put(component+0xc8,std::uintptr_t{0});put(component+0x18,rig::Identity{-1,10});assert(!native_rigid_body(read,base,entity));
    put(component+0x18,rig::Identity{7,10});put(entity,base+0x1c5e258);assert(!native_rigid_body(read,base,entity)); // Unverified pickup class remains excluded.
    put(entity,base+0x1c886a8);
    put(record+0x44,std::uint32_t{138});assert(native_rigid_body(read,base,entity)); // Waking a sleeping dynamic prop preserves ownership.
    put(entity+0xf0,std::uintptr_t{0});assert(!native_rigid_body(read,base,entity));put(entity+0xf0,std::uintptr_t{0x8000000});
    put(record+0x44,std::uint32_t{129});assert(!native_rigid_body(read,base,entity)); // Static.
    put(record+0x44,std::uint32_t{134});assert(!native_rigid_body(read,base,entity)); // Keyframed.
    put(record+0x44,std::uint32_t{130});put(record+0x70,handle+1);assert(!native_rigid_body(read,base,entity)); // Recycled physics handle.
    put(record+0x70,handle);put(record+0x74,handle+1);assert(!native_rigid_body(read,base,entity)); // Compound primary differs.
    put(record+0x74,handle);put(component+0xc8,entity);assert(!native_rigid_body(read,base,entity)); // Native parent attachment.
    put(component+0xc8,std::uintptr_t{0});put(game+0x3130b8,std::uintptr_t{0});assert(!native_rigid_body(read,base,entity));
    put(game+0x3130b8,entity);put(component+0x18,rig::Identity{-1,10});assert(!native_rigid_body(read,base,entity));
    put(component+0x18,rig::Identity{7,10});
    std::array<float,8> bounds{.9f,-.1f,0,0,1.1f,.1f,2.8f,0};
    put(0x8000000+0x1840,bounds);put(0x8000000+0x1860,std::array<float,3>{1,0,0});
    const Vec3 surface{1,0,2.6};const auto tall=native_rigid_body(read,base,entity);
    assert(tall && tall->bounds && tall->contact_distance(surface)==0 && length(tall->pose.position-surface)>2);
    assert(std::abs(tall->contact_distance({1.25,0,2.6})-.15)<1e-6);
    assert(std::abs(tall->contact_distance({1.25,.25,2.6})-std::hypot(.15,.15))<1e-6);
    auto hand=tall->pose;hand.position=surface;PhysicalGrab grab;
    grab.sample(hand,0,true,1,100);grab.sample(hand,0,true,1,110);grab.sample(hand,.9f,true,1,120);
    assert(!grab.bind(tall->pose));assert(grab.bind(tall->pose,tall->contact(surface)));
    assert(length(grab.target().position-tall->pose.position)<1e-9); // Contact pickup preserves the distant native pivot.
    std::array<std::uintptr_t,0x3fff> registry{};registry[0]=0x7000000;registry[1]=entity;
    put(game+0x3130b0,registry);put(entity+0x300,std::array<float,3>{1,0,0});
    std::vector<NativeRigidBody> candidates;
    assert(nearest_grab_body(read,base,surface,.3));
    assert(nearest_grab_body(read,base,surface,2.,&candidates) && candidates.size()==1); // Cache includes surfaces whose pivots are beyond 2m.
    assert(!nearest_grab_body(read,base,surface,4.)); // Ordinary acquisition/cache scope remains bounded at 2m.
    assert(nearest_grab_body(read,base,surface,4.,nullptr,GrabSearchScope::ControlledFixture));
    assert(!nearest_grab_body(read,base,surface,4.1,nullptr,GrabSearchScope::ControlledFixture));
    bounds[4]=bounds[0]-1;put(0x8000000+0x1840,bounds);assert(!native_rigid_body(read,base,entity)->bounds);
    assert(!nearest_grab_body(read,base,surface,.3)); // Invalid geometry cannot expand acquisition.
    bounds[4]=1.1f;bounds[6]=4;put(0x8000000+0x1840,bounds);assert(!native_rigid_body(read,base,entity)->bounds);
    bounds[6]=2.8f;put(0x8000000+0x1840,bounds);put(0x8000000+0x1860,std::array<float,3>{2,0,0});
    assert(!native_rigid_body(read,base,entity)->bounds); // Stale render bounds after native body movement.
    put(0x8000000+0x1860,std::array<float,3>{1,0,0});bounds[2]=1;put(0x8000000+0x1840,bounds);
    assert(!native_rigid_body(read,base,entity)->bounds); // Unrelated box cannot supply a contact.
    put(component+0x18,rig::Identity{7,10});unsigned reads{};
    auto recycling=[&](std::uintptr_t at,void* out,std::size_t size) {const bool ok=read(at,out,size);if(at==component+0x18 && ++reads==1) put(component+0x18,rig::Identity{8,10});return ok;};
    assert(!native_rigid_body(recycling,base,entity));
    {   // a native carryable (Pickable_Simple beside the body) is left to the game's own Use
        const std::uintptr_t list2=0x7000000,pickable=0x7100000;
        put(entity+0x268,rig::List{list2,2,2<<8});put(list2,component);put(list2+8,pickable);put(pickable,base+0x1c5a3b8);
        assert(!native_rigid_body(read,base,entity));
        put(entity+0x268,rig::List{entries,1,1<<8});
    }
    std::cout<<"PASS: native dynamic-body registry/component/handle ownership; static/keyframed/compound/attached/recycled/native-carryable exclusion\n";
}
