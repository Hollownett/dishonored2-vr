#define NOMINMAX
#include "../src/native_kill_range_scope.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace {
using Scope=d2vr::NativeKillRangeScope;
using Reason=Scope::Reason;
constexpr std::uintptr_t image_base=0x140000000,game_address=0x200000000,player_address=0x201000000,
    machine_address=0x202000000,sensor_address=0x203000000,components_address=0x204000000,
    buckets_address=0x205000000,node_address=0x206000000,handler_address=0x207000000,rules_address=0x208000000,
    paired_address=0x209000000,other_address=0x20a000000,paired_list=0x20b000000,other_list=0x20c000000,
    distance_address=0x20d000000,height_address=0x20e000000,other_distance=0x20f000000,awareness_address=0x210000000,
    distance_definition=0x211000000,height_definition=0x212000000,awareness_definition=0x213000000,
    distance_accessor=0x214000000,height_accessor=0x215000000,outcome_address=0x216000000,cache_address=0x217000000,
    awareness_accessor=0x218000000,slot_address=sensor_address+0x78;
std::map<std::uintptr_t,unsigned char> memory;
unsigned reads{},writes{};bool reader_failure{},reader_throw{},reader_fault{};
std::function<void(std::uintptr_t)> after_read;
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* source=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=source[offset];
}
void fill(std::uintptr_t address,std::size_t bytes) {for(std::size_t offset=0;offset<bytes;++offset)memory[address+offset]=0;}
template<class Value> Value get(std::uintptr_t address) {
    Value result{};auto* bytes=reinterpret_cast<unsigned char*>(&result);
    for(std::size_t offset=0;offset<sizeof(result);++offset)bytes[offset]=memory.at(address+offset);
    return result;
}
bool read_memory(std::uintptr_t address,void* output,std::size_t bytes) {
    ++reads;if(reader_failure)return false;if(reader_throw)throw std::runtime_error("reader");
    if(reader_fault) {RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return false;}
    for(std::size_t offset=0;offset<bytes;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    if(after_read)after_read(address);return true;
}
void condition(std::uintptr_t address,unsigned type,int result,int variable,std::uintptr_t definition,std::uintptr_t accessor,float threshold) {
    fill(address,0x30);put(address,image_base+Scope::condition_vtables[type]);put(address+0xc,result);put(address+0x10,variable);
    put(address+0x18,definition);put(address+0x20,accessor);put(address+0x28,int{4});put(address+0x2c,threshold);
}
void reset() {
    memory.clear();after_read={};reader_failure=false;reader_throw=false;reader_fault=false;reads=0;writes=0;
    for(const auto& object:std::array<std::pair<std::uintptr_t,std::size_t>,14>{{
        {game_address,8},{player_address,0x300},{machine_address,0x400},{sensor_address,0x200},{buckets_address,0x800},
        {node_address,0x38},{handler_address,0x88},{paired_address,0x28},{other_address,0x28},
        {distance_accessor,0x20},{height_accessor,0x20},{awareness_accessor,0x20},{cache_address,0x200},{outcome_address,0x28}}})fill(object.first,object.second);
    put(image_base+0x39a5290,game_address);put(game_address,image_base+0x1c940a8);put(game_address+0x3130b0,player_address);
    put(player_address,image_base+0x1c5e428);put(player_address+0xa8,std::uint32_t{123});
    put(player_address+0x268,d2vr::rig::List{components_address,2,32u<<8});put(components_address,machine_address);put(components_address+8,sensor_address);
    put(machine_address,image_base+0x1c73d58);put(machine_address+0x10,player_address);put(machine_address+0x18,d2vr::rig::Identity{5,10});
    put(sensor_address,image_base+0x1b3f998);put(sensor_address+0x10,player_address);put(sensor_address+0x18,d2vr::rig::Identity{6,10});
    put(sensor_address+0x20,machine_address);put(sensor_address+0x38,static_cast<unsigned char>(1));
    put(slot_address,image_base+Scope::slot_vtable);put(slot_address+8,sensor_address);put(slot_address+0x20,unsigned{456});
    put(sensor_address+0x190,buckets_address);put(sensor_address+0x198,int{256});put(sensor_address+0x19c,unsigned{1});put(sensor_address+0x1a0,unsigned{255});
    put(buckets_address,node_address);put(node_address+0x28,handler_address);put(node_address+0x30,std::uintptr_t{});
    for(unsigned category=0;category<3;++category)put(sensor_address+0x1a8+category*8ull,handler_address);
    put(handler_address+0x30,unsigned{123});put(handler_address+0x38,unsigned{0x80000000});put(handler_address+0x40,unsigned{10});
    put(handler_address+0x48,cache_address);put(handler_address+0x50,unsigned{0x200});
    put(handler_address+0x20,d2vr::rig::List{rules_address,2,32u<<8});put(rules_address,paired_address);put(rules_address+8,other_address);
    put(paired_address,d2vr::rig::List{paired_list,3,32u<<8});put(other_address,d2vr::rig::List{other_list,1,32u<<8});
    put(paired_address+0x10,outcome_address);put(other_address+0x10,outcome_address);put(outcome_address,image_base+Scope::outcome_vtable);
    put(image_base+Scope::outcome_vtable+0x10,image_base+0x695390);put(image_base+Scope::outcome_vtable+0x20,image_base+0x11d080);
    for(unsigned index=0;index<4;++index)put(image_base+Scope::condition_vtables[index]+0x20,image_base+Scope::condition_evaluators[index]);
    put(image_base+Scope::accessor_vtable+8,image_base+0x6cbea0);
    put(paired_list,distance_address);put(paired_list+8,height_address);put(paired_list+16,awareness_address);put(other_list,other_distance);
    condition(distance_address,1,0,64,distance_definition,distance_accessor,3.f);
    condition(height_address,1,8,72,height_definition,height_accessor,1.7f);
    condition(other_distance,1,24,64,distance_definition,distance_accessor,3.f);
    condition(awareness_address,0,16,80,awareness_definition,awareness_accessor,0.f);
    for(const auto address:std::array<std::uintptr_t,2>{distance_accessor,height_accessor}) {put(address,image_base+Scope::accessor_vtable);put(address+0x10,slot_address);}
    put(distance_accessor+8,distance_definition);put(height_accessor+8,height_definition);
    put(distance_accessor+0x18,image_base+0x9e9df0);put(height_accessor+0x18,image_base+0x9e9e30);
}
Scope::Snapshot capture(const Scope::Limits& limits=Scope::Limits{}) {
    auto reader=&read_memory;const auto before=memory;const auto snapshot=Scope::capture(reader,image_base,sensor_address,handler_address,1.5f,limits);
    if(!after_read)assert(memory==before);assert(writes==0);return snapshot;
}
void rejected(const std::function<void()>& mutation,Reason expected) {
    reset();mutation();const auto snapshot=capture();assert(!snapshot.ok() && snapshot.reason()==expected);
}
void second_handler(std::uintptr_t cache_pointer) {
    constexpr std::uintptr_t second_node=0x220000000,second=0x221000000;
    fill(second_node,0x38);fill(second,0x88);put(buckets_address+8,second_node);put(sensor_address+0x19c,unsigned{2});
    put(second_node+0x28,second);put(second+0x30,unsigned{123});put(second+0x38,unsigned{0x80000000});
    put(second+0x48,cache_pointer);put(second+0x50,unsigned{0x200});
}
}
int main() {
    static_assert(std::is_trivially_copyable_v<Scope::View>);
    reset();auto snapshot=capture();assert(snapshot.ok() && snapshot.pairs().size()==1);
    auto reader=&read_memory;Scope::View view{};put(handler_address+0x78,paired_address);const auto original_memory=memory;
    assert(snapshot.view(reader,image_base+0x896473,distance_address,handler_address+0x40,view));float threshold{};
    std::memcpy(&threshold,view.bytes.data()+0x2c,4);assert(threshold==4.5f && memory==original_memory);
    assert(snapshot.view(reader,image_base+0x896473,height_address,handler_address+0x40,view));std::memcpy(&threshold,view.bytes.data()+0x2c,4);assert(threshold==1.7f*1.5f);
    assert(snapshot.read_calls()>0 && snapshot.view_read_calls()>0 && snapshot.read_bytes()>0);
    assert(!snapshot.view(reader,image_base+0x1234,distance_address,handler_address+0x40,view) && snapshot.last_view_reason()==Reason::wrong_consumer && snapshot.ok());
    put(cache_address,unsigned{10});put(cache_address+4,static_cast<unsigned char>(1));put(cache_address+68,4.472136f);
    put(paired_address+0x18,std::uint64_t{321});put(paired_address+0x20,static_cast<unsigned char>(6));put(handler_address+0x58,outcome_address);
    assert(snapshot.revalidate(reader));
    put(handler_address+0x40,unsigned{11});assert(!snapshot.revalidate(reader) && snapshot.reason()==Reason::expired_cycle);
    reset();put(other_address,d2vr::rig::List{other_list,2,32u<<8});put(other_list,distance_address);put(other_list+8,height_address);
    put(distance_address+8,unsigned{1});put(height_address+8,unsigned{1});auto shared=capture();assert(shared.ok() && shared.pairs().size()==2);
    rejected([]{put(other_list,distance_address);put(distance_address+8,unsigned{1});},Reason::nonpaired_use);
    rejected([]{put(other_distance+0xc,int{0});},Reason::aliased_cache);
    rejected([]{put(other_distance+0xc,int{4});},Reason::aliased_cache);
    rejected([]{put(other_distance+0x10,int{8});},Reason::aliased_cache);
    rejected([]{put(height_address+0x10,int{68});},Reason::aliased_cache);
    rejected([]{put(distance_address+8,std::numeric_limits<unsigned>::max());},Reason::reference_count);
    rejected([]{put(cache_address,unsigned{10});},Reason::warm_cache);
    rejected([]{put(handler_address+0x38,unsigned{0});},Reason::aliased_cache);
    rejected([]{put(handler_address+0x38,unsigned{64});},Reason::aliased_cache);
    rejected([]{put(other_distance+0x10,int{0x1fc});},Reason::invalid_cache);
    rejected([]{put(other_distance+0xc,int{-8});},Reason::invalid_cache);
    rejected([]{put(other_distance,image_base+1);},Reason::unsupported_condition);
    rejected([]{put(outcome_address,image_base+1);},Reason::unsupported_outcome);
    rejected([]{put(rules_address+8,paired_address);},Reason::invalid_graph);
    rejected([]{put(paired_list+16,distance_address);},Reason::invalid_graph);
    rejected([]{put(node_address+0x30,node_address);},Reason::invalid_graph);
    rejected([]{put(sensor_address+0x19c,unsigned{2});},Reason::invalid_graph);
    rejected([]{put(sensor_address+0x1a0,unsigned{511});},Reason::invalid_graph);
    rejected([]{put(handler_address+0x20,d2vr::rig::List{rules_address,2,1u<<8});},Reason::invalid_graph);
    rejected([]{put(sensor_address+0x20,machine_address+8);},Reason::invalid_owner);
    rejected([]{put(components_address+8,machine_address);},Reason::invalid_owner);
    rejected([]{put(sensor_address+0x18,d2vr::rig::Identity{-1,10});},Reason::invalid_owner);
    rejected([]{put(slot_address+8,sensor_address+8);},Reason::invalid_owner);
    rejected([]{put(slot_address+0x20,unsigned{0x3fff0010});},Reason::invalid_owner);
    rejected([]{put(sensor_address+0x1c0,handler_address);},Reason::invalid_graph);
    rejected([]{put(height_accessor+0x18,image_base+0x9e9df0);},Reason::no_pairs);
    rejected([]{put(height_address+0x2c,2.55f);},Reason::no_pairs);
    rejected([]{second_handler(cache_address+0x100);},Reason::aliased_cache);
    reset();second_handler(0x222000000);assert(capture().ok());
    reset();put(handler_address+0x20,d2vr::rig::List{rules_address,2,2048u<<8});assert(capture().ok());
    for(const auto type:std::array<unsigned,3>{0,2,3}) {reset();put(awareness_address,image_base+Scope::condition_vtables[type]);assert(capture().ok());}
    for(unsigned budget=0;budget<5;++budget) {
        reset();Scope::Limits limits;
        if(budget==0)limits.read_bytes=16;if(budget==1)limits.work=1;if(budget==2)limits.condition_edges=2;if(budget==3)limits.witnesses=2;if(budget==4)limits.rules_per_handler=1;
        const auto limited=capture(limits);assert(!limited.ok() && (limited.reason()==Reason::read_budget || limited.reason()==Reason::work_budget));
    }
    for(auto* flag:std::array<bool*,3>{&reader_failure,&reader_throw,&reader_fault}) {reset();*flag=true;const auto failed=capture();assert(!failed.ok() && failed.reason()==Reason::read_failure);*flag=false;}
    reset();auto stale=capture();put(player_address+0xa8,unsigned{124});assert(!stale.revalidate(reader) && stale.reason()==Reason::changed);
    reset();auto changed=capture();put(distance_accessor+0x18,image_base+1);assert(!changed.revalidate(reader));
    reset();const auto budget_reference=capture();Scope::Limits small_view;small_view.read_bytes=budget_reference.read_bytes()+8;
    auto exhausted=capture(small_view);assert(exhausted.ok());put(handler_address+0x78,paired_address);
    view.bytes.fill(0xab);const auto untouched=view.bytes;assert(!exhausted.view(reader,image_base+0x896473,distance_address,handler_address+0x40,view));
    assert(exhausted.reason()==Reason::read_budget && exhausted.last_view_reason()==Reason::read_budget && view.bytes==untouched);
    reset();bool replaced{};after_read=[&](std::uintptr_t address){if(!replaced && address==distance_address) {replaced=true;put(components_address+8,std::uintptr_t{});}};
    assert(!capture().ok());after_read={};
    reset();const auto clean=capture();std::cout<<"PASS: bounded read-only Kill graph scope, shared-pair closure, cache aliases/cold cycle, typed ownership, faults and immutable revalidation; capture reads="<<clean.read_calls()<<" bytes="<<clean.read_bytes()<<"\n";
}
