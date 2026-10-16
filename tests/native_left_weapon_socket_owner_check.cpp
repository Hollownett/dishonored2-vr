#define NOMINMAX
#include <windows.h>
#include "../src/native_left_weapon_socket_owner.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

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

    using Owner=NativeLeftWeaponSocketOwner;using Socket=NativeLeftWeaponSocket;
    struct Context {
        Memory* memory{};std::uint64_t tick{1000};unsigned fault{};Owner* owner{};bool clear_during_read{};
    } context{&memory};
    auto read=[](void* opaque,std::uintptr_t address,void* output,std::size_t size)->bool {
        auto& state=*static_cast<Context*>(opaque);
        if(state.clear_during_read){state.clear_during_read=false;state.owner->clear();}
        if(state.fault==1)return false;
        if(state.fault==2)throw std::runtime_error("read");
        if(state.fault==3)RaiseException(0xe0000073,0,0,nullptr);
        return (*state.memory)(address,output,size);
    };
    auto clock=[](void* opaque)->std::uint64_t {
        auto& state=*static_cast<Context*>(opaque);
        if(state.fault==4)throw std::runtime_error("clock");
        if(state.fault==5)RaiseException(0xe0000074,0,0,nullptr);
        return state.tick;
    };
    Owner owner;context.owner=&owner;owner.configure(read,&context,clock,&context);
    auto binding_snapshot=std::make_shared<const rig::NativeArmBindingSnapshot>(snapshot);
    Owner::Epochs epochs{1,2,3};
    Socket::Availability availability{7,1000,true,true,false,false,false};
    const auto original=memory;
    const auto acquire_lease=[&] {
        auto lease=Socket::capture(base_address,memory,*binding_snapshot,availability,context.tick);
        assert(lease);return std::make_shared<const Socket::Lease>(std::move(*lease));
    };
    const auto publish=[&]{return owner.publish(acquire_lease(),binding_snapshot,epochs,availability);};
    auto lease=acquire_lease();const auto token=owner.publish(lease,binding_snapshot,epochs,availability);
    assert(token && owner.current(token) && owner.current_token()==token && Owner::acquire(&owner,token));Owner::release(&owner,token);
    assert(!owner.current(token+1) && !Owner::acquire(nullptr,token));
    assert(owner.refresh(binding_snapshot,epochs,availability)==token);
    auto equivalent=std::make_shared<const rig::NativeArmBindingSnapshot>(snapshot);
    assert(owner.refresh(equivalent,epochs,availability)==token);
    assert(owner.current(token,binding_snapshot,epochs) && owner.current(token,equivalent,epochs));
    assert(!owner.current(token+1,binding_snapshot,epochs) && !owner.current(token,{},epochs));
    auto wrong_epochs=epochs;++wrong_epochs.mapping;
    assert(!owner.current(token,binding_snapshot,wrong_epochs));
    memory.put(inventory+0x68,item+0x1000);
    assert(owner.refresh(equivalent,epochs,availability)==token);
    memory.put(carry_states[1]+0x20,static_cast<unsigned char>(1));
    assert(owner.refresh(equivalent,epochs,availability)==token);
    assert(owner.publish(acquire_lease(),equivalent,epochs,availability)==token);
    memory.put(carry_states[1]+0x20,static_cast<unsigned char>(0));
    assert(owner.refresh(equivalent,epochs,availability)==token);
    assert(owner.publish(acquire_lease(),equivalent,epochs,availability)==token);
    context.clear_during_read=true;assert(!owner.current(token));assert(!owner.current(token));
    auto current=publish();assert(current>token);
    for(unsigned fault=1;fault<=5;++fault) {
        context.fault=fault;assert(!owner.current(current));context.fault=0;
        assert(!owner.current(current));const auto next=publish();assert(next>current);current=next;
    }
    for(unsigned occupied=0;occupied<4;++occupied) {
        auto denied=availability;
        if(occupied==0)denied.physical_held=true;if(occupied==1)denied.physical_reserved=true;
        if(occupied==2)denied.native_object=true;if(occupied==3)denied.focused=false;
        assert(!owner.refresh(binding_snapshot,epochs,denied));assert(!owner.current(current));
        const auto next=publish();assert(next>current);current=next;
    }
    context.tick=1101;assert(!owner.current(current));context.tick=1000;current=publish();
    auto changed_epochs=epochs;++changed_epochs.pose;
    assert(!owner.refresh(binding_snapshot,changed_epochs,availability));assert(!owner.current(current));current=publish();
    ++changed_epochs.reference;const auto next_epoch=owner.publish(acquire_lease(),binding_snapshot,changed_epochs,availability);
    assert(next_epoch>current);current=next_epoch;
    changed_epochs=epochs;++changed_epochs.mapping;
    assert(!owner.refresh(binding_snapshot,changed_epochs,availability));current=publish();
    auto altered=snapshot.bindings;altered[0].bind_grip.f[0]=3;
    auto different=std::make_shared<const rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{snapshot.arms,altered,snapshot.sources});
    assert(!owner.current(current,different,epochs));
    assert(!owner.refresh(different,epochs,availability));current=publish();
    const auto rebound=owner.publish(acquire_lease(),different,epochs,availability);assert(rebound>current);
    current=publish();
    for(unsigned side: {0u,2u}) {
        memory.put(carry_states[side]+0x20,static_cast<unsigned char>(1));
        assert(!owner.current(current));memory=original;current=publish();
    }
    memory.put(inventory+0x70,std::uintptr_t{});assert(!owner.current(current));memory=original;current=publish();
    memory.put(equipment+0x18,rig::Identity{500,10});assert(!owner.current(current));
    const auto recycled=publish();assert(recycled>current);memory=original;
    owner.clear();assert(!owner.current(recycled) && !owner.current_token());current=publish();assert(owner.current_token()==current && current>recycled);
    owner.configure(read,&context,clock,&context);assert(!owner.current(current));
    assert(publish()>current);
    assert(!owner.publish({},binding_snapshot,epochs,availability));
    assert(!owner.publish(lease,{},epochs,availability));
    assert(!owner.publish(lease,binding_snapshot,{},availability));
    assert(&Owner::process_owner()==&Owner::process_owner());
    std::cout<<"PASS socket owner current receipts, equivalent binding/opposite slot/right corpse continuity, epochs, faults, retirement and reentrant clear\n";
}

