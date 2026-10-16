#include "../src/native_door_skeleton_content.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using Owner=d2vr::NativeDoorOwner;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes{};
    std::function<void(std::uintptr_t)> before{};
    unsigned reads{},writes{};
    std::uintptr_t fail{};
    bool throwing{};
    void zero(std::uintptr_t address,std::size_t size) {for(std::size_t index=0;index<size;++index)bytes[address+index]=0;}
    template<class Value> void put(std::uintptr_t address,const Value& value) {
        const auto* source=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t index=0;index<sizeof(value);++index)bytes[address+index]=source[index];
    }
    bool operator()(std::uintptr_t address,void* output,std::size_t size) {
        ++reads;if(before)before(address);
        if(throwing)throw std::runtime_error("reader");
        if(address==fail)return false;
        auto* destination=static_cast<unsigned char*>(output);
        for(std::size_t index=0;index<size;++index) {const auto found=bytes.find(address+index);if(found==bytes.end())return false;destination[index]=found->second;}
        return true;
    }
};
struct Fixture {
    static constexpr std::uintptr_t base=0x140000000,game=0x20000000,world=0x21000000,player=0x22000000,door=0x23000000,part=0x24000000,
        component=0x25000000,animation=0x26000000,decl=0x27000000,schema=0x28000000,input_record=0x29000000,node=0x2a000000,
        node_record=0x2b000000,input_buckets=0x2c000000,node_buckets=0x2d000000,door_list=0x2e000000,part_list=0x2f000000,component_decl=0x30000000;
    static constexpr std::uint32_t player_spawn=0x12,door_spawn=0x10013,part_spawn=0x20014;
    Memory memory{};
    Fixture() {
        memory.put(base+0x39a5290,game);memory.put(game,base+0x1c940a8);memory.put(game+0x60410,world);
        memory.put(game+0x3130b0,player);memory.put(game+0x3130b0+8,door);memory.put(game+0x3130b0+16,part);
        memory.put(player,base+0x1c5e428);memory.put(player+0xa8,player_spawn);memory.put(door,base+0x1cb7c68);memory.put(door+0xa8,door_spawn);
        memory.put(part+0xa8,part_spawn);memory.put(door+0x370,component);memory.put(component,base+0x1c569c8);memory.put(component+8,component_decl);
        memory.put(component_decl,base+0x1c56af8);memory.put(component+0x10,door);memory.put(component+0x18,d2vr::rig::Identity{7,0xa});
        memory.put(component+0xdc,part_spawn);memory.put(component+0xf8,animation);memory.put(animation,base+0x1b7f408);
        memory.put(animation+8,decl);memory.put(animation+0x10,part);memory.put(animation+0x18,d2vr::rig::Identity{9,0xa});
        memory.put(door+0x268,d2vr::rig::List{door_list,1,32u<<8});memory.put(door_list,component);
        memory.put(part+0x268,d2vr::rig::List{part_list,1,32u<<8});memory.put(part_list,animation);
        memory.put(component+0x1e0,std::array<unsigned char,4>{3,4,7,8});
        memory.put(animation+0x340,schema);memory.put(animation+0x390,input_record+8);memory.put(decl+0x68,schema);memory.put(decl+0x70,std::uintptr_t{0x1234});
        memory.put(schema+0xc0,0x2000u);memory.put(schema+0x70,input_buckets);memory.put(schema+0x80,0u);memory.put(input_buckets,input_record);
        memory.put(input_record,std::uintptr_t{0x1234});memory.put(input_record+12,std::int32_t{0x100});memory.put(input_record+0x970,std::uintptr_t{});
        memory.put(schema+0x58,node_buckets);memory.put(schema+0x60,1u);memory.put(schema+0x64,1u);memory.put(node_buckets,node_record);
        memory.put(node_record+8,node);memory.put(node_record+0x10,std::int32_t{0x200});memory.put(node_record+0x18,std::uintptr_t{});
        memory.put(node,base+0x1c4bce8);memory.put(node+8,schema);memory.put(node+0x590,static_cast<unsigned char>(1));memory.put(node+0x5b0,std::uintptr_t{0x1234});
        memory.put(component+0x48,0u);memory.put(component+0x4c,0u);memory.put(door+0x378,0x3fff0010u);memory.put(component+0x114,player_spawn);
        memory.put(component+0xf0,std::uintptr_t{});memory.put(component+0x90,std::array<unsigned char,4>{});memory.put(component+0x1e8,std::array<unsigned char,6>{});
        memory.put(component+0x20,static_cast<unsigned char>(1));memory.put(component+0x118,static_cast<unsigned char>(0));
    }
    std::optional<Owner::Snapshot> capture(Owner::Failure* failure=nullptr) {return Owner::capture(base,memory,door,failure);}
};
}

