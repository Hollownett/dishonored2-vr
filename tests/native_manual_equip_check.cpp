#define NOMINMAX
#include "../src/native_manual_equip.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using Policy=d2vr::NativeManualEquip;
constexpr std::uintptr_t inventory=0x230000000,controller=0x231000000,equip=0x232000000,
    entries=0x233000000,weapon=0x234000000,declaration=0x235000000,
    equipment=0x236000000,equipment_decl=0x237000000,equipment_entries=0x238000000;
constexpr std::uint32_t weapon_spawn=0x20002;
Memory memory;
Policy* active_policy{};Policy::Params received{};unsigned native_calls{},locks{};
bool fail_read{},throw_read{},fault_read{},accept_classification=true;
std::function<void()> action;
std::function<void(std::uintptr_t,std::size_t)> read_action;
template<class Value>void put(std::uintptr_t address,const Value& value){memory.put(address,value);}
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    if(fail_read)return false;
    if(throw_read)throw std::runtime_error("fixture read failure");
    if(fault_read){RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return false;}
    const bool result=memory(address,output,size);if(read_action)read_action(address,size);return result;
}
bool classify(const Policy::Scope& scope){return accept_classification && scope.declaration==declaration;}
void __fastcall native(std::uintptr_t state,const Policy::Params* params) {
    assert(state);++native_calls;received=*params;
    if(received.get<std::uint32_t>(24)&2){assert(locks);--locks;}
    if(action)action();
}
Policy::Params params(std::uint32_t code,std::uintptr_t item=0,std::uint32_t request=7) {
    Policy::Params result{};result.bytes.fill(0x9d);result.set(0,request);result.set(4,std::uint32_t{0x5c});
    result.set(8,item);result.set(16,std::uintptr_t{});result.set(24,code);result.set(28,std::uint32_t{0x5b});
    result.set(32,std::uint64_t{});result.set(40,static_cast<unsigned char>(1));return result;
}
void invoke(Policy& policy,const Policy::Params& input,bool occupied=false,std::uintptr_t state=equip) {
    put(reinterpret_cast<std::uintptr_t>(&input),input);const auto before=native_calls,previous=policy.in_flight();
    policy.invoke(state,&input,occupied);assert(native_calls==before+1 && policy.in_flight()==previous);
}
void setup() {
    memory=::fixture();add_carry_fixture(memory);put(player_address+0xa8,std::uint32_t{42});
    put(carry_machine+0x328,inventory);put(carry_machine+0x2f8,controller);
    put(inventory,base_address+0x1b9a878);put(inventory+0x10,player_address);put(inventory+0x18,r::Identity{50,10});
    memory.list(inventory+0x38,entries,2,32);put(entries,declaration);put(entries+8,weapon_spawn);put(inventory+0x70,entries);
    put(declaration,base_address+0x1b9ea48);put(entries+0x10,equipment);put(entries+0x18,equipment_decl);
    put(equipment,base_address+0x1b53f58);put(equipment+8,equipment_decl);put(equipment+0x10,weapon);put(equipment+0x18,r::Identity{51,10});
    put(equipment_decl,base_address+0x1b53e18);put(equipment_decl+0x68,1);
    memory.list(weapon+0x268,equipment_entries,1,1);put(equipment_entries,equipment);
    put(controller,base_address+0x1c72918);put(controller+0x10,player_address);put(controller+0x58,carry_machine);
    put(controller+0x190,entries);put(controller+0x198,std::uintptr_t{});put(controller+0x1a0,entries);put(controller+0x1a8,std::uintptr_t{});
    put(carry_pool+0x10+64*8,equip);put(equip,base_address+0x1c79cf8);put(equip+8,64);put(equip+0x10,carry_machine);
    put(equip+0x20,static_cast<unsigned char>(0));put(equip+0x108,std::uintptr_t{});put(equip+0x118,std::uintptr_t{});put(equip+0x150,static_cast<unsigned char>(0));
    put(weapon,base_address+0x1ce1e98);put(weapon+0xa8,weapon_spawn);put(game_address+0x3130b0+(weapon_spawn>>16)*8ull,weapon);
    put(base_address+Policy::processor_rva,Policy::prologue);put(base_address+Policy::wheel_rva,Policy::wheel_prologue);
    for(const auto& slot:Policy::slots)put(base_address+slot.first,base_address+slot.second);
    for(const auto& pin:Policy::call_pins)put(base_address+pin.rva,pin.bytes);
}
void retained_lifecycle_cases() {
    setup();Policy policy;assert(policy.start_observer(base_address,read_memory,native,classify));assert(policy.enable_at_idle(equip));
    auto draw=params(4),holster=params(0);invoke(policy,holster);put(inventory+0x70,std::uintptr_t{});
    put(equipment+0x18,r::Identity{22897,0x20000002});
    const auto stopped=Policy::qualify(base_address,read_memory,equip,entries);assert(stopped && !r::started(memory,equipment));
    assert(policy.reserve_manual(stopped->owner,1,Policy::Source::hip) && policy.grant(1,*stopped));
    action=[]{put(equipment+0x18,r::Identity{22897,0x20000006});put(inventory+0x70,entries);};
    invoke(policy,draw);action={};assert(received==draw && policy.lease().generation==1);
    const auto starting=Policy::qualify(base_address,read_memory,equip,entries);assert(starting && *starting==*stopped);
    invoke(policy,draw);assert(received==draw && policy.observation().authorized);
    put(equipment+0x18,r::Identity{22897,0x2000000a});assert(r::started(memory,equipment));
    const auto started=Policy::qualify(base_address,read_memory,equip,entries);assert(started && *started==*stopped);
    invoke(policy,draw);assert(received==draw && policy.observation().authorized);
    for(unsigned init=0;init<4;++init)for(unsigned run=0;run<4;++run){
        put(equipment+0x18,r::Identity{22897,0x20000000u|init|(run<<2)});
        assert(Policy::qualify(base_address,read_memory,equip,entries).has_value()==(init==2 && run!=3));
    }
    put(equipment+0x18,r::Identity{-1,0x20000002});assert(!Policy::qualify(base_address,read_memory,equip,entries));
    put(equipment+0x18,r::Identity{22898,0x20000002});invoke(policy,draw);assert(!policy.observation().authorized && policy.observation().changed);
    assert(policy.reserve_manual(stopped->owner,2,Policy::Source::hip));assert(!policy.grant(2,*stopped));
    put(equipment+0x18,r::Identity{22897,0x20000012});const auto drift=Policy::qualify(base_address,read_memory,equip,entries);assert(drift && !(*drift==*stopped));
    assert(!policy.grant(2,*stopped));put(equipment+0x18,r::Identity{22897,0x20000002});
    bool changed{};read_action=[&](std::uintptr_t address,std::size_t size){
        if(!changed && address==equipment+0x18 && size==sizeof(r::Identity)){changed=true;put(equipment+0x18,r::Identity{22897,0x20000006});}
    };
    assert(!Policy::qualify(base_address,read_memory,equip,entries));read_action={};assert(changed && Policy::qualify(base_address,read_memory,equip,entries));
    assert(policy.reserve_manual(stopped->owner,3,Policy::Source::wheel));const auto wheel_scope=Policy::qualify(base_address,read_memory,equip,entries);assert(wheel_scope && policy.grant(3,*wheel_scope));
    invoke(policy,draw);assert(received==draw && policy.observation().authorized);assert(policy.revoke(4));
    for(const auto flags:std::array<std::uint32_t,3>{0x20000002,0x20000006,0x2000000a}){
        put(equipment+0x18,r::Identity{22897,flags});invoke(policy,draw);assert(received.get<std::uint32_t>(24)==0 && !policy.lease().generation);
        put(inventory+0x70,entries+0x38);invoke(policy,draw);assert(received==draw && policy.observation().current_slot_gap);put(inventory+0x70,entries);
    }
    put(inventory+0x18,r::Identity{50,2});assert(!Policy::qualify(base_address,read_memory,equip,entries));assert(policy.stop());
}
bool invoke_seh(Policy* policy,const Policy::Params* input) {
    __try{policy->invoke(equip,input,false);}
    __except(GetExceptionCode()==0xe0525520?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
}
int main() {
    retained_lifecycle_cases();
    setup();Policy policy;active_policy=&policy;assert(Policy::verify(base_address,read_memory));
    assert(policy.start_observer(base_address,read_memory,native,classify) && !policy.writer_enabled());
    const auto scope=Policy::qualify(base_address,read_memory,equip,entries);assert(scope);
    assert(!Policy::qualify(base_address,read_memory,equip,entries+8));
    const auto baseline=memory.bytes;
    for(const auto& mutate:std::array<std::function<void()>,7>{
        []{put(equip+8,53);},[]{put(equip+0x10,carry_machine+8);},[]{put(controller+0x58,carry_machine+8);},
        []{put(inventory+0x10,player_address+8);},[]{put(weapon+0xa8,weapon_spawn+1);},
        []{put(carry_pool+0x10+64*8,equip+8);},[]{put(components_address+8,carry_machine+8);}}){
        mutate();assert(!Policy::qualify(base_address,read_memory,equip,entries));memory.bytes=baseline;
    }
    assert(scope->kind==Policy::Kind::gun);
    put(equipment,base_address+0x1b9cbf8);assert(!Policy::qualify(base_address,read_memory,equip,entries));
    put(equipment_decl,base_address+0x1b9cab8);
    const auto crossbow=Policy::qualify(base_address,read_memory,equip,entries);assert(crossbow && crossbow->kind==Policy::Kind::crossbow);memory.bytes=baseline;
    for(const auto& mutate:std::array<std::function<void()>,6>{
        []{put(equipment,base_address+0x1b9d508);put(equipment_decl,base_address+0x1b9d3c8);},
        []{put(equipment+0x10,weapon+8);},[]{put(equipment+8,equipment_decl+8);},[]{put(equipment_decl+0x68,0);},
        []{put(equipment_entries,equipment+8);},[]{put(declaration,base_address+0x1b9ea50);}}){
        mutate();assert(!Policy::qualify(base_address,read_memory,equip,entries));memory.bytes=baseline;
    }
    auto restore=params(0x16);locks=1;invoke(policy,restore);
    assert(!locks && received==restore && policy.observation().qualified && policy.observation().would_veto && !policy.observation().changed);
    put(equip+0x108,std::uintptr_t{1});assert(!policy.enable_at_idle(equip));put(equip+0x108,std::uintptr_t{});
    assert(policy.enable_at_idle(equip));locks=1;invoke(policy,restore);
    auto expected=restore;expected.set(24,std::uint32_t{0x12});assert(received==expected && !locks && policy.observation().changed);
    auto prepared=params(4);const auto prepare_native_calls=native_calls;
    assert(policy.prepare(equip,prepared,false) && native_calls==prepare_native_calls && prepared.get<std::uint32_t>(24)==0);
    put(inventory+0x70,std::uintptr_t{});locks=1;invoke(policy,restore);
    assert(received==expected && !locks && policy.observation().changed);put(inventory+0x70,entries);
    assert(!policy.grant(1,*scope));assert(policy.reserve_manual(scope->owner,1,Policy::Source::hip));assert(policy.grant(1,*scope));
    auto hip=params(4);invoke(policy,hip);assert(received==hip && policy.observation().authorized);
    auto other_id=params(6,0,999);locks=1;invoke(policy,other_id);assert(received==other_id && !locks && policy.observation().authorized);
    assert(policy.revoke(2));assert(!policy.grant(1,*scope));assert(!policy.reserve_manual(scope->owner,1,Policy::Source::hip));
    invoke(policy,hip);expected=hip;expected.set(24,std::uint32_t{});assert(received==expected && !policy.lease().generation);
    assert(!Policy::actual_wheel_origin(base_address,base_address+0x4dff48));assert(Policy::actual_wheel_origin(base_address,base_address+0x4f57e6));
    assert(policy.reserve_manual(scope->owner,3,Policy::Source::wheel));assert(policy.grant(3,*scope));
    auto temporary=params(0x81);invoke(policy,temporary);assert(received==temporary && policy.lease().generation==3);
    locks=1;invoke(policy,restore);assert(received==restore && policy.observation().authorized && !locks);
    auto wheel=params(0x20,entries);invoke(policy,wheel);assert(received==wheel && !policy.observation().explicit_item_gap && policy.lease().generation==3);
    auto holster=params(0);invoke(policy,holster);assert(received==holster && policy.observation().holster_revoke && !policy.lease().generation);
    assert(policy.reserve_manual(scope->owner,4,Policy::Source::hip));put(inventory+0x70,std::uintptr_t{});
    invoke(policy,holster);assert(policy.observation().holster_revoke && !policy.grant(4,*scope));put(inventory+0x70,entries);
    invoke(policy,hip);assert(!policy.observation().authorized && policy.observation().changed);
    assert(policy.reserve_manual(scope->owner,5,Policy::Source::wheel));assert(policy.grant(5,*scope));
    invoke(policy,hip,true);assert(!policy.observation().authorized && !policy.lease().generation && policy.observation().changed);
    auto unknown=params(0x20,entries+0x38);invoke(policy,unknown);assert(received==unknown && policy.observation().explicit_item_gap);
    put(inventory+0x70,entries+0x38);invoke(policy,hip);
    assert(received==hip && policy.observation().current_slot_gap && !policy.observation().changed);put(inventory+0x70,entries);
    accept_classification=false;invoke(policy,hip);assert(received==hip && policy.observation().classification_gap);accept_classification=true;
    for(auto* flag:std::array<bool*,3>{&fail_read,&throw_read,&fault_read}){
        *flag=true;invoke(policy,hip);*flag=false;assert(received==hip && !policy.observation().qualified);
    }
    action=[&]{assert(!active_policy->stop());};invoke(policy,hip);action={};assert(!policy.writer_enabled() && policy.stop());
    invoke(policy,hip);assert(received==hip);assert(!policy.start_observer(base_address+8,read_memory,native));
    assert(policy.start_observer(base_address,read_memory,native,classify) && !policy.writer_enabled() && !policy.lease().generation);
    action=[]{throw std::runtime_error("native failure");};bool propagated{};
    try{invoke(policy,hip);}catch(const std::runtime_error&){propagated=true;}
    assert(propagated && policy.in_flight()==0);
    action=[]{RaiseException(0xe0525520,0,0,nullptr);};put(reinterpret_cast<std::uintptr_t>(&hip),hip);
    assert(invoke_seh(&policy,&hip) && policy.in_flight()==0);action={};
    bool nested{};action=[&]{if(!nested){nested=true;invoke(policy,hip);}};
    const auto before=native_calls;put(reinterpret_cast<std::uintptr_t>(&hip),hip);policy.invoke(equip,&hip,false);action={};
    assert(native_calls==before+2 && policy.in_flight()==0 && policy.stop());
    for(const auto& pin:Policy::call_pins){memory.bytes[base_address+pin.rva]^=1;assert(!policy.start_observer(base_address,read_memory,native));put(base_address+pin.rva,pin.bytes);}
    memory.bytes[base_address+Policy::processor_rva]^=1;assert(!policy.start_observer(base_address,read_memory,native));
    std::cout<<"native_manual_equip_check passed (per-item lease prototype, unhooked, writer defaults off)\n";
}
