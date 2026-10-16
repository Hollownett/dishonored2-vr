#define NOMINMAX
#include <windows.h>
#include "../src/native_left_weapon_socket.hpp"
#include "../src/native_left_weapon_socket_owner.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
Memory* placement_memory{};
bool placement_read(std::uintptr_t address,void* output,std::size_t size) {
    return placement_memory && (*placement_memory)(address,output,size);
}
}

int main() {
    using namespace d2vr;
    constexpr std::uintptr_t inventory=0x220000000,item=0x221000000,item_decl=0x222000000,
        entity=0x223000000,parent=0x224000000,binding=0x225000000,equipment=0x226000000,
        declaration=0x227000000,components=0x228000000,binding_decl=0x229000000,child_model=0x22a000000;
    auto memory=fixture();add_carry_fixture(memory);
    const auto arms=rig::player_arms(memory,base_address);assert(arms);
    rig::NativeArmBindingSnapshot snapshot{*arms,{},{}};
    memory.list(player_address+0x268,components_address,3,3);
    memory.put(components_address+16,inventory);
    memory.put(player_address+0xa8,1u);memory.put(entity+0xa8,0x10001u);memory.put(entity+0xac,1u);memory.put(parent+0xa8,0x20001u);
    memory.put(game_address+0x3130b8,entity);memory.put(game_address+0x3130c0,parent);
    memory.put(entity,base_address+0x1ce1e98);memory.put(parent,base_address+0x1c87d58);
    memory.put(entity+0xf0,child_model);memory.put(child_model,base_address+0x1b0d628);
    memory.put(parent+0xf0,models[0]);memory.put(animations[0]+0x10,parent);
    memory.put(body_address+0x370,0x20001u);memory.put(body_address+0x60,inventory);
    memory.put(inventory,base_address+0x1b9a878);memory.put(inventory+0x10,player_address);memory.put(inventory+0x18,rig::Identity{300,10});
    memory.put(inventory+0x68,std::array<std::uintptr_t,2>{0,item});memory.list(inventory+0x38,item,1,1);
    memory.put(item,std::array<unsigned char,0x38>{});memory.put(item,item_decl);memory.put(item+8,0x10001u);
    memory.put(item+0x10,equipment);memory.put(item+0x18,declaration);memory.put(item_decl,base_address+0x1b9ea48);
    memory.put(entity+0x358,binding);memory.put(binding,base_address+0x1c573e8);memory.put(binding+0x20,base_address+0x1c574c0);
    memory.put(binding+8,binding_decl);memory.put(binding_decl,base_address+0x123450);memory.put(binding+0x10,entity);
    memory.put(binding+0x18,rig::Identity{301,10});memory.put(binding+0x3c8,parent);memory.put(binding+0x400,animations[0]);
    memory.put(binding+0x3d0,std::int16_t{59});memory.put(binding+0x84,100u);
    memory.list(entity+0x268,components,2,2);memory.put(components,std::array<std::uintptr_t,2>{binding,equipment});
    memory.put(equipment,base_address+0x1b53f58);memory.put(equipment+8,declaration);memory.put(equipment+0x10,entity);
    memory.put(equipment+0x18,rig::Identity{302,10});memory.put(declaration,base_address+0x1b53e18);memory.put(declaration+0x68,1);
    const auto original=memory;
    using Socket=NativeLeftWeaponSocket;
    Socket::Availability availability{7,1000,true,true,false,false,false};
    const auto capture=[&]{return Socket::capture(base_address,memory,snapshot,availability,1000);};
    const auto lease=capture();assert(lease && Socket::current(memory,*lease,availability,1000));
    {
        constexpr std::uintptr_t right_item=item+0x1000;
        memory.put(inventory+0x68,right_item);memory.put(right_item,item_decl);
        memory.put(right_item+0x10,equipment+0x1000);memory.put(right_item+0x18,declaration+0x1000);
        const auto selected=capture();assert(selected && selected->equipment.attachment.slots[1]==0);
        assert(selected->equipment.attachment.slot_tables[1]==0 && selected->equipment.attachment.slot_owner_fields[1]==0 &&
            selected->equipment.attachment.slot_identity_fields[1]==0);
        std::size_t reads{},bytes{},saved_bytes{};
        auto counted=[&](std::uintptr_t address,void* output,std::size_t size) {
            ++reads;bytes+=size;return memory(address,output,size);
        };
        assert(Socket::current(counted,*selected,availability,1000));
        for(const auto& guard:selected->equipment.guards) {
            saved_bytes+=guard.bytes.size();assert(guard.address!=inventory+0x68);
            assert(guard.address<right_item || guard.address>=right_item+0x38);
        }
        std::cout<<"CURRENT guards="<<selected->equipment.guards.size()<<" saved_bytes="<<saved_bytes<<" reads="<<reads<<" bytes="<<bytes<<"\n";
        memory.put(inventory+0x68,std::uintptr_t{});assert(Socket::current(memory,*selected,availability,1000) && capture());
        memory.put(inventory+0x68,right_item);memory.put(right_item+0x18,declaration+0x2000);
        assert(Socket::current(memory,*selected,availability,1000) && capture());
        auto blocked_opposite=[&](std::uintptr_t address,void* output,std::size_t size) {
            if(address==inventory+0x68 || (address>=right_item && address<right_item+0x38))return false;
            return memory(address,output,size);
        };
        assert(Socket::capture(base_address,blocked_opposite,snapshot,availability,1000));
        assert(!native_attachment_candidate(blocked_opposite,base_address,snapshot,binding,3));
        memory.put(inventory+0x70,std::uintptr_t{});assert(!Socket::current(memory,*selected,availability,1000));
        memory=original;
        unsigned repeated{};
        auto changing_duplicate=[&](std::uintptr_t address,void* output,std::size_t size) {
            if(address==binding+0x18 && ++repeated==2)memory.put(address,rig::Identity{400,10});
            return memory(address,output,size);
        };
        assert(!Socket::capture(base_address,changing_duplicate,snapshot,availability,1000));
        memory=original;
    }
    memory.put(binding+0x84,101u);assert(Socket::current(memory,*lease,availability,1000));
    memory.put(carry_states[1]+0x20,static_cast<unsigned char>(1));
    assert(Socket::current(memory,*lease,availability,1000) && capture());
    memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));
    assert(!Socket::current(memory,*lease,availability,1000) && !capture());
    memory=original;memory.put(equipment,base_address+0x1b9cbf8);memory.put(declaration,base_address+0x1b9cab8);
    assert(capture());assert(!Socket::current(memory,*lease,availability,1000));
    {
        memory=original;placement_memory=&memory;
        NativeLeftWeaponSocketOwner socket_owner;
        socket_owner.configure([](void*,std::uintptr_t address,void* output,std::size_t size){return placement_read(address,output,size);},nullptr,
            [](void*)->std::uint64_t{return 1000;},nullptr);
        const auto owned_binding=std::make_shared<const rig::NativeArmBindingSnapshot>(snapshot);
        const NativeLeftWeaponSocketOwner::Epochs owned_epochs{7,8,9};
        const auto pre_hold=std::make_shared<const Socket::Lease>(*capture());
        const auto owned_token=socket_owner.publish(pre_hold,owned_binding,owned_epochs,availability);assert(owned_token);
        constexpr std::uintptr_t held_entity=0x240000000,held_rf=0x241000000,held_components=0x242000000;
        constexpr std::uint32_t held_spawn=0x30001;
        memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[0]+0x68,held_spawn);
        memory.put(game_address+0x3130b0+(held_spawn>>16)*8ull,held_entity);memory.put(held_entity+0xa8,held_spawn);
        memory.put(held_entity+0x358,held_rf);memory.list(held_entity+0x268,held_components,1,1);memory.put(held_components,held_rf);
        memory.put(held_rf,base_address+0x1c573e8);memory.put(held_rf+0x20,base_address+0x1c574c0);
        memory.put(held_rf+0x18,rig::Identity{800,10});memory.put(held_rf+0x10,held_entity);
        auto& placement=NativeHeldPlacement::process_owner();assert(placement.install(base_address,placement_read));placement.context(7,true);
        const auto carry=rig::native_carry_snapshot(memory,base_address,player_address,body_address);assert(carry);
        const auto proof=NativeHeldPlacement::qualify(base_address,placement_read,*carry,1,7,1000,1000);assert(proof);
        assert(!capture() && !Socket::current(memory,*lease,availability,1000));
        availability.placement_serial=placement.publish(*proof,1000);assert(availability.placement_serial);
        assert(!capture() && !Socket::current(memory,*lease,availability,1000));
        assert(placement.completed(availability.placement_serial,1,held_entity,held_rf,7,1000));
        assert(capture() && Socket::current(memory,*lease,availability,1000));
        assert(socket_owner.refresh(owned_binding,owned_epochs,availability)==owned_token);
        assert(socket_owner.publish(std::make_shared<const Socket::Lease>(*capture()),owned_binding,owned_epochs,availability)==owned_token);
        auto wrong_serial=availability;++wrong_serial.placement_serial;
        assert(!Socket::current(memory,*lease,wrong_serial,1000));
        const auto held_memory=memory;
        for(unsigned mutation=0;mutation<5;++mutation) {
            if(mutation==0)memory.put(held_rf+0x18,rig::Identity{801,10});
            if(mutation==1)memory.put(held_entity+0xa8,held_spawn+1);
            if(mutation==2)memory.put(carry_states[0]+0x68,held_spawn+1);
            if(mutation==3)memory.put(carry_states[2]+0x20,static_cast<unsigned char>(1));
            if(mutation==4)memory.put(held_components,std::uintptr_t{});
            assert(!capture() && !Socket::current(memory,*lease,availability,1000));memory=held_memory;
        }
        auto fresh=availability;fresh.tick=1151;
        assert(!Socket::current(memory,*lease,fresh,1151));
        const auto left=NativeHeldPlacement::qualify(base_address,placement_read,*carry,0,7,1000,1000);assert(left);
        availability.placement_serial=placement.publish(*left,1000);
        assert(placement.completed(availability.placement_serial,0,held_entity,held_rf,7,1000));
        assert(!capture() && !Socket::current(memory,*lease,availability,1000));
        availability.placement_serial=placement.publish(*proof,1000);
        assert(placement.completed(availability.placement_serial,1,held_entity,held_rf,7,1000));
        bool dropped{};
        auto drop_during_equipment=[&](std::uintptr_t address,void* output,std::size_t size) {
            if(address==equipment+0x18 && !dropped){dropped=true;placement.clear();}
            return memory(address,output,size);
        };
        assert(!Socket::current(drop_during_equipment,*lease,availability,1000) && dropped);
        placement.clear();assert(!capture() && !Socket::current(memory,*lease,availability,1000));
        memory=held_memory;memory.put(carry_states[0]+0x20,static_cast<unsigned char>(0));
        memory.put(carry_states[2]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[2]+0x68,held_spawn);
        const auto large_carry=rig::native_carry_snapshot(memory,base_address,player_address,body_address);assert(large_carry);
        for(const unsigned large_side:{0u,1u}) {
            const auto large_proof=NativeHeldPlacement::qualify(base_address,placement_read,*large_carry,large_side,7,1000,1000);assert(large_proof);
            availability.placement_serial=placement.publish(*large_proof,1000);assert(availability.placement_serial);
            assert(placement.completed(availability.placement_serial,large_side,held_entity,held_rf,7,1000));
            assert(placement.current_driven_side(*large_carry,7,1000)==large_side);
            assert(!placement.current_driven_right(availability.placement_serial,7,*large_carry,1000));
            assert(!capture() && !Socket::current(memory,*lease,availability,1000));
        }
        placement.disable();availability.placement_serial=0;memory=original;placement_memory=nullptr;
    }
    for(unsigned fault=0;fault<6;++fault) {
        memory=original;auto rejected=availability;
        if(fault==0)rejected.valid=false;if(fault==1)rejected.focused=false;if(fault==2)rejected.physical_held=true;
        if(fault==3)rejected.physical_reserved=true;if(fault==4)rejected.native_object=true;if(fault==5)rejected.epoch=0;
        assert(!Socket::capture(base_address,memory,snapshot,rejected,1000));assert(!Socket::current(memory,*lease,rejected,1000));
    }
    memory=original;assert(!Socket::current(memory,*lease,availability,1101));
    assert(!Socket::current(memory,*lease,availability,999));auto epoch=availability;epoch.epoch=8;
    assert(!Socket::current(memory,*lease,epoch,1000));
    const auto reject=[&](auto address,auto value) {
        memory=original;memory.put(address,value);assert(!capture());assert(!Socket::current(memory,*lease,availability,1000));
    };
    reject(body_address+0x370,0x20002u);reject(item+8,0x10002u);reject(binding+0x400,animations[1]);
    reject(binding+0x3d0,std::int16_t{58});reject(equipment,base_address+0x1b9d508);
    reject(declaration,base_address+0x1b9d3c8);reject(equipment+0x10,parent);reject(declaration+0x68,0);
    reject(inventory+0x70,std::uintptr_t{});reject(components,std::array<std::uintptr_t,2>{equipment,equipment});
    reject(child_model,base_address+0x1b0dd88);
    reject(models[0]+0x1fe8,std::uintptr_t{});reject(descriptors[0]+0x50,skeletons[1]);
    memory=original;memory.recycle_at=equipment+0x18;assert(!capture());
    memory=original;memory.put(equipment+0x18,rig::Identity{303,10});assert(capture());assert(!Socket::current(memory,*lease,availability,1000));
    auto failed=[](std::uintptr_t,void*,std::size_t){return false;};assert(!Socket::capture(base_address,failed,snapshot,availability,1000));
    auto cpp_fault=[](std::uintptr_t,void*,std::size_t)->bool{throw std::runtime_error("read");};
    assert(!Socket::capture(base_address,cpp_fault,snapshot,availability,1000));
    assert(!Socket::current(cpp_fault,*lease,availability,1000));
    auto seh_fault=[](std::uintptr_t,void*,std::size_t)->bool{RaiseException(0xe0000072,0,0,nullptr);return false;};
    assert(!Socket::capture(base_address,seh_fault,snapshot,availability,1000));
    assert(!Socket::current(seh_fault,*lease,availability,1000));
    std::cout<<"PASS typed current left Gun/Crossbow socket, completed right49 continuity, unclaimed/left49 and50 rejection, guarded lifetime and availability\n";
}
