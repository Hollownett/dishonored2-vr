#define NOMINMAX
#include "../src/native_slash_scene_rate.hpp"
#include <cassert>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
namespace d2vr {
struct NativeSlashSceneRateFixture {
    static NativeSlashSceneRate& make() {auto* owner=new NativeSlashSceneRate;semantic_mock(*owner);return *owner;}
    static NativeSlashSceneRate& real_metadata() {return *new NativeSlashSceneRate;}
    static void semantic_mock(NativeSlashSceneRate& owner) {owner.perform_unwind_registered_=true;owner.semantic_fixture_=true;}
    static void setter(NativeSlashSceneRate& owner,NativeSlashSceneRate::Setter setter) {owner.setter_=setter;}
    static bool clean() {return !NativeSlashSceneRate::current_frame_ && !NativeSlashSceneRate::callback_depth_;}
};
}
namespace {
using Policy=d2vr::NativeSlashSceneRate;
constexpr std::uintptr_t image_base=0x140000000,fixture_game=0x200000000,fixture_player=0x201000000,fixture_machine=0x202000000,
    fixture_pool=0x203000000,fixture_state=0x204000000,fixture_attributes=0x205000000,fixture_entries=0x206000000,fixture_components=0x207000000,
    name_address=0x208000000,scene_resource=0x209000000,context_list=0x20a000000,start_pointer=0x20b000000;
std::map<std::uintptr_t,unsigned char> memory;
Policy* owner{};
void* perform_trampoline_override{};
std::array<unsigned,3> creates{},enables{},disables{};
int fail_create=-1,fail_enable=-1,fail_disable=-1,null_trampoline=-1,raise_original=-1;
bool fail_read{},throw_read{},fault_read{},alternate{},launch_secondary=true,fail_primary{},fail_secondary{},setter_success=true;
unsigned logic_mask=2;bool throw_logic{},fault_logic{};
unsigned logic() {if(throw_logic)throw std::runtime_error("logic");if(fault_logic)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return logic_mask;}
bool nested{},inside_nested{},unrelated{},wrong_manager{},wrong_state{},wrong_caller{},cpp_original{};
unsigned perform_calls{},primary_calls{},secondary_calls{},native_setters{};
std::uint32_t identifier=100;
std::function<void()> during_original,after_creation,after_publish,during_setter;
struct RateCall {std::uint32_t identifier;float rate;};
std::vector<RateCall> rates;
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    if(throw_read)throw std::runtime_error("reader");
    if(fault_read) {RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return false;}
    if(fail_read)return false;
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    return true;
}
template<class Value> Value get(std::uintptr_t address) {
    Value result{};auto* output=reinterpret_cast<unsigned char*>(&result);
    for(std::size_t offset=0;offset<sizeof(result);++offset) {assert(memory.count(address+offset));output[offset]=memory.at(address+offset);}
    return result;
}
void pins() {
    for(unsigned index=0;index<3;++index)put(image_base+Policy::rvas[index],Policy::prologues[index]);
    for(const auto& pin:Policy::caller_pins)put(image_base+pin.rva,pin.bytes);
    for(const auto& pin:Policy::slot_pins)put(image_base+pin.first,image_base+pin.second);
    put(image_base+Policy::setter_rva,Policy::setter_code);
}
void fields() {
    using namespace d2vr;
    put(image_base+0x39a5290,fixture_game);put(fixture_game,image_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);
    put(fixture_player,image_base+0x1c5e428);put(fixture_player+0xa8,std::uint32_t{123});put(fixture_player+0x268,rig::List{fixture_components,1,1u<<8});put(fixture_components,fixture_machine);
    put(fixture_machine,image_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,rig::Identity{5,10});
    put(fixture_machine+0x90,fixture_pool);put(fixture_machine+0x330,fixture_attributes);put(fixture_pool+0x10+39*8,fixture_state);
    put(fixture_state,image_base+Policy::slash_vtable_rva);put(fixture_state+8,std::int32_t{39});put(fixture_state+0x10,fixture_machine);put(fixture_state+0x108,std::int32_t{});
    put(fixture_state+0x100,std::uint32_t{17});put(fixture_state+0xc8,std::uint32_t{18});
    put(fixture_attributes,image_base+Policy::attributes_vtable_rva);put(fixture_attributes+0x48,rig::List{fixture_entries,1,1u<<8});
    put(fixture_entries,name_address);put(name_address,Policy::attribute_name);put(fixture_entries+0x20,.375f);put(fixture_entries+0x24,1.f);
}
bool __fastcall setter(std::uintptr_t manager,std::uint32_t request,float rate) {
    assert(manager==fixture_game+0x1f5338);rates.push_back({request,rate});if(during_setter)during_setter();return setter_success;
}
void maybe_raise(int index) {if(raise_original==index)RaiseException(0xe1234567,0,0,nullptr);}
std::uint32_t* __fastcall primary(std::uintptr_t actual_state,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,float start) {
    ++primary_calls;assert(actual_state && scene==scene_resource && context==context_list && start==.125f);maybe_raise(1);
    *output=fail_primary?0:++identifier;put(reinterpret_cast<std::uintptr_t>(output),*output);return output;
}
std::uint32_t* __fastcall secondary(std::uintptr_t manager,std::uint32_t* output,std::uintptr_t scene,std::uintptr_t context,std::uint64_t fifth,std::uintptr_t start,std::uint64_t seventh) {
    ++secondary_calls;assert(manager && scene==scene_resource && context==context_list);
    assert(fifth==0x1234567887654321ull && start==start_pointer && seventh==0xfedcba9876543210ull);maybe_raise(2);
    *output=fail_secondary?0:++identifier;put(reinterpret_cast<std::uintptr_t>(output),*output);return output;
}
void __fastcall perform(std::uintptr_t actual_state) {
    ++perform_calls;maybe_raise(0);if(cpp_original)throw std::runtime_error("native original");if(during_original)during_original();
    if(nested && !inside_nested) {inside_nested=true;owner->invoke_perform(actual_state);inside_nested=false;}
    if(unrelated) {
        std::uint32_t ignored{};
        owner->invoke_primary(image_base+0x854ca8,fixture_state+8,&ignored,scene_resource,context_list,.125f);
        owner->invoke_secondary(image_base+0x1234,fixture_game+0x1f5338,&ignored,scene_resource,context_list,0x1234567887654321ull,start_pointer,0xfedcba9876543210ull);
    }
    std::uint32_t primary_id{};
    auto* primary_result=owner->invoke_primary(image_base+(wrong_caller?0x1234:alternate?0x854b7d:0x854ca8),wrong_state?actual_state+8:actual_state,&primary_id,scene_resource,context_list,.125f);
    assert(primary_result==&primary_id);if(!primary_id)return;
    std::uint32_t secondary_id{};
    if(launch_secondary) {
        auto* secondary_result=owner->invoke_secondary(image_base+0x86c7da,fixture_game+0x1f5338+(wrong_manager?8:0),&secondary_id,scene_resource,context_list,0x1234567887654321ull,start_pointer,0xfedcba9876543210ull);
        assert(secondary_result==&secondary_id);put(actual_state+0xc8,secondary_id);
    }
    if(after_creation)after_creation();put(actual_state+0x100,primary_id);if(after_publish)after_publish();
    const auto rate=get<float>(fixture_entries+0x24);
    if(rate!=1.f) {++native_setters;if(get<std::uint32_t>(actual_state+0xc8)>0)++native_setters;}
}
unsigned hook_index(LPVOID target) {
    for(unsigned index=0;index<3;++index)if(reinterpret_cast<std::uintptr_t>(target)==image_base+Policy::rvas[index])return index;
    assert(false);return 0;
}
void reset_behavior() {
    fail_primary=false;fail_secondary=false;launch_secondary=true;alternate=false;setter_success=true;
    nested=false;inside_nested=false;unrelated=false;wrong_manager=false;wrong_state=false;wrong_caller=false;
    cpp_original=false;raise_original=-1;logic_mask=2;throw_logic=false;fault_logic=false;
    during_original={};after_creation={};after_publish={};during_setter={};rates.clear();fields();owner->enabled(true);
}
void invoke_expect(unsigned expected) {
    const auto before=perform_calls;const auto attribute_word=get<std::uint64_t>(fixture_entries+0x20);rates.clear();
    owner->invoke_perform(fixture_state);assert(perform_calls==before+1 && rates.size()==expected);
    for(const auto& call:rates)assert(call.rate==1.7f && call.identifier>100);
    assert(get<std::uint64_t>(fixture_entries+0x20)==attribute_word && owner->in_flight()==0 && d2vr::NativeSlashSceneRateFixture::clean());
}
bool invoke_seh() {
    __try {owner->invoke_perform(fixture_state);}__except(EXCEPTION_EXECUTE_HANDLER) {return true;}
    return false;
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* trampoline) {
    const auto index=hook_index(target);++creates[index];if(fail_create==static_cast<int>(index))return MH_ERROR_MEMORY_ALLOC;
    const std::array<void*,3> originals{reinterpret_cast<void*>(&perform),reinterpret_cast<void*>(&primary),reinterpret_cast<void*>(&secondary)};
    *trampoline=null_trampoline==static_cast<int>(index)?nullptr:index==0 && perform_trampoline_override?perform_trampoline_override:originals[index];return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    const auto index=hook_index(target);++enables[index];if(fail_enable==static_cast<int>(index))return MH_ERROR_MEMORY_PROTECT;
    memory[reinterpret_cast<std::uintptr_t>(target)]=0xe9;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    const auto index=hook_index(target);++disables[index];if(fail_disable==static_cast<int>(index))return MH_ERROR_MEMORY_PROTECT;
    put(reinterpret_cast<std::uintptr_t>(target),Policy::prologues[index]);return MH_OK;
}
int main() {
    using Fixture=d2vr::NativeSlashSceneRateFixture;
    pins();assert(Policy::verify(image_base,read_memory));
    for(const auto& pin:Policy::caller_pins) {memory[image_base+pin.rva]^=1;assert(!Policy::verify(image_base,read_memory));pins();}
    for(unsigned index=0;index<3;++index) {memory[image_base+Policy::rvas[index]]^=1;assert(!Policy::verify(image_base,read_memory));pins();}
    for(const auto& pin:Policy::slot_pins) {put(image_base+pin.first,image_base+1);assert(!Policy::verify(image_base,read_memory));pins();}
    memory[image_base+Policy::setter_rva]^=1;assert(!Policy::verify(image_base,read_memory));pins();
    for(auto* flag:std::array<bool*,3>{&fail_read,&throw_read,&fault_read}) {*flag=true;assert(!Policy::verify(image_base,read_memory));*flag=false;}
    for(unsigned index=0;index<3;++index) {
        auto& partial=Fixture::make();fail_create=static_cast<int>(index);assert(!partial.install(image_base,read_memory,logic));fail_create=-1;
        assert(partial.disable() && partial.ready_for_install());assert(partial.install(image_base,read_memory,logic) && partial.has_trampolines());assert(partial.disable());
        auto& enabled_partial=Fixture::make();fail_enable=static_cast<int>(index);assert(!enabled_partial.install(image_base,read_memory,logic));fail_enable=-1;
        assert(enabled_partial.disable() && enabled_partial.ready_for_install());assert(enabled_partial.install(image_base,read_memory,logic));assert(enabled_partial.disable());
    }
    owner=&Policy::process_owner();Fixture::semantic_mock(*owner);assert(owner->install(image_base,read_memory,logic));Fixture::setter(*owner,setter);
    const auto retained_creates=creates;reset_behavior();
    const auto native_before=native_setters;invoke_expect(2);assert(native_setters==native_before);
    reset_behavior();alternate=true;invoke_expect(2);
    reset_behavior();launch_secondary=false;invoke_expect(1);assert(get<std::uint32_t>(fixture_state+0xc8)==18);
    reset_behavior();fail_primary=true;invoke_expect(0);assert(get<std::uint32_t>(fixture_state+0x100)==17);
    reset_behavior();fail_secondary=true;invoke_expect(1);
    reset_behavior();wrong_caller=true;invoke_expect(0);
    reset_behavior();wrong_state=true;invoke_expect(0);
    reset_behavior();wrong_manager=true;invoke_expect(1);
    for(const auto mask:std::array<unsigned,4>{0,64,72,3}) {reset_behavior();logic_mask=mask;invoke_expect(0);}
    reset_behavior();after_creation=[]{logic_mask=64;};invoke_expect(0);
    reset_behavior();throw_logic=true;invoke_expect(0);
    reset_behavior();fault_logic=true;invoke_expect(0);
    reset_behavior();after_publish=[]{put(fixture_state+0x100,std::uint32_t{17});};invoke_expect(0);
    reset_behavior();after_publish=[]{put(fixture_state+0xc8,std::uint32_t{18});};invoke_expect(1);
    reset_behavior();during_setter=[]{owner->enabled(false);};invoke_expect(1);
    reset_behavior();unrelated=true;invoke_expect(2);
    reset_behavior();nested=true;rates.clear();const auto calls_before=perform_calls;owner->invoke_perform(fixture_state);
    assert(perform_calls==calls_before+2 && rates.size()==4 && Fixture::clean() && owner->in_flight()==0);
    for(const float rate:std::array<float,5>{1.25f,1.7f,0.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        reset_behavior();put(fixture_entries+0x24,rate);rates.clear();owner->invoke_perform(fixture_state);assert(rates.empty());
    }
    reset_behavior();after_creation=[]{put(fixture_entries+0x24,1.25f);};owner->invoke_perform(fixture_state);assert(rates.empty());
    reset_behavior();put(fixture_entries+0x24,1.25f);after_creation=[]{put(fixture_entries+0x24,1.f);};owner->invoke_perform(fixture_state);assert(rates.empty());
    for(const auto& mutation:std::array<std::function<void()>,8>{
        []{put(fixture_player+0xa8,std::uint32_t{124});},[]{put(fixture_machine+0x18,d2vr::rig::Identity{6,10});},
        []{put(fixture_components,fixture_machine+8);},[]{put(fixture_machine+0x90,fixture_pool+8);},[]{put(fixture_pool+0x10+39*8,fixture_state+8);},
        []{put(fixture_state+0x108,std::int32_t{1});},[]{put(fixture_attributes+0x48,d2vr::rig::List{fixture_entries,0,1u<<8});},[]{put(fixture_entries,name_address+8);}}) {
        reset_behavior();after_creation=mutation;owner->invoke_perform(fixture_state);assert(rates.empty());
    }
    reset_behavior();after_creation=[]{owner->enabled(false);owner->enabled(true);};invoke_expect(0);
    for(auto* flag:std::array<bool*,3>{&fail_read,&throw_read,&fault_read}) {
        reset_behavior();*flag=true;owner->invoke_perform(fixture_state);*flag=false;assert(rates.empty() && Fixture::clean() && !owner->in_flight());
    }
    reset_behavior();during_original=[]{assert(!owner->disable(0));owner->enabled(true);};invoke_expect(0);
    assert(owner->disable() && owner->install(image_base,read_memory,logic));
    reset_behavior();setter_success=false;const auto failures_before=owner->setter_failures();invoke_expect(2);assert(owner->setter_failures()==failures_before+2);
    for(int index=0;index<3;++index) {
        reset_behavior();raise_original=index;assert(invoke_seh());assert(owner->in_flight()==0 && Fixture::clean());raise_original=-1;
        assert(owner->disable() && owner->install(image_base,read_memory,logic));
    }
    reset_behavior();cpp_original=true;bool caught{};try {owner->invoke_perform(fixture_state);}catch(const std::runtime_error&) {caught=true;}
    assert(caught && owner->in_flight()==0 && Fixture::clean());
    reset_behavior();std::mutex gate;std::condition_variable changed;bool entered{},released{};
    during_original=[&]{std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});};
    std::thread pending([]{owner->invoke_perform(fixture_state);});
    {std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[&]{return entered;});}
    assert(!owner->disable(0) && !owner->ready_for_install() && owner->in_flight()==1);
    {std::lock_guard<std::mutex> lock(gate);released=true;}changed.notify_all();pending.join();
    assert(rates.empty() && owner->disable() && owner->ready_for_install());
    assert(owner->install(image_base,read_memory,logic));reset_behavior();fail_disable=1;
    assert(!owner->disable(0) && !owner->enabled() && !owner->ready_for_install());fail_disable=-1;
    assert(owner->disable() && owner->install(image_base,read_memory,logic));reset_behavior();
    fail_read=true;assert(!owner->disable(0));fail_read=false;assert(owner->disable());
    assert(owner->install(image_base,read_memory,logic));reset_behavior();assert(owner->disable() && creates==retained_creates);
    owner->factor(1.5f);assert(owner->factor()==1.5f);owner->factor(std::numeric_limits<float>::quiet_NaN());assert(owner->factor()==1.5f);
    auto& unprofiled=Fixture::real_metadata();const auto enables_before=enables;
    assert(!unprofiled.install(image_base,read_memory,logic) && enables==enables_before && !unprofiled.perform_unwind_registered());assert(unprofiled.disable());
    auto* actual_trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(actual_trampoline);
    std::memcpy(actual_trampoline,Policy::prologues[0].data(),8);const auto original_tail=Policy::expected_perform_tail(image_base);
    std::memcpy(actual_trampoline+8,original_tail.data(),original_tail.size());
    std::array<unsigned char,14> untouched_relay{};untouched_relay.fill(0xcc);std::memcpy(actual_trampoline+22,untouched_relay.data(),untouched_relay.size());
    perform_trampoline_override=actual_trampoline;auto& registered=Fixture::real_metadata();
    assert(registered.install(image_base,read_memory,logic) && registered.perform_unwind_registered());perform_trampoline_override=nullptr;
    assert(!std::memcmp(actual_trampoline,Policy::prologues[0].data(),8) && !std::memcmp(actual_trampoline+22,untouched_relay.data(),untouched_relay.size()));
    assert(actual_trampoline[8]==0x49 && actual_trampoline[9]==0xbb && actual_trampoline[18]==0x41 && actual_trampoline[19]==0xff && actual_trampoline[20]==0xe3);
    for(const unsigned offset:std::array<unsigned,5>{3,4,8,18,21}) {
        alignas(16) std::array<DWORD64,128> stack{};const auto entry_rsp=reinterpret_cast<DWORD64>(&stack[64]);
        stack[64]=0x12345678;stack[63]=0x77777777;CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
        context.Rip=reinterpret_cast<DWORD64>(actual_trampoline)+offset;context.Rsp=entry_rsp-(offset>=8?0x68:offset>=4?8:0);context.Rsi=0x55555555;context.Rax=entry_rsp;
        DWORD64 table_base{};auto* function=RtlLookupFunctionEntry(context.Rip,&table_base,nullptr);assert(function);
        void* handler_data{};DWORD64 establisher{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,table_base,context.Rip,function,&context,&handler_data,&establisher,nullptr);
        assert(context.Rip==0x12345678 && context.Rsp==entry_rsp+8 && context.Rsi==(offset>=4?0x77777777:0x55555555) && context.Rax==entry_rsp);
    }
    DWORD64 table_base{};assert(!RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(actual_trampoline)+22,&table_base,nullptr));
    assert(registered.disable() && registered.install(image_base,read_memory,logic) && registered.perform_unwind_registered());assert(registered.disable());
    const auto registered_enables=enables;actual_trampoline[18]^=1;
    assert(!registered.install(image_base,read_memory,logic) && enables==registered_enables);assert(registered.disable());
    actual_trampoline[18]^=1;assert(registered.install(image_base,read_memory,logic));assert(registered.disable());
    for(unsigned index=0;index<3;++index) {
        auto& corrupt=Fixture::make();null_trampoline=static_cast<int>(index);
        assert(!corrupt.install(image_base,read_memory,logic) && !corrupt.ready_for_install());null_trampoline=-1;
        assert(!corrupt.disable() && !corrupt.ready_for_install());
    }
    std::cout<<"PASS: mutation-free Slash fresh scenes, float/seven-argument ABI, native default branch, modifiers, nested/SEH cleanup and retained lifecycle\n";
}
