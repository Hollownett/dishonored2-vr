#define NOMINMAX
#include "../src/native_weapon_hands.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Visibility=d2vr::NativeWeaponVisibility;
constexpr std::uintptr_t fixture_inventory=0x230000000,fixture_controller=0x231000000,fixture_equip=0x232000000,
    fixture_entries=0x233000000,fixture_arm=0x234000000,gun=0x235000000,gun_model=0x236000000,
    binding=0x237000000,children=0x238000000,hip=0x239000000;
constexpr std::uint32_t fixture_arm_id=0x10001,gun_id=0x20002,hip_id=0x30003;
void add_entity(Memory& memory,std::uintptr_t address,std::uint32_t spawn,std::uintptr_t table,std::uintptr_t model=0) {
    memory.put(game_address+0x3130b0+(spawn>>16)*8ull,address);memory.put(address,base_address+table);
    memory.put(address+0xa8,spawn);memory.put(address+0xf0,model);memory.put(address+0x358,std::uintptr_t{});
    memory.put(address+0x2e8,std::uintptr_t{});memory.put(address+0xd0,std::uint8_t{});
    memory.put(address+0x300,std::array<float,3>{10,20,30});
    memory.put(address+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    memory.list(address+0xb0,0,0,0);
}
Memory visibility_fixture() {
    auto memory=fixture();add_carry_fixture(memory);
    memory.put(player_address+0xa8,std::uint32_t{42});memory.put(body_address+0x60,fixture_inventory);
    memory.put(fixture_inventory,base_address+0x1b9a878);memory.put(fixture_inventory+0x10,player_address);
    memory.put(fixture_inventory+0x18,r::Identity{50,10});memory.list(fixture_inventory+0x38,fixture_entries,2,32);
    memory.put(fixture_inventory+0x70,fixture_entries);memory.put(carry_machine+0x2f8,fixture_controller);
    memory.put(fixture_controller,base_address+0x1c72918);memory.put(fixture_controller+0x10,player_address);
    memory.put(fixture_controller+0x58,carry_machine);
    memory.put(fixture_controller+0x190,fixture_entries);memory.put(fixture_controller+0x1a0,std::uintptr_t{});
    memory.put(carry_pool+0x10+64*8,fixture_equip);memory.put(fixture_equip,base_address+0x1c79cf8);
    memory.put(fixture_equip+8,64);memory.put(fixture_equip+0x10,carry_machine);memory.put(fixture_equip+0x20,std::uint8_t{1});
    memory.put(fixture_equip+0x108,std::uintptr_t{});memory.put(fixture_equip+0x118,fixture_entries);
    memory.put(fixture_entries+8,gun_id);memory.put(fixture_entries+0x38+8,hip_id);
    add_entity(memory,fixture_arm,fixture_arm_id,0x1c5e428);add_entity(memory,gun,gun_id,0x1ce1e98,gun_model);
    add_entity(memory,hip,hip_id,0x1c5e428);memory.put(gun_model,base_address+0x1b0d628);
    memory.put(gun_model+0x1860,std::array<float,15>{1,0,0,0,0,1,0,0,0,0,1,0,10,20,30});
    memory.put(body_address+0x370,fixture_arm_id);memory.list(fixture_arm+0xb0,children,1,32);memory.put(children,gun);
    memory.put(gun+0x358,binding);memory.put(binding,base_address+0x1c573e8);memory.put(binding+0x20,base_address+0x1c574c0);
    memory.list(gun+0x268,children+0x100,1,1);memory.put(children+0x100,binding);
    memory.put(binding+0x18,r::Identity{51,10});memory.put(binding+0x10,gun);memory.put(binding+0x3c8,fixture_arm);
    memory.put(binding+0x400,animations[0]);memory.put(binding+0x3d0,std::int16_t{59});
    return memory;
}
}
int main() {
    auto memory=visibility_fixture();const auto original=memory.bytes;
    auto value=Visibility::capture(base_address,memory,{hip_id,0},1000);
    assert(value.owner_valid && value.items_complete && value.hierarchy_complete && value.hierarchy_count==2);
    assert(value.items[0].member && value.items[0].entity.weapon && value.items[0].entity.rf_valid);
    assert(value.items[0].entity.parent==fixture_arm && value.items[0].entity.rf_owner==gun && value.items[0].entity.hidden==0);
    assert(value.hip[0].valid && value.hip[0].spawn==hip_id && memory.bytes==original);
    const auto& observed=value.items[0].entity;
    assert(observed.origin_valid && observed.axis_valid && observed.model_frame_valid);
    assert((observed.origin==std::array<float,3>{10,20,30}) && observed.axis[4]==1 && observed.model_frame[14]==30);
    assert(value.hip[0].origin_valid && value.hip[0].axis_valid && !value.hip[0].model_frame_valid);
    for(unsigned variant=0;variant<6;++variant) {
        memory.bytes=original;
        if(variant==0)memory.bytes.erase(gun+0x300);
        if(variant==1)memory.put(gun+0x300,std::numeric_limits<float>::quiet_NaN());
        if(variant==2)memory.put(gun+0x30c,std::numeric_limits<float>::infinity());
        if(variant==3)memory.put(gun_model+0x1860,1.1e7f);
        if(variant==4)memory.bytes.erase(gun_model+0x1860);
        if(variant==5)memory.put(gun+0x30c,-1.1e7f);
        const auto captured=Visibility::entity(base_address,memory,game_address,gun_id);
        assert(captured.valid && captured.rf_valid);
        assert(captured.origin_valid==(variant!=0 && variant!=1));
        assert(captured.axis_valid==(variant!=2 && variant!=5));
        assert(captured.model_frame_valid==(variant!=3 && variant!=4));
        if(!captured.origin_valid)assert((captured.origin==std::array<float,3>{}));
        if(!captured.axis_valid)assert((captured.axis==std::array<float,9>{}));
        if(!captured.model_frame_valid)assert((captured.model_frame==std::array<float,15>{}));
    }
    for(unsigned variant=0;variant<7;++variant) {
        memory.bytes=original;unsigned changed_reads{};
        const auto watched=variant==0?gun+0x300:variant==1?gun+0x30c:gun_model+0x1860;
        auto changing=[&](std::uintptr_t address,void* output,std::size_t size) {
            const bool result=memory(address,output,size);
            if(address==watched && ++changed_reads==1) {
                if(variant<3)memory.put(watched,2.f);
                if(variant==3)memory.put(gun+0xa8,gun_id+1);
                if(variant==4)memory.put(gun+0xf0,gun_model+0x1000);
                if(variant==5)memory.put(gun_model,base_address+0x1b0d629);
            }
            return variant==6 && address==watched && changed_reads==2?false:result;
        };
        const auto captured=Visibility::entity(base_address,changing,game_address,gun_id);
        if(variant>=3 && variant<=5)assert(!captured.valid);
        else {
            assert(captured.valid && captured.rf_valid && changed_reads==2);
            assert(captured.origin_valid==(variant!=0) && captured.axis_valid==(variant!=1));
            assert(captured.model_frame_valid==(variant!=2 && variant!=6));
        }
    }
    memory.bytes=original;
    memory.put(fixture_inventory+0x70,std::uintptr_t{});
    value=Visibility::capture(base_address,memory,{hip_id,0},1001);
    assert(value.owner_valid && value.items[0].valid && !value.items[0].address && value.items[1].entity.weapon);
    memory.bytes=original;memory.put(gun+0xd0,std::uint8_t{3});
    value=Visibility::capture(base_address,memory,{},1002);assert(value.items[0].entity.hidden==3);
    memory.bytes=original;memory.put(gun+0xa8,gun_id+1);
    value=Visibility::capture(base_address,memory,{},1003);assert(value.owner_valid && !value.items_complete && value.hierarchy[1].spawn==gun_id+1);
    memory.bytes=original;memory.put(binding+0x10,fixture_arm);
    value=Visibility::capture(base_address,memory,{},1004);assert(value.items[0].entity.valid && !value.items[0].entity.rf_valid);
    memory.bytes=original;memory.put(children,fixture_arm);
    value=Visibility::capture(base_address,memory,{},1005);assert(!value.hierarchy_complete);
    memory.bytes=original;memory.list(fixture_arm+0xb0,children,33,64);
    value=Visibility::capture(base_address,memory,{},1006);assert(!value.hierarchy_complete);
    memory.bytes=original;memory.put(fixture_controller+0x190,fixture_entries+8);
    value=Visibility::capture(base_address,memory,{},1007);assert(!value.items_complete && !value.items[1].member);
    memory.bytes=original;memory.recycle_at=carry_machine+0x18;
    value=Visibility::capture(base_address,memory,{},1008);assert(!value.owner_valid);
    memory.bytes=original;memory.bytes.erase(gun+0xd0);
    value=Visibility::capture(base_address,memory,{},1009);assert(!value.items_complete);
    memory.bytes=original;memory.put(carry_states[0],std::uintptr_t{});
    value=Visibility::capture(base_address,memory,{},1010);assert(value.owner_valid && value.items_complete);
    auto failing=[](std::uintptr_t,void*,std::size_t)->bool {throw std::runtime_error("read");};
    bool propagated=false;try {(void)Visibility::capture(base_address,failing,{},1011);}catch(const std::runtime_error&) {propagated=true;}
    assert(propagated);
    d2vr::NativeWeaponHands::Request request{};request.op=d2vr::NativeWeaponHands::Op::gun_holster;
    for(unsigned index=0;index<10;++index)d2vr::NativeWeaponHands::request(request);
    const auto counters=d2vr::NativeWeaponHands::visibility();
    assert(counters.holster_enqueued==8 && counters.holster_dropped==2 && !counters.holster_serviced && !counters.holster_request_returned);
    std::cout<<"Snapshot bytes="<<sizeof(Visibility::Snapshot)<<'\n';
    memory.bytes=original;unsigned calls=0;std::size_t bytes=0;
    auto counted=[&](std::uintptr_t address,void* output,std::size_t size) {++calls;bytes+=size;return memory(address,output,size);};
    assert(Visibility::capture(base_address,counted,{hip_id,0},1012).owner_valid);
    std::cout<<"Synthetic read calls="<<calls<<" bytes="<<bytes<<'\n';
    std::cout<<"PASS: read-only current/remembered/hip full-spawn visibility, RF owner, bounded hierarchy, recycle/fault and carry-independent diagnostics\n";
}