namespace {
using Skeleton=d2vr::NativeDoorSkeletonContent;
struct SkeletonFixture:Fixture {
    static constexpr std::uintptr_t model=0x31000000,mesh=0x32000000,skeleton=0x33000000,clip=0x34000000,blob=0x35000000,
        joint_ids=0x36000000,pool=0x37000000,strings=0x38000000;
    Owner::Snapshot owner{};
    SkeletonFixture() {
        const auto captured=Fixture::capture();assert(captured);owner=*captured;
        for(const auto& pin:Skeleton::pins)for(unsigned index=0;index<pin.size;++index)memory.put(base+pin.rva+index,pin.bytes[index]);
        memory.put(base+0x1b0d628+0x20,base+0x25df10);memory.put(base+0x1b0bd08+0x80,base+0x24f1c0);
        memory.put(base+0x1b0bd08+0x88,base+0x24f240);memory.put(base+0x1b093c8+0x38,base+0x23cb00);
        memory.put(part+0xf0,model);memory.put(model,base+0x1b0d628);memory.put(model+0x1fe8,mesh);memory.put(mesh+0x50,skeleton);
        memory.put(skeleton,base+0x1b0bd08);memory.put(clip,base+0x1b0ba38);memory.put(clip+0x30,skeleton);
        memory.put(skeleton+0x158,blob);memory.put(skeleton+0x160,static_cast<std::uint32_t>(Skeleton::resident.size()));memory.put(blob,Skeleton::resident);
        memory.put(skeleton+0x70,d2vr::rig::List{joint_ids,6,32u<<8});
        memory.put(joint_ids,std::array<std::uint16_t,6>{1,2,3,4,5,6});
        memory.put(base+0x22995c0,base+0x22995d0);memory.put(base+0x22995d0,base+0x1b093c8);
        memory.put(base+0x22995d0+8,d2vr::rig::List{pool,7,32u<<8});
        for(unsigned index=0;index<Skeleton::names.size();++index) {
            memory.put(pool+(index+1)*0x48ull+0x10,strings+index*0x40ull);
            std::array<char,24> name{};std::memcpy(name.data(),Skeleton::names[index],std::strlen(Skeleton::names[index])+1);memory.put(strings+index*0x40ull,name);
        }
    }
    std::optional<Skeleton::Lease> capture() {return Skeleton::capture(base,memory,owner,clip);}
};
}
int main() {
    static_assert(std::is_trivially_copyable_v<Skeleton::Lease>);
    {
        SkeletonFixture fixture;const auto lease=fixture.capture();assert(lease);assert(lease->content==Skeleton::resident);
        assert(lease->name_ids[1]==2 && Skeleton::current(fixture.memory,*lease));assert(fixture.memory.writes==0);
        fixture.memory.put(Fixture::component+0x48,1u);fixture.memory.put(Fixture::component+0x90,std::array<unsigned char,4>{1,1,1,4});
        assert(Skeleton::current(fixture.memory,*lease));
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::model,Fixture::base+0x1b0dd88);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::clip+0x30,SkeletonFixture::skeleton+0x1000);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::skeleton,Fixture::base+0x1b0ba38);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::skeleton+0x160,447u);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::blob+0x80,static_cast<unsigned char>(7));assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::blob+0x1c,0xffffffffu);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::skeleton+0x70,d2vr::rig::List{SkeletonFixture::joint_ids,7,32u<<8});assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::skeleton+0x70,d2vr::rig::List{SkeletonFixture::joint_ids,6,5u<<8});assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::joint_ids,std::array<std::uint16_t,6>{1,1,3,4,5,6});assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::joint_ids,std::array<std::uint16_t,6>{1,0xffff,3,4,5,6});assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(Fixture::base+0x22995d0+8,d2vr::rig::List{SkeletonFixture::pool,6,32u<<8});assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(SkeletonFixture::strings+0x40,static_cast<char>('x'));assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(Fixture::base+0x22995c0,Fixture::base+0x22995d8);assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.put(Fixture::base+Skeleton::pins[0].rva,static_cast<unsigned char>(0));assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.fail=SkeletonFixture::blob;assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;fixture.memory.before=[](std::uintptr_t address) {if(address==SkeletonFixture::blob)throw std::runtime_error("content");};assert(!fixture.capture());
    }
#if defined(_MSC_VER)
    {
        SkeletonFixture fixture;fixture.memory.before=[](std::uintptr_t address) {if(address==SkeletonFixture::blob)RaiseException(0xe0011234,0,0,nullptr);};assert(!fixture.capture());
    }
#endif
    {
        SkeletonFixture fixture;unsigned reads{};fixture.memory.put(SkeletonFixture::model+0x1000,Fixture::base+0x1b0d628);
        fixture.memory.put(SkeletonFixture::model+0x1000+0x1fe8,SkeletonFixture::mesh);
        fixture.memory.before=[&](std::uintptr_t address) {
            if(address==Fixture::part+0xf0 && ++reads==2)fixture.memory.put(address,SkeletonFixture::model+0x1000);
        };
        assert(!fixture.capture());
    }
    {
        SkeletonFixture fixture;const auto lease=fixture.capture();assert(lease);
        fixture.memory.put(Fixture::animation+0x18,d2vr::rig::Identity{10,0xa});assert(!Skeleton::current(fixture.memory,*lease));
    }
    {
        SkeletonFixture fixture;const auto lease=fixture.capture();assert(lease);
        fixture.memory.put(Fixture::part+0xa8,Fixture::part_spawn+1);assert(!Skeleton::current(fixture.memory,*lease));
    }
    std::cout<<"native_door_skeleton_content_check PASS\n";return 0;
}
