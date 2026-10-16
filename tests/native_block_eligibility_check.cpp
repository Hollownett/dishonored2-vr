#define NOMINMAX
#include "../src/native_block_eligibility.hpp"
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
namespace d2vr {
struct NativeBlockEligibilityFixture {
    static NativeBlockEligibility& isolated_owner() {static auto* owner=new NativeBlockEligibility;return *owner;}
};
}
namespace {
constexpr std::uintptr_t image_base=0x140000000,game_address=0x200000000,player_address=0x201000000,
    machine_address=0x202000000,pool_address=0x203000000,block_address=0x204000000,equip_address=0x205000000,
    components_address=0x206000000,block_decl=0x207000000,equip_decl=0x208000000;
std::map<std::uintptr_t,unsigned char> memory;
unsigned creates{},enables{},disables{},original_calls{};
bool fail_create{},fail_enable{},fail_disable{},fail_read{},throw_read{},fault_read{},null_trampoline{};
std::uint8_t native_result=1;
std::function<void()> original_action;
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    if(throw_read)throw std::runtime_error("reader failure");
    if(fault_read) {RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return false;}
    if(fail_read)return false;
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    return true;
}
bool other_reader(std::uintptr_t address,void* output,std::size_t size) {return read_memory(address,output,size);}
std::uint8_t __fastcall original(std::uintptr_t machine,std::int32_t state) {
    assert(machine && state>=0);++original_calls;if(original_action)original_action();return native_result;
}
bool invoke_native_exception(d2vr::NativeBlockEligibility* policy,std::uintptr_t caller) {
    __try {policy->invoke(caller,machine_address,64);}
    __except(GetExceptionCode()==0xe0421110?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
void pins() {
    using Policy=d2vr::NativeBlockEligibility;
    put(image_base+Policy::prologue_rva,Policy::prologue);put(image_base+Policy::caller_rva,Policy::caller_code);
    for(const auto& pin:Policy::slot_pins)put(image_base+pin.first,image_base+pin.second);
}
void owner_fields() {
    using namespace d2vr;
    put(image_base+0x39a5290,game_address);put(game_address,image_base+0x1c940a8);
    put(game_address+0x3130b0,player_address);put(player_address,image_base+0x1c5e428);put(player_address+0xa8,std::uint32_t{123});
    put(player_address+0x268,rig::List{components_address,1,1u<<8});put(components_address,machine_address);
    put(machine_address,image_base+0x1c73d58);put(machine_address+0x10,player_address);put(machine_address+0x18,rig::Identity{5,10});
    put(machine_address+0x90,pool_address);put(pool_address+0x10+44*8,block_address);put(pool_address+0x10+64*8,equip_address);
    put(block_address,image_base+NativeBlockEligibility::block_vtable_rva);put(equip_address,image_base+NativeBlockEligibility::equip_vtable_rva);
    put(block_address+8,std::int32_t{44});put(equip_address+8,std::int32_t{64});put(block_address+0x10,machine_address);put(equip_address+0x10,machine_address);
    put(block_address+0x18,block_decl);put(equip_address+0x18,equip_decl);put(block_address+0x20,static_cast<unsigned char>(1));put(block_address+0x54,std::int32_t{});
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* trampoline) {
    assert(reinterpret_cast<std::uintptr_t>(target)==image_base+d2vr::NativeBlockEligibility::prologue_rva);++creates;
    if(fail_create)return MH_ERROR_MEMORY_ALLOC;
    if(null_trampoline) {*trampoline=nullptr;return MH_OK;}
    *trampoline=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    assert(reinterpret_cast<std::uintptr_t>(target)==image_base+d2vr::NativeBlockEligibility::prologue_rva);++enables;
    if(fail_enable)return MH_ERROR_MEMORY_PROTECT;
    memory[reinterpret_cast<std::uintptr_t>(target)]=0xe9;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    assert(reinterpret_cast<std::uintptr_t>(target)==image_base+d2vr::NativeBlockEligibility::prologue_rva);++disables;
    if(fail_disable)return MH_ERROR_MEMORY_PROTECT;
    put(reinterpret_cast<std::uintptr_t>(target),d2vr::NativeBlockEligibility::prologue);return MH_OK;
}
int main() {
    using namespace d2vr;using Policy=NativeBlockEligibility;auto& policy=Policy::process_owner();pins();
    assert(Policy::verify(image_base,read_memory) && policy.stopped() && policy.ready_for_install());
    const auto before_hooks=creates+enables+disables;
    memory[image_base+Policy::caller_rva]^=1;assert(!policy.install(image_base,read_memory) && creates+enables+disables==before_hooks);pins();
    memory[image_base+Policy::prologue_rva]^=1;assert(!policy.install(image_base,read_memory) && creates+enables+disables==before_hooks);pins();
    put(image_base+Policy::slot_pins[0].first,image_base+1);assert(!policy.install(image_base,read_memory) && creates+enables+disables==before_hooks);pins();
    fail_read=true;assert(!Policy::verify(image_base,read_memory));fail_read=false;
    throw_read=true;assert(!Policy::verify(image_base,read_memory));throw_read=false;
    fault_read=true;assert(!Policy::verify(image_base,read_memory));fault_read=false;
    fail_create=true;assert(!policy.install(image_base,read_memory) && !policy.installed() && policy.stopped());fail_create=false;
    fail_enable=true;assert(!policy.install(image_base,read_memory) && !policy.ready_for_install());fail_enable=false;
    assert(!policy.install(image_base,read_memory));assert(policy.disable() && policy.ready_for_install());
    const auto created_count=creates;assert(policy.has_trampoline());assert(policy.install(image_base,read_memory) && policy.installed() && !policy.enabled() && creates==created_count);
    owner_fields();const auto caller=image_base+Policy::return_rva;
    auto invoke=[&](std::uintptr_t address,std::uintptr_t machine,std::int32_t state,std::uint8_t expected) {
        const auto count=original_calls;const auto bytes=memory;assert(policy.invoke(address,machine,state)==expected && original_calls==count+1);
        if(!original_action)assert(memory==bytes);
    };
    invoke(caller,machine_address,64,1);policy.enabled(true);assert(policy.enabled());
    const auto veto_start=policy.vetoes();invoke(caller,machine_address,64,0);assert(policy.vetoes()==veto_start+1);
    native_result=0;invoke(caller,machine_address,64,0);assert(policy.vetoes()==veto_start+1);native_result=1;
    for(const auto offset:std::array<std::uintptr_t,12>{0x3912b4,0x391391,0x460873,0x460c5c,0x49b52e,0x78b05a,0x78b07a,0x7f813d,0x7f8c5a,0x828f99,0x868044,0x123456})
        invoke(image_base+offset,machine_address,64,1);
    invoke(caller,machine_address,53,1);invoke(caller,machine_address,90,1);invoke(caller,machine_address+8,64,1);
    for(const auto& mutation:std::array<std::function<void()>,9>{
        []{put(block_address+0x20,static_cast<unsigned char>(0));},[]{put(block_address+0x54,std::int32_t{1});},
        []{put(block_address+8,std::int32_t{43});},[]{put(equip_address+8,std::int32_t{65});},
        []{put(equip_address,image_base+1);},[]{put(block_address+0x10,machine_address+8);},
        []{put(components_address,machine_address+8);},[]{put(machine_address+0x10,player_address+8);},
        []{put(image_base+Policy::slot_pins[1].first,image_base+1);}}) {
        owner_fields();pins();memory[image_base+Policy::prologue_rva]=0xe9;mutation();invoke(caller,machine_address,64,1);
    }
    owner_fields();pins();memory[image_base+Policy::prologue_rva]=0xe9;
    for(auto* flag:std::array<bool*,3>{&fail_read,&throw_read,&fault_read}) {*flag=true;invoke(caller,machine_address,64,1);*flag=false;}
    for(const auto& mutation:std::array<std::function<void()>,7>{
        []{put(player_address+0xa8,std::uint32_t{124});},[]{put(machine_address+0x18,rig::Identity{6,10});},
        []{put(game_address+0x3130b0,player_address+8);},[]{put(machine_address+0x90,pool_address+8);},
        []{put(pool_address+0x10+44*8,block_address+8);},[]{put(block_address+0x18,block_decl+8);},
        []{Policy::process_owner().enabled(false);}}) {
        owner_fields();policy.enabled(true);original_action=mutation;invoke(caller,machine_address,64,1);original_action={};
    }
    for(auto* flag:std::array<bool*,3>{&fail_read,&throw_read,&fault_read}) {
        owner_fields();policy.enabled(true);original_action=[&]{*flag=true;};invoke(caller,machine_address,64,1);*flag=false;original_action={};
    }
    owner_fields();policy.enabled(true);const auto disables_before=disables;
    original_action=[] {RaiseException(0xe0421110,0,0,nullptr);};
    assert(invoke_native_exception(&policy,caller) && policy.in_flight()==0);
    original_action=[] {throw std::runtime_error("native C++ failure");};
    bool caught_cpp{};
    try {policy.invoke(caller,machine_address,64);}catch(const std::runtime_error&){caught_cpp=true;}
    assert(caught_cpp && policy.in_flight()==0);original_action={};
    original_action=[&]{assert(!policy.disable(0) && disables==disables_before);};invoke(caller,machine_address,64,1);original_action={};
    assert(!policy.enabled() && policy.installed());assert(policy.disable() && policy.stopped());
    assert(!policy.install(image_base,other_reader) && !policy.install(image_base+8,read_memory));
    assert(policy.install(image_base,read_memory));policy.enabled(true);
    fail_disable=true;assert(!policy.disable(0) && policy.installed() && !policy.enabled() && !policy.ready_for_install());fail_disable=false;
    invoke(caller,machine_address,64,1);assert(policy.disable() && policy.stopped());
    assert(policy.install(image_base,read_memory));policy.enabled(true);
    std::mutex gate;std::condition_variable changed;bool entered{},released{};
    original_action=[&]{std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});};
    std::uint8_t pending_result{};std::thread pending([&]{pending_result=policy.invoke(caller,machine_address,64);});
    {std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[&]{return entered;});}
    assert(policy.in_flight()==1 && !policy.disable(0) && !policy.installed() && !policy.stopped() && !policy.ready_for_install());
    assert(!policy.install(image_base,read_memory));
    {std::lock_guard<std::mutex> lock(gate);released=true;}changed.notify_all();pending.join();original_action={};
    assert(pending_result==1 && policy.in_flight()==0 && policy.disable() && policy.stopped());
    assert(policy.install(image_base,read_memory));assert(creates==created_count);assert(policy.disable());
    const auto final_vetoes=policy.vetoes();invoke(caller,machine_address,64,1);assert(policy.vetoes()==final_vetoes && policy.stopped());
    assert(policy.install(image_base,read_memory));fail_read=true;assert(!policy.disable(0) && !policy.stopped());fail_read=false;
    assert(policy.disable() && policy.ready_for_install());
    auto& corrupt=NativeBlockEligibilityFixture::isolated_owner();null_trampoline=true;
    const auto enables_before_corrupt=enables,creates_before_corrupt=creates;
    assert(!corrupt.install(image_base,read_memory) && creates==creates_before_corrupt+1 && !corrupt.has_trampoline());
    assert(!corrupt.disable() && !corrupt.stopped() && !corrupt.ready_for_install());
    null_trampoline=false;
    assert(!corrupt.install(image_base,read_memory) && !corrupt.installed() && creates==creates_before_corrupt+1 && enables==enables_before_corrupt);
    std::cout<<"PASS: mutation-free Block eligibility, original-once routing, current leases, faults, pins and retained stop/re-enable lifecycle\n";
}
