#include "../src/native_door_owner.hpp"
#include "../src/native_door_clip_selection.hpp"
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
int main() {
    {
        Fixture fixture;auto& memory=fixture.memory;
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0x4a});
        memory.put(Fixture::animation+0x18,d2vr::rig::Identity{9,0x3a});
        const auto sleeping=fixture.capture();assert(sleeping);
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0xa});
        memory.put(Fixture::animation+0x18,d2vr::rig::Identity{9,0x7fffffa});
        assert(Owner::current(memory,*sleeping));
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{8,0xa});assert(!Owner::current(memory,*sleeping));
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0x800000a});assert(!Owner::current(memory,*sleeping));
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0xb});assert(!Owner::current(memory,*sleeping));
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,8});assert(!fixture.capture());
        memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0x4a});
        unsigned observed{};memory.before=[&](std::uintptr_t address) {
            if(address==Fixture::component+0x18 && ++observed==2)memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0xa});
        };
        assert(fixture.capture());
        memory.before={};memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0x4a});observed=0;
        memory.before=[&](std::uintptr_t address) {
            if(address==Fixture::component+0x18 && ++observed==2)memory.put(Fixture::component+0x18,d2vr::rig::Identity{8,0xa});
        };
        assert(!fixture.capture());
        memory.before={};memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0xa});
        memory.put(Fixture::animation+0x18,d2vr::rig::Identity{9,0x6a});observed=0;
        memory.before=[&](std::uintptr_t address) {
            if(address==Fixture::animation+0x18 && ++observed==2)memory.put(Fixture::animation+0x18,d2vr::rig::Identity{9,0x3a});
        };
        assert(fixture.capture());
        for(const std::uint32_t changed:{0x800000au,0xbu,8u}) {
            memory.before={};memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0xa});observed=0;
            memory.before=[&](std::uintptr_t address) {
                if(address==Fixture::component+0x18 && ++observed==2)memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,changed});
            };
            assert(!fixture.capture());
        }
    }
    {
        Fixture fixture;auto& memory=fixture.memory;
        const auto snapshot=fixture.capture();assert(snapshot);
        using Selection=d2vr::NativeDoorClipSelection;
        using Content=d2vr::NativeDoorClipContent;
        constexpr std::uintptr_t holder=0x31000000,arena=0x32000000,clips=0x33000000,descriptor=0x34000000,buffer=0x35000000;
        const auto context=arena+snapshot->context_offset;
        memory.put(snapshot->container,holder);memory.put(holder,arena);memory.put(context+0x10,snapshot->container+0x38);
        memory.put(context+0x18,snapshot->container+0x20);memory.put(context+0xa78,d2vr::rig::List{clips,5,5u<<8});
        memory.put(clips+snapshot->clips[1]*0x48ull+0x30,descriptor);memory.put(descriptor,Fixture::base+Content::vtable_rva);
        memory.put(descriptor+0x38,1.f);memory.put(descriptor+0x48,buffer);memory.put(descriptor+0x50,std::uint32_t{Content::resident_size});
        for(std::size_t index=0;index<Content::resident_size;++index)memory.put(buffer+index,Content::ccw_bytes[index]);
        const auto selected=Selection::capture(memory,*snapshot,true);assert(selected && selected->content.ccw && selected->context.index==4);
        assert(Selection::current(memory,*selected));
        memory.put(context+0xa80,4u);assert(!Selection::current(memory,*selected));memory.put(context+0xa80,5u);
        memory.put(context+0xa84,4u<<8);assert(!Selection::current(memory,*selected));memory.put(context+0xa84,5u<<8);
        memory.put(context+0x10,std::uintptr_t{});assert(!Selection::current(memory,*selected));memory.put(context+0x10,snapshot->container+0x38);
        memory.put(holder,std::uintptr_t{});assert(!Selection::current(memory,*selected));memory.put(holder,arena);
        memory.put(buffer+200,static_cast<unsigned char>(Content::ccw_bytes[200]^1));assert(!Selection::current(memory,*selected));
        memory.put(buffer+200,Content::ccw_bytes[200]);
        memory.put(clips+snapshot->clips[1]*0x48ull+0x30,descriptor+0x100);assert(!Selection::current(memory,*selected));
        memory.put(clips+snapshot->clips[1]*0x48ull+0x30,descriptor);
        memory.put(Fixture::animation+0x18,d2vr::rig::Identity{10,0xa});assert(!Selection::current(memory,*selected));
        memory.put(Fixture::animation+0x18,snapshot->animation_identity);
        memory.fail=context+0xa78;assert(!Selection::current(memory,*selected));memory.fail=0;
        memory.before=[&](std::uintptr_t address) {if(address==context+0xa78)throw std::runtime_error("clip selection reader");};
        assert(!Selection::current(memory,*selected));memory.before={};
#if defined(_MSC_VER)
        memory.before=[&](std::uintptr_t address) {if(address==context+0xa78)RaiseException(0xe0421234,0,0,nullptr);};
        assert(!Selection::current(memory,*selected));memory.before={};
#endif
        unsigned seen{};memory.before=[&](std::uintptr_t address) {if(address==snapshot->container && ++seen==2)memory.put(snapshot->container,holder+0x100);};
        assert(!Selection::current(memory,*selected));
    }
    {
        Fixture fixture;const auto snapshot=fixture.capture();assert(snapshot);assert(snapshot->binding==Fixture::input_record+8);
        assert(snapshot->clips[0]==3 && snapshot->clips[3]==8 && snapshot->world==Fixture::world);
        assert(Owner::current(fixture.memory,*snapshot));assert(Owner::state(fixture.memory,*snapshot)->eligible());
        fixture.memory.put(Fixture::component+0x48,1u);fixture.memory.put(Fixture::component+0x90,std::array<unsigned char,4>{1,1,1,4});
        fixture.memory.put(Fixture::component+0x1e8,std::array<unsigned char,6>{1,0,0,0,0,0});
        assert(Owner::current(fixture.memory,*snapshot));const auto state=Owner::state(fixture.memory,*snapshot);assert(state && state->current==1 && !state->eligible());
        fixture.memory.put(Fixture::animation+0x338,std::uintptr_t{0x12345678});assert(Owner::current(fixture.memory,*snapshot));
        assert(fixture.memory.writes==0);
    }
    {
        Fixture fixture;const auto snapshot=fixture.capture();assert(snapshot);
        fixture.memory.put(Fixture::component+0x18,d2vr::rig::Identity{8,0xa});assert(!Owner::current(fixture.memory,*snapshot));
    }
    {
        Fixture fixture;const auto snapshot=fixture.capture();assert(snapshot);
        fixture.memory.put(Fixture::animation+0x18,d2vr::rig::Identity{10,0xa});assert(!Owner::current(fixture.memory,*snapshot));
    }
    {
        Fixture fixture;const auto snapshot=fixture.capture();assert(snapshot);
        fixture.memory.put(Fixture::part+0xa8,Fixture::part_spawn+1);assert(!Owner::current(fixture.memory,*snapshot));
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::door+0x268,d2vr::rig::List{Fixture::door_list,2,32u<<8});fixture.memory.put(Fixture::door_list+8,Fixture::component);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::part_list,Fixture::component);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::component+0x18,d2vr::rig::Identity{7,0});assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::component+0x1e0,std::array<unsigned char,4>{3,4,7,0xff});assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::animation+0x390,Fixture::input_record+16);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::decl+0x68,Fixture::schema+0x100);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::input_record+0x970,Fixture::input_record);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::node_record+0x18,Fixture::node_record);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::node+0x5b0,std::uintptr_t{0x4321});assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::node_record+0x10,std::int32_t{-1});assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::input_record+12,std::int32_t{0x1ff0});assert(!fixture.capture());
    }
    {
        Fixture fixture;Owner::Failure failure{};fixture.memory.fail=Fixture::node+0x590;assert(!fixture.capture(&failure));assert(failure==Owner::Failure::Read);
    }
    {
        Fixture fixture;fixture.memory.throwing=true;assert(!fixture.capture());
    }
    {
        Fixture fixture;unsigned visits{};fixture.memory.before=[&](std::uintptr_t address) {
            if(address==Fixture::component+0x18 && ++visits==2)fixture.memory.put(address,d2vr::rig::Identity{8,0xa});
        };
        Owner::Failure failure{};assert(!fixture.capture(&failure));assert(failure==Owner::Failure::Changed);
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::schema+0x60,Owner::scan_limit+1);assert(!fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::door+0x268,d2vr::rig::List{Fixture::door_list,1,(Owner::scan_limit+1)<<8});assert(fixture.capture());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::schema+0x64,2u);fixture.memory.put(Fixture::node_record+0x18,Fixture::node_record+0x100);
        fixture.memory.put(Fixture::node_record+0x108,Fixture::node);fixture.memory.put(Fixture::node_record+0x110,std::int32_t{0x200});
        fixture.memory.put(Fixture::node_record+0x118,std::uintptr_t{});assert(!fixture.capture());
    }
    {
        Fixture fixture;const auto snapshot=fixture.capture();assert(snapshot);fixture.memory.put(Fixture::game+0x60410,Fixture::world+0x1000);
        assert(!Owner::current(fixture.memory,*snapshot));
    }
#if defined(_MSC_VER)
    {
        Fixture fixture;fixture.memory.before=[](std::uintptr_t) {RaiseException(0xe0011234,0,0,nullptr);};assert(!fixture.capture());
    }
#endif
    std::cout<<"native_door_owner_check PASS\n";
}
