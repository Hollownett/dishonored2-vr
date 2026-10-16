#define NOMINMAX
#include "../src/native_choke_distance_scope.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace d2vr {
struct NativeChokeDistanceScopeFixture {
    static NativeChokeDistanceScope& owner(bool semantic=true) {auto* result=new NativeChokeDistanceScope;result->semantic_fixture_=semantic;return *result;}
    static bool clean() {return !NativeChokeDistanceScope::current_frame_ && !NativeChokeDistanceScope::callback_depth_;}
    static void shim_unwind() {
        auto& policy=owner(false);assert(policy.prepare_shim());const auto code=policy.shim_bytes();
        assert((code.size()==55 && policy.shim_metadata_->unwind==std::array<unsigned char,12>{1,9,3,0,9,0x64,9,0,4,0xa2,0,0}));
        alignas(16) std::array<unsigned char,256> stack{};const auto entry_rsp=reinterpret_cast<DWORD64>(stack.data()+136);
        const DWORD64 caller=0x123456789abcdef0,input=0x44444444,output=0x77777777;
        std::memcpy(reinterpret_cast<void*>(entry_rsp),&caller,8);std::memcpy(reinterpret_cast<void*>(entry_rsp-0x10),&input,8);
        for(const auto offset:{9u,14u,17u,22u,27u,37u,39u,45u,50u,54u})for(const bool qualified:{false,true}) {
            CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rip=policy.shim_+offset;
            context.Rsp=offset==54?entry_rsp:entry_rsp-0x58;context.Rsi=qualified && offset>=50?output:input;
            DWORD64 base{};const auto function=RtlLookupFunctionEntry(context.Rip,&base,nullptr);assert(function==&policy.shim_metadata_->function);
            void* handler{};DWORD64 frame{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,function,&context,&handler,&frame,nullptr);
            assert(context.Rip==caller && context.Rsp==entry_rsp+8 && context.Rsi==(qualified && offset>=50?output:input));
        }
    }
    static void trampoline_unwind() {
        for(unsigned index=0;index<2;++index) {
            auto& policy=owner(false);policy.base_=0x140000000;assert(policy.prepare_shim());
            auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
            const auto address=reinterpret_cast<std::uintptr_t>(code);const auto prefix=index?5u:6u;
            std::memcpy(code,policy.prologues[index].data(),prefix);
            const auto tail=policy.tail(policy.base_+policy.rvas[index]+prefix,false);
            const auto relay=policy.tail(index?policy.shim_:reinterpret_cast<std::uintptr_t>(&NativeChokeDistanceScope::entry_hook),false);
            std::memcpy(code+prefix,tail.data(),14);std::memcpy(code+prefix+14,relay.data(),14);policy.originals_[index]=code;
            assert(policy.prepare_trampoline(index) && policy.registered_[index] && policy.prepare_trampoline(index));
            alignas(16) std::array<unsigned char,256> stack{};const auto entry_rsp=reinterpret_cast<DWORD64>(stack.data()+136);
            const DWORD64 caller=0x123456789abcdef0,saved=0x33333333;
            std::memcpy(reinterpret_cast<void*>(entry_rsp),&caller,8);
            std::memcpy(reinterpret_cast<void*>(entry_rsp+(index?8:-8)),&saved,8);
            for(const auto offset:std::array<unsigned,4>{0,prefix,prefix+10,prefix+14}) {
                CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rip=address+offset;
                const bool relay_pc=offset==prefix+14;
                context.Rsp=entry_rsp+(!index && offset>=prefix && !relay_pc?-0x78:0);context.Rbx=offset>=prefix && !relay_pc?1:saved;
                DWORD64 base{};const auto function=RtlLookupFunctionEntry(context.Rip,&base,nullptr);assert(function);
                void* handler{};DWORD64 frame{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,function,&context,&handler,&frame,nullptr);
                assert(context.Rip==caller && context.Rsp==entry_rsp+8 && context.Rbx==saved);
            }
            code[prefix+14]^=1;assert(!policy.prepare_trampoline(index));code[prefix+14]^=1;
            policy.metadata_[index]->unwind[0]^=1;assert(!policy.prepare_trampoline(index));policy.metadata_[index]->unwind[0]^=1;
        }
        auto& policy=owner(false);policy.base_=0x140000000;assert(policy.prepare_shim());
        auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
        std::memcpy(code,policy.prologues[0].data(),6);const auto tail=policy.tail(policy.base_+policy.rvas[0]+6,false);
        const auto relay=policy.tail(reinterpret_cast<std::uintptr_t>(&NativeChokeDistanceScope::entry_hook),false);
        std::memcpy(code+6,tail.data(),14);std::memcpy(code+20,relay.data(),14);policy.originals_[0]=code;
        code[128]=1;RUNTIME_FUNCTION foreign{0,34,128};assert(RtlAddFunctionTable(&foreign,1,reinterpret_cast<DWORD64>(code)));
        assert(!policy.prepare_trampoline(0) && !policy.registered_[0]);assert(RtlDeleteFunctionTable(&foreign));
        assert(policy.prepare_trampoline(0));
    }
};
}
namespace {
using Policy=d2vr::NativeChokeDistanceScope;
constexpr std::uintptr_t base=0x140000000,game_address=0x200000000,player_address=0x201000000,machine=0x202000000,
    sensor=0x203000000,declaration=0x204000000,components_address=0x205000000;
std::map<std::uintptr_t,unsigned char> memory;
Policy* active{};
std::function<void()> entry_action,distance_action;
unsigned entry_calls{},distance_calls{},creates{},enables{},disables{},logic_value=2;
int fail_create=-1,fail_enable=-1,fail_disable=-1;
bool null_trampoline{},throw_read{},fault_read{};
std::array<unsigned char,0x98> source{};
bool expect_view=true;
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    if(throw_read)throw std::runtime_error("read");if(fault_read)RaiseException(0xe0421111,0,0,nullptr);
    for(std::size_t offset=0;offset<size;++offset) {const auto found=memory.find(address+offset);if(found==memory.end())return false;static_cast<unsigned char*>(output)[offset]=found->second;}return true;
}
unsigned logic() {return logic_value;}
void pins() {
    for(unsigned index=0;index<2;++index)put(base+Policy::rvas[index],Policy::prologues[index]);
    put(base+0x3a6a9d,Policy::caller_code);put(base+0x1d0b738,base+0x94fd10);
    for(const auto& pin:Policy::consumer_pins)for(unsigned offset=0;offset<pin.size;++offset)memory[base+pin.rva+offset]=pin.bytes[offset];
}
void owners() {
    using namespace d2vr;
    put(base+0x39a5290,game_address);put(game_address,base+0x1c940a8);put(game_address+0x3130b0,player_address);put(player_address,base+0x1c5e428);put(player_address+0xa8,std::uint32_t{123});
    put(player_address+0x268,rig::List{components_address,2,2u<<8});put(components_address,machine);put(components_address+8,sensor);
    put(machine,base+0x1c73d58);put(machine+0x10,player_address);put(machine+0x18,rig::Identity{5,10});
    put(sensor,base+Policy::sensor_vtable_rva);put(sensor+8,declaration);put(sensor+0x10,player_address);put(sensor+0x18,rig::Identity{6,10});put(sensor+0x20,machine);put(sensor+0x38,static_cast<unsigned char>(1));
    source.fill(0x5a);const auto table=base+Policy::decl_vtable_rva;std::memcpy(source.data(),&table,8);
    for(unsigned index=0;index<6;++index)std::memcpy(source.data()+Policy::offsets[index],&Policy::shipped[index],4);put(declaration,source);
}
float __fastcall native_distance(std::uintptr_t value) {assert(value==sensor);++distance_calls;if(distance_action)distance_action();return 1.25f;}
void check_result(const Policy::DistanceResult& output) {
    assert(output.distance==1.25f);
    if(!expect_view) {assert(output.declaration==declaration);return;}
    assert(output.declaration!=declaration);const auto* bytes=reinterpret_cast<const unsigned char*>(output.declaration);
    for(std::size_t offset=0;offset<source.size();++offset) {
        bool scaled=false;for(const auto at:Policy::offsets)if(offset>=at && offset<at+4)scaled=true;
        if(!scaled)assert(bytes[offset]==source[offset]);
    }
    for(unsigned index=0;index<6;++index) {float value{};std::memcpy(&value,bytes+Policy::offsets[index],4);assert(value==Policy::shipped[index]*active->factor());}
}
void __fastcall native_entry(std::uintptr_t value) {
    assert(value==sensor);++entry_calls;if(entry_action)entry_action();
    Policy::DistanceResult output{};active->invoke_distance(sensor,base+Policy::return_rva,declaration,&output,sensor);check_result(output);
}
bool native_exception() {
    __try {active->invoke_entry(sensor);}__except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}return false;
}
void clean() {assert(active->in_flight()==0 && d2vr::NativeChokeDistanceScopeFixture::clean());}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* trampoline) {
    const auto index=reinterpret_cast<std::uintptr_t>(target)==base+Policy::rvas[0]?0:1;assert(reinterpret_cast<std::uintptr_t>(target)==base+Policy::rvas[index]);++creates;
    if(fail_create==index)return MH_ERROR_MEMORY_ALLOC;
    *trampoline=null_trampoline?nullptr:index?reinterpret_cast<void*>(&native_distance):reinterpret_cast<void*>(&native_entry);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    const auto index=reinterpret_cast<std::uintptr_t>(target)==base+Policy::rvas[0]?0:1;++enables;if(fail_enable==index)return MH_ERROR_MEMORY_PROTECT;
    memory[reinterpret_cast<std::uintptr_t>(target)]=0xe9;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    const auto index=reinterpret_cast<std::uintptr_t>(target)==base+Policy::rvas[0]?0:1;++disables;if(fail_disable==index)return MH_ERROR_MEMORY_PROTECT;
    put(reinterpret_cast<std::uintptr_t>(target),Policy::prologues[index]);return MH_OK;
}
int main() {
    using Fixture=d2vr::NativeChokeDistanceScopeFixture;
    Fixture::shim_unwind();Fixture::trampoline_unwind();pins();owners();active=&Fixture::owner();
    assert(Policy::verify(base,read));
    for(const auto& pin:Policy::consumer_pins) {memory[base+pin.rva]^=1;const auto count=creates;assert(!active->install(base,read,logic) && creates==count);pins();}
    throw_read=true;assert(!Policy::verify(base,read));throw_read=false;fault_read=true;assert(!Policy::verify(base,read));fault_read=false;
    fail_create=1;assert(!active->install(base,read,logic) && active->stopped());fail_create=-1;
    fail_enable=1;assert(!active->install(base,read,logic) && active->stopped() && !active->installed(0));fail_enable=-1;
    assert(active->install(base,read,logic) && !active->enabled());expect_view=false;active->invoke_entry(sensor);clean();
    active->enabled(true);expect_view=true;const auto before=distance_calls;const auto unchanged=memory;active->invoke_entry(sensor);clean();assert(distance_calls==before+1 && memory==unchanged);
    Policy::DistanceResult foreign{};active->invoke_distance(sensor,base+1,0x123456789abcdef0,&foreign,sensor);assert(foreign.distance==1.25f && foreign.declaration==0x123456789abcdef0);clean();
    bool nested=false;entry_action=[&]{if(!nested) {nested=true;active->invoke_entry(sensor);nested=false;}};active->invoke_entry(sensor);entry_action={};clean();
    for(const auto& mutation:std::array<std::function<void()>,8>{
        []{put(sensor+0x38,static_cast<unsigned char>(0));},[]{put(sensor+0x18,d2vr::rig::Identity{9,10});},
        []{put(player_address+0xa8,std::uint32_t{124});},[]{put(sensor+8,declaration+8);},[]{put(components_address+8,machine);},
        []{put(declaration+0x90,1.f);},[]{logic_value=1;},[]{active->factor(1.6f);}}) {
        owners();logic_value=2;active->factor(1.5f);expect_view=false;distance_action=mutation;active->invoke_entry(sensor);distance_action={};clean();
    }
    owners();logic_value=2;active->factor(1.5f);expect_view=true;
    distance_action=[] {throw std::runtime_error("distance");};bool caught=false;try {active->invoke_entry(sensor);}catch(const std::runtime_error&) {caught=true;}assert(caught);clean();
    distance_action=[] {RaiseException(0xe0424242,0,0,nullptr);};assert(native_exception());clean();distance_action={};
    entry_action=[&]{if(!nested) {nested=true;active->invoke_entry(sensor);}else RaiseException(0xe0424242,0,0,nullptr);};assert(native_exception());nested=false;entry_action={};clean();
    entry_action=[]{assert(!active->disable(0));};expect_view=false;active->invoke_entry(sensor);entry_action={};clean();assert(!active->enabled() && active->disable() && active->stopped());
    assert(active->install(base,read,logic));active->enabled(true);expect_view=true;
    std::atomic<bool> entered{},released{};entry_action=[&]{entered=true;while(!released)Sleep(1);};
    std::thread worker([&]{active->invoke_entry(sensor);});while(!entered)Sleep(1);
    assert(!active->disable(0) && active->installed(0) && !active->installed(1) && !active->ready_for_install());
    expect_view=false;released=true;worker.join();entry_action={};assert(active->disable() && active->stopped());clean();
    auto& null_owner=Fixture::owner();null_trampoline=true;assert(!null_owner.install(base,read,logic) && !null_owner.ready_for_install());null_trampoline=false;
    std::cout<<"PASS Choke private declaration ownership/call-once/nesting/C++/SEH/stop/fail-closed semantics; actual emitted55B shim and both generated trampoline/relay Windows unwind checks\n";
}
